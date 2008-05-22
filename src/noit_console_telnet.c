/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 2007
 *      OmnitTI Computer Consulting, Inc.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "noit_defines.h"
#include <unistd.h>
#include <ctype.h>
#include <sys/ioctl.h>
#ifdef HAVE_SYS_IOCTL_COMPAT_H
#include <sys/ioctl_compat.h>
#endif
#include <arpa/telnet.h>
#include "noit_console.h"
#include "noit_console_telnet.h"
#include "utils/noit_hash.h"

/*
RCSID("$Id: state.c,v 1.14.12.1 2004/06/21 08:21:58 lha Exp $");
*/

unsigned char	doopt[] = { IAC, DO, '%', 'c', 0 };
unsigned char	dont[] = { IAC, DONT, '%', 'c', 0 };
unsigned char	will[] = { IAC, WILL, '%', 'c', 0 };
unsigned char	wont[] = { IAC, WONT, '%', 'c', 0 };
int	not42 = 1;

/*
 * Buffer for sub-options, and macros
 * for suboptions buffer manipulations
 */

#define subbuffer ncct->telnet->_subbuffer
#define subpointer ncct->telnet->_subpointer
#define subend ncct->telnet->_subend
#define subsave ncct->telnet->_subsave
#define slctab ncct->telnet->_slctab
#define pty ncct->pty_master
#define do_dont_resp ncct->telnet->_do_dont_resp
#define will_wont_resp ncct->telnet->_will_wont_resp
#define termbuf ncct->telnet->_termbuf
#define termbuf2 ncct->telnet->_termbuf2
#define SYNCHing ncct->telnet->_SYNCHing
#define terminalname ncct->telnet->_terminalname
#define flowmode ncct->telnet->_flowmode
#define linemode ncct->telnet->_linemode
#define uselinemode ncct->telnet->_uselinemode
#define alwayslinemode ncct->telnet->_alwayslinemode
#define restartany ncct->telnet->_restartany
#define _terminit ncct->telnet->__terminit
#define turn_on_sga ncct->telnet->_turn_on_sga
#define useeditmode ncct->telnet->_useeditmode
#define editmode ncct->telnet->_editmode
#define def_slcbuf ncct->telnet->_def_slcbuf
#define def_slclen ncct->telnet->_def_slclen
#define slcchange ncct->telnet->_slcchange
#define slcptr ncct->telnet->_slcptr
#define slcbuf ncct->telnet->_slcbuf
#define def_tspeed ncct->telnet->_def_tspeed
#define def_rspeed ncct->telnet->_def_rspeed
#define def_row ncct->telnet->_def_row
#define def_col ncct->telnet->_def_col

#define settimer(x)     (ncct->telnet->_clocks.x = ++ncct->telnet->_clocks.system)
#define sequenceIs(x,y) (ncct->telnet->_clocks.x < ncct->telnet->_clocks.y)

#define dooption(a) noit_console_telnet_dooption(ncct,a)
#define dontoption(a) noit_console_telnet_dontoption(ncct,a)
#define willoption(a) noit_console_telnet_willoption(ncct,a)
#define wontoption(a) noit_console_telnet_wontoption(ncct,a)
#define send_do(a,i) noit_console_telnet_send_do(ncct,a,i)
#define send_dont(a,i) noit_console_telnet_send_dont(ncct,a,i)
#define send_will(a,i) noit_console_telnet_send_will(ncct,a,i)
#define send_wont(a,i) noit_console_telnet_send_wont(ncct,a,i)
#define send_status() noit_console_telnet_send_status(ncct)
#define doclientstat() noit_console_telnet_doclientstat(ncct)

#ifndef	TCSIG
# ifdef	TIOCSIG
#  define TCSIG TIOCSIG
# endif
#endif

static void
netflush(noit_console_closure_t ncct) {
  int unused;
  noit_console_continue_sending(ncct, &unused);
}
static void set_termbuf(noit_console_closure_t ncct);
static void init_termbuf(noit_console_closure_t ncct);
static void defer_terminit(noit_console_closure_t ncct);
static void flowstat(noit_console_closure_t ncct);
static int spcset(noit_console_closure_t ncct, int func, cc_t *valp, cc_t **valpp);
static void suboption(noit_console_closure_t ncct);
static void send_slc(noit_console_closure_t ncct);
static void default_slc(noit_console_closure_t ncct);
static void get_slc_defaults(noit_console_closure_t ncct);
static void add_slc(noit_console_closure_t ncct, char func, char flag, cc_t val);
static void start_slc(noit_console_closure_t ncct, int getit);
static int end_slc(noit_console_closure_t ncct, unsigned char **bufp);
static void process_slc(noit_console_closure_t ncct, unsigned char func, unsigned char flag, cc_t val);
static void change_slc(noit_console_closure_t ncct, char func, char flag, cc_t val);
static void check_slc(noit_console_closure_t ncct);
static void do_opt_slc(noit_console_closure_t ncct, unsigned char *ptr, int len);

#ifdef LINEMODE
static void
send_slc(noit_console_closure_t ncct)
{
	int i;

	/*
	 * Send out list of triplets of special characters
	 * to client.  We only send info on the characters
	 * that are currently supported.
	 */
	for (i = 1; i <= NSLC; i++) {
		if ((slctab[i].defset.flag & SLC_LEVELBITS) == SLC_NOSUPPORT)
			continue;
		add_slc(ncct, (unsigned char)i, slctab[i].current.flag,
							slctab[i].current.val);
	}

}  /* end of send_slc */

/*
 * default_slc
 *
 * Set pty special characters to all the defaults.
 */
static void
default_slc(noit_console_closure_t ncct)
{
	int i;

	for (i = 1; i <= NSLC; i++) {
		slctab[i].current.val = slctab[i].defset.val;
		if (slctab[i].current.val == (cc_t)(_POSIX_VDISABLE))
			slctab[i].current.flag = SLC_NOSUPPORT;
		else
			slctab[i].current.flag = slctab[i].defset.flag;
		if (slctab[i].sptr) {
			*(slctab[i].sptr) = slctab[i].defset.val;
		}
	}
	slcchange = 1;

}  /* end of default_slc */
#endif	/* LINEMODE */

/*
 * get_slc_defaults
 *
 * Initialize the slc mapping table.
 */
static void
get_slc_defaults(noit_console_closure_t ncct)
{
	int i;

	init_termbuf(ncct);

	for (i = 1; i <= NSLC; i++) {
		slctab[i].defset.flag =
			spcset(ncct, i, &slctab[i].defset.val, &slctab[i].sptr);
		slctab[i].current.flag = SLC_NOSUPPORT;
		slctab[i].current.val = 0;
	}

}  /* end of get_slc_defaults */

#ifdef	LINEMODE
/*
 * add_slc
 *
 * Add an slc triplet to the slc buffer.
 */
static void
add_slc(noit_console_closure_t ncct, char func, char flag, cc_t val)
{

	if ((*slcptr++ = (unsigned char)func) == 0xff)
		*slcptr++ = 0xff;

	if ((*slcptr++ = (unsigned char)flag) == 0xff)
		*slcptr++ = 0xff;

	if ((*slcptr++ = (unsigned char)val) == 0xff)
		*slcptr++ = 0xff;

}  /* end of add_slc */

/*
 * start_slc
 *
 * Get ready to process incoming slc's and respond to them.
 *
 * The parameter getit is non-zero if it is necessary to grab a copy
 * of the terminal control structures.
 */
static void
start_slc(noit_console_closure_t ncct, int getit)
{

	slcchange = 0;
	if (getit)
		init_termbuf(ncct);
	(void) snprintf((char *)slcbuf, 5, "%c%c%c%c",
					IAC, SB, TELOPT_LINEMODE, LM_SLC);
	slcptr = slcbuf + 4;

}  /* end of start_slc */

/*
 * end_slc
 *
 * Finish up the slc negotiation.  If something to send, then send it.
 */
int
end_slc(noit_console_closure_t ncct, unsigned char **bufp)
{
	int len;

	/*
	 * If a change has occured, store the new terminal control
	 * structures back to the terminal driver.
	 */
	if (slcchange) {
		set_termbuf(ncct);
	}

	/*
	 * If the pty state has not yet been fully processed and there is a
	 * deferred slc request from the client, then do not send any
	 * sort of slc negotiation now.  We will respond to the client's
	 * request very soon.
	 */
	if (def_slcbuf && (_terminit == 0)) {
		return(0);
	}

	if (slcptr > (slcbuf + 4)) {
		if (bufp) {
			*bufp = &slcbuf[4];
			return(slcptr - slcbuf - 4);
		} else {
			(void) snprintf((char *)slcptr, 3, "%c%c", IAC, SE);
			slcptr += 2;
			len = slcptr - slcbuf;
			nc_write(ncct, slcbuf, len);
			netflush(ncct);  /* force it out immediately */
		}
	}
	return (0);

}  /* end of end_slc */

/*
 * process_slc
 *
 * Figure out what to do about the client's slc
 */
static void
process_slc(noit_console_closure_t ncct, unsigned char func, unsigned char flag, cc_t val)
{
	int hislevel, mylevel, ack;

	/*
	 * Ensure that we know something about this function
	 */
	if (func > NSLC) {
		add_slc(ncct, func, SLC_NOSUPPORT, 0);
		return;
	}

	/*
	 * Process the special case requests of 0 SLC_DEFAULT 0
	 * and 0 SLC_VARIABLE 0.  Be a little forgiving here, don't
	 * worry about whether the value is actually 0 or not.
	 */
	if (func == 0) {
		if ((flag = flag & SLC_LEVELBITS) == SLC_DEFAULT) {
			default_slc(ncct);
			send_slc(ncct);
		} else if (flag == SLC_VARIABLE) {
			send_slc(ncct);
		}
		return;
	}

	/*
	 * Appears to be a function that we know something about.  So
	 * get on with it and see what we know.
	 */

	hislevel = flag & SLC_LEVELBITS;
	mylevel = slctab[func].current.flag & SLC_LEVELBITS;
	ack = flag & SLC_ACK;
	/*
	 * ignore the command if:
	 * the function value and level are the same as what we already have;
	 * or the level is the same and the ack bit is set
	 */
	if (hislevel == mylevel && (val == slctab[func].current.val || ack)) {
		return;
	} else if (ack) {
		/*
		 * If we get here, we got an ack, but the levels don't match.
		 * This shouldn't happen.  If it does, it is probably because
		 * we have sent two requests to set a variable without getting
		 * a response between them, and this is the first response.
		 * So, ignore it, and wait for the next response.
		 */
		return;
	} else {
		change_slc(ncct, func, flag, val);
	}

}  /* end of process_slc */

/*
 * change_slc
 *
 * Process a request to change one of our special characters.
 * Compare client's request with what we are capable of supporting.
 */
static void
change_slc(noit_console_closure_t ncct, char func, char flag, cc_t val)
{
	int hislevel, mylevel;

	hislevel = flag & SLC_LEVELBITS;
	mylevel = slctab[(int)func].defset.flag & SLC_LEVELBITS;
	/*
	 * If client is setting a function to NOSUPPORT
	 * or DEFAULT, then we can easily and directly
	 * accomodate the request.
	 */
	if (hislevel == SLC_NOSUPPORT) {
		slctab[(int)func].current.flag = flag;
		slctab[(int)func].current.val = (cc_t)_POSIX_VDISABLE;
		flag |= SLC_ACK;
		add_slc(ncct, func, flag, val);
		return;
	}
	if (hislevel == SLC_DEFAULT) {
		/*
		 * Special case here.  If client tells us to use
		 * the default on a function we don't support, then
		 * return NOSUPPORT instead of what we may have as a
		 * default level of DEFAULT.
		 */
		if (mylevel == SLC_DEFAULT) {
			slctab[(int)func].current.flag = SLC_NOSUPPORT;
		} else {
			slctab[(int)func].current.flag = slctab[(int)func].defset.flag;
		}
		slctab[(int)func].current.val = slctab[(int)func].defset.val;
		add_slc(ncct, func, slctab[(int)func].current.flag,
						slctab[(int)func].current.val);
		return;
	}

	/*
	 * Client wants us to change to a new value or he
	 * is telling us that he can't change to our value.
	 * Some of the slc's we support and can change,
	 * some we do support but can't change,
	 * and others we don't support at all.
	 * If we can change it then we have a pointer to
	 * the place to put the new value, so change it,
	 * otherwise, continue the negotiation.
	 */
	if (slctab[(int)func].sptr) {
		/*
		 * We can change this one.
		 */
		slctab[(int)func].current.val = val;
		*(slctab[(int)func].sptr) = val;
		slctab[(int)func].current.flag = flag;
		flag |= SLC_ACK;
		slcchange = 1;
		add_slc(ncct, func, flag, val);
	} else {
		/*
		* It is not possible for us to support this
		* request as he asks.
		*
		* If our level is DEFAULT, then just ack whatever was
		* sent.
		*
		* If he can't change and we can't change,
		* then degenerate to NOSUPPORT.
		*
		* Otherwise we send our level back to him, (CANTCHANGE
		* or NOSUPPORT) and if CANTCHANGE, send
		* our value as well.
		*/
		if (mylevel == SLC_DEFAULT) {
			slctab[(int)func].current.flag = flag;
			slctab[(int)func].current.val = val;
			flag |= SLC_ACK;
		} else if (hislevel == SLC_CANTCHANGE &&
				    mylevel == SLC_CANTCHANGE) {
			flag &= ~SLC_LEVELBITS;
			flag |= SLC_NOSUPPORT;
			slctab[(int)func].current.flag = flag;
		} else {
			flag &= ~SLC_LEVELBITS;
			flag |= mylevel;
			slctab[(int)func].current.flag = flag;
			if (mylevel == SLC_CANTCHANGE) {
				slctab[(int)func].current.val =
					slctab[(int)func].defset.val;
				val = slctab[(int)func].current.val;
			}
		}
		add_slc(ncct, func, flag, val);
	}

}  /* end of change_slc */

