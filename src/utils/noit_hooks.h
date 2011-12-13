#ifndef _UTILS_NOIT_HOOK_
#define _UTILS_NOIT_HOOK_

#include "noit_defines.h"
#include "noit_hash.h"

typedef int (*noit_hooks_function_t)(void*);

typedef enum {
  NOIT_HOOK_OK = 0
} noit_hook_status_t;

typedef struct _noit_hook {
  noit_hooks_function_t func;
  struct _noit_hook *next;
} noit_hook_t;

typedef struct _noit_hooks {
  noit_hash_table hooks;
} noit_hooks_t;

API_EXPORT(void)
  noit_hooks_create(noit_hooks_t **nh);

API_EXPORT(void)
  noit_hooks_add(noit_hooks_t *nh, const char *name, noit_hooks_function_t func);

API_EXPORT(int)
  noit_hooks_retrieve(noit_hooks_t *nh, const char *name, noit_hook_t **hook);

#endif
