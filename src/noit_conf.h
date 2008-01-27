/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#ifndef _NOIT_CONF_H
#define _NOIT_CONF_H

#include "noit_defines.h"
#include "utils/noit_hash.h"

typedef enum { noit_true, noit_false } noit_conf_boolean;
typedef void * noit_conf_section_t;

API_EXPORT(void) noit_conf_init();
API_EXPORT(int) noit_conf_load(const char *path);
API_EXPORT(int) noit_conf_save(const char *path);

API_EXPORT(noit_conf_section_t)
  noit_conf_get_section(noit_conf_section_t section, const char *path);
API_EXPORT(noit_conf_section_t *)
  noit_conf_get_sections(noit_conf_section_t section, const char *path,
                         int *cnt);

API_EXPORT(noit_hash_table *)
  noit_conf_get_hash(noit_conf_section_t section, const char *path);

API_EXPORT(int) noit_conf_get_string(noit_conf_section_t section,
                                     const char *path, char **value);
API_EXPORT(int) noit_conf_get_stringbuf(noit_conf_section_t section,
                                        const char *path, char *value, int len);
API_EXPORT(int) noit_conf_get_int(noit_conf_section_t section,
                                  const char *path, int *value);
API_EXPORT(int) noit_conf_get_float(noit_conf_section_t section,
                                    const char *path, float *value);
API_EXPORT(int) noit_conf_get_boolean(noit_conf_section_t section,
                                      const char *path, noit_conf_boolean *value);

API_EXPORT(int) noit_conf_set_string(noit_conf_section_t section,
                                     const char *path, const char *value);
API_EXPORT(int) noit_conf_set_int(noit_conf_section_t section,
                                  const char *path, int value);
API_EXPORT(int) noit_conf_set_float(noit_conf_section_t section,
                                    const char *path, float value);
API_EXPORT(int) noit_conf_set_boolean(noit_conf_section_t section,
                                      const char *path, noit_conf_boolean value);

#endif
