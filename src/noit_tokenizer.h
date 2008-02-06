/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#ifndef _NOIT_TOKENIZER_H
#define _NOIT_TOKENIZER_H

/* Pass a string as input and an allocation array of char * place holders.
   The function will white-space slit them and return each token (malloc'd)
   in vector[ 0 .. *cnt-1 ].

   This function is smart, handling quoted strings and escape sequences.
   'enclosed strings'    may contain any character except '
   "enclosed strings"    are interpretted with all C escape sequences.
   unenclosed\ strings   are may not start wth ' or ",
                         but may contain escaped spaces (not tabs).

   *cnt will be set to the number of tokens placed in vector.

   RETURNS:
     the number of tokens found (may be larger than *cnt),
       or
     -X where X is the character offset of the parser error.
*/
   
int noit_tokenize(const char *input, char **vector, int *cnt);

#endif