#if	defined(USE_TERMIO) && (VEOF == VMIN)
cc_t oldeofc = '\004';
#endif

/*
 * check_slc
 *
 * Check the special characters in use and notify the client if any have
 * changed.  Only those characters that are capable of being changed are
 * likely to have changed.  If a local change occurs, kick the support level
 * and flags up to the defaults.
 */
static void
check_slc(noit_console_closure_t ncct)
{
	int i;

	for (i = 1; i <= NSLC; i++) {
#if	defined(USE_TERMIO) && (VEOF == VMIN)
		/*
		 * In a perfect world this would be a neat little
		 * function.  But in this world, we should not notify
		 * client of changes to the VEOF char when
		 * ICANON is off, because it is not representing
		 * a special character.
		 */
		if (i == SLC_EOF) {
			if (!noit_console_telnet_tty_isediting(ncct))
				continue;
			else if (slctab[i].sptr)
				oldeofc = *(slctab[i].sptr);
		}
#endif	/* defined(USE_TERMIO) && defined(SYSV_TERMIO) */
		if (slctab[i].sptr &&
				(*(slctab[i].sptr) != slctab[i].current.val)) {
			slctab[i].current.val = *(slctab[i].sptr);
			if (*(slctab[i].sptr) == (cc_t)_POSIX_VDISABLE)
				slctab[i].current.flag = SLC_NOSUPPORT;
			else
				slctab[i].current.flag = slctab[i].defset.flag;
			add_slc(ncct, (unsigned char)i, slctab[i].current.flag,
						slctab[i].current.val);
		}
	}
}  /* check_slc */

/*
 * do_opt_slc
 *
 * Process an slc option buffer.  Defer processing of incoming slc's
 * until after the terminal state has been processed.  Save the first slc
 * request that comes along, but discard all others.
 *
 * ptr points to the beginning of the buffer, len is the length.
 */
static void
do_opt_slc(noit_console_closure_t ncct, unsigned char *ptr, int len)
{
	unsigned char func, flag;
	cc_t val;
	unsigned char *end = ptr + len;

	if (_terminit) {  /* go ahead */
		while (ptr < end) {
			func = *ptr++;
			if (ptr >= end) break;
			flag = *ptr++;
			if (ptr >= end) break;
			val = (cc_t)*ptr++;

			process_slc(ncct, func, flag, val);

		}
	} else {
		/*
		 * save this slc buffer if it is the first, otherwise dump
		 * it.
		 */
		if (def_slcbuf == (unsigned char *)0) {
			def_slclen = len;
			def_slcbuf = (unsigned char *)malloc((unsigned)len);
			if (def_slcbuf == (unsigned char *)0)
				return;  /* too bad */
			memmove(def_slcbuf, ptr, len);
		}
	}

}  /* end of do_opt_slc */

/*
 * deferslc
 *
 * Do slc stuff that was deferred.
 */
static void
deferslc(noit_console_closure_t ncct)
{
	if (def_slcbuf) {
		start_slc(ncct, 1);
		do_opt_slc(ncct, def_slcbuf, def_slclen);
		(void) end_slc(ncct, 0);
		free(def_slcbuf);
		def_slcbuf = (unsigned char *)0;
		def_slclen = 0;
	}

}  /* end of deferslc */
#endif

#ifdef	LINEMODE
/*
 * tty_flowmode()	Find out if flow control is enabled or disabled.
 * tty_linemode()	Find out if linemode (external processing) is enabled.
 * tty_setlinemod(on)	Turn on/off linemode.
 * tty_isecho()		Find out if echoing is turned on.
 * tty_setecho(on)	Enable/disable character echoing.
 * tty_israw()		Find out if terminal is in RAW mode.
 * tty_binaryin(on)	Turn on/off BINARY on input.
 * tty_binaryout(on)	Turn on/off BINARY on output.
 * tty_isediting()	Find out if line editing is enabled.
 * tty_istrapsig()	Find out if signal trapping is enabled.
 * tty_setedit(on)	Turn on/off line editing.
 * tty_setsig(on)	Turn on/off signal trapping.
 * tty_issofttab()	Find out if tab expansion is enabled.
 * tty_setsofttab(on)	Turn on/off soft tab expansion.
 * tty_islitecho()	Find out if typed control chars are echoed literally
 * tty_setlitecho()	Turn on/off literal echo of control chars
 * tty_tspeed(val)	Set transmit speed to val.
 * tty_rspeed(val)	Set receive speed to val.
 */


int
noit_console_telnet_tty_linemode(noit_console_closure_t ncct)
{
#ifndef    USE_TERMIO
#ifdef TS_EXTPROC
    return(termbuf.state & TS_EXTPROC);
#else
    return 0;
#endif
#else
#ifdef EXTPROC
    return(termbuf.c_lflag & EXTPROC);
#endif
#endif
}

void
noit_console_telnet_tty_setlinemode(noit_console_closure_t ncct, int on)
{
#ifdef    TIOCEXT
    set_termbuf(ncct);
    (void) ioctl(pty, TIOCEXT, (char *)&on);
    init_termbuf(ncct);
#else    /* !TIOCEXT */
# ifdef    EXTPROC
    if (on)
        termbuf.c_lflag |= EXTPROC;
    else
        termbuf.c_lflag &= ~EXTPROC;
# endif
#endif    /* TIOCEXT */
}

void
noit_console_telnet_tty_setsig(noit_console_closure_t ncct, int on)
{
#ifndef	USE_TERMIO
	if (on)
		;
#else
	if (on)
		termbuf.c_lflag |= ISIG;
	else
		termbuf.c_lflag &= ~ISIG;
#endif
}

void
noit_console_telnet_tty_setedit(noit_console_closure_t ncct, int on)
{
#ifndef USE_TERMIO
    if (on)
        termbuf.sg.sg_flags &= ~CBREAK;
    else
        termbuf.sg.sg_flags |= CBREAK;
#else
    if (on)
        termbuf.c_lflag |= ICANON;
    else
        termbuf.c_lflag &= ~ICANON;
#endif
}
#endif	/* LINEMODE */

int
noit_console_telnet_tty_istrapsig(noit_console_closure_t ncct)
{
#ifndef USE_TERMIO
    return(!(termbuf.sg.sg_flags&RAW));
#else
    return(termbuf.c_lflag & ISIG);
#endif
}

#ifdef	LINEMODE
int
noit_console_telnet_tty_isediting(noit_console_closure_t ncct)
{
#ifndef USE_TERMIO
    return(!(termbuf.sg.sg_flags & (CBREAK|RAW)));
#else
    return(termbuf.c_lflag & ICANON);
#endif
}
#endif

int
noit_console_telnet_tty_issofttab(noit_console_closure_t ncct)
{
#ifndef    USE_TERMIO
    return (termbuf.sg.sg_flags & XTABS);
#else
# ifdef    OXTABS
    return (termbuf.c_oflag & OXTABS);
# endif
# ifdef    TABDLY
    return ((termbuf.c_oflag & TABDLY) == TAB3);
# endif
#endif
}

void
noit_console_telnet_tty_setsofttab(noit_console_closure_t ncct, int on)
{
#ifndef    USE_TERMIO
    if (on)
        termbuf.sg.sg_flags |= XTABS;
    else
        termbuf.sg.sg_flags &= ~XTABS;
#else
    if (on) {
# ifdef    OXTABS
        termbuf.c_oflag |= OXTABS;
# endif
# ifdef    TABDLY
        termbuf.c_oflag &= ~TABDLY;
        termbuf.c_oflag |= TAB3;
# endif
    } else {
# ifdef    OXTABS
        termbuf.c_oflag &= ~OXTABS;
# endif
# ifdef    TABDLY
        termbuf.c_oflag &= ~TABDLY;
        termbuf.c_oflag |= TAB0;
# endif
    }
#endif
}

int
noit_console_telnet_tty_islitecho(noit_console_closure_t ncct)
{
#ifndef    USE_TERMIO
    return (!(termbuf.lflags & LCTLECH));
#else
# ifdef    ECHOCTL
    return (!(termbuf.c_lflag & ECHOCTL));
# endif
# ifdef    TCTLECH
    return (!(termbuf.c_lflag & TCTLECH));
# endif
# if    !defined(ECHOCTL) && !defined(TCTLECH)
    return (0);    /* assumes ctl chars are echoed '^x' */
# endif
#endif
}

void
noit_console_telnet_tty_setlitecho(noit_console_closure_t ncct, int on)
{
#ifndef    USE_TERMIO
    if (on)
        termbuf.lflags &= ~LCTLECH;
    else
        termbuf.lflags |= LCTLECH;
#else
# ifdef    ECHOCTL
    if (on)
        termbuf.c_lflag &= ~ECHOCTL;
    else
        termbuf.c_lflag |= ECHOCTL;
# endif
# ifdef    TCTLECH
    if (on)
        termbuf.c_lflag &= ~TCTLECH;
    else
        termbuf.c_lflag |= TCTLECH;
# endif
#endif
}

int
noit_console_telnet_tty_iscrnl(noit_console_closure_t ncct)
{
#ifndef	USE_TERMIO
    return (termbuf.sg.sg_flags & CRMOD);
#else
    return (termbuf.c_iflag & ICRNL);
#endif
}

void
noit_console_telnet_tty_tspeed(noit_console_closure_t ncct, int val)
{
#ifdef	DECODE_BAUD
    struct termspeeds *tp;

    for (tp = termspeeds; (tp->speed != -1) && (val > tp->speed); tp++)
	;
    if (tp->speed == -1)	/* back up to last valid value */
	--tp;
    cfsetospeed(&termbuf, tp->value);
#else	/* DECODE_BUAD */
    cfsetospeed(&termbuf, val);
#endif	/* DECODE_BUAD */
}

void
noit_console_telnet_tty_rspeed(noit_console_closure_t ncct, int val)
{
#ifdef	DECODE_BAUD
    struct termspeeds *tp;

    for (tp = termspeeds; (tp->speed != -1) && (val > tp->speed); tp++)
	;
    if (tp->speed == -1)	/* back up to last valid value */
	--tp;
    cfsetispeed(&termbuf, tp->value);
#else	/* DECODE_BAUD */
    cfsetispeed(&termbuf, val);
#endif	/* DECODE_BAUD */
}


int
noit_console_telnet_tty_flowmode(noit_console_closure_t ncct)
{
#ifndef USE_TERMIO
    return(((termbuf.tc.t_startc) > 0 && (termbuf.tc.t_stopc) > 0) ? 1 : 0);
#else
    return((termbuf.c_iflag & IXON) ? 1 : 0);
#endif
}

void
noit_console_telnet_tty_binaryin(noit_console_closure_t ncct, int on)
{
#ifndef	USE_TERMIO
    if (on)
        termbuf.lflags |= LPASS8;
    else
        termbuf.lflags &= ~LPASS8;
#else
    if (on)
        termbuf.c_iflag &= ~ISTRIP;
    else
        termbuf.c_iflag |= ISTRIP;
#endif
}

int
noit_console_telnet_tty_isbinaryin(noit_console_closure_t ncct)
{
#ifndef	USE_TERMIO
	return(termbuf.lflags & LPASS8);
#else
	return(!(termbuf.c_iflag & ISTRIP));
#endif
}

