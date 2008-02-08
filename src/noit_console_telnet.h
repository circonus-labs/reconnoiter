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
 *
 *	@(#)defs.h	8.1 (Berkeley) 6/4/93
 */

#ifndef _NOIT_CONSOLE_TELNET_H
#define _NOIT_CONSOLE_TELNET_H

#include "noit_defines.h"
#include "utils/noit_hash.h"

#include <arpa/telnet.h>
#include <sys/ioctl.h>
#include <sys/ioctl_compat.h>
#ifdef HAVE_TERMIOS_H
#include <termios.h>
#endif
#ifdef HAVE_TERM_H
#include <term.h>
#endif

#define LINEMODE 1

#ifndef _POSIX_VDISABLE
# ifdef VDISABLE
#  define _POSIX_VDISABLE VDISABLE
# else
#  define _POSIX_VDISABLE ((unsigned char)'\377')
# endif
#endif

/*
 * Structures of information for each special character function.
 */
typedef unsigned char cc_t;

typedef struct {
	unsigned char	flag;		/* the flags for this function */
	cc_t		val;		/* the value of the special character */
} slcent, *Slcent;

typedef struct {
	slcent		defset;		/* the default settings */
	slcent		current;	/* the current settings */
	cc_t		*sptr;		/* a pointer to the char in */
					/* system data structures */
} slcfun, *Slcfun;

#ifndef	USE_TERMIO
struct termbuf {
	struct sgttyb sg;
	struct tchars tc;
	struct ltchars ltc;
	int state;
	int lflags;
};
# define	cfsetospeed(tp, val)	(tp)->sg.sg_ospeed = (val)
# define	cfsetispeed(tp, val)	(tp)->sg.sg_ispeed = (val)
# define	cfgetospeed(tp)		(tp)->sg.sg_ospeed
# define	cfgetispeed(tp)		(tp)->sg.sg_ispeed
#else	/* USE_TERMIO */
# ifndef	TCSANOW
#  ifdef TCSETS
#   define	TCSANOW		TCSETS
#   define	TCSADRAIN	TCSETSW
#   define	tcgetattr(f, t)	ioctl(f, TCGETS, (char *)t)
#  else
#   ifdef TCSETA
#    define	TCSANOW		TCSETA
#    define	TCSADRAIN	TCSETAW
#    define	tcgetattr(f, t)	ioctl(f, TCGETA, (char *)t)
#   else
#    define	TCSANOW		TIOCSETA
#    define	TCSADRAIN	TIOCSETAW
#    define	tcgetattr(f, t)	ioctl(f, TIOCGETA, (char *)t)
#   endif
#  endif
#  define	tcsetattr(f, a, t)	ioctl(f, a, t)
#  define	cfsetospeed(tp, val)	(tp)->c_cflag &= ~CBAUD; \
					(tp)->c_cflag |= (val)
#  define	cfgetospeed(tp)		((tp)->c_cflag & CBAUD)
#  ifdef CIBAUD
#   define	cfsetispeed(tp, val)	(tp)->c_cflag &= ~CIBAUD; \
					(tp)->c_cflag |= ((val)<<IBSHIFT)
#   define	cfgetispeed(tp)		(((tp)->c_cflag & CIBAUD)>>IBSHIFT)
#  else
#   define	cfsetispeed(tp, val)	(tp)->c_cflag &= ~CBAUD; \
					(tp)->c_cflag |= (val)
#   define	cfgetispeed(tp)		((tp)->c_cflag & CBAUD)
#  endif
# endif /* TCSANOW */
#endif	/* USE_TERMIO */

/*
 * The following are some clocks used to decide how to interpret
 * the relationship between various variables.
 */

struct clocks_t {
    int
	system,			/* what the current time is */
	echotoggle,		/* last time user entered echo character */
	modenegotiated,		/* last time operating mode negotiated */
	didnetreceive,		/* last time we read data from network */
	ttypesubopt,		/* ttype subopt is received */
	tspeedsubopt,		/* tspeed subopt is received */
	environsubopt,		/* environ subopt is received */
	oenvironsubopt,		/* old environ subopt is received */
	xdisplocsubopt,		/* xdisploc subopt is received */
	baseline,		/* time started to do timed action */
	gotDM;			/* when did we last see a data mark */
};

typedef struct {
  noit_hash_table _env;
  unsigned char *_subbuffer;
  unsigned char *_subpointer;
  unsigned char *_subend;
  unsigned char *_subsave;
  unsigned char  _pty_buf[1024];
  int            _pty_fill;
  slcfun         _slctab[NSLC + 1];
  char           _options[256];
  char           _do_dont_resp[256];
  char           _will_wont_resp[256];
  int            _SYNCHing;
  int            _flowmode;
  int            _restartany;
  int            __terminit;
  int            _linemode;
  int            _uselinemode;
  int            _alwayslinemode;
  int            _editmode;
  int            _useeditmode;
  unsigned char *_def_slcbuf;
  int            _def_slclen;
  int            _slcchange;	/* change to slc is requested */
  unsigned char *_slcptr;	/* pointer into slc buffer */
  unsigned char  _slcbuf[NSLC*6];	/* buffer for slc negotiation */

  int            _def_tspeed; /* This one */
  int            _def_rspeed; /* and this one, need init to -1 */
#if	!defined(LINEMODE) || !defined(KLUDGELINEMODE)
  int            _turn_on_sga;
#endif
#ifdef TIOCSWINSZ
  int            _def_row;
  int            _def_col;
#endif
  char           _terminalname[41];
  struct clocks_t _clocks;
#ifdef USE_TERMIO
  struct termios _termbuf, _termbuf2;	/* pty control structure */
#else
  struct termbuf _termbuf, _termbuf2;	/* pty control structure */
#endif
} *noit_console_telnet_closure_t;


/*
 * We keep track of each side of the option negotiation.
 */

#define	MY_STATE_WILL		0x01
#define	MY_WANT_STATE_WILL	0x02
#define	MY_STATE_DO		0x04
#define	MY_WANT_STATE_DO	0x08

/*
 * Macros to check the current state of things
 */

#define	my_state_is_do(opt)		(ncct->telnet->_options[opt]&MY_STATE_DO)
#define	my_state_is_will(opt)		(ncct->telnet->_options[opt]&MY_STATE_WILL)
#define my_want_state_is_do(opt)	(ncct->telnet->_options[opt]&MY_WANT_STATE_DO)
#define my_want_state_is_will(opt)	(ncct->telnet->_options[opt]&MY_WANT_STATE_WILL)

#define	my_state_is_dont(opt)		(!my_state_is_do(opt))
#define	my_state_is_wont(opt)		(!my_state_is_will(opt))
#define my_want_state_is_dont(opt)	(!my_want_state_is_do(opt))
#define my_want_state_is_wont(opt)	(!my_want_state_is_will(opt))

#define	set_my_state_do(opt)		(ncct->telnet->_options[opt] |= MY_STATE_DO)
#define	set_my_state_will(opt)		(ncct->telnet->_options[opt] |= MY_STATE_WILL)
#define	set_my_want_state_do(opt)	(ncct->telnet->_options[opt] |= MY_WANT_STATE_DO)
#define	set_my_want_state_will(opt)	(ncct->telnet->_options[opt] |= MY_WANT_STATE_WILL)

#define	set_my_state_dont(opt)		(ncct->telnet->_options[opt] &= ~MY_STATE_DO)
#define	set_my_state_wont(opt)		(ncct->telnet->_options[opt] &= ~MY_STATE_WILL)
#define	set_my_want_state_dont(opt)	(ncct->telnet->_options[opt] &= ~MY_WANT_STATE_DO)
#define	set_my_want_state_wont(opt)	(ncct->telnet->_options[opt] &= ~MY_WANT_STATE_WILL)

/*
 * Tricky code here.  What we want to know is if the MY_STATE_WILL
 * and MY_WANT_STATE_WILL bits have the same value.  Since the two
 * bits are adjacent, a little arithmatic will show that by adding
 * in the lower bit, the upper bit will be set if the two bits were
 * different, and clear if they were the same.
 */
#define my_will_wont_is_changing(opt) \
			((ncct->telnet->_options[opt]+MY_STATE_WILL) & MY_WANT_STATE_WILL)

#define my_do_dont_is_changing(opt) \
			((ncct->telnet->_options[opt]+MY_STATE_DO) & MY_WANT_STATE_DO)

/*
 * Make everything symmetrical
 */

#define	HIS_STATE_WILL			MY_STATE_DO
#define	HIS_WANT_STATE_WILL		MY_WANT_STATE_DO
#define HIS_STATE_DO			MY_STATE_WILL
#define HIS_WANT_STATE_DO		MY_WANT_STATE_WILL

#define	his_state_is_do			my_state_is_will
#define	his_state_is_will		my_state_is_do
#define his_want_state_is_do		my_want_state_is_will
#define his_want_state_is_will		my_want_state_is_do

#define	his_state_is_dont		my_state_is_wont
#define	his_state_is_wont		my_state_is_dont
#define his_want_state_is_dont		my_want_state_is_wont
#define his_want_state_is_wont		my_want_state_is_dont

#define	set_his_state_do		set_my_state_will
#define	set_his_state_will		set_my_state_do
#define	set_his_want_state_do		set_my_want_state_will
#define	set_his_want_state_will		set_my_want_state_do

#define	set_his_state_dont		set_my_state_wont
#define	set_his_state_wont		set_my_state_dont
#define	set_his_want_state_dont		set_my_want_state_wont
#define	set_his_want_state_wont		set_my_want_state_dont

#define his_will_wont_is_changing	my_do_dont_is_changing
#define his_do_dont_is_changing		my_will_wont_is_changing

struct __noit_console_closure;
void
  noit_console_telnet_willoption(struct __noit_console_closure *ncct,
                                 int option);
void
  noit_console_telnet_wontoption(struct __noit_console_closure *ncct,
                                 int option);
void
  noit_console_telnet_dooption(struct __noit_console_closure *ncct,
                               int option);
void
  noit_console_telnet_dontoption(struct __noit_console_closure *ncct,
                                 int option);
void
  noit_console_telnet_send_do(struct __noit_console_closure *ncct,
                              int option, int init);
void
  noit_console_telnet_send_dont(struct __noit_console_closure *ncct,
                                int option, int init);
void
  noit_console_telnet_send_will(struct __noit_console_closure *ncct,
                                int option, int init);
void
  noit_console_telnet_send_wont(struct __noit_console_closure *ncct,
                                int option, int init);
void
  noit_console_telnet_doclientstat(struct __noit_console_closure *ncct);
void
  noit_console_telnet_send_status(struct __noit_console_closure *ncct);

void noit_console_telnet_free(noit_console_telnet_closure_t);
noit_console_telnet_closure_t
  noit_console_telnet_alloc(struct __noit_console_closure *ncct);
int
  noit_console_telnet_telrcv(struct __noit_console_closure *ncct,
                             const void *buf, int buflen);
void ptyflush(struct __noit_console_closure *ncct);

#endif
