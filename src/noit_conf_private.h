/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#ifndef _NOIT_CONF_PRIVATE_H
#define _NOIT_CONF_PRIVATE_H

#include "noit_defines.h"
#include "utils/noit_hash.h"
#include "noit_console.h"

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>

API_EXPORT(int) noit_conf_xml_xpath(xmlDocPtr *mc, xmlXPathContextPtr *xp);
API_EXPORT(int)
  _noit_conf_get_string(noit_conf_section_t section, xmlNodePtr *vnode,
                        const char *path, char **value);

#endif