void
noit_console_telnet_tty_binaryout(noit_console_closure_t ncct, int on)
{
#ifndef	USE_TERMIO
    if (on)
        termbuf.lflags |= LLITOUT;
    else
        termbuf.lflags &= ~LLITOUT;
#else
    if (on) {
	termbuf.c_cflag &= ~(CSIZE|PARENB);
	termbuf.c_cflag |= CS8;
	termbuf.c_oflag &= ~OPOST;
    } else {
	termbuf.c_cflag &= ~CSIZE;
	termbuf.c_cflag |= CS7|PARENB;
	termbuf.c_oflag |= OPOST;
    }
#endif
}

int
noit_console_telnet_tty_isbinaryout(noit_console_closure_t ncct)
{
#ifndef	USE_TERMIO
	return(termbuf.lflags & LLITOUT);
#else
	return(!(termbuf.c_oflag&OPOST));
#endif
}

int
noit_console_telnet_tty_restartany(noit_console_closure_t ncct)
{
#ifndef USE_TERMIO
# ifdef	DECCTQ
    return((termbuf.lflags & DECCTQ) ? 0 : 1);
# else
    return(-1);
# endif
#else
    return((termbuf.c_iflag & IXANY) ? 1 : 0);
#endif
}

int
noit_console_telnet_tty_isecho(noit_console_closure_t ncct)
{
#ifndef USE_TERMIO
	return (termbuf.sg.sg_flags & ECHO);
#else
	return (termbuf.c_lflag & ECHO);
#endif
}

void
noit_console_telnet_tty_setecho(noit_console_closure_t ncct, int on)
{
    int i;
    i = ECHO;
#ifdef CRMOD
    i |= CRMOD;
#endif
#ifndef	USE_TERMIO
    if (on)
        termbuf.sg.sg_flags |= i;
    else
        termbuf.sg.sg_flags &= ~(i);
#else
    if (on)
        termbuf.c_lflag |= i;
    else
        termbuf.c_lflag &= ~i;
#endif
}

static void
init_termbuf(noit_console_closure_t ncct)
{
#ifndef	USE_TERMIO
	(void) ioctl(pty, TIOCGETP, (char *)&termbuf.sg);
	(void) ioctl(pty, TIOCGETC, (char *)&termbuf.tc);
	(void) ioctl(pty, TIOCGLTC, (char *)&termbuf.ltc);
# ifdef	TIOCGSTATE
	(void) ioctl(pty, TIOCGSTATE, (char *)&termbuf.state);
# endif
#else
	(void) tcgetattr(pty, &termbuf);
#endif
	termbuf2 = termbuf;
}

#if	defined(LINEMODE) && defined(TIOCPKT_IOCTL)
void
copy_termbuf(noit_console_closure_t ncct, char *cp, size_t len)
{
	if (len > sizeof(termbuf))
		len = sizeof(termbuf);
	memmove((char *)&termbuf, cp, len);
	termbuf2 = termbuf;
}
#endif	/* defined(LINEMODE) && defined(TIOCPKT_IOCTL) */

static void
set_termbuf(noit_console_closure_t ncct)
{
	/*
	 * Only make the necessary changes.
	 */
#ifndef	USE_TERMIO
	if (memcmp((char *)&termbuf.sg, (char *)&termbuf2.sg,
							sizeof(termbuf.sg)))
		(void) ioctl(pty, TIOCSETN, (char *)&termbuf.sg);
	if (memcmp((char *)&termbuf.tc, (char *)&termbuf2.tc,
							sizeof(termbuf.tc)))
		(void) ioctl(pty, TIOCSETC, (char *)&termbuf.tc);
	if (memcmp((char *)&termbuf.ltc, (char *)&termbuf2.ltc,
							sizeof(termbuf.ltc)))
		(void) ioctl(pty, TIOCSLTC, (char *)&termbuf.ltc);
	if (termbuf.lflags != termbuf2.lflags)
		(void) ioctl(pty, TIOCLSET, (char *)&termbuf.lflags);
#else	/* USE_TERMIO */
	if (memcmp((char *)&termbuf, (char *)&termbuf2, sizeof(termbuf)))
		(void) tcsetattr(pty, TCSANOW, &termbuf);
#endif	/* USE_TERMIO */
}

#ifdef	LINEMODE
/*
 * localstat
 *
 * This function handles all management of linemode.
 *
 * Linemode allows the client to do the local editing of data
 * and send only complete lines to the server.  Linemode state is
 * based on the state of the pty driver.  If the pty is set for
 * external processing, then we can use linemode.  Further, if we
 * can use real linemode, then we can look at the edit control bits
 * in the pty to determine what editing the client should do.
 *
 * Linemode support uses the following state flags to keep track of
 * current and desired linemode state.
 *	alwayslinemode : true if -l was specified on the telnetd
 * 	command line.  It means to have linemode on as much as
 *	possible.
 *
 * 	lmodetype: signifies whether the client can
 *	handle real linemode, or if use of kludgeomatic linemode
 *	is preferred.  It will be set to one of the following:
 *		REAL_LINEMODE : use linemode option
 *		NO_KLUDGE : don't initiate kludge linemode.
 *		KLUDGE_LINEMODE : use kludge linemode
 *		NO_LINEMODE : client is ignorant of linemode
 *
 *	linemode, uselinemode : linemode is true if linemode
 *	is currently on, uselinemode is the state that we wish
 *	to be in.  If another function wishes to turn linemode
 *	on or off, it sets or clears uselinemode.
 *
 *	editmode, useeditmode : like linemode/uselinemode, but
 *	these contain the edit mode states (edit and trapsig).
 *
 * The state variables correspond to some of the state information
 * in the pty.
 *	linemode:
 *		In real linemode, this corresponds to whether the pty
 *		expects external processing of incoming data.
 *		In kludge linemode, this more closely corresponds to the
 *		whether normal processing is on or not.  (ICANON in
 *		system V, or COOKED mode in BSD.)
 *		If the -l option was specified (alwayslinemode), then
 *		an attempt is made to force external processing on at
 *		all times.
 *
 * The following heuristics are applied to determine linemode
 * handling within the server.
 *	1) Early on in starting up the server, an attempt is made
 *	   to negotiate the linemode option.  If this succeeds
 *	   then lmodetype is set to REAL_LINEMODE and all linemode
 *	   processing occurs in the context of the linemode option.
 *	2) If the attempt to negotiate the linemode option failed,
 *	   and the "-k" (don't initiate kludge linemode) isn't set,
 *	   then we try to use kludge linemode.  We test for this
 *	   capability by sending "do Timing Mark".  If a positive
 *	   response comes back, then we assume that the client
 *	   understands kludge linemode (ech!) and the
 *	   lmodetype flag is set to KLUDGE_LINEMODE.
 *	3) Otherwise, linemode is not supported at all and
 *	   lmodetype remains set to NO_LINEMODE (which happens
 *	   to be 0 for convenience).
 *	4) At any time a command arrives that implies a higher
 *	   state of linemode support in the client, we move to that
 *	   linemode support.
 *
 * A short explanation of kludge linemode is in order here.
 *	1) The heuristic to determine support for kludge linemode
 *	   is to send a do timing mark.  We assume that a client
 *	   that supports timing marks also supports kludge linemode.
 *	   A risky proposition at best.
 *	2) Further negotiation of linemode is done by changing the
 *	   the server's state regarding SGA.  If server will SGA,
 *	   then linemode is off, if server won't SGA, then linemode
 *	   is on.
 */
static void
localstat(noit_console_closure_t ncct)
{
	int need_will_echo = 0;

	/*
	 * Check for changes to flow control if client supports it.
	 */
	flowstat(ncct);

	/*
	 * Check linemode on/off state
	 */
	uselinemode = noit_console_telnet_tty_linemode(ncct);

	/*
	 * If alwayslinemode is on, and pty is changing to turn it off, then
	 * force linemode back on.
	 */
	if (alwayslinemode && linemode && !uselinemode) {
		uselinemode = 1;
		noit_console_telnet_tty_setlinemode(ncct, uselinemode);
	}

	if (uselinemode) {
		/*
		 * Check for state of BINARY options.
		 *
		 * We only need to do the binary dance if we are actually going
		 * to use linemode.  As this confuses some telnet clients
		 * that don't support linemode, and doesn't gain us
		 * anything, we don't do it unless we're doing linemode.
		 * -Crh (henrich@msu.edu)
		 */

		if (noit_console_telnet_tty_isbinaryin(ncct)) {
			if (his_want_state_is_wont(TELOPT_BINARY))
				send_do(TELOPT_BINARY, 1);
		} else {
			if (his_want_state_is_will(TELOPT_BINARY))
				send_dont(TELOPT_BINARY, 1);
		}

		if (noit_console_telnet_tty_isbinaryout(ncct)) {
			if (my_want_state_is_wont(TELOPT_BINARY))
				send_will(TELOPT_BINARY, 1);
		} else {
			if (my_want_state_is_will(TELOPT_BINARY))
				send_wont(TELOPT_BINARY, 1);
		}
	}


	/*
	 * Do echo mode handling as soon as we know what the
	 * linemode is going to be.
	 * If the pty has echo turned off, then tell the client that
	 * the server will echo.  If echo is on, then the server
	 * will echo if in character mode, but in linemode the
	 * client should do local echoing.  The state machine will
	 * not send anything if it is unnecessary, so don't worry
	 * about that here.
	 *
	 * If we need to send the WILL ECHO (because echo is off),
	 * then delay that until after we have changed the MODE.
	 * This way, when the user is turning off both editing
	 * and echo, the client will get editing turned off first.
	 * This keeps the client from going into encryption mode
	 * and then right back out if it is doing auto-encryption
	 * when passwords are being typed.
	 */
	if (uselinemode) {
		if (noit_console_telnet_tty_isecho(ncct))
			send_wont(TELOPT_ECHO, 1);
		else
			need_will_echo = 1;
#ifdef	KLUDGELINEMODE
		if (lmodetype == KLUDGE_OK)
			lmodetype = KLUDGE_LINEMODE;
#endif
	}

	/*
	 * If linemode is being turned off, send appropriate
	 * command and then we're all done.
	 */
	 if (!uselinemode && linemode) {
# ifdef	KLUDGELINEMODE
		if (lmodetype == REAL_LINEMODE) {
# endif	/* KLUDGELINEMODE */
			send_dont(TELOPT_LINEMODE, 1);
# ifdef	KLUDGELINEMODE
		} else if (lmodetype == KLUDGE_LINEMODE)
			send_will(TELOPT_SGA, 1);
# endif	/* KLUDGELINEMODE */
		send_will(TELOPT_ECHO, 1);
		linemode = uselinemode;
		goto done;
	}

# ifdef	KLUDGELINEMODE
	/*
	 * If using real linemode check edit modes for possible later use.
	 * If we are in kludge linemode, do the SGA negotiation.
	 */
	if (lmodetype == REAL_LINEMODE) {
# endif	/* KLUDGELINEMODE */
		useeditmode = 0;
		if (noit_console_telnet_tty_isediting(ncct))
			useeditmode |= MODE_EDIT;
		if (noit_console_telnet_tty_istrapsig(ncct))
			useeditmode |= MODE_TRAPSIG;
		if (noit_console_telnet_tty_issofttab(ncct))
			useeditmode |= MODE_SOFT_TAB;
		if (noit_console_telnet_tty_islitecho(ncct))
			useeditmode |= MODE_LIT_ECHO;
# ifdef	KLUDGELINEMODE
	} else if (lmodetype == KLUDGE_LINEMODE) {
		if (noit_console_telnet_tty_isediting(ncct) && uselinemode)
			send_wont(TELOPT_SGA, 1);
		else
			send_will(TELOPT_SGA, 1);
	}
# endif	/* KLUDGELINEMODE */

	/*
	 * Negotiate linemode on if pty state has changed to turn it on.
	 * Send appropriate command and send along edit mode, then all done.
	 */
	if (uselinemode && !linemode) {
# ifdef	KLUDGELINEMODE
		if (lmodetype == KLUDGE_LINEMODE) {
			send_wont(TELOPT_SGA, 1);
		} else if (lmodetype == REAL_LINEMODE) {
# endif	/* KLUDGELINEMODE */
			send_do(TELOPT_LINEMODE, 1);
			/* send along edit modes */
			nc_printf(ncct, "%c%c%c%c%c%c%c", IAC, SB,
				TELOPT_LINEMODE, LM_MODE, useeditmode,
				IAC, SE);
			editmode = useeditmode;
# ifdef	KLUDGELINEMODE
		}
# endif	/* KLUDGELINEMODE */
		linemode = uselinemode;
		goto done;
	}

# ifdef	KLUDGELINEMODE
	/*
	 * None of what follows is of any value if not using
	 * real linemode.
	 */
	if (lmodetype < REAL_LINEMODE)
		goto done;
# endif	/* KLUDGELINEMODE */

	if (linemode && his_state_is_will(TELOPT_LINEMODE)) {
		/*
		 * If edit mode changed, send edit mode.
		 */
		 if (useeditmode != editmode) {
			/*
			 * Send along appropriate edit mode mask.
			 */
			nc_printf(ncct, "%c%c%c%c%c%c%c", IAC, SB,
				TELOPT_LINEMODE, LM_MODE, useeditmode,
				IAC, SE);
			editmode = useeditmode;
		}


		/*
		 * Check for changes to special characters in use.
		 */
		start_slc(ncct, 0);
		check_slc(ncct);
		(void) end_slc(ncct, 0);
	}

done:
	if (need_will_echo)
		send_will(TELOPT_ECHO, 1);
	/*
	 * Some things should be deferred until after the pty state has
	 * been set by the local process.  Do those things that have been
	 * deferred now.  This only happens once.
	 */
	if (_terminit == 0) {
		_terminit = 1;
		defer_terminit(ncct);
	}

	netflush(ncct);
	set_termbuf(ncct);
	return;

}  /* end of localstat */
#endif	/* LINEMODE */

