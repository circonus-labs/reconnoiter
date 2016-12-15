#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include "noit_check_log_helpers.h"

static void usage(const char *prog) {
  fprintf(stderr, "%s [-stratcon]\n", prog);
  fprintf(stderr, "\t-stratcon] assumes the second field is an IP\n\n");
  fprintf(stderr, "This tool takes B{1,2} records from stdin and emits S/M records to stdout\n");
}
char buff[1024*1024*16];
int main(int argc, char **argv) {
  int expect_ip = 0;
  if(argc > 2 || (argc == 2 && strcmp(argv[1], "-stratcon"))) {
    usage(argv[0]);
    return 2;
  }
  if(argc == 2) expect_ip = 1;
  while(fgets(buff, sizeof(buff), stdin) != NULL) {
    char **lines = NULL;
    int i;
    char *ip = buff, *cp = buff;
    while(*cp == ' ') cp++;
    while(*cp) {
      if(*cp == ' ' && ip[-1] != '\t') *ip++ = '\t';
      else if(*cp != ' ') *ip++ = *cp;
      cp++;
    }
    *ip = '\0';
fprintf(stderr, "buff: %zd bytes\n", strlen(buff));
    int nm = noit_check_log_b_to_sm(buff, strlen(buff), &lines, expect_ip);
    for(i=0; i<nm; i++) {
      /* zip through the line and change spaces to tabs. */
      printf("%s\n", lines[i]);
      free(lines[i]);
    }
    free(lines);
  }
}
