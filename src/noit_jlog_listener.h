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

#ifndef _NOIT_JLOG_LISTENER_H
#define _NOIT_JLOG_LISTENER_H

#include "noit_defines.h"
#include "eventer/eventer.h"

#define NOIT_JLOG_DATA_FEED 0xda7afeed
#define NOIT_JLOG_DATA_TEMP_FEED 0x7e66feed

typedef struct {
  char *feed_name;
  noit_atomic32_t connections;
  struct timeval last_connection;
  struct timeval last_checkpoint;
} jlog_feed_stats_t;

API_EXPORT(void)
  noit_jlog_listener_init(void);

API_EXPORT(int)
  noit_jlog_handler(eventer_t e, int mask, void *closure,
                    struct timeval *now);

/* This API call is only safe from the eventer thread */
API_EXPORT(jlog_feed_stats_t *)
  noit_jlog_feed_stats(const char *sub);

API_EXPORT(int)
  noit_jlog_foreach_feed_stats(int (*f)(jlog_feed_stats_t *, void *),
                               void *);

#endif
