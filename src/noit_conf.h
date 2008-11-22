/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#ifndef _NOIT_CONF_H
#define _NOIT_CONF_H

#include "noit_defines.h"
#include "utils/noit_hash.h"
#include "noit_console.h"

#include <uuid/uuid.h>

typedef void * noit_conf_section_t;

#define NOIT_CONF_T_USERDATA "noit::state::conf_t"
typedef struct {
  char *path;
  uuid_t current_check;
  char prompt[50];
} noit_conf_t_userdata_t;

/* seconds == 0 disable config journaling watchdog */
API_EXPORT(void) noit_conf_coalesce_changes(u_int32_t seconds);
/* Start the watchdog */
API_EXPORT(void) noit_conf_watch_and_journal_watchdog(int (*f)(void *), void *c);

/* marks the config as changed.. if you manipulate the XML tree in any way
 * you must call this function to "let the system know."  This is used
 * to notice changes which are in turn flushed out.
 */
API_EXPORT(void) noit_conf_mark_changed();

API_EXPORT(void) noit_conf_init(const char *toplevel);
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
                                      const char *path, noit_boolean *value);
API_EXPORT(int)
  noit_conf_get_uuid(noit_conf_section_t section,
                     const char *path, uuid_t out);

API_EXPORT(int) noit_conf_set_string(noit_conf_section_t section,
                                     const char *path, const char *value);
API_EXPORT(int) noit_conf_set_int(noit_conf_section_t section,
                                  const char *path, int value);
API_EXPORT(int) noit_conf_set_float(noit_conf_section_t section,
                                    const char *path, float value);
API_EXPORT(int) noit_conf_set_boolean(noit_conf_section_t section,
                                      const char *path, noit_boolean value);

API_EXPORT(int)
  noit_conf_reload(noit_console_closure_t ncct,
                   int argc, char **argv,
                   noit_console_state_t *state, void *closure);
API_EXPORT(int)
  noit_conf_write_terminal(noit_console_closure_t ncct,
                           int argc, char **argv,
                           noit_console_state_t *state, void *closure);
API_EXPORT(int)
  noit_conf_write_file(noit_console_closure_t ncct,
                       int argc, char **argv,
                       noit_console_state_t *state, void *closure);

API_EXPORT(char *)
  noit_conf_xml_in_mem(size_t *len);

API_EXPORT(int)
  noit_conf_write_log();

API_EXPORT(void) noit_conf_log_init(const char *toplevel);

#endif
