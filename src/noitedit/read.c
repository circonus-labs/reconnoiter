/*	$NetBSD: read.c,v 1.19 2001/01/10 07:45:41 jdolecek Exp $	*/

/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Christos Zoulas of Cornell University.
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

#include "noitedit/compat.h"
#if !defined(lint) && !defined(SCCSID)
#if 0
static char sccsid[] = "@(#)read.c	8.1 (Berkeley) 6/4/93";
#else
__RCSID("$NetBSD: read.c,v 1.19 2001/01/10 07:45:41 jdolecek Exp $");
#endif
#endif /* not lint && not SCCSID */

/*
 * read.c: Clean this junk up! This is horrible code.
 *	   Terminal read functions
 */
#include "noitedit/sys.h"
#include <errno.h>
#include <sys/ioctl.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <stdlib.h>
#include "noitedit/el.h"

#define OKCMD     -1
#define EAGAINCMD -2

#define ISEAGAIN(el) ((el)->el_nb_state == EAGAINCMD)

private int	read__fixio(int, int);
private int	read_preread(EditLine *);
private int	read_getcmd(EditLine *, el_action_t *, char *);
private int	read_char(EditLine *, char *);

#ifdef DEBUG_EDIT
private void
read_debug(EditLine *el)
{

	if (el->el_line.cursor > el->el_line.lastchar)
		(void) el->el_err_printf(el, "cursor > lastchar\r\n");
	if (el->el_line.cursor < el->el_line.buffer)
		(void) el->el_err_printf(el, "cursor < buffer\r\n");
	if (el->el_line.cursor > el->el_line.limit)
		(void) el->el_err_printf(el, "cursor > limit\r\n");
	if (el->el_line.lastchar > el->el_line.limit)
		(void) el->el_err_printf(el, "lastchar > limit\r\n");
	if (el->el_line.limit != &el->el_line.buffer[EL_BUFSIZ - 2])
		(void) el->el_err_printf(el, "limit != &buffer[EL_BUFSIZ-2]\r\n");
}
#endif /* DEBUG_EDIT */


/* read__fixio():
 *	Try to recover from a read error
 */
/* ARGSUSED */
private int
read__fixio(int fd, int e)
{

	switch (e) {
	case -1:		/* Make sure that the code is reachable */

#ifdef EWOULDBLOCK
	case EWOULDBLOCK:
#ifndef TRY_AGAIN
#define	TRY_AGAIN
#endif
#endif /* EWOULDBLOCK */

#if defined(POSIX) && defined(EAGAIN)
#if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
	case EAGAIN:
#ifndef TRY_AGAIN
#define	TRY_AGAIN
#endif
#endif /* EWOULDBLOCK && EWOULDBLOCK != EAGAIN */
#endif /* POSIX && EAGAIN */

		e = 0;
#ifdef TRY_AGAIN
#if defined(F_SETFL) && defined(O_NDELAY)
/*
		if ((e = fcntl(fd, F_GETFL, 0)) == -1)
			return (-1);

		if (fcntl(fd, F_SETFL, e & ~O_NDELAY) == -1)
			return (-1);
		else
			e = 1;
*/
#endif /* F_SETFL && O_NDELAY */

#ifdef FIONBIO
/*
		{
			int zero = 0;

			if (ioctl(fd, FIONBIO, (ioctl_t) & zero) == -1)
				return (-1);
			else
				e = 1;
		}
*/
#endif /* FIONBIO */

#endif /* TRY_AGAIN */
		return (e ? 0 : -1);

	case EINTR:
		return (0);

	default:
		return (-1);
	}
}


/* read_preread():
 *	Try to read the stuff in the input queue;
 */