/*
 * flowstat
 *
 * Check for changes to flow control
 */
static void
flowstat(noit_console_closure_t ncct)
{
    if (his_state_is_will(TELOPT_LFLOW)) {
	if (noit_console_telnet_tty_flowmode(ncct) != flowmode) {
	    flowmode = noit_console_telnet_tty_flowmode(ncct);
	    nc_printf(ncct, "%c%c%c%c%c%c",
			IAC, SB, TELOPT_LFLOW,
			flowmode ? LFLOW_ON : LFLOW_OFF,
			IAC, SE);
	}
	if (noit_console_telnet_tty_restartany(ncct) != restartany) {
	    restartany = noit_console_telnet_tty_restartany(ncct);
	    nc_printf(ncct, "%c%c%c%c%c%c",
			IAC, SB, TELOPT_LFLOW,
			restartany ? LFLOW_RESTART_ANY
			: LFLOW_RESTART_XON,
			IAC, SE);
	}
    }
}

/*
 * clientstat
 *
 * Process linemode related requests from the client.
 * Client can request a change to only one of linemode, editmode or slc's
 * at a time, and if using kludge linemode, then only linemode may be
 * affected.
 */
void
clientstat(noit_console_closure_t ncct, int code, int parm1, int parm2)
{
    /*
     * Get a copy of terminal characteristics.
     */
    init_termbuf(ncct);

    /*
     * Process request from client. code tells what it is.
     */
    switch (code) {
#ifdef	LINEMODE
	case TELOPT_LINEMODE:
		/*
		 * Don't do anything unless client is asking us to change
		 * modes.
		 */
		uselinemode = (parm1 == WILL);
		if (uselinemode != linemode) {
# ifdef	KLUDGELINEMODE
			/*
			 * If using kludge linemode, make sure that
			 * we can do what the client asks.
			 * We can not turn off linemode if alwayslinemode
			 * and the ICANON bit is set.
			 */
			if (lmodetype == KLUDGE_LINEMODE) {
				if (alwayslinemode && noit_console_telnet_tty_isediting(ncct)) {
					uselinemode = 1;
				}
			}

			/*
			 * Quit now if we can't do it.
			 */
			if (uselinemode == linemode)
				return;

			/*
			 * If using real linemode and linemode is being
			 * turned on, send along the edit mode mask.
			 */
			if (lmodetype == REAL_LINEMODE && uselinemode)
# else	/* KLUDGELINEMODE */
			if (uselinemode)
# endif	/* KLUDGELINEMODE */
			{
				useeditmode = 0;
				if (noit_console_telnet_tty_isediting(ncct))
					useeditmode |= MODE_EDIT;
				if (noit_console_telnet_tty_istrapsig(ncct))
					useeditmode |= MODE_TRAPSIG;
				if (noit_console_telnet_tty_issofttab(ncct))
					useeditmode |= MODE_SOFT_TAB;
				if (noit_console_telnet_tty_islitecho(ncct))
					useeditmode |= MODE_LIT_ECHO;
				nc_printf(ncct, "%c%c%c%c%c%c%c", IAC,
					SB, TELOPT_LINEMODE, LM_MODE,
							useeditmode, IAC, SE);
				editmode = useeditmode;
			}


			noit_console_telnet_tty_setlinemode(ncct, uselinemode);

			linemode = uselinemode;

			if (!linemode)
				send_will(TELOPT_ECHO, 1);
		}
		break;

	case LM_MODE:
	    {
		int ack, changed;

		/*
		 * Client has sent along a mode mask.  If it agrees with
		 * what we are currently doing, ignore it; if not, it could
		 * be viewed as a request to change.  Note that the server
		 * will change to the modes in an ack if it is different from
		 * what we currently have, but we will not ack the ack.
		 */
		 useeditmode &= MODE_MASK;
		 ack = (useeditmode & MODE_ACK);
		 useeditmode &= ~MODE_ACK;

		 if ((changed = (useeditmode ^ editmode))) {
			/*
			 * This check is for a timing problem.  If the
			 * state of the tty has changed (due to the user
			 * application) we need to process that info
			 * before we write in the state contained in the
			 * ack!!!  This gets out the new MODE request,
			 * and when the ack to that command comes back
			 * we'll set it and be in the right mode.
			 */
#ifdef LINEMODE
			if (ack)
				localstat(ncct);
#endif
			if (changed & MODE_EDIT)
				noit_console_telnet_tty_setedit(ncct, useeditmode & MODE_EDIT);

			if (changed & MODE_TRAPSIG)
				noit_console_telnet_tty_setsig(ncct, useeditmode & MODE_TRAPSIG);

			if (changed & MODE_SOFT_TAB)
				noit_console_telnet_tty_setsofttab(ncct, useeditmode & MODE_SOFT_TAB);

			if (changed & MODE_LIT_ECHO)
				noit_console_telnet_tty_setlitecho(ncct, useeditmode & MODE_LIT_ECHO);

			set_termbuf(ncct);

 			if (!ack) {
				nc_printf(ncct, "%c%c%c%c%c%c%c", IAC,
					SB, TELOPT_LINEMODE, LM_MODE,
 					useeditmode|MODE_ACK,
 					IAC, SE);
 			}

			editmode = useeditmode;
		}

		break;

	    }  /* end of case LM_MODE */
#endif	/* LINEMODE */
    case TELOPT_NAWS:
#ifdef	TIOCSWINSZ
	{
	    struct winsize ws;

	    def_col = parm1;
	    def_row = parm2;

	    /*
	     * Change window size as requested by client.
	     */

	    ws.ws_col = parm1;
	    ws.ws_row = parm2;
	    ioctl(pty, TIOCSWINSZ, (char *)&ws);
	}
#endif	/* TIOCSWINSZ */

    break;

    case TELOPT_TSPEED:
	{
	    def_tspeed = parm1;
	    def_rspeed = parm2;
	    /*
	     * Change terminal speed as requested by client.
	     * We set the receive speed first, so that if we can't
	     * store seperate receive and transmit speeds, the transmit
	     * speed will take precedence.
	     */
	    noit_console_telnet_tty_rspeed(ncct, parm2);
	    noit_console_telnet_tty_tspeed(ncct, parm1);
	    set_termbuf(ncct);

	    break;

	}  /* end of case TELOPT_TSPEED */

    default:
	/* What? */
	break;
    }  /* end of switch */

    netflush(ncct);
}

#ifdef	LINEMODE
/*
 * defer_terminit
 *
 * Some things should not be done until after the login process has started
 * and all the pty modes are set to what they are supposed to be.  This
 * function is called when the pty state has been processed for the first time.
 * It calls other functions that do things that were deferred in each module.
 */
static void
defer_terminit(noit_console_closure_t ncct)
{

	/*
	 * local stuff that got deferred.
	 */
	if (def_tspeed != -1) {
		clientstat(ncct, TELOPT_TSPEED, def_tspeed, def_rspeed);
		def_tspeed = def_rspeed = 0;
	}

#ifdef	TIOCSWINSZ
	if (def_col || def_row) {
		struct winsize ws;

		memset((char *)&ws, 0, sizeof(ws));
		ws.ws_col = def_col;
		ws.ws_row = def_row;
		(void) ioctl(pty, TIOCSWINSZ, (char *)&ws);
	}
#endif

	/*
	 * The only other module that currently defers anything.
	 */
	deferslc(ncct);

}  /* end of defer_terminit */
#endif

/*
 * spcset(func, valp, valpp)
 *
 * This function takes various special characters (func), and
 * sets *valp to the current value of that character, and
 * *valpp to point to where in the "termbuf" structure that
 * value is kept.
 *
 * It returns the SLC_ level of support for this function.
 */

#ifndef	USE_TERMIO
static int
spcset(noit_console_closure_t ncct, int func, cc_t *valp, cc_t **valpp)
{
	switch(func) {
	case SLC_EOF:
		*valp = termbuf.tc.t_eofc;
		*valpp = (cc_t *)&termbuf.tc.t_eofc;
		return(SLC_VARIABLE);
	case SLC_EC:
		*valp = termbuf.sg.sg_erase;
		*valpp = (cc_t *)&termbuf.sg.sg_erase;
		return(SLC_VARIABLE);
	case SLC_EL:
		*valp = termbuf.sg.sg_kill;
		*valpp = (cc_t *)&termbuf.sg.sg_kill;
		return(SLC_VARIABLE);
	case SLC_IP:
		*valp = termbuf.tc.t_intrc;
		*valpp = (cc_t *)&termbuf.tc.t_intrc;
		return(SLC_VARIABLE|SLC_FLUSHIN|SLC_FLUSHOUT);
	case SLC_ABORT:
		*valp = termbuf.tc.t_quitc;
		*valpp = (cc_t *)&termbuf.tc.t_quitc;
		return(SLC_VARIABLE|SLC_FLUSHIN|SLC_FLUSHOUT);
	case SLC_XON:
		*valp = termbuf.tc.t_startc;
		*valpp = (cc_t *)&termbuf.tc.t_startc;
		return(SLC_VARIABLE);
	case SLC_XOFF:
		*valp = termbuf.tc.t_stopc;
		*valpp = (cc_t *)&termbuf.tc.t_stopc;
		return(SLC_VARIABLE);
	case SLC_AO:
		*valp = termbuf.ltc.t_flushc;
		*valpp = (cc_t *)&termbuf.ltc.t_flushc;
		return(SLC_VARIABLE);
	case SLC_SUSP:
		*valp = termbuf.ltc.t_suspc;
		*valpp = (cc_t *)&termbuf.ltc.t_suspc;
		return(SLC_VARIABLE);
	case SLC_EW:
		*valp = termbuf.ltc.t_werasc;
		*valpp = (cc_t *)&termbuf.ltc.t_werasc;
		return(SLC_VARIABLE);
	case SLC_RP:
		*valp = termbuf.ltc.t_rprntc;
		*valpp = (cc_t *)&termbuf.ltc.t_rprntc;
		return(SLC_VARIABLE);
	case SLC_LNEXT:
		*valp = termbuf.ltc.t_lnextc;
		*valpp = (cc_t *)&termbuf.ltc.t_lnextc;
		return(SLC_VARIABLE);
	case SLC_FORW1:
		*valp = termbuf.tc.t_brkc;
		*valpp = (cc_t *)&termbuf.ltc.t_lnextc;
		return(SLC_VARIABLE);
	case SLC_BRK:
	case SLC_SYNCH:
	case SLC_AYT:
	case SLC_EOR:
		*valp = (cc_t)0;
		*valpp = (cc_t *)0;
		return(SLC_DEFAULT);
	default:
		*valp = (cc_t)0;
		*valpp = (cc_t *)0;
		return(SLC_NOSUPPORT);
	}
}

#else	/* USE_TERMIO */


#define	setval(a, b)	*valp = termbuf.c_cc[a]; \
			*valpp = &termbuf.c_cc[a]; \
			return(b);
#define	defval(a) *valp = ((cc_t)a); *valpp = (cc_t *)0; return(SLC_DEFAULT);

