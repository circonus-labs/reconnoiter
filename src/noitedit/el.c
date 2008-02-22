/*	$NetBSD: el.c,v 1.21 2001/01/05 22:45:30 christos Exp $	*/

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
static char sccsid[] = "@(#)el.c	8.2 (Berkeley) 1/3/94";
#else
__RCSID("$NetBSD: el.c,v 1.21 2001/01/05 22:45:30 christos Exp $");
#endif
#endif /* not lint && not SCCSID */

/*
 * el.c: EditLine interface functions
 */
#include "noitedit/sys.h"

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <sys/types.h>
#if !_MSC_VER
#include <sys/param.h>
#endif
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <pthread.h>
#include "noitedit/el.h"

pthread_key_t tputs_hack;
public void
el_multi_init() {
  pthread_key_create(&tputs_hack, NULL);
}

public void
el_multi_set_el(EditLine *el) {
  pthread_setspecific(tputs_hack, el);
}

public EditLine *
el_multi_get_el() {
  return (EditLine *)pthread_getspecific(tputs_hack);
}

/* el_init():
 *	Initialize editline and set default parameters.
 */
public EditLine *
el_init(const char *prog, int infd, eventer_t in_e,
        int outfd, eventer_t out_e, int errfd, eventer_t err_e)
{

	EditLine *el = (EditLine *) el_malloc(sizeof(EditLine));
#ifdef DEBUG
	char *tty;
#endif

	if (el == NULL)
		return (NULL);

	memset(el, 0, sizeof(EditLine));

	el->el_infd = infd;
	el->el_in_e = in_e;
	el->el_outfd = outfd;
	el->el_out_e = out_e;
	el->el_errfd = errfd;
	el->el_err_e = err_e;
	el->el_err_printf = el_err_printf;
	el->el_std_printf = el_std_printf;
	el->el_std_putc = el_std_putc;
	el->el_std_flush = el_std_flush;
	el->el_prog = strdup(prog);

	/*
         * Initialize all the modules. Order is important!!!
         */
	el->el_flags = 0;

	(void) term_init(el);
	(void) key_init(el);
	(void) map_init(el);
	if (tty_init(el) == -1)
		el->el_flags |= NO_TTY;
	(void) ch_init(el);
	(void) search_init(el);
	(void) hist_init(el);
	(void) prompt_init(el);
	(void) sig_init(el);

	return (el);
}


/* el_end():
 *	Clean up.
 */
public void
el_end(EditLine *el)
{

	if (el == NULL)
		return;

	el_reset(el);

	term_end(el);
	key_end(el);
	map_end(el);
	tty_end(el);
	ch_end(el);
	search_end(el);
	hist_end(el);
	prompt_end(el);
	sig_end(el);

	el_free((ptr_t) el->el_prog);
	el_free((ptr_t) el);
}


/* el_reset():
 *	Reset the tty and the parser
 */
public void
el_reset(EditLine *el)
{

	tty_cookedmode(el);
	ch_reset(el);		/* XXX: Do we want that? */
}


/* el_set():
 *	set the editline parameters
 */
public int
el_set(EditLine *el, int op, ...)
{
	va_list va;
	int rv;
	va_start(va, op);

	if (el == NULL)
		return (-1);
	switch (op) {
	case EL_PROMPT:
	case EL_RPROMPT:
		rv = prompt_set(el, va_arg(va, el_pfunc_t), op);
		break;

        case EL_ERRPRINTFFN:
		el->el_err_printf = va_arg(va, el_printffunc_t);
		rv = 0;
		break;

        case EL_STDPRINTFFN:
		el->el_std_printf = va_arg(va, el_printffunc_t);
		rv = 0;
		break;

        case EL_STDPUTCFN:
		el->el_std_putc = va_arg(va, el_putcfunc_t);
		rv = 0;
		break;

        case EL_STDFLUSHFN:
		el->el_std_flush = va_arg(va, el_flushfunc_t);
		rv = 0;
		break;

	case EL_USERDATA:
		el->el_userdata = va_arg(va, void *);
		rv = 0;
		break;

	case EL_TERMINAL:
		rv = term_set(el, va_arg(va, char *));
		break;

	case EL_EDITOR:
		rv = map_set_editor(el, va_arg(va, char *));
		break;

	case EL_SIGNAL:
		if (va_arg(va, int))
			el->el_flags |= HANDLE_SIGNALS;
		else
			el->el_flags &= ~HANDLE_SIGNALS;
		rv = 0;
		break;

	case EL_BIND:
	case EL_TELLTC:
	case EL_SETTC:
	case EL_ECHOTC:
	case EL_SETTY:
	{
		char *argv[20];
		int i;

		for (i = 1; i < 20; i++)
			if ((argv[i] = va_arg(va, char *)) == NULL)
				break;

		switch (op) {
		case EL_BIND:
			argv[0] = "bind";
			rv = map_bind(el, i, argv);
			break;

		case EL_TELLTC:
			argv[0] = "telltc";
			rv = term_telltc(el, i, argv);
			break;

		case EL_SETTC:
			argv[0] = "settc";
			rv = term_settc(el, i, argv);
			break;

		case EL_ECHOTC:
			argv[0] = "echotc";
			rv = term_echotc(el, i, argv);
			break;

		case EL_SETTY:
			argv[0] = "setty";
			rv = tty_stty(el, i, argv);
			break;

		default:
			rv = -1;
			EL_ABORT((el->el_errfile, "Bad op %d\n", op));
			break;
		}
		break;
	}

	case EL_ADDFN:
	{
		char *name = va_arg(va, char *);
		char *help = va_arg(va, char *);
		el_func_t func = va_arg(va, el_func_t);

		rv = map_addfunc(el, name, help, func);
		break;
	}

	case EL_HIST:
	{
		hist_fun_t func = va_arg(va, hist_fun_t);
		ptr_t ptr = va_arg(va, char *);

		rv = hist_set(el, func, ptr);
		break;
	}

	case EL_EDITMODE:
		if (va_arg(va, int))
			el->el_flags &= ~EDIT_DISABLED;
		else
			el->el_flags |= EDIT_DISABLED;
		rv = 0;
		break;

	default:
		rv = -1;
	}

	va_end(va);
	return (rv);
}


