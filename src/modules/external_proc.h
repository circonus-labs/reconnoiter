/*
 * Copyright (c) 2009, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * Copyright (c) 2015, Circonus, Inc. All rights reserved.
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

#ifndef MODULES_EXTERNAL_PROC_H
#define MODULES_EXTERNAL_PROC_H

#include <mtev_defines.h>
#include <eventer/eventer.h>
#include <mtev_hash.h>

typedef enum {
  EXTERNAL_DEFAULT_TYPE = 0,
  EXTERNAL_NAGIOS_TYPE = 1,
  EXTERNAL_JSON_TYPE = 2
} external_special_t;

struct external_response {
  int64_t check_no;
  int32_t exit_code;
  int16_t stdout_truncated;
  int16_t stderr_truncated;
  int stdoutlen_sofar;
  uint32_t stdoutlen;
  char *stdoutbuff;
  int stderrlen_sofar;
  uint32_t stderrlen;
  char *stderrbuff;
};
typedef struct {
  mtev_log_stream_t nlerr;
  mtev_log_stream_t nldeb;
  int child;
  int pipe_n2e[2];
  int pipe_e2n[2];
  char* path;
  char* nagios_regex;
  eventer_jobq_t *jobq;
  uint64_t check_no_seq;
  mtev_hash_table external_checks;
  mtev_hash_table *options;
  uint32_t max_out_len;
  struct external_response *cr;
} external_data_t;

typedef struct {
  int64_t check_no;
  int32_t exit_code;
  int16_t stdout_truncated;
  int16_t stderr_truncated;
  uint32_t stdoutlen;
} __attribute__((packed)) external_header;

int external_child(external_data_t *);

#endif
