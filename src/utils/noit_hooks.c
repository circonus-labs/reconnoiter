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
  if (noit_hash_retrieve(&nh->hooks, name, strlen(name), (void**)&hook)) {
    new_hook->next = hook;
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
