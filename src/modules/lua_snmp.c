/*
 * Copyright (c) 2007-2013, OmniTI Computer Consulting, Inc.
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

#include "lua_noit.h"
#include "noit_defines.h"

#include <string.h>
#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

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
    int maxsize = MAX_OID_LEN*4;
    char outbuff[maxsize];
    memset(outbuff, 0, maxsize);
    ret = get_node(in, theoid, &theSize);
    if (!ret) {
      lua_pushstring(L, "error");
      return 1;
    }
    for (i=0; i < theSize; i++) {
      sprintf(outbuff, "%s.%d", outbuff, theoid[i]);
      if (strlen(outbuff) > maxsize-5) {
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
  register_mib_handlers();
  read_premib_configs();
  read_configs();
  init_snmp("lua_snmp");
  lua_pushnil(L);
  return 1;
}

static const luaL_reg snmp_funcs[] =
{
  { "convert_mib", nl_convert_mib },
  { "init_snmp", nl_init_mib },
  { NULL, NULL }
};

int luaopen_snmp(lua_State *L)
{
 luaL_register(L, "snmp", snmp_funcs);
 return 0;
}