int
spcset(noit_console_closure_t ncct, int func, cc_t *valp, cc_t **valpp)
{
	switch(func) {
	case SLC_EOF:
		setval(VEOF, SLC_VARIABLE);
	case SLC_EC:
		setval(VERASE, SLC_VARIABLE);
	case SLC_EL:
		setval(VKILL, SLC_VARIABLE);
	case SLC_IP:
		setval(VINTR, SLC_VARIABLE|SLC_FLUSHIN|SLC_FLUSHOUT);
	case SLC_ABORT:
		setval(VQUIT, SLC_VARIABLE|SLC_FLUSHIN|SLC_FLUSHOUT);
	case SLC_XON:
#ifdef	VSTART
		setval(VSTART, SLC_VARIABLE);
#else
		defval(0x13);
#endif
	case SLC_XOFF:
#ifdef	VSTOP
		setval(VSTOP, SLC_VARIABLE);
#else
		defval(0x11);
#endif
	case SLC_EW:
#ifdef	VWERASE
		setval(VWERASE, SLC_VARIABLE);
#else
		defval(0);
#endif
	case SLC_RP:
#ifdef	VREPRINT
		setval(VREPRINT, SLC_VARIABLE);
#else
		defval(0);
#endif
	case SLC_LNEXT:
#ifdef	VLNEXT
		setval(VLNEXT, SLC_VARIABLE);
#else
		defval(0);
#endif
	case SLC_AO:
#if	!defined(VDISCARD) && defined(VFLUSHO)
# define VDISCARD VFLUSHO
#endif
#ifdef	VDISCARD
		setval(VDISCARD, SLC_VARIABLE|SLC_FLUSHOUT);
#else
		defval(0);
#endif
	case SLC_SUSP:
#ifdef	VSUSP
		setval(VSUSP, SLC_VARIABLE|SLC_FLUSHIN);
#else
		defval(0);
#endif
#ifdef	VEOL
	case SLC_FORW1:
		setval(VEOL, SLC_VARIABLE);
#endif
#ifdef	VEOL2
	case SLC_FORW2:
		setval(VEOL2, SLC_VARIABLE);
#endif
	case SLC_AYT:
#ifdef	VSTATUS
		setval(VSTATUS, SLC_VARIABLE);
#else
		defval(0);
#endif

	case SLC_BRK:
	case SLC_SYNCH:
	case SLC_EOR:
		defval(0);

	default:
		*valp = 0;
		*valpp = 0;
		return(SLC_NOSUPPORT);
	}
}
#endif	/* USE_TERMIO */

void
ptyflush(noit_console_closure_t ncct) {
  if(ncct->telnet->_pty_fill == 0) return;
  write(ncct->pty_slave, ncct->telnet->_pty_buf, ncct->telnet->_pty_fill);
  ncct->telnet->_pty_fill = 0;
}

static void
netclear(noit_console_closure_t ncct) {
  ncct->outbuf_len = 0;
}

static void
pty_write(noit_console_closure_t ncct, void *buf, int len) {
  if(len > sizeof(ncct->telnet->_pty_buf)) {
    int i;
    /* split it up */
    for(i=0; i<len; i+=sizeof(ncct->telnet->_pty_buf)) {
      pty_write(ncct, buf + i, MIN(sizeof(ncct->telnet->_pty_buf),len-i));
    }
  }
  while(ncct->telnet->_pty_fill + len > sizeof(ncct->telnet->_pty_buf)) {
    ptyflush(ncct);
  }
  memcpy(ncct->telnet->_pty_buf + ncct->telnet->_pty_fill, buf, len);
  ncct->telnet->_pty_fill += len;
}

/*
 * Send interrupt to process on other side of pty.
 * If it is in raw mode, just write NULL;
 * otherwise, write intr char.
 */
void
interrupt(noit_console_closure_t ncct)
{
#ifdef	TCSIG
	ptyflush(ncct);	/* half-hearted */
	(void) ioctl(pty, TCSIG, (char *)SIGINT);
#else	/* TCSIG */
	unsigned char ch;
	ptyflush(ncct);	/* half-hearted */
	init_termbuf(ncct);
	ch = slctab[SLC_IP].sptr ?
			(unsigned char)*slctab[SLC_IP].sptr : '\177';
	pty_write(ncct, &ch, 1);
#endif	/* TCSIG */
}

/*
 * Send quit to process on other side of pty.
 * If it is in raw mode, just write NULL;
 * otherwise, write quit char.
 */
void
sendbrk(noit_console_closure_t ncct)
{
#ifdef	TCSIG
	ptyflush(ncct);	/* half-hearted */
	(void) ioctl(pty, TCSIG, (char *)SIGQUIT);
#else	/* TCSIG */
	unsigned char ch;
	ptyflush(ncct);	/* half-hearted */
	init_termbuf(ncct);
	ch = slctab[SLC_ABORT].sptr ?
			(unsigned char)*slctab[SLC_ABORT].sptr : '\034';
	pty_write(ncct, &ch, 1);
#endif	/* TCSIG */
}

void
sendsusp(noit_console_closure_t ncct)
{
#ifdef	SIGTSTP
# ifdef	TCSIG
	ptyflush(ncct);	/* half-hearted */
	(void) ioctl(pty, TCSIG, (char *)SIGTSTP);
# else	/* TCSIG */
	unsigned char ch;
	ptyflush(ncct);	/* half-hearted */
	ch = slctab[SLC_SUSP].sptr ?
			(unsigned char)*slctab[SLC_SUSP].sptr : '\032';
	pty_write(ncct, &ch, 1);
# endif	/* TCSIG */
#endif	/* SIGTSTP */
}

/*
 * When we get an AYT, if ^T is enabled, use that.  Otherwise,
 * just send back "[Yes]".
 */
void
recv_ayt(noit_console_closure_t ncct)
{
#if	defined(SIGINFO) && defined(TCSIG)
	if (slctab[SLC_AYT].sptr && *slctab[SLC_AYT].sptr != _POSIX_VDISABLE) {
		(void) ioctl(pty, TCSIG, (char *)SIGINFO);
		return;
	}
#endif
	nc_printf(ncct, "\r\n[Yes]\r\n");
}

void
doeof(noit_console_closure_t ncct)
{
	unsigned char ch;
	init_termbuf(ncct);

#if	defined(LINEMODE) && defined(USE_TERMIO) && (VEOF == VMIN)
	if (!noit_console_telnet_tty_isediting(ncct)) {
		extern char oldeofc;
		pty_write(ncct, &oldeofc, 1);
		return;
	}
#endif
	ch = slctab[SLC_EOF].sptr ?
			(unsigned char)*slctab[SLC_EOF].sptr : '\004';
	pty_write(ncct, &ch, 1);
}

noit_console_telnet_closure_t
noit_console_telnet_alloc(noit_console_closure_t ncct) {
  int on;
  noit_console_telnet_closure_t telnet, tmp;
  static unsigned char ttytype_sbbuf[] = {
    IAC, SB, TELOPT_TTYPE, TELQUAL_SEND, IAC, SE
  };
  tmp = ncct->telnet;

  ncct->telnet = calloc(1, sizeof(*telnet));
  subbuffer = malloc(1024*64);
  subpointer = subbuffer;
  subend= subbuffer;
  def_tspeed = -1;
  def_rspeed = -1;
  get_slc_defaults(ncct);
  if (my_state_is_wont(TELOPT_SGA))
    send_will(TELOPT_SGA, 1);
  send_do(TELOPT_ECHO, 1);
  send_do(TELOPT_NAWS, 1);
  send_will(TELOPT_STATUS, 1);
  flowmode = 1;		/* default flow control state */
  restartany = -1;	/* uninitialized... */
  send_do(TELOPT_LFLOW, 1);
  if (his_want_state_is_will(TELOPT_ECHO)) {
    willoption(TELOPT_ECHO);
  }
  if (my_state_is_wont(TELOPT_ECHO))
    send_will(TELOPT_ECHO, 1);
  on = 1;
  init_termbuf(ncct);
#ifdef LINEMODE
  localstat(ncct);
#endif
  send_do(TELOPT_TTYPE, 1);
  send_do(TELOPT_TSPEED, 1);
  send_do(TELOPT_XDISPLOC, 1);
  send_do(TELOPT_NEW_ENVIRON, 1);
  send_do(TELOPT_OLD_ENVIRON, 1);
  nc_write(ncct, ttytype_sbbuf, sizeof(ttytype_sbbuf));

  telnet = ncct->telnet;
  ncct->telnet = tmp;
  return telnet;
}
void
noit_console_telnet_free(noit_console_telnet_closure_t telnet) {
  free(telnet->_subbuffer);
  noit_hash_destroy(&telnet->_env, free, free);
  free(telnet);
}

#define	SB_CLEAR()	subpointer = subbuffer
#define	SB_TERM()	{ subend = subpointer; SB_CLEAR(); }
#define	SB_ACCUM(c)	if (subpointer < (subbuffer+sizeof subbuffer)) { \
    *subpointer++ = (c); \
			     }
#define	SB_GET()	((*subpointer++)&0xff)
#define	SB_EOF()	(subpointer >= subend)
#define	SB_LEN()	(subend - subpointer)

#ifdef	ENV_HACK
unsigned char *subsave;
#define SB_SAVE()	subsave = subpointer;
#define	SB_RESTORE()	subpointer = subsave;
#endif


/*
 * State for recv fsm
 */
#define	TS_DATA		0	/* base state */
#define	TS_IAC		1	/* look for double IAC's */
#define	TS_CR		2	/* CR-LF ->'s CR */
#define	TS_SB		3	/* throw away begin's... */
#define	TS_SE		4	/* ...end's (suboption negotiation) */
#define	TS_WILL		5	/* will option negotiation */
#define	TS_WONT		6	/* wont -''- */
#define	TS_DO		7	/* do -''- */
#define	TS_DONT		8	/* dont -''- */

