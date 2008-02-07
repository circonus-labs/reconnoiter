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
#include <sys/ioctl_compat.h>
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
#define restartany ncct->telnet->_restartany
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
static void
suboption(noit_console_closure_t ncct);

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
    return((termbuf.c_iflag & IXON) ? 1 : 0);
}

void
noit_console_telnet_tty_binaryin(noit_console_closure_t ncct, int on)
{
    if (on) {
	termbuf.c_iflag &= ~ISTRIP;
    } else {
	termbuf.c_iflag |= ISTRIP;
    }
}

int
noit_console_telnet_tty_restartany(noit_console_closure_t ncct)
{
    return((termbuf.c_iflag & IXANY) ? 1 : 0);
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

void
noit_console_telnet_tty_binaryout(noit_console_closure_t ncct, int on)
{
    if (on) {
	termbuf.c_cflag &= ~(CSIZE|PARENB);
	termbuf.c_cflag |= CS8;
	termbuf.c_oflag &= ~OPOST;
    } else {
	termbuf.c_cflag &= ~CSIZE;
	termbuf.c_cflag |= CS7|PARENB;
	termbuf.c_oflag |= OPOST;
    }
}

void
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

void
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

/*
 * flowstat
 *
 * Check for changes to flow control
 */
void
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
int
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
	if (!noit_console_telnet_tty_isediting()) {
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
  int i, on;
  noit_console_telnet_closure_t telnet, tmp;
  tmp = ncct->telnet;

  ncct->telnet = calloc(1, sizeof(*telnet));
  subbuffer = malloc(1024*64);
  subpointer = subbuffer;
  subend= subbuffer;
  def_tspeed = -1;
  def_rspeed = -1;
  for (i = 1; i <= NSLC; i++) {
    slctab[i].defset.flag =
      spcset(ncct, i, &slctab[i].defset.val, &slctab[i].sptr);
    slctab[i].current.flag = SLC_NOSUPPORT;
    slctab[i].current.val = 0;
  }
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
#ifdef ENCRYPTION
		int nc = *netip;
		if (decrypt_input)
		    nc = (*decrypt_input)(nc & 0xff);
#endif
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
		/*
		 * We never respond to a WILL TM, and
		 * we leave the state WONT.
		 */
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

/*
 * When we get a DONT SGA, we will try once to turn it
 * back on.  If the other side responds DONT SGA, we
 * leave it at that.  This is so that when we talk to
 * clients that understand KLUDGELINEMODE but not LINEMODE,
 * we'll keep them in char-at-a-time mode.
 */
int turn_on_sga = 0;

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
	    turn_on_sga = 0;
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
	    {
		init_termbuf(ncct);
		noit_console_telnet_tty_setecho(ncct, 0);
		set_termbuf(ncct);
	    }
	break;

	case TELOPT_SGA:
	    set_my_want_state_wont(option);
	    if (my_state_is_will(option))
		send_wont(option, 0);
	    set_my_state_wont(option);
	    if (turn_on_sga ^= 1)
		send_will(option, 1);
	    return;

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


    ADD(IAC);
    ADD(SE);

    nc_write(ncct, statusbuf, ncp - statusbuf);
    netflush(ncct);	/* Send it on its way */
}
