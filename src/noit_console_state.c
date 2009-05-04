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
#include "noit_module.h"

#include <netinet/in.h>
#include <arpa/inet.h>
#include <pcre.h>

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

cmd_info_t console_command_help = {
  "help", noit_console_help, noit_console_opt_delegate, NULL, NULL
};
cmd_info_t console_command_exit = {
  "exit", noit_console_state_pop, NULL, NULL, NULL
};
cmd_info_t console_command_shutdown = {
  "shutdown", noit_console_shutdown, NULL, NULL, NULL
};
cmd_info_t console_command_restart = {
  "restart", noit_console_restart, NULL, NULL, NULL
};

void
noit_console_add_help(const char *topic, console_cmd_func_t topic_func,
                      console_opt_func_t ac) {
  noit_console_state_t *s = console_command_help.dstate;
  if(!s) {
    console_command_help.dstate = s = calloc(1, sizeof(*s));
    noit_skiplist_init(&s->cmds);
    noit_skiplist_set_compare(&s->cmds, cmd_info_compare, cmd_info_comparek);
  }
  noit_console_state_add_cmd(s, NCSCMD(topic, topic_func, ac, NULL, NULL));
}

static char *default_prompt = NULL;

void
noit_console_set_default_prompt(const char *prompt) {
  char *tofree = default_prompt;
  default_prompt = strdup(prompt);
  if(tofree) free(tofree);
}
static char *
noit_console_state_prompt(EditLine *el) {
  static char *tl = "noit# ";
  if(default_prompt) return default_prompt;
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
  maxlen = (slen / patlen) * MAX(vlen,patlen) + (slen % patlen) + 1;
  cp = result = malloc(maxlen);
  searchstart = src;
  while((nextpat = strstr(searchstart, pat)) != NULL) {
    memcpy(cp, searchstart, nextpat - searchstart); /* pull the prefix */
    cp += nextpat - searchstart;                    /* advance destination */
    memcpy(cp, value, vlen);                        /* copy replacement */
    cp += vlen;                                     /* advance destination */
    searchstart = nextpat + patlen;                 /* set new searchstart */
  }
  /* Pick up the trailer (plus '\0') */
  memcpy(cp, searchstart, strlen(searchstart)+1);
  return result;
}
static pcre *IP_match = NULL;
static pcre *numeric_match = NULL;
static int
expand_range(const char *range, char ***set, int max_count, const char **err) {
  int count, erroff, ovector[30], rv;
  char buff[32]; /* submatches */
  const char *pcre_err;
  *err = NULL;
  if(!IP_match) {
    IP_match = pcre_compile("^(full:)?(\\d+\\.\\d+\\.\\d+\\.\\d+)/(\\d+)$",
                            0, &pcre_err, &erroff, NULL);
    if(!IP_match) {
      *err = "IP match pattern failed to compile!";
      noitL(noit_error, "pcre_compiled failed offset %d: %s\n", erroff, pcre_err);
      return -1;
    }
  }
  if(!numeric_match) {
    numeric_match = pcre_compile("^(\\d+)(?:,(\\d+))?\\.\\.(\\d+)$",
                                 0, &pcre_err, &erroff, NULL);
    if(!numeric_match) {
      *err = "numeric match pattern failed to compile!";
      noitL(noit_error, "pcre_compiled failed offset %d: %s\n", erroff, pcre_err);
      return -1;
    }
  }
  rv = pcre_exec(IP_match, NULL, range, strlen(range), 0, 0, ovector, 30);
  if(rv >= 0) {
    int mask, full = 0, i;
    u_int32_t host_addr;
    struct in_addr addr;
    /* 0 is the full monty, 1 is "" or "full:", 2 is the IP, 3 is the mask */
    pcre_copy_substring(range, ovector, rv, 1, buff, sizeof(buff));
    full = buff[0] ? 1 : 0;
    pcre_copy_substring(range, ovector, rv, 3, buff, sizeof(buff));
    mask = atoi(buff);
    if(mask == 32) full = 1; /* host implies.. the host */
    if(mask < 0 || mask > 32) {
      *err = "invalid netmask";
      return 0;
    }
    count = 1 << (32-mask);
    pcre_copy_substring(range, ovector, rv, 2, buff, sizeof(buff));
    if(inet_pton(AF_INET, buff, &addr) != 1) {
      *err = "could not parse IP address";
      return 0;
    }
    host_addr = ntohl(addr.s_addr);
    host_addr &= ~((u_int32_t)count - 1);

    if(!full) count -= 2; /* No network or broadcast */
    if(count > max_count || !count) return -count;
    if(!full) host_addr++; /* Skip the network address */

    *set = malloc(count * sizeof(**set));
    for(i=0; i<count; i++)  {
      addr.s_addr = htonl(host_addr + i);
      inet_ntop(AF_INET, &addr, buff, sizeof(buff));
      (*set)[i] = strdup(buff);
    }
    return count;
  }
  rv = pcre_exec(numeric_match, NULL, range, strlen(range), 0, 0, ovector, 30);
  if(rv >= 0) {
    int s, n, e, i;
    pcre_copy_substring(range, ovector, rv, 1, buff, sizeof(buff));
    s = atoi(buff);
    pcre_copy_substring(range, ovector, rv, 3, buff, sizeof(buff));
    e = atoi(buff);
    pcre_copy_substring(range, ovector, rv, 2, buff, sizeof(buff));
    if(buff[0]) n = atoi(buff);
    else n = (s<e) ? s+1 : s-1;

    /* Ensure that s < n < e */
    if((s<e && s>n) || (s>e && s<n)) {
      *err = "mixed up sequence";
      return 0;
    }
    i = n - s; /* Our increment */
    count = (e - s) / i + 1;
    *set = malloc(count * sizeof(**set));
    count = 0;
    for(; (i>0 && s<=e) || (i<0 && s>=e); s += i) {
      snprintf(buff, sizeof(buff), "%d", s);
      (*set)[count] = strdup(buff);
      count++;
    }
    return count;
  }
  *err = "cannot understand range";
  return 0;
}
int
noit_console_generic_apply(noit_console_closure_t ncct,
                           int argc, char **argv,
                           noit_console_state_t *dstate,
                           void *closure) {
  int i, j, count;
  char *name, *range;
  char **nargv, **expanded = NULL;
  const char *err;
  int problems = 0;
  if(argc < 3) {
    nc_printf(ncct, "apply <name> <range> cmd ...\n");
    return -1;
  }
  name = argv[0];
  range = argv[1];
  argc -= 2;
  argv += 2;

  count = expand_range(range, &expanded, 256, &err);
  if(!count) {
    nc_printf(ncct, "apply error: '%s' range produced nothing [%s]\n",
              range, err ? err : "unknown error");
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
noit_console_render_help(noit_console_closure_t ncct,
                         noit_console_state_t *dstate) {
  noit_skiplist_node *iter = NULL;
  if(!dstate) {
    nc_printf(ncct, "No help available.\n");
    return -1;
  }
  for(iter = noit_skiplist_getlist(&dstate->cmds); iter;
      noit_skiplist_next(&dstate->cmds,&iter)) {
    cmd_info_t *cmd = iter->data;
    if(strcmp(cmd->name, "help")) nc_printf(ncct, "  ==> '%s'\n", cmd->name);
  }
  return 0;
}

int
noit_console_state_delegate(noit_console_closure_t ncct,
                            int argc, char **argv,
                            noit_console_state_t *dstate,
                            void *closure) {
  noit_console_state_stack_t tmps = { 0 };

  if(argc == 0) {
    noit_console_render_help(ncct, dstate);
    nc_printf(ncct, "incomplete command.\n");
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
    noit_console_render_help(ncct, stack->state);
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
      if(ambiguous || !strcmp(argv[0], "?")) {
        char *partial = ambiguous ? argv[0] : "";
        if(ambiguous) nc_printf(ncct, "Ambiguous command: '%s'\n", argv[0]);
        amb = ambiguous ? next : noit_skiplist_getlist(&stack->state->cmds);
        for(; amb; noit_skiplist_next(&stack->state->cmds, &amb)) {
          cmd = amb->data;
          if(!strlen(partial) || strncasecmp(cmd->name, partial, strlen(partial)) == 0)
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
  if(ncct->state_stack->name) free(ncct->state_stack->name);
  ncct->state_stack->name = strdup(cmd->name);
  return cmd->func(ncct, argc-1, argv+1, cmd->dstate, cmd->closure);
}
int
noit_console_state_do(noit_console_closure_t ncct, int argc, char **argv) {
  return _noit_console_state_do(ncct, ncct->state_stack, argc, argv);
}

noit_console_state_t *
noit_console_state_alloc(void) {
  noit_console_state_t *s;
  s = calloc(1, sizeof(*s));
  noit_skiplist_init(&s->cmds);
  noit_skiplist_set_compare(&s->cmds, cmd_info_compare, cmd_info_comparek);
  noit_console_state_add_cmd(s,
      NCSCMD("apply", noit_console_generic_apply, NULL, NULL, NULL));
  noit_console_state_add_cmd(s, &console_command_help);
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
                   console_opt_func_t ac,
                   noit_console_state_t *dstate, void *closure) {
  cmd_info_t *cmd;
  cmd = calloc(1, sizeof(*cmd));
  cmd->name = strdup(name);
  cmd->func = func;
  cmd->autocomplete = ac;
  cmd->dstate = dstate;
  cmd->closure = closure;
  return cmd;
}

noit_console_state_t *
noit_console_state_initial() {
  static noit_console_state_t *_top_level_state = NULL;
  if(!_top_level_state) {
    static noit_console_state_t *no_state;
    _top_level_state = noit_console_state_alloc();
    noit_console_state_add_cmd(_top_level_state, &console_command_exit);
    noit_console_state_add_cmd(_top_level_state,
      NCSCMD("show", noit_console_state_delegate, noit_console_opt_delegate,
             noit_console_state_alloc(), NULL));
    no_state = noit_console_state_alloc();
    noit_console_state_add_cmd(_top_level_state,
      NCSCMD("no", noit_console_state_delegate, noit_console_opt_delegate,
             no_state, NULL));

    noit_console_state_add_cmd(_top_level_state, &console_command_shutdown);
    noit_console_state_add_cmd(_top_level_state, &console_command_restart);
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
noit_console_shutdown(noit_console_closure_t ncct, int argc, char **argv,
                      noit_console_state_t *dstate, void *unused) {
  exit(2);
}
int
noit_console_restart(noit_console_closure_t ncct, int argc, char **argv,
                     noit_console_state_t *dstate, void *unused) {
  exit(1);
}
int
noit_console_help(noit_console_closure_t ncct, int argc, char **argv,
                  noit_console_state_t *dstate, void *unused) {
  noit_console_state_stack_t *current;
  current = ncct->state_stack;

  if(!argc) {
    noit_console_state_stack_t *i = current;
    if(!current) {
      nc_printf(ncct, "no state!\n");
      return -1;
    }
    for(i=current;i;i=i->last) {
      if(i != current)
        nc_printf(ncct, " -> '%s'\n", i->name ? i->name : "(null)");
    }
    if(dstate) {
      nc_printf(ncct, "= Topics =\n");
      noit_console_render_help(ncct, dstate);
    }
    if(current->state) {
      nc_printf(ncct, "\n= Commands =\n");
      noit_console_render_help(ncct, current->state);
    }
    return 0;
  }
  else if(argc > 0) {
    nc_printf(ncct, "Help for '%s':\n", argv[0]);
    if(noit_console_state_delegate(ncct, argc, argv, dstate, NULL) == 0)
      return 0;
  }
  nc_printf(ncct, "command not understood.\n");
  return -1;
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
  if(current->name) free(current->name);
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
