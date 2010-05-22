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

#include "noit_defines.h"
#include "noit_listener.h"
#include "noit_http.h"

#ifndef NOIT_REST_H
#define NOIT_REST_H

#define NOIT_CONTROL_GET    0x47455420 /* "GET " */
#define NOIT_CONTROL_HEAD   0x48454144 /* "HEAD" */
#define NOIT_CONTROL_POST   0x504f5354 /* "POST" */
#define NOIT_CONTROL_DELETE 0x44454c45 /* "DELE" */
#define NOIT_CONTROL_PUT    0x50555420 /* "PUT " */
#define NOIT_CONTROL_MERGE  0x4d455247 /* "MERG" */

typedef struct noit_http_rest_closure noit_http_rest_closure_t;

typedef int (*rest_request_handler)(noit_http_rest_closure_t *,
                                    int npats, char **pats);
typedef noit_boolean (*rest_authorize_func_t)(noit_http_rest_closure_t *,
                                              int npats, char **pats);
struct noit_http_rest_closure {
  noit_http_session_ctx *http_ctx;
  acceptor_closure_t *ac;
  char *remote_cn;
  rest_request_handler fastpath;
  int nparams;
  char **params;
  int wants_shutdown;
  void *call_closure;
  void (*call_closure_free)(void *);
};

API_EXPORT(void) noit_http_rest_init();

API_EXPORT(noit_boolean)
  noit_http_rest_access(noit_http_rest_closure_t *restc,
                        int npats, char **pats) ;

API_EXPORT(noit_boolean)
  noit_http_rest_client_cert_auth(noit_http_rest_closure_t *restc,
                                  int npats, char **pats);

API_EXPORT(int)
  noit_http_rest_register(const char *method, const char *base,
                          const char *expression, rest_request_handler f);

API_EXPORT(int)
  noit_http_rest_register_auth(const char *method, const char *base,
                               const char *expression, rest_request_handler f,
                               rest_authorize_func_t auth);

API_EXPORT(xmlDocPtr)
  rest_get_xml_upload(noit_http_rest_closure_t *restc,
                      int *mask, int *complete) ;

API_EXPORT(int)
  noit_rest_simple_file_handler(noit_http_rest_closure_t *restc,
                                int npats, char **pats);

#endif
