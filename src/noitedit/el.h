/*	$NetBSD: el.h,v 1.8 2001/01/06 14:44:50 jdolecek Exp $	*/

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
 *
 *	@(#)el.h	8.1 (Berkeley) 6/4/93
 */

/*
 * el.h: Internal structures.
 */
#ifndef _h_el
#define	_h_el
/*
 * Local defaults
   #define	VIDEFAULT
   #define	KSHVI
 */
#define	ANCHOR

#include "noit_defines.h"
#include "eventer/eventer.h"
#include <stdio.h>
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_TERMIO_H
#include <termio.h>
#endif
#ifdef HAVE_TERMIOS_H
#include <termios.h>
#endif
#ifdef HAVE_CURSES_H
#include <curses.h>
#endif
#ifdef HAVE_TERM_H
#include <term.h>
#endif

#define	EL_BUFSIZ	1024		/* Maximum line size		*/

#define	HANDLE_SIGNALS	1<<0
#define	NO_TTY		1<<1
#define	EDIT_DISABLED	1<<2

typedef int bool_t;			/* True or not			*/

typedef unsigned char el_action_t;	/* Index to command array	*/

typedef struct coord_t {		/* Position on the screen	*/
	int	h;
	int	v;
} coord_t;

typedef struct el_line_t {
	char	*buffer;		/* Input line			*/
	char	*cursor;		/* Cursor position		*/
	char	*lastchar;		/* Last character		*/
	const char	*limit;			/* Max position			*/
} el_line_t;

/*
 * Editor state
 */
typedef struct el_state_t {
	int		inputmode;	/* What mode are we in?		*/
	int		doingarg;	/* Are we getting an argument?	*/
	int		argument;	/* Numeric argument		*/
	int		metanext;	/* Is the next char a meta char */
	el_action_t	lastcmd;	/* Previous command		*/
} el_state_t;

/*
 * Until we come up with something better...
 */
#define	el_malloc(a)	malloc(a)
#define	el_realloc(a,b)	realloc(a, b)
#define	el_free(a)	free(a)

#include "noitedit/tty.h"
#include "noitedit/prompt.h"
#include "noitedit/key.h"
#include "noitedit/el_term.h"
#include "noitedit/refresh.h"
#include "noitedit/chared.h"
#include "noitedit/common.h"
#include "noitedit/search.h"
#include "noitedit/hist.h"
#include "noitedit/map.h"
#include "noitedit/parse.h"
#include "noitedit/sig.h"
#include "noitedit/help.h"

struct editline {
	char		 *el_prog;	/* the program name		*/
	int		  el_outfd;	/* Output file descriptor	*/
        eventer_t         el_out_e;
	int               el_errfd;     /* Error file descriptor        */
        eventer_t         el_err_e;
	int		  el_infd;	/* Input file descriptor	*/
        eventer_t         el_in_e;
	int		  el_flags;	/* Various flags.		*/
	coord_t		  el_cursor;	/* Cursor location		*/
	char		**el_display;	/* Real screen image = what is there */
	char		**el_vdisplay;	/* Virtual screen image = what we see */
        key_node_t       *el_keystate;  /* last char keystate progression */
	el_line_t	  el_line;	/* The current line information	*/
	el_state_t	  el_state;	/* Current editor state		*/
	el_term_t	  el_term;	/* Terminal dependent stuff	*/
	el_tty_t	  el_tty;	/* Tty dependent stuff		*/
	el_refresh_t	  el_refresh;	/* Refresh stuff		*/
	el_prompt_t	  el_prompt;	/* Prompt stuff			*/
	el_prompt_t	  el_rprompt;	/* Prompt stuff			*/
	el_chared_t	  el_chared;	/* Characted editor stuff	*/
	el_map_t	  el_map;	/* Key mapping stuff		*/
	el_key_t	  el_key;	/* Key binding stuff		*/
	el_history_t	  el_history;	/* History stuff		*/
	el_search_t	  el_search;	/* Search stuff			*/
	el_signal_t	  el_signal;	/* Signal handling stuff	*/
	int               el_nb_state;  /* Did we eagain?               */
	int             (*el_err_printf)(struct editline *, const char *, ...);
	int             (*el_std_printf)(struct editline *, const char *, ...);
	int             (*el_std_putc)(int, struct editline *);
	int             (*el_std_flush)(struct editline *);
	void		 *el_userdata;
};

protected int	el_editmode(EditLine *, int, char **);
protected int   el_err_printf(EditLine *, const char *, ...);
protected int   el_err_vprintf(EditLine *, const char *, va_list);
protected int   el_std_printf(EditLine *, const char *, ...);
protected int   el_std_vprintf(EditLine *, const char *, va_list);
protected int	el_std_putc(int, EditLine *);
protected int	el_std_flush(EditLine *);

#ifdef DEBUG
#define EL_ABORT(a)	(void) (el->el_err_printf(el, "%s, %d: ", \
				__FILE__, __LINE__), fprintf a, abort())
#else
#define EL_ABORT(a)	abort()
#endif
#endif /* _h_el */