/* el_get():
 *	retrieve the editline parameters
 */
public int
el_get(EditLine *el, int op, void *ret)
{
	int rv;
	union {
		void *vptr;
		const char *cptr;
		el_pfunc_t elpf;
		el_printffunc_t eprintff;
		el_putcfunc_t eputcf;
		el_flushfunc_t eflushf;
	} vret;

	vret.vptr = ret;
	if (el == NULL || ret == NULL)
		return (-1);
	switch (op) {
	case EL_PROMPT:
	case EL_RPROMPT:
		rv = prompt_get(el, &vret.elpf, op);
		break;

	case EL_ERRPRINTFFN:
		*((el_printffunc_t *)ret) = el->el_err_printf;
		rv = 0;
		break;

	case EL_STDPRINTFFN:
		*((el_printffunc_t *)ret) = el->el_std_printf;
		rv = 0;
		break;

	case EL_STDPUTCFN:
		*((el_putcfunc_t *)ret) = el->el_std_putc;
		rv = 0;
		break;

	case EL_STDFLUSHFN:
		*((el_flushfunc_t *)ret) = el->el_std_flush;
		rv = 0;
		break;

	case EL_USERDATA:
		*((void **)ret) = el->el_userdata;
		rv = 0;
		break;

	case EL_EDITOR:
		rv = map_get_editor(el, &vret.cptr);
		break;

	case EL_SIGNAL:
		*((int *) ret) = (el->el_flags & HANDLE_SIGNALS);
		rv = 0;
		break;

	case EL_EDITMODE:
		*((int *) ret) = (!(el->el_flags & EDIT_DISABLED));
		rv = 0;
		break;

#if 0				/* XXX */
	case EL_TERMINAL:
		rv = term_get(el, (const char *) &ret);
		break;

	case EL_BIND:
	case EL_TELLTC:
	case EL_SETTC:
	case EL_ECHOTC:
	case EL_SETTY:
	{
		char *argv[20];
		int i;

		for (i = 1; i < 20; i++)
			if ((argv[i] = va_arg(va, char *)) == NULL)
				break;

		switch (op) {
		case EL_BIND:
			argv[0] = "bind";
			rv = map_bind(el, i, argv);
			break;

		case EL_TELLTC:
			argv[0] = "telltc";
			rv = term_telltc(el, i, argv);
			break;

		case EL_SETTC:
			argv[0] = "settc";
			rv = term_settc(el, i, argv);
			break;

		case EL_ECHOTC:
			argv[0] = "echotc";
			rv = term_echotc(el, i, argv);
			break;

		case EL_SETTY:
			argv[0] = "setty";
			rv = tty_stty(el, i, argv);
			break;

		default:
			rv = -1;
			EL_ABORT((el->errfile, "Bad op %d\n", op));
			break;
		}
		break;
	}

	case EL_ADDFN:
	{
		char *name = va_arg(va, char *);
		char *help = va_arg(va, char *);
		el_func_t func = va_arg(va, el_func_t);

		rv = map_addfunc(el, name, help, func);
		break;
	}

	case EL_HIST:
		{
			hist_fun_t func = va_arg(va, hist_fun_t);
			ptr_t ptr = va_arg(va, char *);
			rv = hist_set(el, func, ptr);
		}
		break;
#endif /* XXX */

	default:
		rv = -1;
	}

	return (rv);
}


/* el_line():
 *	Return editing info
 */
