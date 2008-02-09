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

cmd_info_t _cit_exit = { "exit", noit_console_state_pop };
cmd_info_t *_tl_cmds[] = {
  &_cit_exit,
  NULL
};

int
noit_console_state_do(noit_console_closure_t ncct, int argc, char **argv) {
  cmd_info_t *cmd;

  if(!argc) return -1;
  if(!noit_hash_retrieve(&ncct->state->cmds,
                         argv[0], strlen(argv[0]), (void **)&cmd)) {
    nc_printf(ncct, "No such command: '%s'\n", argv[0]);
    return -1;
  }
  return cmd->func(ncct, argc-1, argv+1);
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
void
noit_console_state_free(noit_console_state_t *st) {
  noit_hash_destroy(&st->cmds, NULL, NULL);
}
noit_console_state_t *
noit_console_state_initial() {
  return noit_console_state_build(noit_console_state_prompt, _tl_cmds,
                                  noit_console_state_free);
}

char *
noit_console_state_prompt(EditLine *el) {
  static char *tl = "noit# ";
  return tl;
}

int
noit_console_state_pop(noit_console_closure_t ncct, int argc, char **argv) {
  noit_console_state_t *current;

  if(argc) {
    nc_printf(ncct, "no arguments allowed to this command.\n");
    return -1;
  }
  if(!ncct->state || !ncct->state->stacked) {
    ncct->wants_shutdown = 1;
    return 0;
  }

  current = ncct->state;
  ncct->state = ncct->state->stacked;
  current->stacked = NULL;
  if(current->statefree) current->statefree(current);
  noit_console_state_init(ncct);
  return 0;
}

int
noit_console_state_init(noit_console_closure_t ncct) {
  if(ncct->el && ncct->state->console_prompt_function) {
    el_set(ncct->el, EL_PROMPT, ncct->state->console_prompt_function);
  }
  return 0;
}
