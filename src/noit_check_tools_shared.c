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

#include "noit_defines.h"
#include "noit_check_tools.h"
#include "utils/noit_str.h"

#include <assert.h>

static noit_hash_table interpolation_operators = NOIT_HASH_EMPTY;

static int
interpolate_oper_copy(char *buff, int len, const char *key,
                      const char *replacement) {
  (void)key;
  strlcpy(buff, replacement, len);
  return strlen(buff);
}
static int
interpolate_oper_ccns(char *buff, int len, const char *key,
                      const char *replacement) {
  char *start;
  start = strstr(replacement, "::");
  return interpolate_oper_copy(buff, len, key,
                               start ? (start + 2) : replacement);
}

int
noit_check_interpolate_register_oper_fn(const char *name,
                                        intperpolate_oper_fn f) {
  noit_hash_replace(&interpolation_operators,
                    strdup(name), strlen(name),
                    (void *)f,
                    free, NULL);
  return 0;
}

int
noit_check_interpolate(char *buff, int len, const char *fmt,
                       noit_hash_table *attrs,
                       noit_hash_table *config) {
  char *copy = NULL;
  char closer;
  const char *fmte, *key;
  char keycopy[128];
  int keylen;
  int replaced_something = 1;
  int iterations = 3;

  while(replaced_something && iterations > 0) {
    char *cp = buff, * const end = buff + len;
    iterations--;
    replaced_something = 0;
    while(*fmt && cp < end) {
      switch(*fmt) {
        case '%':
          if(fmt[1] == '{' || fmt[1] == '[') {
            closer = (fmt[1] == '{') ? '}' : ']';
            fmte = fmt + 2;
            key = fmte;
            while(*fmte && *fmte != closer) fmte++;
            if(*fmte == closer) {
              /* We have a full key here */
              const char *replacement, *oper, *nkey;
              intperpolate_oper_fn oper_sprint;

              /* keys can be of the form: :operator:key */
              oper = key;
              if(*oper == ':' &&
                 (nkey = strnstrn(":", 1, oper + 1, fmte - key - 1)) != NULL) {
                void *voper;
                oper++;
                /* find oper, nkey-oper */
                if(!noit_hash_retrieve(&interpolation_operators,
                                       oper, nkey - oper,
                                       &voper)) {
                  /* this isn't an understood interpolator */
                  *cp++ = *fmt++;
                  continue;
                }
                else
                  oper_sprint = (intperpolate_oper_fn)voper;
                nkey++;
              }
              else {
                oper_sprint = interpolate_oper_copy;
                nkey = key;
              }
              /* make a C string copy to pass to the interpolation function */
              keylen = fmte - nkey;
              memcpy(keycopy, nkey, MIN(sizeof(keycopy)-1, keylen));
              keycopy[MIN(sizeof(keycopy)-1,keylen)] = '\0';

              if(!noit_hash_retr_str((closer == '}') ?  config : attrs,
                                     nkey, keylen, &replacement))
                replacement = "";
              fmt = fmte + 1; /* Format points just after the end of the key */
              cp += oper_sprint(cp, end-cp, keycopy, replacement);
              *(end-1) = '\0'; /* In case the oper_sprint didn't teminate */
              replaced_something = 1;
              break;
            }
          }
        default:
          *cp++ = *fmt++;
      }
    }
    *cp = '\0';
    if(copy) free(copy);
    copy = NULL;
    if(replaced_something)
      copy = strdup(buff);
    fmt = copy;
  }
  return strlen(buff);
}

void
noit_check_extended_id_split(const char *in, int len,
                             char *target, int target_len,
                             char *module, int module_len,
                             char *name, int name_len,
                             char *uuid, int uuid_len) {
  if(!in || len == 0) return;
  if(target) *target = '\0';
  if(module) *module = '\0';
  if(name) *name = '\0';
  if(uuid) *uuid = '\0';
  if(uuid && len >= UUID_STR_LEN) {
    memcpy(uuid, in + len - UUID_STR_LEN, UUID_STR_LEN);
    uuid[UUID_STR_LEN] = '\0';
  }
  if(len > UUID_STR_LEN) {
    const char *tcp = in;
    const char *mcp, *ncp, *ucp;
    /* find the end of the target */
    mcp = strchr(tcp,'`');
    if(!mcp) return;
    /* copy in the target */
    if(target && target_len > mcp-tcp) {
      memcpy(target,tcp,mcp-tcp);
      target[mcp-tcp] = '\0';
    }
    mcp++;
    ncp = strchr(mcp,'`');
    if(!ncp) return;
    /* copy in the module */
    if(module && module_len > ncp-mcp) {
      memcpy(module,mcp,ncp-mcp);
      module[ncp-mcp] = '\0';
    }
    ncp++;
    /* copy in the name */
    ucp = in + len - UUID_STR_LEN - 1;
    if(ncp < ucp) {
      if(name && name_len > ucp-ncp) {
        memcpy(name, ncp, ucp-ncp);
        name[ucp-ncp] = '\0';
      }
    }
  }
}

void
noit_check_make_attrs(noit_check_t *check, noit_hash_table *attrs) {
#define CA_STORE(a,b) noit_hash_store(attrs, a, strlen(a), b)
  CA_STORE("target", check->target);
  CA_STORE("target_ip", check->target_ip);
  CA_STORE("name", check->name);
  CA_STORE("module", check->module);
}
void
noit_check_release_attrs(noit_hash_table *attrs) {
  noit_hash_destroy(attrs, NULL, NULL);
}

static int
interpolate_oper_random(char *buff, int len, const char *key,
                        const char *replacement) {
  if(!strcmp(key, "integer")) {
    long val = lrand48();
    return snprintf(buff, len, "%lld", (long long int)val);
  }
  else if(!strcmp(key, "uuid")) {
    uuid_t val;
    char val_str[UUID_STR_LEN+1];
    uuid_generate(val);
    uuid_unparse_lower(val, val_str);
    return snprintf(buff, len, "%s", val_str);
  }
  return snprintf(buff, len, "random_what");
}

void
noit_check_tools_shared_init() {
  noit_check_interpolate_register_oper_fn("copy", interpolate_oper_copy);
  noit_check_interpolate_register_oper_fn("ccns", interpolate_oper_ccns);
  noit_check_interpolate_register_oper_fn("random", interpolate_oper_random);
}