public const LineInfo *
el_line(EditLine *el)
{

	return (const LineInfo *) (void *) &el->el_line;
}

static const char elpath[] = "/.editrc";

/* el_source():
 *	Source a file
 */
public int
el_source(EditLine *el, const char *fname)
{
	FILE *fp;
	size_t len;
	char *ptr, path[MAXPATHLEN];

	fp = NULL;
	if (fname == NULL) {
		if (issetugid())
			return (-1);
#if _WIN32
		if (getenv("HOMEDRIVE") && getenv("HOMEPATH")) {
			snprintf(path, sizeof(path), "%s%s%s", getenv("HOMEDRIVE"), getenv("HOMEPATH"), elpath);
			fname = path;
		}
#else
		if ((ptr = getenv("HOME")) == NULL)
			return (-1);
		if (strlcpy(path, ptr, sizeof(path)) >= sizeof(path))
			return (-1);
		if (strlcat(path, elpath, sizeof(path)) >= sizeof(path))
			return (-1);
		fname = path;
#endif
	}
	if (fname == NULL)
		return -1;
	if (fp == NULL)
		fp = fopen(fname, "r");
	if (fp == NULL) {
		return (-1);
	}

	while ((ptr = fgetln(fp, &len)) != NULL) {
		if (len > 0 && ptr[len - 1] == '\n')
			--len;
		ptr[len] = '\0';
		if (parse_line(el, ptr) == -1) {
			(void) fclose(fp);
			return (-1);
		}
	}

	(void) fclose(fp);
	return (0);
}


/* el_resize():
 *	Called from program when terminal is resized
 */
public void
el_resize(EditLine *el)
{
	int lins, cols;
#if !_MSC_VER
	sigset_t oset, nset;

	(void) sigemptyset(&nset);
	(void) sigaddset(&nset, SIGWINCH);
	(void) sigprocmask(SIG_BLOCK, &nset, &oset);
#endif

	/* get the correct window size */
	if (term_get_size(el, &lins, &cols))
		term_change_size(el, lins, cols);

#if !_MSC_VER
	(void) sigprocmask(SIG_SETMASK, &oset, NULL);
#endif
}


/* el_beep():
 *	Called from the program to beep
 */
public void
el_beep(EditLine *el)
{

	term_beep(el);
}


/* el_editmode()
 *	Set the state of EDIT_DISABLED from the `edit' command.
 */
protected int
/*ARGSUSED*/
el_editmode(EditLine *el, int argc, char **argv)
{
	const char *how;

	if (argv == NULL || argc != 2 || argv[1] == NULL)
		return (-1);

	how = argv[1];
	if (strcmp(how, "on") == 0)
		el->el_flags &= ~EDIT_DISABLED;
	else if (strcmp(how, "off") == 0)
		el->el_flags |= EDIT_DISABLED;
	else {
		(void) el->el_err_printf(el, "edit: Bad value `%s'.\n", how);
		return (-1);
	}
	return (0);
}

protected int
el_err_vprintf(EditLine *el, const char *fmt, va_list arg)
{
	int len, mask;
	char buffer[1024];
	len = vsnprintf(buffer, sizeof(buffer), fmt, arg);
	if(len > sizeof(buffer)) len = sizeof(buffer);
	if(el->el_err_e)
		return el->
			el_err_e->
			opset->
			write(el->el_err_e->fd, buffer, len,
				&mask, el->el_err_e);
	return write(el->el_errfd, buffer, len);
}

protected int
el_err_printf(EditLine *el, const char *fmt, ...)
{
	int len;
	va_list arg;
	va_start(arg, fmt);
	len = el_err_vprintf(el, fmt, arg);
	va_end(arg);
	return len;
}

protected int
el_std_vprintf(EditLine *el, const char *fmt, va_list arg)
{
	int len, mask;
	char buffer[1024];
	len = vsnprintf(buffer, sizeof(buffer), fmt, arg);
	if(len > sizeof(buffer)) len = sizeof(buffer);
	if(el->el_out_e)
		return el->
			el_out_e->
			opset->
			write(el->el_out_e->fd, buffer,
				len, &mask, el->el_out_e);
	return write(el->el_outfd, buffer, len);
}

protected int
el_std_printf(EditLine *el, const char *fmt, ...)
{
	int len;
	va_list arg;
	va_start(arg, fmt);
	len = el_std_vprintf(el, fmt, arg);
	va_end(arg);
	return len;
}

protected int
el_std_putc(int i, EditLine *el)
{
	int mask;
	unsigned char c = i & 0xff;
	if(el->el_out_e)
		return el->
			el_out_e->
			opset->
			write(el->el_out_e->fd, &c,
				1, &mask, el->el_out_e);
	return write(el->el_outfd, &c, 1);
}
protected int
el_std_flush(EditLine *el)
{
	return 0;
}