private int
read_preread(EditLine *el)
{
	int chrs = 0;

	if (el->el_chared.c_macro.nline) {
		el_free((ptr_t) el->el_chared.c_macro.nline);
		el->el_chared.c_macro.nline = NULL;
	}
	if (el->el_tty.t_mode == ED_IO)
		return (0);

#ifdef FIONREAD
	(void) ioctl(el->el_infd, FIONREAD, (ioctl_t) & chrs);
	if (chrs > 0) {
		char buf[EL_BUFSIZ];
		int mask;

		if(el->el_in_e)
			chrs = el->
				el_in_e->
				opset->
				read(el->el_in_e->fd, buf,
				    (size_t) MIN(chrs, EL_BUFSIZ - 1),
				    &mask, el->el_in_e);
		else
			chrs = read(el->el_infd, buf,
			    (size_t) MIN(chrs, EL_BUFSIZ - 1));
		if (chrs > 0) {
			buf[chrs] = '\0';
			el->el_chared.c_macro.nline = strdup(buf);
			el_push(el, el->el_chared.c_macro.nline);
		}
	}
#endif /* FIONREAD */

	return (chrs > 0);
}


/* el_push():
 *	Push a macro
 */
public void
el_push(EditLine *el, const char *str)
{
	c_macro_t *ma = &el->el_chared.c_macro;

	if (str != NULL && ma->level + 1 < EL_MAXMACRO) {
		ma->level++;
		/* LINTED const cast */
		ma->macro[ma->level] = (char *) str;
	} else {
		term_beep(el);
		term__flush();
	}
}


public int
el_internal_read_getcmd(EditLine *el, el_action_t *cmdnum, char *ch, int nonblock)
{
	el_action_t cmd = ED_UNASSIGNED;
	int num;

	while (cmd == ED_UNASSIGNED || cmd == ED_SEQUENCE_LEAD_IN) {
		if ((num = el_getc(el, ch)) != 1) {	/* if EOF or error */
			/* So solaris doesn't read -> -1 with EAGAIN when
			 * no data is ready on the terminal.. it returns 0.
			 * I'm likely not understanding something, but that
			 * seems deeply flawed.  Regardless, I cannot think
			 * of a case where we'd want to return something other
			 * than EAGAINCMD... this seems "more correct" than
			 * only returning it whe errno == EAGAIN

				if(errno == EAGAIN) return EAGAINCMD;
				return (num);

			 */
			return EAGAINCMD;
		}
		
#ifdef	KANJI
		if ((*ch & 0200)) {
			el->el_state.metanext = 0;
			cmd = CcViMap[' '];
			break;
		} else
#endif /* KANJI */

		if (el->el_state.metanext) {
			el->el_state.metanext = 0;
			*ch |= 0200;
		}
		cmd = el->el_map.current[(unsigned char) *ch];
		if (cmd == ED_SEQUENCE_LEAD_IN || el->el_keystate) {
			key_value_t val;
			switch (key_get(el, ch, &val)) {
			case XK_CMD:
				cmd = val.cmd;
				break;
			case XK_STR:
				el_push(el, val.str);
				break;
#ifdef notyet
			case XK_EXE:
				/* XXX: In the future to run a user function */
				RunCommand(val.str);
				break;
#endif
			default:
				EL_ABORT((el->el_errfile, "Bad XK_ type \n"));
				break;
			}
		}
		if (el->el_map.alt == NULL)
			el->el_map.current = el->el_map.key;

		if (cmd == ED_UNASSIGNED && nonblock)
			break;
	}
	*cmdnum = cmd;
	return (OKCMD);
}

/* read_getcmd():
 *	Return next command from the input stream.
 */
private int
read_getcmd(EditLine *el, el_action_t *cmdnum, char *ch)
{
	return el_internal_read_getcmd(el, cmdnum, ch, 0);
}


/* read_char():
 *	Read a character from the tty.
 */
private int
read_char(EditLine *el, char *cp)
{
	int num_read;
	int tried = 0;

	do {
		int mask;
		if(el->el_in_e)
			num_read = el->
				el_in_e->
				opset->
				read(el->el_in_e->fd, cp, 1,
				    &mask, el->el_in_e);
		else
			num_read = read(el->el_infd, cp, 1);

		if (num_read)
			break;
		if (num_read == -1) {
			if (errno == EAGAIN) return 0;
		 	if (!tried && read__fixio(el->el_infd, errno) == 0) {
				tried = 1;
			} else {
				*cp = '\0';
				return -1;
			}
		}
	} while (num_read == -1);

	return (num_read);
}

