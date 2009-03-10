#ifndef MODULES_EXTERNAL_PROC_H
#define MODULES_EXTERNAL_PROC_H

#include "noit_defines.h"
#include "eventer/eventer.h"
#include "utils/noit_atomic.h"
#include "utils/noit_hash.h"

struct external_response {
  int64_t check_no;
  int32_t exit_code;
  int stdoutlen_sofar;
  u_int16_t stdoutlen;
  char *stdoutbuff;
  int stderrlen_sofar;
  u_int16_t stderrlen;
  char *stderrbuff;
};
typedef struct {
  noit_log_stream_t nlerr;
  noit_log_stream_t nldeb;
  int child;
  int pipe_n2e[2];
  int pipe_e2n[2];
  eventer_jobq_t *jobq;
  noit_atomic64_t check_no_seq;
  noit_hash_table external_checks;

  struct external_response *cr;
} external_data_t;

int external_child(external_data_t *);

#endif
