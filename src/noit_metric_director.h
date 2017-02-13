/*
 * Copyright (c) 2016, Circonus, Inc. All rights reserved.
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
 *     * Neither the name Circonus, Inc. nor the names of its contributors
 *       may be used to endorse or promote products derived from this
 *       software without specific prior written permission.
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

#ifndef NOIT_METRIC_DIRECTOR_H
#define NOIT_METRIC_DIRECTOR_H

#include <mtev_defines.h>
#include <mtev_hooks.h>
#include <uuid/uuid.h>
#include "noit_message_decoder.h"

/**
 * Funnel metrics to certain threads for processing.
 * 
 * The director will de-duplicate the incoming messages on a 10 second window based on an md5 of the incoming
 * message contents.  If you don't want messages de-deplicated, switch it off using the noit_metric_director_dedupe(mtev_false)
 * call.
 * 
 */
void noit_metric_director_init();

/**
 * get your receiving lane. (allocated a FIFO for the calling thread)
 */
int noit_metric_director_my_lane();

/**
 * see init(), will dedupe by default.  Pass mtev_false to switch it off 
 */
void noit_metric_directory_dedupe(mtev_boolean dedupe);

/* Tells noit to funnel all observed lines matching this id-metric
 * back to this thread */
void noit_adjust_metric_interest(uuid_t id, const char *metric, short cnt);

/* Tells noit that this thread is interested in recieving "check" information.
 * This includes C records and S records.
 */
void noit_adjust_checks_interest(short cnt);

/* This gets the next line you've subscribed to, if avaialable. */
noit_metric_message_t *noit_metric_director_lane_next();

void noit_metric_director_message_ref(void *message);
void noit_metric_director_message_deref(void *message);
void noit_metric_director_init_globals(void);
void noit_metric_director_flush(eventer_t e);

int64_t noit_metric_director_get_messages_received();
int64_t noit_metric_director_get_messages_distributed();

MTEV_HOOK_PROTO(metric_director_want, (noit_metric_message_t *, int *, int),
                void *, closure, (void *closure, noit_metric_message_t *m, int *wants, int want_len));

#endif