int
noit_console_telnet_telrcv(noit_console_closure_t ncct,
                           const void *buf, int buflen)
{
    int c;
    unsigned char cc;
    unsigned char *netip = (unsigned char *)buf;
    int ncc = buflen;
    static int state = TS_DATA;

    while (ncc > 0) {
	c = *netip++ & 0377, ncc--;
#ifdef ENCRYPTION
	if (decrypt_input)
	    c = (*decrypt_input)(c);
#endif
	switch (state) {

	case TS_CR:
	    state = TS_DATA;
	    /* Strip off \n or \0 after a \r */
	    if ((c == 0) || (c == '\n')) {
		break;
	    }
	    /* FALL THROUGH */

	case TS_DATA:
	    if (c == IAC) {
		state = TS_IAC;
		break;
	    }
	    /*
	     * We now map \r\n ==> \r for pragmatic reasons.
	     * Many client implementations send \r\n when
	     * the user hits the CarriageReturn key.
	     *
	     * We USED to map \r\n ==> \n, since \r\n says
	     * that we want to be in column 1 of the next
	     * printable line, and \n is the standard
	     * unix way of saying that (\r is only good
	     * if CRMOD is set, which it normally is).
	     */
	    if ((c == '\r') && his_state_is_wont(TELOPT_BINARY)) {
		int nc = *netip;
#ifdef ENCRYPTION
		if (decrypt_input)
		    nc = (*decrypt_input)(nc & 0xff);
#endif
		if (linemode && (ncc > 0) && (('\n' == nc) ||
			 ((0 == nc) && noit_console_telnet_tty_iscrnl(ncct))) ) {
			netip++; ncc--;
			c = '\n';
		} else
		{
#ifdef ENCRYPTION
		    if (decrypt_input)
			(void)(*decrypt_input)(-1);
#endif
		    state = TS_CR;
		}
	    }
            cc = (unsigned char)c;
            pty_write(ncct, &cc, 1);
	    break;

	case TS_IAC:
	gotiac:			switch (c) {

	    /*
	     * Send the process on the pty side an
	     * interrupt.  Do this with a NULL or
	     * interrupt char; depending on the tty mode.
	     */
	case IP:
	    interrupt(ncct);
	    break;

	case BREAK:
	    sendbrk(ncct);
	    break;

	    /*
	     * Are You There?
	     */
	case AYT:
	    recv_ayt(ncct);
	    break;

	    /*
	     * Abort Output
	     */
	case AO:
	    {
		ptyflush(ncct);	/* half-hearted */
		init_termbuf(ncct);

		if (slctab[SLC_AO].sptr &&
		    *slctab[SLC_AO].sptr != (cc_t)(_POSIX_VDISABLE)) {
                    cc = (unsigned char)*slctab[SLC_AO].sptr;
                    pty_write(ncct, &cc, 1);
		}

		netclear(ncct);	/* clear buffer back */
		nc_printf (ncct, "%c%c", IAC, DM);
		/* Wha?
                neturg = nfrontp-1;
                */
                /* off by one XXX */
		break;
	    }

	/*
	 * Erase Character and
	 * Erase Line
	 */
	case EC:
	case EL:
	    {
		cc_t ch;

		ptyflush(ncct);	/* half-hearted */
		init_termbuf(ncct);
		if (c == EC)
		    ch = *slctab[SLC_EC].sptr;
		else
		    ch = *slctab[SLC_EL].sptr;
		if (ch != (cc_t)(_POSIX_VDISABLE)) {
                    cc = (unsigned char)ch;
                    pty_write(ncct, &cc, 1);
                }
		break;
	    }

	/*
	 * Check for urgent data...
	 */
	case DM:
#ifdef SUPPORT_OOB
	    SYNCHing = stilloob(net);
	    settimer(gotDM);
#endif
	    break;


	    /*
	     * Begin option subnegotiation...
	     */
	case SB:
	    state = TS_SB;
	    SB_CLEAR();
	    continue;

	case WILL:
	    state = TS_WILL;
	    continue;

	case WONT:
	    state = TS_WONT;
	    continue;

	case DO:
	    state = TS_DO;
	    continue;

	case DONT:
	    state = TS_DONT;
	    continue;
	case EOR:
	    if (his_state_is_will(TELOPT_EOR))
		doeof(ncct);
	    break;

	    /*
	     * Handle RFC 10xx Telnet linemode option additions
	     * to command stream (EOF, SUSP, ABORT).
	     */
	case xEOF:
	    doeof(ncct);
	    break;

	case SUSP:
	    sendsusp(ncct);
	    break;

	case ABORT:
	    sendbrk(ncct);
	    break;

	case IAC:
            cc = (unsigned char)c;
            pty_write(ncct, &cc, 1);
	    break;
	}
	state = TS_DATA;
	break;

	case TS_SB:
	    if (c == IAC) {
		state = TS_SE;
	    } else {
		SB_ACCUM(c);
	    }
	    break;

	case TS_SE:
	    if (c != SE) {
		if (c != IAC) {
		    /*
		     * bad form of suboption negotiation.
		     * handle it in such a way as to avoid
		     * damage to local state.  Parse
		     * suboption buffer found so far,
		     * then treat remaining stream as
		     * another command sequence.
		     */

		    /* for DIAGNOSTICS */
		    SB_ACCUM(IAC);
		    SB_ACCUM(c);
		    subpointer -= 2;

		    SB_TERM();
		    suboption(ncct);
		    state = TS_IAC;
		    goto gotiac;
		}
		SB_ACCUM(c);
		state = TS_SB;
	    } else {
		/* for DIAGNOSTICS */
		SB_ACCUM(IAC);
		SB_ACCUM(SE);
		subpointer -= 2;

		SB_TERM();
		suboption(ncct);	/* handle sub-option */
		state = TS_DATA;
	    }
	    break;

	case TS_WILL:
	    willoption(c);
	    state = TS_DATA;
	    continue;

	case TS_WONT:
	    wontoption(c);
	    if (c==TELOPT_ENCRYPT && his_do_dont_is_changing(TELOPT_ENCRYPT) )
                dontoption(c);
	    state = TS_DATA;
	    continue;

	case TS_DO:
	    dooption(c);
	    state = TS_DATA;
	    continue;

	case TS_DONT:
	    dontoption(c);
	    state = TS_DATA;
	    continue;

	default:
            return -1;
	}
    }
    return 0;
}  /* end of telrcv */

/*
 * The will/wont/do/dont state machines are based on Dave Borman's
 * Telnet option processing state machine.
 *
 * These correspond to the following states:
 *	my_state = the last negotiated state
 *	want_state = what I want the state to go to
 *	want_resp = how many requests I have sent
 * All state defaults are negative, and resp defaults to 0.
 *
 * When initiating a request to change state to new_state:
 *
 * if ((want_resp == 0 && new_state == my_state) || want_state == new_state) {
 *	do nothing;
 * } else {
 *	want_state = new_state;
 *	send new_state;
 *	want_resp++;
 * }
 *
 * When receiving new_state:
 *
 * if (want_resp) {
 *	want_resp--;
 *	if (want_resp && (new_state == my_state))
 *		want_resp--;
 * }
 * if ((want_resp == 0) && (new_state != want_state)) {
 *	if (ok_to_switch_to new_state)
 *		want_state = new_state;
 *	else
 *		want_resp++;
 *	send want_state;
 * }
 * my_state = new_state;
 *
 * Note that new_state is implied in these functions by the function itself.
 * will and do imply positive new_state, wont and dont imply negative.
 *
 * Finally, there is one catch.  If we send a negative response to a
 * positive request, my_state will be the positive while want_state will
 * remain negative.  my_state will revert to negative when the negative
 * acknowlegment arrives from the peer.  Thus, my_state generally tells
 * us not only the last negotiated state, but also tells us what the peer
 * wants to be doing as well.  It is important to understand this difference
 * as we may wish to be processing data streams based on our desired state
 * (want_state) or based on what the peer thinks the state is (my_state).
 *
 * This all works fine because if the peer sends a positive request, the data
 * that we receive prior to negative acknowlegment will probably be affected
 * by the positive state, and we can process it as such (if we can; if we
 * can't then it really doesn't matter).  If it is that important, then the
 * peer probably should be buffering until this option state negotiation
 * is complete.
 *
 */
void
noit_console_telnet_send_do(noit_console_closure_t ncct, int option, int init)
{
    if (init) {
	if ((do_dont_resp[option] == 0 && his_state_is_will(option)) ||
	    his_want_state_is_will(option))
	    return;
	/*
	 * Special case for TELOPT_TM:  We send a DO, but pretend
	 * that we sent a DONT, so that we can send more DOs if
	 * we want to.
	 */
	if (option == TELOPT_TM)
	    set_his_want_state_wont(option);
	else
	    set_his_want_state_will(option);
	do_dont_resp[option]++;
    }
    nc_printf(ncct, (const char *)doopt, option);
}

#ifdef	AUTHENTICATION
extern void auth_request(void);
#endif
#ifdef	ENCRYPTION
extern void encrypt_send_support();
#endif

void
noit_console_telnet_willoption(noit_console_closure_t ncct, int option)
{
    int changeok = 0;
    void (*func)(noit_console_closure_t) = 0;

    /*
     * process input from peer.
     */

    if (do_dont_resp[option]) {
	do_dont_resp[option]--;
	if (do_dont_resp[option] && his_state_is_will(option))
	    do_dont_resp[option]--;
    }
    if (do_dont_resp[option] == 0) {
	if (his_want_state_is_wont(option)) {
	    switch (option) {

	    case TELOPT_BINARY:
		init_termbuf(ncct);
		noit_console_telnet_tty_binaryin(ncct, 1);
		set_termbuf(ncct);
		changeok++;
		break;

	    case TELOPT_ECHO:
		/*
		 * See comments below for more info.
		 */
		not42 = 0;	/* looks like a 4.2 system */
		break;

	    case TELOPT_TM:
#if	defined(LINEMODE) && defined(KLUDGELINEMODE)
			/*
			 * This telnetd implementation does not really
			 * support timing marks, it just uses them to
			 * support the kludge linemode stuff.  If we
			 * receive a will or wont TM in response to our
			 * do TM request that may have been sent to
			 * determine kludge linemode support, process
			 * it, otherwise TM should get a negative
			 * response back.
			 */
			/*
			 * Handle the linemode kludge stuff.
			 * If we are not currently supporting any
			 * linemode at all, then we assume that this
			 * is the client telling us to use kludge
			 * linemode in response to our query.  Set the
			 * linemode type that is to be supported, note
			 * that the client wishes to use linemode, and
			 * eat the will TM as though it never arrived.
			 */
			if (lmodetype < KLUDGE_LINEMODE) {
				lmodetype = KLUDGE_LINEMODE;
				clientstat(ncct, TELOPT_LINEMODE, WILL, 0);
				send_wont(TELOPT_SGA, 1);
			} else if (lmodetype == NO_AUTOKLUDGE) {
				lmodetype = KLUDGE_OK;
			}
#endif	/* defined(LINEMODE) && defined(KLUDGELINEMODE) */
		break;

	    case TELOPT_LFLOW:
		/*
		 * If we are going to support flow control
		 * option, then don't worry peer that we can't
		 * change the flow control characters.
		 */
		slctab[SLC_XON].defset.flag &= ~SLC_LEVELBITS;
		slctab[SLC_XON].defset.flag |= SLC_DEFAULT;
		slctab[SLC_XOFF].defset.flag &= ~SLC_LEVELBITS;
		slctab[SLC_XOFF].defset.flag |= SLC_DEFAULT;
	    case TELOPT_TTYPE:
	    case TELOPT_SGA:
	    case TELOPT_NAWS:
	    case TELOPT_TSPEED:
	    case TELOPT_XDISPLOC:
	    case TELOPT_NEW_ENVIRON:
	    case TELOPT_OLD_ENVIRON:
		changeok++;
		break;

#ifdef	LINEMODE
		case TELOPT_LINEMODE:
# ifdef	KLUDGELINEMODE
			/*
			 * Note client's desire to use linemode.
			 */
			lmodetype = REAL_LINEMODE;
# endif	/* KLUDGELINEMODE */
			func = noit_console_telnet_doclientstat;
			changeok++;
			break;
#endif	/* LINEMODE */

#ifdef	AUTHENTICATION
	    case TELOPT_AUTHENTICATION:
		func = auth_request;
		changeok++;
		break;
#endif

#ifdef	ENCRYPTION
	    case TELOPT_ENCRYPT:
		func = encrypt_send_support;
		changeok++;
		break;
#endif
			
	    default:
		break;
	    }
	    if (changeok) {
		set_his_want_state_will(option);
		send_do(option, 0);
	    } else {
		do_dont_resp[option]++;
		send_dont(option, 0);
	    }
	} else {
	    /*
	     * Option processing that should happen when
	     * we receive conformation of a change in
	     * state that we had requested.
	     */
	    switch (option) {
	    case TELOPT_ECHO:
		not42 = 0;	/* looks like a 4.2 system */
		/*
		 * Egads, he responded "WILL ECHO".  Turn
		 * it off right now!
		 */
		send_dont(option, 1);
		/*
		 * "WILL ECHO".  Kludge upon kludge!
		 * A 4.2 client is now echoing user input at
		 * the tty.  This is probably undesireable and
		 * it should be stopped.  The client will
		 * respond WONT TM to the DO TM that we send to
		 * check for kludge linemode.  When the WONT TM
		 * arrives, linemode will be turned off and a
		 * change propogated to the pty.  This change
		 * will cause us to process the new pty state
		 * in localstat(), which will notice that
		 * linemode is off and send a WILL ECHO
		 * so that we are properly in character mode and
		 * all is well.
		 */
		break;

#ifdef	LINEMODE
		case TELOPT_LINEMODE:
# ifdef	KLUDGELINEMODE
			/*
			 * Note client's desire to use linemode.
			 */
			lmodetype = REAL_LINEMODE;
# endif	/* KLUDGELINEMODE */
			func = noit_console_telnet_doclientstat;
			break;
#endif	/* LINEMODE */
#ifdef	AUTHENTICATION
	    case TELOPT_AUTHENTICATION:
		func = auth_request;
		break;
#endif

#ifdef	ENCRYPTION
	    case TELOPT_ENCRYPT:
		func = encrypt_send_support;
		break;
#endif

	    case TELOPT_LFLOW:
		func = flowstat;
		break;
	    }
	}
    }
    set_his_state_will(option);
    if (func)
	(*func)(ncct);
}  /* end of willoption */

void
noit_console_telnet_send_dont(noit_console_closure_t ncct, int option, int init)
{
    if (init) {
	if ((do_dont_resp[option] == 0 && his_state_is_wont(option)) ||
	    his_want_state_is_wont(option))
	    return;
	set_his_want_state_wont(option);
	do_dont_resp[option]++;
    }
    nc_printf(ncct, (const char *)dont, option);
}

