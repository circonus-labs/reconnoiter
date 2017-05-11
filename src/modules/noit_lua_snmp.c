/*
 * Copyright (c) 2007-2013, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * Copyright (c) 2015, Circonus, Inc. All rights reserved.
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

#include "noit_config.h"
#include "mtev_defines.h"

#include <string.h>
#define LUA_COMPAT_MODULE
#include "lua_mtev.h"

#ifdef HAVE_NETSNMP
/* some ncurses implementations will #define clear which is a
 * directly referenced struct element in some net-snmp headers.
 * people can't code... so we are left to do stupid things like this.
 */
#undef clear

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

static mtev_boolean snmp_initialized = mtev_false;
static pthread_mutex_t snmp_lock = PTHREAD_MUTEX_INITIALIZER;

static int
nl_convert_mib(lua_State *L) {
  const char *in;
  size_t inlen = 0;
  oid theoid[MAX_OID_LEN];
  size_t theSize = MAX_OID_LEN;
  int ret;
  int i;

  memset(theoid, 0, sizeof(theoid));
  if(lua_gettop(L) != 1) {
    luaL_error(L, "convert_mib requires one argument"); 
  }
  in = lua_tolstring(L, 1, &inlen);
  if (in[0] == '.') {
    lua_pushstring(L, in);
  }
  else {
    char outbuff[MAX_OID_LEN*8], *cp = outbuff;
    memset(outbuff, 0, sizeof(outbuff));
    ret = get_node(in, theoid, &theSize);
    if (!ret) {
      lua_pushstring(L, "error");
      return 1;
    }
    for (i=0; i < theSize; i++) {
      int len;
      len = snprintf(cp, sizeof(outbuff) - (cp-outbuff), ".%d", (int)theoid[i]);
      if(len >= 0) cp += len;
      else {
        lua_pushstring(L, "error");
        return 1;
      }
    }
    lua_pushstring(L, outbuff);
  }
  return 1;
}

static int 
nl_init_mib(lua_State *L) {
  pthread_mutex_lock(&snmp_lock);
  if (snmp_initialized == mtev_false) {
    register_mib_handlers();
    read_premib_configs();
    read_configs();
    init_snmp("lua_snmp");
    snmp_initialized = mtev_true;
  }
  pthread_mutex_unlock(&snmp_lock);
  lua_pushnil(L);
  return 1;
}

static const luaL_Reg snmp_funcs[] =
{
  { "convert_mib", nl_convert_mib },
  { "init_snmp", nl_init_mib },
  { NULL, NULL }
};
#endif

int luaopen_snmp(lua_State *L)
{
#ifdef HAVE_NETSNMP
  luaL_openlib(L, "snmp", snmp_funcs, 0);
#else
  lua_pushnil(L);
#endif
  return 1;
}

