/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name OmniTI Computer Consulting, Inc. nor the names
 *       of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written
 *       permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _NOIT_CONSOLE_H
#define _NOIT_CONSOLE_H

#include "noit_defines.h"
#include "eventer/eventer.h"
#include "noitedit/histedit.h"
#include "noit_console_telnet.h"
#include "utils/noit_hash.h"
#include "utils/noit_skiplist.h"
#include <stdarg.h>

struct _console_state;
struct _console_state_stack;
struct __noit_console_closure;

typedef int (*console_cmd_func_t)(struct __noit_console_closure *,
                                  int, char **,
                                  struct _console_state *, void *);
typedef char * (*console_opt_func_t)(struct __noit_console_closure *,
                                     struct _console_state_stack *stack,
                                     struct _console_state *state,
                                     int argc, char **argv,
                                     int idx);
typedef char *(*console_prompt_func_t)(EditLine *);
typedef void (*state_free_func_t)(struct _console_state *);
typedef void (*state_userdata_free_func_t)(void *);

typedef struct {
  const char            *name;
  console_cmd_func_t     func;
  console_opt_func_t     autocomplete;
  struct _console_state *dstate;
  void                  *closure;
} cmd_info_t;

/* This performs a pop (exiting if at toplevel) */
extern cmd_info_t console_command_exit;
extern cmd_info_t console_command_help;
extern cmd_info_t console_command_shutdown;
extern cmd_info_t console_command_restart;

typedef struct {
  char                      *name;
  void                      *data;
  state_userdata_free_func_t freefunc;
} noit_console_userdata_t;

API_EXPORT(void)
  noit_console_userdata_set(struct __noit_console_closure *,
                            const char *name, void *data,
                            state_userdata_free_func_t freefunc);
API_EXPORT(void *)
  noit_console_userdata_get(struct __noit_console_closure *,
                            const char *name);

typedef struct _console_state {
  console_prompt_func_t      console_prompt_function;
  /*noit_hash_table            cmds; */
  noit_skiplist              cmds;
  state_free_func_t          statefree;
} noit_console_state_t;

typedef struct _console_state_stack {
  char *name;
  noit_console_state_t *state;
  void *userdata;
  struct _console_state_stack *last;
} noit_console_state_stack_t;

typedef struct __noit_console_closure {
  int initialized;
  char feed_path[128];
  eventer_t e;           /* The event it is attached to.  This
                          * is needed so it can write itself out */
  int   wants_shutdown;  /* Set this to 1 to have it die */

  /* nice console support */
  EditLine *el;
  History *hist;
  noit_hash_table userdata;
  /* This is console completion magic */
  int noit_edit_complete_cmdnum;
  int rl_point;
  int rl_end;

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

API_EXPORT(void) noit_console_init(const char *);

API_EXPORT(void) noit_console_set_default_prompt(const char *);

API_EXPORT(int)
  noit_console_handler(eventer_t e, int mask, void *closure,
                       struct timeval *now);


API_EXPORT(int)
  nc_printf(noit_console_closure_t ncct, const char *fmt, ...);

API_EXPORT(int)
  nc_vprintf(noit_console_closure_t ncct, const char *fmt, va_list arg);

API_EXPORT(int)
  nc_write(noit_console_closure_t ncct, const void *buf, int len);

API_EXPORT(int)
  noit_console_continue_sending(noit_console_closure_t ncct,
                                int *mask);

API_EXPORT(int)
  noit_console_state_init(noit_console_closure_t ncct);

API_EXPORT(int)
  noit_console_state_pop(noit_console_closure_t ncct, int argc, char **argv,
                         noit_console_state_t *, void *);

API_EXPORT(int)
  noit_console_help(noit_console_closure_t ncct, int argc, char **argv,
                    noit_console_state_t *, void *);

API_EXPORT(int)
  noit_console_shutdown(noit_console_closure_t ncct, int argc, char **argv,
                        noit_console_state_t *, void *);

API_EXPORT(int)
  noit_console_restart(noit_console_closure_t ncct, int argc, char **argv,
                       noit_console_state_t *, void *);

API_EXPORT(int)
  noit_console_state_add_cmd(noit_console_state_t *state,
                             cmd_info_t *cmd);

API_EXPORT(cmd_info_t *)
  noit_console_state_get_cmd(noit_console_state_t *state,
                             const char *name);

API_EXPORT(noit_console_state_t *)
  noit_console_state_build(console_prompt_func_t promptf, cmd_info_t **clist,
                           state_free_func_t sfreef);

API_EXPORT(void)
  noit_console_state_push_state(noit_console_closure_t ncct,
                                noit_console_state_t *);

API_EXPORT(noit_console_state_t *)
  noit_console_state_initial();

API_EXPORT(noit_console_state_t *)
  noit_console_state_alloc(void);

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
                              int argc, char **argv,
                              noit_console_state_t *dstate,
                              void *closure);
 
API_EXPORT(cmd_info_t *)
  NCSCMD(const char *name, console_cmd_func_t func, console_opt_func_t ac,
         noit_console_state_t *dstate, void *closure);

API_EXPORT(int)
  noit_console_write_xml(void *vncct, const char *buffer, int len);

API_EXPORT(int)
  noit_console_close_xml(void *vncct);

API_EXPORT(void)
  noit_console_add_help(const char *topic, console_cmd_func_t topic_func,
                        console_opt_func_t autocomplete);

API_EXPORT(unsigned char)
  noit_edit_complete(EditLine *el, int invoking_key);

API_EXPORT(char *)
  noit_console_opt_delegate(noit_console_closure_t ncct,
                            noit_console_state_stack_t *stack,
                            noit_console_state_t *state,
                            int argc, char **argv,
                            int idx);

#endif
