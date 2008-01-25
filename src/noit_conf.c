/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#include "noit_defines.h"
#include <stdio.h>
#include "noit_conf.h"
#include "utils/noit_hash.h"

/* tmp hash impl, replace this with something nice */
static noit_hash_table _tmp_config = NOIT_HASH_EMPTY;


static noit_hash_table _compiled_fallback = NOIT_HASH_EMPTY;
static struct {
  const char *key;
  const char *val;
} config_info[] = {
  /*
   * These are compile-time fallbacks to be used in the event
   * that the current running config does not have values for
   * these config paths.
   *
   * PLEASE: keep them alphabetically sorted.
   */
  { "/global/modules/directory", MODULES_DIR },

  { NULL, NULL }
};


void noit_conf_init() {
  int i;
  for(i = 0; config_info[i].key != NULL; i++) {
    noit_hash_store(&_compiled_fallback,
                    strdup(config_info[i].key), strlen(config_info[i].key),
                    (void *)strdup(config_info[i].val));
  }
}

int noit_conf_load(const char *path) {
  return -1;
}
int noit_conf_save(const char *path) {
  return -1;
}

int noit_conf_get_string(const char *path, char **value) {
  char *str;
  if(noit_hash_retrieve(&_tmp_config,
                        path, strlen(path), (void **)&str)) {
    *value = strdup(str);
  }
  if(noit_hash_retrieve(&_compiled_fallback,
                        path, strlen(path), (void **)&str)) {
    *value = strdup(str);
  }
  return 0;
}
int noit_conf_set_string(const char *path, const char *value) {
  noit_hash_replace(&_tmp_config,
                    strdup(path), strlen(path), (void *)strdup(value),
                    free, free);
  return 1;
}
int noit_conf_get_int(const char *path, int *value) {
  char *str;
  long longval;
  if(noit_conf_get_string(path, &str)) {
    int base = 10;
    if(str[0] == '0') {
      if(str[1] == 'x') base = 16;
      else base = 8;
    }
    longval = strtol(str, NULL, base);
    free(str);
    *value = (int)longval;
    return 1;
  }
  return 0;
}
int noit_conf_set_int(const char *path, int value) {
  char buffer[32];
  snprintf(buffer, 32, "%d", value);
  return noit_conf_set_string(path, buffer);
}
int noit_conf_get_float(const char *path, float *value) {
  char *str;
  if(noit_conf_get_string(path, &str)) {
    *value = atof(str);
    free(str);
    return 1;
  }
  return 0;
}
int noit_conf_set_float(const char *path, float value) {
  char buffer[32];
  snprintf(buffer, 32, "%f", value);
  return noit_conf_set_string(path, buffer);
}
int noit_conf_get_boolean(const char *path, noit_conf_boolean *value) {
  char *str;
  if(noit_conf_get_string(path, &str)) {
    if(!strcasecmp(str, "true")) *value = true;
    else *value = false;
    free(str);
    return 1;
  }
  return 0;
}
int noit_conf_set_boolean(const char *path, noit_conf_boolean value) {
  if(value == true)
    return noit_conf_set_string(path, "true");
  return noit_conf_set_string(path, "false");
}