public int el_internal_read_char(EditLine *el, char *cp)
{
	return read_char(el, cp);
}

/* el_getc():
 *	Read a character
 */
public int
el_getc(EditLine *el, char *cp)
{
	int num_read;
	c_macro_t *ma = &el->el_chared.c_macro;

	term__flush();
	for (;;) {
		if (ma->level < 0) {
			if (!read_preread(el))
				break;
		}
		if (ma->level < 0)
			break;

		if (*ma->macro[ma->level] == 0) {
			ma->level--;
			continue;
		}
		*cp = *ma->macro[ma->level]++ & 0377;
		if (*ma->macro[ma->level] == 0) {	/* Needed for QuoteMode
							 * On */
			ma->level--;
		}
		return (1);
	}

#ifdef DEBUG_READ
	(void) el->el_err_printf(el, "Turning raw mode on\r\n");
#endif /* DEBUG_READ */
	if (tty_rawmode(el) < 0)/* make sure the tty is set up correctly */
		return (0);

#ifdef DEBUG_READ
	(void) el->el_err_printf(el, "Reading a character\r\n");
#endif /* DEBUG_READ */
	num_read = read_char(el, cp);
#ifdef DEBUG_READ
	(void) el->el_err_printf(el, "Got it(%d) [%02x]\r\n",
                                 (int)num_read, (unsigned char)*cp);
#endif /* DEBUG_READ */
	return (num_read);
}

public int el_gets_dispatch(EditLine *el, el_action_t cmdnum, char ch, int *num)
{
	int retval;

	if ((int) cmdnum >= el->el_map.nfunc) {	/* BUG CHECK command */
#ifdef DEBUG_EDIT
		(void) el->el_err_printf(el,
					   "ERROR: illegal command from key 0%o\r\n", ch);
#endif /* DEBUG_EDIT */
		return 0;	/* try again */
	}
	/* now do the real command */
#ifdef DEBUG_READ
	{
		el_bindings_t *b;
		for (b = el->el_map.help; b->name; b++)
			if (b->func == cmdnum)
				break;
		if (b->name)
			(void) el->el_err_printf(el,
						   "Executing %s\r\n", b->name);
		else
			(void) el->el_err_printf(el,
						   "Error command = %d\r\n", cmdnum);
	}
#endif /* DEBUG_READ */
	retval = (*el->el_map.func[cmdnum]) (el, ch);

	/* save the last command here */
	el->el_state.lastcmd = cmdnum;

	/* use any return value */
	switch (retval) {
		case CC_CURSOR:
			el->el_state.argument = 1;
			el->el_state.doingarg = 0;
			re_refresh_cursor(el);
			break;

		case CC_REDISPLAY:
			re_clear_lines(el);
			re_clear_display(el);
			/* FALLTHROUGH */

		case CC_REFRESH:
			el->el_state.argument = 1;
			el->el_state.doingarg = 0;
			re_refresh(el);
			break;

		case CC_REFRESH_BEEP:
			el->el_state.argument = 1;
			el->el_state.doingarg = 0;
			re_refresh(el);
			term_beep(el);
			break;

		case CC_NORM:	/* normal char */
			el->el_state.argument = 1;
			el->el_state.doingarg = 0;
			break;

		case CC_ARGHACK:	/* Suggested by Rich Salz */
			/* <rsalz@pineapple.bbn.com> */
			break;	/* keep going... */

		case CC_EOF:	/* end of file typed */
			*num = 0;
			break;

		case CC_NEWLINE:	/* normal end of line */
			*num = el->el_line.lastchar - el->el_line.buffer;
			break;

		case CC_FATAL:	/* fatal error, reset to known state */
#ifdef DEBUG_READ
			(void) el->el_err_printf(el,
						   "*** editor fatal ERROR ***\r\n\r\n");
#endif /* DEBUG_READ */
			/* put (real) cursor in a known place */
			re_clear_display(el);	/* reset the display stuff */
			ch_reset(el);	/* reset the input pointers */
			re_refresh(el);	/* print the prompt again */
			el->el_state.argument = 1;
			el->el_state.doingarg = 0;
			break;

		case CC_ERROR:
		default:	/* functions we don't know about */
#ifdef DEBUG_READ
			(void) el->el_err_printf(el,
						   "*** editor ERROR ***\r\n\r\n");
#endif /* DEBUG_READ */
			el->el_state.argument = 1;
			el->el_state.doingarg = 0;
			term_beep(el);
			term__flush();
			break;
	}
	return 1;
}

