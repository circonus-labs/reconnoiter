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

cmd_info_t console_command_exit = {
  "exit", noit_console_state_pop, NULL, NULL
};

static char *
noit_console_state_prompt(EditLine *el) {
  static char *tl = "noit# ";
  return tl;
}

static char *
apply_replace(const char *src, const char *name, const char *value) {
  char *result, *cp;
  const char *nextpat, *searchstart;
  char pat[256];
  int maxlen, patlen, vlen, slen;
  snprintf(pat, sizeof(pat), "{%s}", name);
  patlen = strlen(pat);
  vlen = strlen(value);
  slen = strlen(src);
  /* Worst case is just a stream of replacements. */
  maxlen = (slen / patlen) * vlen + (slen % patlen) + 1;
  cp = result = malloc(maxlen);
  searchstart = src;
  while((nextpat = strstr(searchstart, pat)) != NULL) {
    memcpy(cp, searchstart, nextpat - searchstart); /* pull the prefix */
    cp += nextpat - searchstart;                    /* advance destination */
    memcpy(cp, value, vlen);                        /* copy replacement */
    cp += vlen;                                     /* advance destination */
    searchstart += patlen;                          /* set new searchstart */
  }
  /* Pick up the trailer (plus '\0') */
  memcpy(cp, searchstart, strlen(searchstart)+1);
  return result;
}
static int
expand_range(const char *range, char ***set) {
  int count;
  /* (full:)?(\d+).(\d+)\.(\d+)(?:\.(\d+))?(?:/(\d+))? */
  /* (\d+)(?:,(\d+))?\.\.(\d+) */
/* FIXME: TEST! */
  *set = malloc(10 * sizeof(*set));
  for(count=0;count<10;count++) {
    (*set)[count] = strdup("00-test-expand");
    snprintf((*set)[count], 3, "%02d", count);
    (*set)[count][2] = '-';
  }

  return count;
}
int
noit_console_generic_apply(noit_console_closure_t ncct,
                           int argc, char **argv,
                           noit_console_state_t *dstate,
                           void *closure) {
  int i, j, count;
  char *name, *range;
  char **nargv, **expanded;
  int problems = 0;
  if(argc < 3) {
    nc_printf(ncct, "apply <name> <range> cmd ...\n");
    return -1;
  }
  name = argv[0];
  range = argv[1];
  argc -= 2;
  argv += 2;

  count = expand_range(range, &expanded);
  if(!count) {
    nc_printf(ncct, "apply error: '%s' range produced nothing\n", range);
    return -1;
  }
  if(count < 0) {
    nc_printf(ncct, "apply error: '%s' range would produce %d items.\n",
              range, count);
    return -1;
  }
  nargv = malloc(argc * sizeof(*nargv));
  for(i=0; i<count; i++) {
    for(j=0; j<argc; j++) nargv[j] = apply_replace(argv[j], name, expanded[i]);
    if(noit_console_state_do(ncct, argc, nargv)) problems = -1;
    for(j=0; j<argc; j++) free(nargv[j]);
    free(expanded[i]);
  }
  free(nargv);
  free(expanded);
  return problems;
}

int
noit_console_state_delegate(noit_console_closure_t ncct,
                            int argc, char **argv,
                            noit_console_state_t *dstate,
                            void *closure) {
  noit_console_state_stack_t tmps = { 0 };

  if(argc == 0) {
    nc_printf(ncct, "arguments expected\n");
    /* XXX: noit_console_render_help(dstate); */
    return -1;
  }
  if(!dstate) {
    nc_printf(ncct, "internal error: no delegate state\n");
    return -1;
  }
  tmps.state = dstate;
  return _noit_console_state_do(ncct, &tmps, argc, argv);
}

