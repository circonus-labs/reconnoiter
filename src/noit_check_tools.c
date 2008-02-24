/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#include "noit_defines.h"
#include "noit_check_tools.h"

int
noit_check_interpolate(char *buff, int len, const char *fmt,
                       noit_hash_table *attrs,
                       noit_hash_table *config) {
  char *copy = NULL;
  char closer;
  const char *fmte, *key;
  int replaced_something = 1;
  int iterations = 3;

  while(replaced_something && iterations > 0) {
    char *cp = buff, * const end = buff + len;
    iterations--;
    replaced_something = 0;
    while(*fmt && cp < end) {
      switch(*fmt) {
        case '%':
          if(fmt[1] == '{' || fmt[1] == '[') {
            closer = (fmt[1] == '{') ? '}' : ']';
            fmte = fmt + 2;
            key = fmte;
            while(*fmte && *fmte != closer) fmte++;
            if(*fmte == closer) {
              /* We have a full key here */
              const char *replacement;
              if(!noit_hash_retrieve((closer == '}') ?  config : attrs,
                                     key, fmte - key, (void **)&replacement))
                replacement = "";
              fmt = fmte + 1; /* Format points just after the end of the key */
              strlcpy(cp, replacement, end-cp);
              cp += strlen(cp);
              replaced_something = 1;
              break;
            }
          }
        default:
          *cp++ = *fmt++;
      }
    }
    *cp = '\0';
    if(copy) free(copy);
    if(replaced_something)
      copy = strdup(buff);
    fmt = copy;
  }
  return strlen(buff);
}