void
noit_console_telnet_wontoption(noit_console_closure_t ncct, int option)
{
    /*
     * Process client input.
	 */

    if (do_dont_resp[option]) {
	do_dont_resp[option]--;
	if (do_dont_resp[option] && his_state_is_wont(option))
	    do_dont_resp[option]--;
    }
    if (do_dont_resp[option] == 0) {
	if (his_want_state_is_will(option)) {
	    /* it is always ok to change to negative state */
	    switch (option) {
	    case TELOPT_ECHO:
		not42 = 1; /* doesn't seem to be a 4.2 system */
		break;

	    case TELOPT_BINARY:
		init_termbuf(ncct);
		noit_console_telnet_tty_binaryin(ncct, 0);
		set_termbuf(ncct);
		break;

#ifdef	LINEMODE
            case TELOPT_LINEMODE:
# ifdef	KLUDGELINEMODE
                /*
		 * If real linemode is supported, then client is
		 * asking to turn linemode off.
		 */
		if (lmodetype != REAL_LINEMODE)
			break;
		lmodetype = KLUDGE_LINEMODE;
# endif	/* KLUDGELINEMODE */
		clientstat(ncct, TELOPT_LINEMODE, WONT, 0);
		break;
#endif	/* LINEMODE */

	    case TELOPT_TM:
		/*
		 * If we get a WONT TM, and had sent a DO TM,
		 * don't respond with a DONT TM, just leave it
		 * as is.  Short circut the state machine to
		 * achive this.
		 */
		set_his_want_state_wont(TELOPT_TM);
		return;

	    case TELOPT_LFLOW:
		/*
		 * If we are not going to support flow control
		 * option, then let peer know that we can't
		 * change the flow control characters.
		 */
		slctab[SLC_XON].defset.flag &= ~SLC_LEVELBITS;
		slctab[SLC_XON].defset.flag |= SLC_CANTCHANGE;
		slctab[SLC_XOFF].defset.flag &= ~SLC_LEVELBITS;
		slctab[SLC_XOFF].defset.flag |= SLC_CANTCHANGE;
		break;

#ifdef AUTHENTICATION
	    case TELOPT_AUTHENTICATION:
		auth_finished(0, AUTH_REJECT);
		break;
#endif

		/*
		 * For options that we might spin waiting for
		 * sub-negotiation, if the client turns off the
		 * option rather than responding to the request,
		 * we have to treat it here as if we got a response
		 * to the sub-negotiation, (by updating the timers)
		 * so that we'll break out of the loop.
		 */
	    case TELOPT_TTYPE:
		settimer(ttypesubopt);
		break;

	    case TELOPT_TSPEED:
		settimer(tspeedsubopt);
		break;

	    case TELOPT_XDISPLOC:
		settimer(xdisplocsubopt);
		break;

	    case TELOPT_OLD_ENVIRON:
		settimer(oenvironsubopt);
		break;

	    case TELOPT_NEW_ENVIRON:
		settimer(environsubopt);
		break;

	    default:
		break;
	    }
	    set_his_want_state_wont(option);
	    if (his_state_is_will(option))
		send_dont(option, 0);
	} else {
	    switch (option) {
	    case TELOPT_TM:
#if	defined(LINEMODE) && defined(KLUDGELINEMODE)
		if (lmodetype < NO_AUTOKLUDGE) {
			lmodetype = NO_LINEMODE;
			clientstat(TELOPT_LINEMODE, WONT, 0);
			send_will(TELOPT_SGA, 1);
			send_will(TELOPT_ECHO, 1);
		}
#endif	/* defined(LINEMODE) && defined(KLUDGELINEMODE) */
		break;

#ifdef AUTHENTICATION
	    case TELOPT_AUTHENTICATION:
		auth_finished(0, AUTH_REJECT);
		break;
#endif
	    default:
		break;
	    }
	}
    }
    set_his_state_wont(option);

}  /* end of wontoption */

void
noit_console_telnet_send_will(noit_console_closure_t ncct, int option, int init)
{
    if (init) {
	if ((will_wont_resp[option] == 0 && my_state_is_will(option))||
	    my_want_state_is_will(option))
	    return;
	set_my_want_state_will(option);
	will_wont_resp[option]++;
    }
    nc_printf (ncct, (const char *)will, option);
}

void
noit_console_telnet_dooption(noit_console_closure_t ncct, int option)
{
    int changeok = 0;

    /*
     * Process client input.
     */

    if (will_wont_resp[option]) {
	will_wont_resp[option]--;
	if (will_wont_resp[option] && my_state_is_will(option))
	    will_wont_resp[option]--;
    }
    if ((will_wont_resp[option] == 0) && (my_want_state_is_wont(option))) {
	switch (option) {
	case TELOPT_ECHO:
#ifdef	LINEMODE
# ifdef	KLUDGELINEMODE
	    if (lmodetype == NO_LINEMODE)
# else
	    if (his_state_is_wont(TELOPT_LINEMODE))
# endif
#endif
	    {
		init_termbuf(ncct);
		noit_console_telnet_tty_setecho(ncct, 1);
		set_termbuf(ncct);
	    }
	changeok++;
	break;

	case TELOPT_BINARY:
	    init_termbuf(ncct);
	    noit_console_telnet_tty_binaryout(ncct, 1);
	    set_termbuf(ncct);
	    changeok++;
	    break;

	case TELOPT_SGA:
#if	defined(LINEMODE) && defined(KLUDGELINEMODE)
		/*
		 * If kludge linemode is in use, then we must
		 * process an incoming do SGA for linemode
		 * purposes.
		 */
	    if (lmodetype == KLUDGE_LINEMODE) {
		/*
		 * Receipt of "do SGA" in kludge
		 * linemode is the peer asking us to
		 * turn off linemode.  Make note of
		 * the request.
		 */
		clientstat(TELOPT_LINEMODE, WONT, 0);
		/*
		 * If linemode did not get turned off
		 * then don't tell peer that we did.
		 * Breaking here forces a wont SGA to
		 * be returned.
		 */
		if (linemode)
			break;
	    }
#else
	    turn_on_sga = 0;
#endif	/* defined(LINEMODE) && defined(KLUDGELINEMODE) */
	    changeok++;
	    break;

	case TELOPT_STATUS:
	    changeok++;
	    break;

	case TELOPT_TM:
	    /*
	     * Special case for TM.  We send a WILL, but
	     * pretend we sent a WONT.
	     */
	    send_will(option, 0);
	    set_my_want_state_wont(option);
	    set_my_state_wont(option);
	    return;

	case TELOPT_LOGOUT:
	    /*
	     * When we get a LOGOUT option, respond
	     * with a WILL LOGOUT, make sure that
	     * it gets written out to the network,
	     * and then just go away...
	     */
	    set_my_want_state_will(TELOPT_LOGOUT);
	    send_will(TELOPT_LOGOUT, 0);
	    set_my_state_will(TELOPT_LOGOUT);
	    netflush(ncct);
            return;
	    break;

#ifdef ENCRYPTION
	case TELOPT_ENCRYPT:
	    changeok++;
	    break;
#endif
	case TELOPT_LINEMODE:
	case TELOPT_TTYPE:
	case TELOPT_NAWS:
	case TELOPT_TSPEED:
	case TELOPT_LFLOW:
	case TELOPT_XDISPLOC:
#ifdef	TELOPT_ENVIRON
	case TELOPT_NEW_ENVIRON:
#endif
	case TELOPT_OLD_ENVIRON:
	default:
	    break;
	}
	if (changeok) {
	    set_my_want_state_will(option);
	    send_will(option, 0);
	} else {
	    will_wont_resp[option]++;
	    send_wont(option, 0);
	}
    }
    set_my_state_will(option);
}  /* end of dooption */

void
noit_console_telnet_send_wont(noit_console_closure_t ncct, int option, int init)
{
    if (init) {
	if ((will_wont_resp[option] == 0 && my_state_is_wont(option)) ||
	    my_want_state_is_wont(option))
	    return;
	set_my_want_state_wont(option);
	will_wont_resp[option]++;
    }
    nc_printf (ncct, (const char *)wont, option);
}

void
noit_console_telnet_dontoption(noit_console_closure_t ncct, int option)
{
    /*
     * Process client input.
	 */


    if (will_wont_resp[option]) {
	will_wont_resp[option]--;
	if (will_wont_resp[option] && my_state_is_wont(option))
	    will_wont_resp[option]--;
    }
    if ((will_wont_resp[option] == 0) && (my_want_state_is_will(option))) {
	switch (option) {
	case TELOPT_BINARY:
	    init_termbuf(ncct);
	    noit_console_telnet_tty_binaryout(ncct, 0);
	    set_termbuf(ncct);
	    break;

	case TELOPT_ECHO:	/* we should stop echoing */
#ifdef	LINEMODE
# ifdef	KLUDGELINEMODE
	    if ((lmodetype != REAL_LINEMODE) &&
		    (lmodetype != KLUDGE_LINEMODE))
# else
	    if (his_state_is_wont(TELOPT_LINEMODE))
# endif
#endif
	    {
		init_termbuf(ncct);
		noit_console_telnet_tty_setecho(ncct, 0);
		set_termbuf(ncct);
	    }
	break;

	case TELOPT_SGA:
#if	defined(LINEMODE) && defined(KLUDGELINEMODE)
		/*
		 * If kludge linemode is in use, then we
		 * must process an incoming do SGA for
		 * linemode purposes.
		 */
	    if ((lmodetype == KLUDGE_LINEMODE) ||
		    (lmodetype == KLUDGE_OK)) {
		/*
		 * The client is asking us to turn
		 * linemode on.
		 */
		lmodetype = KLUDGE_LINEMODE;
		clientstat(TELOPT_LINEMODE, WILL, 0);
		/*
		 * If we did not turn line mode on,
		 * then what do we say?  Will SGA?
		 * This violates design of telnet.
		 * Gross.  Very Gross.
		 */
	    }
	    break;
#else
	    set_my_want_state_wont(option);
	    if (my_state_is_will(option))
		send_wont(option, 0);
	    set_my_state_wont(option);
	    if (turn_on_sga ^= 1)
		send_will(option, 1);
	    return;
#endif	/* defined(LINEMODE) && defined(KLUDGELINEMODE) */

	default:
	    break;
	}

	set_my_want_state_wont(option);
	if (my_state_is_will(option))
	    send_wont(option, 0);
    }
    set_my_state_wont(option);

}  /* end of dontoption */

#ifdef	ENV_HACK
int env_ovar = -1;
int env_ovalue = -1;
#else	/* ENV_HACK */
# define env_ovar OLD_ENV_VAR
# define env_ovalue OLD_ENV_VALUE
#endif	/* ENV_HACK */

/*
 * suboption()
 *
 *	Look at the sub-option buffer, and try to be helpful to the other
 * side.
 *
 *	Currently we recognize:
 *
 *	Terminal type is
 *	Linemode
 *	Window size
 *	Terminal speed
 */
