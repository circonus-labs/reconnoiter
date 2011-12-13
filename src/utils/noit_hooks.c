#include "noit_hooks.h"

void
noit_hooks_create(noit_hooks_t **nh) {
  *nh = calloc(1, sizeof(**nh));
  noit_hash_init(&(*nh)->hooks);
}
void
noit_hooks_add(noit_hooks_t *nh, const char *name,
               noit_hooks_function_t func) {
  noit_hook_t *hook = NULL;;
  noit_hook_t *new_hook = calloc(1, sizeof(*new_hook));
  new_hook->func = func;
  new_hook->next = hook;
  if (noit_hash_retrieve(&nh->hooks, name, strlen(name), (void**)&hook)) {
    noit_hash_replace(&nh->hooks, strdup(name), strlen(name), (void *)new_hook, NULL, NULL);
  }
  else {
    noit_hash_store(&nh->hooks, name, strlen(name), new_hook);
  }
}
int
noit_hooks_retrieve(noit_hooks_t *nh, const char *name, noit_hook_t **hook) {
  return noit_hash_retrieve(&nh->hooks, name, strlen(name), (void**)hook);
}
