/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#ifndef _NOIT_CONSOLE_H
#define _NOIT_CONSOLE_H

#include "noit_defines.h"
#include "eventer/eventer.h"
#include "noitedit/histedit.h"
#include "noit_console_telnet.h"
#include "utils/noit_hash.h"
#include <stdarg.h>

struct _console_state;
struct __noit_console_closure;

typedef int (*console_cmd_func_t)(struct __noit_console_closure *,
                                  int, char **, void *);
typedef char *(*console_prompt_func_t)(EditLine *);
typedef void (*state_free_func_t)(struct _console_state *);

typedef struct cmd_info {
  const char          *name;
  console_cmd_func_t   func;
  void                *closure;
} cmd_info_t;

/* This performs a pop (exiting if at toplevel) */
extern cmd_info_t console_command_exit;

typedef struct _console_state {
  console_prompt_func_t  console_prompt_function;
  noit_hash_table        cmds;
  state_free_func_t      statefree;
} noit_console_state_t;

typedef struct _console_state_stack {
  noit_console_state_t *state;
  struct _console_state_stack *last;
} noit_console_state_stack_t;

typedef struct __noit_console_closure {
  eventer_t e;           /* The event it is attached to.  This
                          * is needed so it can write itself out */
  int   wants_shutdown;  /* Set this to 1 to have it die */

  /* nice console support */
  EditLine *el;
  History *hist;

  noit_console_state_stack_t *state_stack;

  int   pty_master;
  int   pty_slave;

  /* Output buffer for non-blocking sends */
  char *outbuf;
  int   outbuf_allocd;
  int   outbuf_len;
  int   outbuf_cooked;
  int   outbuf_completed;

  /* This tracks telnet protocol state (if we're doing telnet) */
  noit_console_telnet_closure_t telnet;
  void (*output_cooker)(struct __noit_console_closure *);
} * noit_console_closure_t;

API_EXPORT(void) noit_console_init();

API_EXPORT(int)
  noit_console_handler(eventer_t e, int mask, void *closure,
                       struct timeval *now);


API_EXPORT(int)
  nc_printf(noit_console_closure_t ncct, const char *fmt, ...);

API_EXPORT(int)
  nc_vprintf(noit_console_closure_t ncct, const char *fmt, va_list arg);

API_EXPORT(int)
  nc_write(noit_console_closure_t ncct, void *buf, int len);

API_EXPORT(int)
  noit_console_continue_sending(noit_console_closure_t ncct,
                                int *mask);

API_EXPORT(int)
  noit_console_state_init(noit_console_closure_t ncct);

API_EXPORT(int)
  noit_console_state_pop(noit_console_closure_t ncct, int argc, char **argv,
                         void *);

API_EXPORT(int)
  noit_console_state_add_cmd(noit_console_state_t *state,
                             cmd_info_t *cmd);

API_EXPORT(noit_console_state_t *)
  noit_console_state_build(console_prompt_func_t promptf, cmd_info_t **clist,
                           state_free_func_t sfreef);

API_EXPORT(void)
  noit_console_state_push_state(noit_console_closure_t ncct,
                                noit_console_state_t *);

API_EXPORT(noit_console_state_t *)
  noit_console_state_initial();

API_EXPORT(void)
  noit_console_state_free(noit_console_state_t *st);

API_EXPORT(int)
  noit_console_state_do(noit_console_closure_t ncct, int argc, char **argv);

API_EXPORT(int)
  _noit_console_state_do(noit_console_closure_t ncct,
                         noit_console_state_stack_t *stack,
                         int argc, char **argv);

API_EXPORT(int)
  noit_console_state_delegate(noit_console_closure_t ncct,
                              int argc, char **argv, void *closure);
 
API_EXPORT(cmd_info_t *)
  NCSCMD(const char *name, console_cmd_func_t func, void *closure);

#endif