static void
suboption(noit_console_closure_t ncct)
{
    int subchar;

    subchar = SB_GET();
    switch (subchar) {
    case TELOPT_TSPEED: {
	int xspeed, rspeed;

	if (his_state_is_wont(TELOPT_TSPEED))	/* Ignore if option disabled */
	    break;

	settimer(tspeedsubopt);

	if (SB_EOF() || SB_GET() != TELQUAL_IS)
	    return;

	xspeed = atoi((char *)subpointer);

	while (SB_GET() != ',' && !SB_EOF());
	if (SB_EOF())
	    return;

	rspeed = atoi((char *)subpointer);
	clientstat(ncct, TELOPT_TSPEED, xspeed, rspeed);

	break;

    }  /* end of case TELOPT_TSPEED */

    case TELOPT_TTYPE: {		/* Yaaaay! */
	char *terminaltype;
	if (his_state_is_wont(TELOPT_TTYPE))	/* Ignore if option disabled */
	    break;
	settimer(ttypesubopt);

	if (SB_EOF() || SB_GET() != TELQUAL_IS) {
	    return;		/* ??? XXX but, this is the most robust */
	}

	terminaltype = terminalname;

	while ((terminaltype < (terminalname + sizeof terminalname-1)) &&
	       !SB_EOF()) {
	    int c;

	    c = SB_GET();
	    if (isupper(c)) {
		c = tolower(c);
	    }
	    *terminaltype++ = c;    /* accumulate name */
	}
	*terminaltype = 0;
	terminaltype = terminalname;
	break;
    }  /* end of case TELOPT_TTYPE */

    case TELOPT_NAWS: {
	int xwinsize, ywinsize;

	if (his_state_is_wont(TELOPT_NAWS))	/* Ignore if option disabled */
	    break;

	if (SB_EOF())
	    return;
	xwinsize = SB_GET() << 8;
	if (SB_EOF())
	    return;
	xwinsize |= SB_GET();
	if (SB_EOF())
	    return;
	ywinsize = SB_GET() << 8;
	if (SB_EOF())
	    return;
	ywinsize |= SB_GET();
	clientstat(ncct, TELOPT_NAWS, xwinsize, ywinsize);

	break;

    }  /* end of case TELOPT_NAWS */
#ifdef	LINEMODE
    case TELOPT_LINEMODE: {
	int request;

	if (his_state_is_wont(TELOPT_LINEMODE))	/* Ignore if option disabled */
		break;
	/*
	 * Process linemode suboptions.
	 */
	if (SB_EOF())
	    break;		/* garbage was sent */
	request = SB_GET();	/* get will/wont */

	if (SB_EOF())
	    break;		/* another garbage check */

	if (request == LM_SLC) {  /* SLC is not preceeded by WILL or WONT */
		/*
		 * Process suboption buffer of slc's
		 */
		start_slc(ncct, 1);
		do_opt_slc(ncct, subpointer, subend - subpointer);
		(void) end_slc(ncct, 0);
		break;
	} else if (request == LM_MODE) {
		if (SB_EOF())
		    return;
		useeditmode = SB_GET();  /* get mode flag */
		clientstat(ncct, LM_MODE, 0, 0);
		break;
	}

	if (SB_EOF())
	    break;
	switch (SB_GET()) {  /* what suboption? */
	case LM_FORWARDMASK:
		/*
		 * According to spec, only server can send request for
		 * forwardmask, and client can only return a positive response.
		 * So don't worry about it.
		 */

	default:
		break;
	}
	break;
    }  /* end of case TELOPT_LINEMODE */
#endif
    case TELOPT_STATUS: {
	int mode;

	if (SB_EOF())
	    break;
	mode = SB_GET();
	switch (mode) {
	case TELQUAL_SEND:
	    if (my_state_is_will(TELOPT_STATUS))
		send_status();
	    break;

	case TELQUAL_IS:
	    break;

	default:
	    break;
	}
	break;
    }  /* end of case TELOPT_STATUS */

    case TELOPT_XDISPLOC: {
	if (SB_EOF() || SB_GET() != TELQUAL_IS)
	    return;
	settimer(xdisplocsubopt);
	subpointer[SB_LEN()] = '\0';
        noit_hash_replace(&ncct->telnet->_env,
                          strdup("DISPLAY"), strlen("DISPLAY"),
                          strdup((char *)subpointer),
                          free, free);
	break;
    }  /* end of case TELOPT_XDISPLOC */

#ifdef	TELOPT_NEW_ENVIRON
    case TELOPT_NEW_ENVIRON:
#endif
    case TELOPT_OLD_ENVIRON: {
	int c;
	char *cp, *varp, *valp;

	if (SB_EOF())
	    return;
	c = SB_GET();
	if (c == TELQUAL_IS) {
	    if (subchar == TELOPT_OLD_ENVIRON)
		settimer(oenvironsubopt);
	    else
		settimer(environsubopt);
	} else if (c != TELQUAL_INFO) {
	    return;
	}

#ifdef	TELOPT_NEW_ENVIRON
	if (subchar == TELOPT_NEW_ENVIRON) {
	    while (!SB_EOF()) {
		c = SB_GET();
		if ((c == NEW_ENV_VAR) || (c == ENV_USERVAR))
		    break;
	    }
	} else
#endif
	    {
#ifdef	ENV_HACK
		/*
		 * We only want to do this if we haven't already decided
		 * whether or not the other side has its VALUE and VAR
		 * reversed.
		 */
		if (env_ovar < 0) {
		    int last = -1;		/* invalid value */
		    int empty = 0;
		    int got_var = 0, got_value = 0, got_uservar = 0;

		    /*
		     * The other side might have its VALUE and VAR values
		     * reversed.  To be interoperable, we need to determine
		     * which way it is.  If the first recognized character
		     * is a VAR or VALUE, then that will tell us what
		     * type of client it is.  If the fist recognized
		     * character is a USERVAR, then we continue scanning
		     * the suboption looking for two consecutive
		     * VAR or VALUE fields.  We should not get two
		     * consecutive VALUE fields, so finding two
		     * consecutive VALUE or VAR fields will tell us
		     * what the client is.
		     */
		    SB_SAVE();
		    while (!SB_EOF()) {
			c = SB_GET();
			switch(c) {
			case OLD_ENV_VAR:
			    if (last < 0 || last == OLD_ENV_VAR
				|| (empty && (last == OLD_ENV_VALUE)))
				goto env_ovar_ok;
			    got_var++;
			    last = OLD_ENV_VAR;
			    break;
			case OLD_ENV_VALUE:
			    if (last < 0 || last == OLD_ENV_VALUE
				|| (empty && (last == OLD_ENV_VAR)))
				goto env_ovar_wrong;
			    got_value++;
			    last = OLD_ENV_VALUE;
			    break;
			case ENV_USERVAR:
			    /* count strings of USERVAR as one */
			    if (last != ENV_USERVAR)
				got_uservar++;
			    if (empty) {
				if (last == OLD_ENV_VALUE)
				    goto env_ovar_ok;
				if (last == OLD_ENV_VAR)
				    goto env_ovar_wrong;
			    }
			    last = ENV_USERVAR;
			    break;
			case ENV_ESC:
			    if (!SB_EOF())
				c = SB_GET();
			    /* FALL THROUGH */
			default:
			    empty = 0;
			    continue;
			}
			empty = 1;
		    }
		    if (empty) {
			if (last == OLD_ENV_VALUE)
			    goto env_ovar_ok;
			if (last == OLD_ENV_VAR)
			    goto env_ovar_wrong;
		    }
		    /*
		     * Ok, the first thing was a USERVAR, and there
		     * are not two consecutive VAR or VALUE commands,
		     * and none of the VAR or VALUE commands are empty.
		     * If the client has sent us a well-formed option,
		     * then the number of VALUEs received should always
		     * be less than or equal to the number of VARs and
		     * USERVARs received.
		     *
		     * If we got exactly as many VALUEs as VARs and
		     * USERVARs, the client has the same definitions.
		     *
		     * If we got exactly as many VARs as VALUEs and
		     * USERVARS, the client has reversed definitions.
		     */
		    if (got_uservar + got_var == got_value) {
		    env_ovar_ok:
			env_ovar = OLD_ENV_VAR;
			env_ovalue = OLD_ENV_VALUE;
		    } else if (got_uservar + got_value == got_var) {
		    env_ovar_wrong:
			env_ovar = OLD_ENV_VALUE;
			env_ovalue = OLD_ENV_VAR;
		    }
		}
		SB_RESTORE();
#endif

		while (!SB_EOF()) {
		    c = SB_GET();
		    if ((c == env_ovar) || (c == ENV_USERVAR))
			break;
		}
	    }

	if (SB_EOF())
	    return;

	cp = varp = (char *)subpointer;
	valp = 0;

	while (!SB_EOF()) {
	    c = SB_GET();
	    if (subchar == TELOPT_OLD_ENVIRON) {
		if (c == env_ovar)
		    c = NEW_ENV_VAR;
		else if (c == env_ovalue)
		    c = NEW_ENV_VALUE;
	    }
	    switch (c) {

	    case NEW_ENV_VALUE:
		*cp = '\0';
		cp = valp = (char *)subpointer;
		break;

	    case NEW_ENV_VAR:
	    case ENV_USERVAR:
		*cp = '\0';
		if (valp)
                    noit_hash_replace(&ncct->telnet->_env,
                                      strdup(varp), strlen(varp),
                                      strdup(valp),
                                      free, free);
		else
                    noit_hash_delete(&ncct->telnet->_env, varp, strlen(varp),
                                     free, free);
		cp = varp = (char *)subpointer;
		valp = 0;
		break;

	    case ENV_ESC:
		if (SB_EOF())
		    break;
		c = SB_GET();
		/* FALL THROUGH */
	    default:
		*cp++ = c;
		break;
	    }
	}
	*cp = '\0';
	if (valp)
            noit_hash_replace(&ncct->telnet->_env,
                              strdup(varp), strlen(varp),
                              strdup(valp),
                              free, free);
	else
            noit_hash_delete(&ncct->telnet->_env, varp, strlen(varp),
                             free, free);
	break;
    }  /* end of case TELOPT_NEW_ENVIRON */
#ifdef AUTHENTICATION
    case TELOPT_AUTHENTICATION:
	if (SB_EOF())
	    break;
	switch(SB_GET()) {
	case TELQUAL_SEND:
	case TELQUAL_REPLY:
	    /*
	     * These are sent by us and cannot be sent by
	     * the client.
	     */
	    break;
	case TELQUAL_IS:
	    auth_is(subpointer, SB_LEN());
	    break;
	case TELQUAL_NAME:
	    auth_name(subpointer, SB_LEN());
	    break;
	}
	break;
#endif
#ifdef ENCRYPTION
    case TELOPT_ENCRYPT:
	if (SB_EOF())
	    break;
	switch(SB_GET()) {
	case ENCRYPT_SUPPORT:
	    encrypt_support(subpointer, SB_LEN());
	    break;
	case ENCRYPT_IS:
	    encrypt_is(subpointer, SB_LEN());
	    break;
	case ENCRYPT_REPLY:
	    encrypt_reply(subpointer, SB_LEN());
	    break;
	case ENCRYPT_START:
	    encrypt_start(subpointer, SB_LEN());
	    break;
	case ENCRYPT_END:
	    encrypt_end();
	    break;
	case ENCRYPT_REQSTART:
	    encrypt_request_start(subpointer, SB_LEN());
	    break;
	case ENCRYPT_REQEND:
	    /*
	     * We can always send an REQEND so that we cannot
	     * get stuck encrypting.  We should only get this
	     * if we have been able to get in the correct mode
	     * anyhow.
	     */
	    encrypt_request_end();
	    break;
	case ENCRYPT_ENC_KEYID:
	    encrypt_enc_keyid(subpointer, SB_LEN());
	    break;
	case ENCRYPT_DEC_KEYID:
	    encrypt_dec_keyid(subpointer, SB_LEN());
	    break;
	default:
	    break;
	}
	break;
#endif

    default:
	break;
    }  /* end of switch */

}  /* end of suboption */

void
noit_console_telnet_doclientstat(noit_console_closure_t ncct)
{
    clientstat(ncct, TELOPT_LINEMODE, WILL, 0);
}

#undef ADD
#define	ADD(c)	 *ncp++ = c
#define	ADD_DATA(c) { *ncp++ = c; if (c == SE || c == IAC) *ncp++ = c; }

void
noit_console_telnet_send_status(noit_console_closure_t ncct)
{
    unsigned char statusbuf[256];
    unsigned char *ncp;
    unsigned char i;

    ncp = statusbuf;

    netflush(ncct);	/* get rid of anything waiting to go out */

    ADD(IAC);
    ADD(SB);
    ADD(TELOPT_STATUS);
    ADD(TELQUAL_IS);

    /*
     * We check the want_state rather than the current state,
     * because if we received a DO/WILL for an option that we
     * don't support, and the other side didn't send a DONT/WONT
     * in response to our WONT/DONT, then the "state" will be
     * WILL/DO, and the "want_state" will be WONT/DONT.  We
     * need to go by the latter.
     */
    for (i = 0; i < (unsigned char)NTELOPTS; i++) {
	if (my_want_state_is_will(i)) {
	    ADD(WILL);
	    ADD_DATA(i);
	}
	if (his_want_state_is_will(i)) {
	    ADD(DO);
	    ADD_DATA(i);
	}
    }

    if (his_want_state_is_will(TELOPT_LFLOW)) {
	ADD(SB);
	ADD(TELOPT_LFLOW);
	if (flowmode) {
	    ADD(LFLOW_ON);
	} else {
	    ADD(LFLOW_OFF);
	}
	ADD(SE);

	if (restartany >= 0) {
	    ADD(SB);
	    ADD(TELOPT_LFLOW);
	    if (restartany) {
		ADD(LFLOW_RESTART_ANY);
	    } else {
		ADD(LFLOW_RESTART_XON);
	    }
	    ADD(SE);
	}
    }

#ifdef	LINEMODE
	if (his_want_state_is_will(TELOPT_LINEMODE)) {
		unsigned char *cp, *cpe;
		int len;

		ADD(SB);
		ADD(TELOPT_LINEMODE);
		ADD(LM_MODE);
		ADD_DATA(editmode);
		ADD(SE);

		ADD(SB);
		ADD(TELOPT_LINEMODE);
		ADD(LM_SLC);
		start_slc(ncct, 0);
		send_slc(ncct);
		len = end_slc(ncct, &cp);
		for (cpe = cp + len; cp < cpe; cp++)
			ADD_DATA(*cp);
		ADD(SE);
	}
#endif	/* LINEMODE */

    ADD(IAC);
    ADD(SE);

    nc_write(ncct, statusbuf, ncp - statusbuf);
    netflush(ncct);	/* Send it on its way */
}
