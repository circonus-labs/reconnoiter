#include "noit_defines.h"

#include <stdio.h>
#include "noitedit/compat.h"

#ifndef HAVE_FGETLN
char *fgetln(FILE *stream, size_t *len)
{
  char buf[8192];
  if (buf == fgets(buf, sizeof(buf), stream)) {
    char *ptr = strdup(buf);
	*len = strlen(ptr);
	return ptr;
  }
  return NULL;
}
#endif
