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
#include <stdlib.h>
#include <string.h>

struct token {
  char *token;
  const char *start;
  const char *end;
  const char *next;
  enum { NT_IDENT, NT_DQSTRING, NT_SPACE, NT_UNKNOWN, NT_EOF } type;
};
#define SET_TOKEN(t,a) (t)->next = (a)

static void c_unescape(char *p, char *only) {
  char *bt = p;
#define ASSIGN(a) *(bt++) = (a)
  while(p[0] != '\0') {
    if(p[0] == '\\' && p[1] != '\0' && (!only || p[1] == *only)) {
      switch(p[1]) {
        case ' ': ASSIGN(' '); p+=2; break;
        case '"': ASSIGN('"'); p+=2; break;
        case 'n': ASSIGN('\n'); p+=2; break;
        case 'r': ASSIGN('\r'); p+=2; break;
        case 't': ASSIGN('\t'); p+=2; break;
        case 'a': ASSIGN('\a'); p+=2; break;
        case 'b': ASSIGN('\b'); p+=2; break;
        case 'v': ASSIGN('\v'); p+=2; break;
        case 'f': ASSIGN('\f'); p+=2; break;
        case '0': ASSIGN('\0'); p+=2; break;
        case '\\': ASSIGN('\\'); p+=2; break;
        default: ASSIGN(*p); p++; ASSIGN(*p); p++; break;
      }
    }
    else {
      ASSIGN(*p); p++;
    }
  }
  *bt = '\0';
}

#define BAIL_UNKNOWN do { t->type = NT_UNKNOWN; return -1; } while(0)
static int token_scan(struct token *t)
{
  t->start = t->end = t->next;

 mainpattern:
/*!re2c
    re2c:define:YYCTYPE  = "unsigned char";
    re2c:define:YYCURSOR = t->next;
    re2c:yyfill:enable   = 0;
    re2c:yych:conversion = 1;
    re2c:indent:top      = 1;

    [ \t\r\n]+      { t->token = NULL;
                      t->end = t->next;
                      t->type = NT_SPACE;
                      return 1; }
    ["]             { t->type = NT_DQSTRING;
                      if(t->start != t->end) {
                        t->start++;
                        t->end = t->next - 1;
                        t->token = malloc(t->end-t->start + 1);
                        strlcpy(t->token, t->start, t->end-t->start + 1);
                        c_unescape(t->token, NULL);
                        return 1;
                      }
                      else
                        goto dqstring;
                    }
    "'"             { t->type = NT_IDENT;
                      if(t->start != t->end) {
                        t->start++;
                        t->end = t->next - 1;
                        t->token = malloc(t->end-t->start + 1);
                        strlcpy(t->token, t->start, t->end-t->start + 1);
                        return 1;
                      }
                      else
                        goto sqstring;
                    }
    [^\000'" \t\r\n] ([^\000 \t\r\n]|[\\][ ])*
                    { char only = ' ';
                      t->end = t->next;
                      t->type = NT_IDENT;
                      t->token = malloc(t->end-t->start + 1);
                      strlcpy(t->token, t->start, t->end-t->start + 1);
                      c_unescape(t->token, &only);
                      return 1;
                    }
    [\000]          { t->token = NULL;
                      t->type = NT_EOF;
                      return 0;
                    }
    [\000-\377]     { BAIL_UNKNOWN; }
*/

 sqstring:
/*!re2c
    [^'\000]*       { t->end = t->next;
                      goto mainpattern; }
    [\000]          { BAIL_UNKNOWN; }
*/

 dqstring:
/*!re2c
    [\\][nrtabvf0"\\]
                    { goto dqstring; }
    "\\" ( [^\000] \ [nrtabvf0"\\] )
                    { goto dqstring; }
    ["]             { t->end = t->next--;
                      goto mainpattern;
                    }
    [^"\000]\[\\"]  { goto dqstring; }
    [\000]          { BAIL_UNKNOWN; }
*/
}

int noit_tokenize(const char *input, char **vector, int *cnt) {
  struct token t;
  int i = 0;

  SET_TOKEN(&t, input);
  while(token_scan(&t) != -1) {
    switch(t.type) {
      case NT_IDENT:
      case NT_DQSTRING:
        if(i<*cnt) vector[i] = t.token;
        i++;
        break;
      case NT_SPACE:
        break;
      case NT_EOF:
        if(i<*cnt) *cnt = i;
        return i;
      case NT_UNKNOWN:
        /* UNREACHED */
        goto failure;
    }
  }
 failure:
  if(i<*cnt) *cnt = i;
  return input - t.next;
}
