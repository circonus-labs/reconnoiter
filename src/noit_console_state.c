/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#include "noit_defines.h"

#include "eventer/eventer.h"
#include "utils/noit_log.h"
#include "utils/noit_hash.h"
#include "noit_listener.h"
#include "noit_console.h"
#include "noit_tokenizer.h"

cmd_info_t console_command_exit = { "exit", noit_console_state_pop, NULL };

static char *
noit_console_state_prompt(EditLine *el) {
  static char *tl = "noit# ";
  return tl;
}

int
noit_console_state_delegate(noit_console_closure_t ncct,
                            int argc, char **argv, void *closure) {
  noit_console_state_stack_t tmps = { 0 };

  if(argc == 0) {
    nc_printf(ncct, "arguments expected\n");
    return -1;
  }
  tmps.state = closure;
  return _noit_console_state_do(ncct, &tmps, argc, argv);
}

int
_noit_console_state_do(noit_console_closure_t ncct,
                       noit_console_state_stack_t *stack,
                       int argc, char **argv) {
  cmd_info_t *cmd;

  if(!argc) {
    nc_printf(ncct, "arguments expected\n");
    return -1;
  }
  if(!noit_hash_retrieve(&stack->state->cmds,
                         argv[0], strlen(argv[0]), (void **)&cmd)) {
    nc_printf(ncct, "No such command: '%s'\n", argv[0]);
    return -1;
  }
  return cmd->func(ncct, argc-1, argv+1, cmd->closure);
}
int
noit_console_state_do(noit_console_closure_t ncct, int argc, char **argv) {
  return _noit_console_state_do(ncct, ncct->state_stack, argc, argv);
}

int cmd_info_comparek(void *akv, void *bv) {
  char *ak = (char *)akv;
  cmd_info_t *b = (cmd_info_t *)bv;
  return strcasecmp(ak, b->name);
}
int cmd_info_compare(void *av, void *bv) {
  cmd_info_t *a = (cmd_info_t *)av;
  cmd_info_t *b = (cmd_info_t *)bv;
  return strcasecmp(a->name, b->name);
}

int
noit_console_state_add_cmd(noit_console_state_t *state,
                           cmd_info_t *cmd) {
  return noit_hash_store(&state->cmds, cmd->name, strlen(cmd->name), cmd);
}

noit_console_state_t *
noit_console_state_build(console_prompt_func_t promptf, cmd_info_t **clist,
                         state_free_func_t sfreef) {
  noit_console_state_t *state;
  state = calloc(1, sizeof(*state));
  state->console_prompt_function = promptf;
  while(*clist) {
    noit_hash_store(&state->cmds,
                    (*clist)->name, strlen((*clist)->name),
                    *clist);
    clist++;
  }
  state->statefree = sfreef;
  return state;
}

cmd_info_t *NCSCMD(const char *name, console_cmd_func_t func,
                   void *closure) {
  cmd_info_t *cmd;
  cmd = calloc(1, sizeof(*cmd));
  cmd->name = strdup(name);
  cmd->func = func;
  cmd->closure = closure;
  return cmd;
}

noit_console_state_t *
noit_console_state_initial() {
  static noit_console_state_t *_top_level_state = NULL;
  if(!_top_level_state) {
    _top_level_state = calloc(1, sizeof(*_top_level_state));
    noit_console_state_add_cmd(_top_level_state, &console_command_exit);
  }
  return _top_level_state;
}

void
noit_console_state_push_state(noit_console_closure_t ncct,
                              noit_console_state_t *state) {
  noit_console_state_stack_t *stack;
  stack = calloc(1, sizeof(*stack));
  stack->last = ncct->state_stack;
  stack->state = state;
  ncct->state_stack = stack;
}

int
noit_console_state_pop(noit_console_closure_t ncct, int argc, char **argv,
                       void *unused) {
  noit_console_state_stack_t *current;

  if(argc) {
    nc_printf(ncct, "no arguments allowed to this command.\n");
    return -1;
  }
  if(!ncct->state_stack || !ncct->state_stack->last) {
    ncct->wants_shutdown = 1;
    return 0;
  }

  current = ncct->state_stack;
  ncct->state_stack = current->last;
  current->last = NULL;
  if(current->state->statefree) current->state->statefree(current->state);
  free(current);
  noit_console_state_init(ncct);
  return 0;
}

int
noit_console_state_init(noit_console_closure_t ncct) {
  if(ncct->el) {
    console_prompt_func_t f;
    f = ncct->state_stack->state->console_prompt_function;
    el_set(ncct->el, EL_PROMPT, f ? f : noit_console_state_prompt);
  }
  return 0;
}
