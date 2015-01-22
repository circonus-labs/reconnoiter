#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "jlog.h"

#define SLEEP_10MS_IN_US 10000
#define SLEEP_1S_IN_US 1000000
jlog_ctx* ctx;
const char *lf = "\n";
char subscriber[32] = "jlog-tail";

int main(int argc, char** argv) {
  const char* path;
  jlog_id begin, end;
  int count, two_times_a_charm = 0;
  int sleeptime = SLEEP_10MS_IN_US;

  if(argc != 2) {
    fprintf(stderr, "usage: %s /path/to/jlog\n", argv[0]);
    exit(1);
  }
  path = argv[1];

  snprintf(subscriber, sizeof(subscriber), "jlog-tail-%d", (int)getpid());
  ctx = jlog_new(path);
  jlog_ctx_add_subscriber(ctx, subscriber, JLOG_END);
  
  if (jlog_ctx_open_reader(ctx, subscriber) != 0) {
    fprintf(stderr, "jlog_ctx_open_reader failed: %d %s\n", jlog_ctx_err(ctx), jlog_ctx_err_string(ctx));
    exit(1);
  }

  jlog_ctx_remove_subscriber(ctx, subscriber);

  while(1) {
    count = jlog_ctx_read_interval(ctx, &begin, &end);
    if (count > 0) {
      int i;
      jlog_message m;
   
      two_times_a_charm = 0; 
      for (i = 0; i < count; i++, JLOG_ID_ADVANCE(&begin)) {
        end = begin;
      
        if (jlog_ctx_read_message(ctx, &begin, &m) == 0) {
          if(m.mess_len > 0) {
            const char *use_lf = lf;
            if(((char *)m.mess)[m.mess_len-1] == '\n') use_lf = "";
            printf("%.*s%s", m.mess_len, (char*)m.mess, use_lf);
            fflush(stdout);
          }
          else {
            printf("... empty message ...\n");
          }
        } else {
          fprintf(stderr, "jlog_ctx_read_message failed: %d %s\n", jlog_ctx_err(ctx), jlog_ctx_err_string(ctx));
        }
      }

      // checkpoint (commit) our read:
      jlog_ctx_read_checkpoint(ctx, &end);
    }
    if(two_times_a_charm > 1) {
      sleeptime *= 2;
      if(sleeptime > SLEEP_1S_IN_US) sleeptime = SLEEP_1S_IN_US;
      usleep(sleeptime);
    }
    else sleeptime = SLEEP_10MS_IN_US;
    two_times_a_charm++;
  }
}