public int
el_eagain(EditLine *el)
{
	return ISEAGAIN(el);
}

public const char *
el_gets(EditLine *el, int *nread)
{
	el_action_t cmdnum = 0;
	int num;		/* how many chars we have read at NL */
	char ch;
#ifdef FIONREAD
	c_macro_t *ma = &el->el_chared.c_macro;
#endif /* FIONREAD */

	if (nread)
		*nread = 0;

	if(el_eagain(el)) {
		el->el_nb_state = 0;
		goto eagain_resume;
	}

	if (el->el_flags & HANDLE_SIGNALS)
		sig_set(el);

	if (el->el_flags & NO_TTY) {
		char *cp = el->el_line.buffer;
		size_t idx;

#if _WIN32
		printf("%s", el->el_prompt.p_func(el));
#endif

		while (read_char(el, cp) == 1) {
			/* make sure there is space for next character */
			if (cp + 1 >= el->el_line.limit) {
				idx = (cp - el->el_line.buffer);
				if (!ch_enlargebufs(el, 2))
					break;
				cp = &el->el_line.buffer[idx];
			}
			cp++;
			if (cp[-1] == '\r' || cp[-1] == '\n')
				break;
		}

		el->el_line.cursor = el->el_line.lastchar = cp;
		*cp = '\0';
		if (nread)
			*nread = el->el_line.cursor - el->el_line.buffer;
		return (el->el_line.buffer);
	}
	re_clear_display(el);	/* reset the display stuff */
	ch_reset(el);

#ifdef FIONREAD
	if (el->el_tty.t_mode == EX_IO && ma->level < 0) {
		long chrs = 0;

		(void) ioctl(el->el_infd, FIONREAD, (ioctl_t) & chrs);
		if (chrs == 0) {
			if (tty_rawmode(el) < 0) {
				if (nread)
					*nread = 0;
				return (NULL);
			}
		}
	}
#endif /* FIONREAD */

	re_refresh(el);		/* print the prompt */

	if (el->el_flags & EDIT_DISABLED) {
		char *cp = el->el_line.buffer;
		size_t idx;

		term__flush();

		while (read_char(el, cp) == 1) {
			/* make sure there is space next character */
			if (cp + 1 >= el->el_line.limit) {
				idx = (cp - el->el_line.buffer);
				if (!ch_enlargebufs(el, 2))
					break;
				cp = &el->el_line.buffer[idx];
			}
			cp++;
			if (cp[-1] == '\r' || cp[-1] == '\n')
				break;
		}

		el->el_line.cursor = el->el_line.lastchar = cp;
		*cp = '\0';
		if (nread)
			*nread = el->el_line.cursor - el->el_line.buffer;
		return (el->el_line.buffer);
	}
 eagain_resume:
	for (num = OKCMD; num == OKCMD;) {	/* while still editing this
						 * line */
#ifdef DEBUG_EDIT
		read_debug(el);
#endif /* DEBUG_EDIT */
		/* if EOF or error */
		if ((num = read_getcmd(el, &cmdnum, &ch)) != OKCMD) {
#ifdef DEBUG_READ
			(void) el->el_err_printf(el,
			    "Returning from el_gets %d\r\n", num);
#endif /* DEBUG_READ */
			if(num == EAGAINCMD) {
				el->el_nb_state = EAGAINCMD;
				return NULL;
			}
			break;
		}
		el_gets_dispatch(el, cmdnum, ch, &num);
	}
	el->el_nb_state = 0;

				/* make sure the tty is set up correctly */
	(void) tty_cookedmode(el);
	term__flush();		/* flush any buffered output */
	if (el->el_flags & HANDLE_SIGNALS)
		sig_clr(el);
	if (nread)
		*nread = num;
	return (num ? el->el_line.buffer : NULL);
}
