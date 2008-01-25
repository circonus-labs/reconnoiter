/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#ifndef _NOIT_CONF_H
#define _NOIT_CONF_H

#include "noit_defines.h"

typedef enum { true, false } noit_conf_boolean;

API_EXPORT(void) noit_conf_init();
API_EXPORT(int) noit_conf_load(const char *path);
API_EXPORT(int) noit_conf_save(const char *path);

API_EXPORT(int) noit_conf_get_string(const char *path, char **value);
API_EXPORT(int) noit_conf_get_int(const char *path, int *value);
API_EXPORT(int) noit_conf_get_float(const char *path, float *value);
API_EXPORT(int) noit_conf_get_boolean(const char *path, noit_conf_boolean *value);

API_EXPORT(int) noit_conf_set_string(const char *path, const char *value);
API_EXPORT(int) noit_conf_set_int(const char *path, int value);
API_EXPORT(int) noit_conf_set_float(const char *path, float value);
API_EXPORT(int) noit_conf_set_boolean(const char *path, noit_conf_boolean value);

#endif
