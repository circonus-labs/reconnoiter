#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include "noit_check_log_helpers.h"

static void usage(const char *prog) {
  fprintf(stderr, "%s\n", prog);
  fprintf(stderr, "This tool takes B{1,2} records from stdin and emits S/M records to stdout\n");
}
char buff[1024*1024*16];
int main(int argc, char **argv) {
  if(argc > 1) {
    usage(argv[0]);
    return 2;
  }
  while(fgets(buff, sizeof(buff), stdin) != NULL) {
    char **lines = NULL;
    int i;
    char *dp = buff, *sp = buff;
    while(*sp) {
      if(*sp == ' ' || *sp == '\t') {
        if(dp > buff && dp[-1] == '\t') {
          sp++; continue;
        }
        *dp = '\t';
      }
      else *dp = *sp;
      sp++; dp++;
    }
    *dp = '\0';
    int nm = noit_check_log_b_to_sm(buff, strlen(buff), &lines, -1);
    for(i=0; i<nm; i++) {
      /* zip through the line and change spaces to tabs. */
      printf("%s\n", lines[i]);
      free(lines[i]);
    }
    free(lines);
  }
}