int
_noit_console_state_do(noit_console_closure_t ncct,
                       noit_console_state_stack_t *stack,
                       int argc, char **argv) {
  noit_skiplist_node *next, *amb = NULL;
  cmd_info_t *cmd;

  if(!argc) {
    nc_printf(ncct, "arguments expected\n");
    return -1;
  }
  cmd = noit_skiplist_find_neighbors(&stack->state->cmds, argv[0],
                                     NULL, NULL, &next);
  if(!cmd) {
    int ambiguous = 0;
    if(next) {
      cmd_info_t *pcmd = NULL;
      cmd = next->data;
      amb = next;
      noit_skiplist_next(&stack->state->cmds, &amb);
      if(amb) pcmd = amb->data;
      /* So cmd is the next in line... pcmd is the one after that.
       * If they both strncasecmp to 0, we're ambiguous,
       *    neither, then we're not found.
       *    only cmd, then we've found a partial, unambiguous.
       */
      if(strncasecmp(cmd->name, argv[0], strlen(argv[0])) == 0) {
        if(pcmd && strncasecmp(pcmd->name, argv[0], strlen(argv[0])) == 0) {
          cmd = NULL;
          ambiguous = 1;
        }
      }
      else
        cmd = NULL;
    }
    if(!cmd) {
      if(ambiguous) {
        nc_printf(ncct, "Ambiguous command: '%s'\n", argv[0]);
        amb = next;
        for(amb = next; amb; noit_skiplist_next(&stack->state->cmds, &amb)) {
          cmd = amb->data;
          if(strncasecmp(cmd->name, argv[0], strlen(argv[0])) == 0)
            nc_printf(ncct, "\t%s\n", cmd->name);
          else
            break;
        }
      }
      else
        nc_printf(ncct, "No such command: '%s'\n", argv[0]);
      return -1;
    }
  }
  return cmd->func(ncct, argc-1, argv+1, cmd->dstate, cmd->closure);
}
int
noit_console_state_do(noit_console_closure_t ncct, int argc, char **argv) {
  return _noit_console_state_do(ncct, ncct->state_stack, argc, argv);
}

int cmd_info_comparek(const void *akv, const void *bv) {
  char *ak = (char *)akv;
  cmd_info_t *b = (cmd_info_t *)bv;
  return strcasecmp(ak, b->name);
}
int cmd_info_compare(const void *av, const void *bv) {
  cmd_info_t *a = (cmd_info_t *)av;
  cmd_info_t *b = (cmd_info_t *)bv;
  return strcasecmp(a->name, b->name);
}

noit_console_state_t *
noit_console_state_alloc(void) {
  noit_console_state_t *s;
  s = calloc(1, sizeof(*s));
  noit_skiplist_init(&s->cmds);
  noit_skiplist_set_compare(&s->cmds, cmd_info_compare, cmd_info_comparek);
  noit_console_state_add_cmd(s,
      NCSCMD("apply", noit_console_generic_apply, NULL, NULL));
  return s;
}

int
noit_console_state_add_cmd(noit_console_state_t *state,
                           cmd_info_t *cmd) {
  return (noit_skiplist_insert(&state->cmds, cmd) != NULL);
}

cmd_info_t *
noit_console_state_get_cmd(noit_console_state_t *state,
                           const char *name) {
  cmd_info_t *cmd;
  cmd = noit_skiplist_find(&state->cmds, name, NULL);
  return cmd;
}

noit_console_state_t *
noit_console_state_build(console_prompt_func_t promptf, cmd_info_t **clist,
                         state_free_func_t sfreef) {
  noit_console_state_t *state;
  state = noit_console_state_alloc();
  state->console_prompt_function = promptf;
  while(*clist) {
    noit_skiplist_insert(&state->cmds, *clist);
    clist++;
  }
  state->statefree = sfreef;
  return state;
}

cmd_info_t *NCSCMD(const char *name, console_cmd_func_t func,
                   noit_console_state_t *dstate, void *closure) {
  cmd_info_t *cmd;
  cmd = calloc(1, sizeof(*cmd));
  cmd->name = strdup(name);
  cmd->func = func;
  cmd->dstate = dstate;
  cmd->closure = closure;
  return cmd;
}

noit_console_state_t *
noit_console_state_initial() {
  static noit_console_state_t *_top_level_state = NULL;
  if(!_top_level_state) {
    static noit_console_state_t *show_state;
    _top_level_state = noit_console_state_alloc();
    noit_console_state_add_cmd(_top_level_state, &console_command_exit);
    show_state = noit_console_state_alloc();
    noit_console_state_add_cmd(_top_level_state,
      NCSCMD("show", noit_console_state_delegate, show_state, NULL));
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
                       noit_console_state_t *dstate, void *unused) {
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
