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

#ifndef _NOIT_CONF_H
#define _NOIT_CONF_H

#include "noit_defines.h"
#include "utils/noit_hash.h"
#include "noit_console.h"

#include <uuid/uuid.h>
#include <pcre.h>

typedef void * noit_conf_section_t;

#define NOIT_CONF_T_USERDATA "noit::state::conf_t"
typedef struct {
  char *path;
  uuid_t current_check;
  char filter_name[50];
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
API_EXPORT(char *) noit_conf_config_filename();

API_EXPORT(void) noit_console_conf_init();

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
API_EXPORT(int) noit_conf_string_to_int(const char *str);
API_EXPORT(int) noit_conf_get_float(noit_conf_section_t section,
                                    const char *path, float *value);
API_EXPORT(float) noit_conf_string_to_float(const char *str);
API_EXPORT(int) noit_conf_get_boolean(noit_conf_section_t section,
                                      const char *path, noit_boolean *value);
API_EXPORT(noit_boolean) noit_conf_string_to_boolean(const char *str);

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
  noit_conf_write_file_console(noit_console_closure_t ncct,
                               int argc, char **argv,
                               noit_console_state_t *state, void *closure);
API_EXPORT(int)
  noit_conf_write_file(char **err);

API_EXPORT(char *)
  noit_conf_xml_in_mem(size_t *len);

API_EXPORT(int)
  noit_conf_write_log();

API_EXPORT(void) noit_conf_log_init(const char *toplevel);
API_EXPORT(int) noit_conf_log_init_rotate(const char *, noit_boolean);

#define EXPOSE_CHECKER(name) \
  API_EXPORT(pcre *) noit_conf_get_valid_##name##_checker()
#define DECLARE_CHECKER(name) \
static pcre *checker_valid_##name; \
pcre *noit_conf_get_valid_##name##_checker() { return checker_valid_##name; }
#define COMPILE_CHECKER(name, expr) do { \
  const char *errorstr; \
  int erroff; \
  checker_valid_##name = pcre_compile(expr, 0, &errorstr, &erroff, NULL); \
  if(! checker_valid_##name) { \
    noitL(noit_error, "noit_conf error: compile checker %s failed: %s\n", \
          #name, errorstr); \
    exit(-1); \
  } \
} while(0)

EXPOSE_CHECKER(name);

#endif
