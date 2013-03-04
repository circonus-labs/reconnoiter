/*
* all.c -- Lua core, libraries and interpreter in a single file
** See Copyright Notice in lua.h
*/

#define luaall_c
#define lobject_c

#define LUA_CORE
#define LUA_COMPAT_MODULE

extern void luai_writestring(const char *, int);
extern void luai_writeline();

#include "luaconf.h"

#include "lapi.c"
#include "lcode.c"
#include "ldebug.c"
#include "ldo.c"
#include "ldump.c"
#include "lfunc.c"
#include "lgc.c"
#include "llex.c"
#include "lmem.c"
#include "lobject.c"
#include "lopcodes.c"
#include "lparser.c"
#include "lstate.c"
#include "lstring.c"
#include "ltable.c"
#include "ltm.c"
#include "lundump.c"
#include "lvm.c"
#include "lzio.c"

#include "lauxlib.c"
#include "lbaselib.c"
#include "ldblib.c"
#include "liolib.c"
#include "linit.c"
#include "lmathlib.c"
#include "loadlib.c"
#include "loslib.c"
#include "lstrlib.c"
#include "ltablib.c"
#include "lbitlib.c"
#include "lcorolib.c"
#include "lctype.c"

