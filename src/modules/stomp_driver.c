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
#include "noit_module.h"
#include "eventer/eventer.h"
#include "utils/noit_log.h"
#include "stratcon_iep.h"
#include "noit_conf.h"
#include "libstomp.h"

struct stomp_driver {
  stomp_connection *connection;
  apr_pool_t *pool;
  char *exchange;
  char *user;
  char *pass;
  char *destination;
  char *hostname;
  int port;
};

static iep_thread_driver_t *noit_stomp_allocate() {
  struct stomp_driver *dr;
  dr = calloc(1, sizeof(*dr));
  if(apr_pool_create(&dr->pool, NULL) != APR_SUCCESS) {
    free(dr);
    return NULL;
  }
  noit_conf_get_string(NULL, "/stratcon/iep/mq/exchange", &dr->exchange);
  if(!noit_conf_get_string(NULL, "/stratcon/iep/mq/destination", &dr->destination))
  if(!dr->destination) dr->destination = strdup("/queue/noit.firehose");
  noit_conf_get_string(NULL, "/stratcon/iep/mq/username", &dr->user);
  noit_conf_get_string(NULL, "/stratcon/iep/mq/password", &dr->pass);
  noit_conf_get_string(NULL, "/stratcon/iep/mq/hostname", &dr->hostname);
  if(!dr->hostname) dr->hostname = strdup("127.0.0.1");
  if(!noit_conf_get_int(NULL, "/stratcon/iep/mq/port", &dr->port))
    dr->port = 61613;
  return (iep_thread_driver_t *)dr;
}
static int noit_stomp_disconnect(iep_thread_driver_t *d) {
  struct stomp_driver *dr = (struct stomp_driver *)d;
  if(dr->connection) {
    stomp_disconnect(&dr->connection);
    return 0;
  }
  return -1;
}
static void noit_stomp_deallocate(iep_thread_driver_t *d) {
  struct stomp_driver *dr = (struct stomp_driver *)d;
  if(dr->connection) stomp_disconnect(&dr->connection);
  if(dr->pool) apr_pool_destroy(dr->pool);
  if(dr->exchange) free(dr->exchange);
  if(dr->destination) free(dr->destination);
  if(dr->user) free(dr->user);
  if(dr->pass) free(dr->pass);
  if(dr->hostname) free(dr->hostname);
  free(dr);
}
static int noit_stomp_connect(iep_thread_driver_t *dr) {
  struct stomp_driver *driver = (struct stomp_driver *)dr;
  apr_status_t rc;
  stomp_frame frame;

  if(!driver->connection) {
    if(stomp_connect(&driver->connection, driver->hostname, driver->port,
                     driver->pool)!= APR_SUCCESS) {
      noitL(noit_error, "MQ connection failed\n");
      stomp_disconnect(&driver->connection);
      return -1;
    }

    frame.command = "CONNECT";
    frame.headers = apr_hash_make(driver->pool);

    // This is for RabbitMQ Support
    if(driver->user && driver->pass) {
      apr_hash_set(frame.headers, "login",
                   APR_HASH_KEY_STRING, driver->user);
      apr_hash_set(frame.headers, "passcode",
                   APR_HASH_KEY_STRING, driver->pass);
    }
    if(driver->exchange)
      apr_hash_set(frame.headers, "exchange", APR_HASH_KEY_STRING, driver->exchange);

    frame.body = NULL;
    frame.body_length = -1;
    rc = stomp_write(driver->connection, &frame, driver->pool);
    if(rc != APR_SUCCESS) {
      noitL(noit_error, "MQ STOMP CONNECT failed, %d\n", rc);
      stomp_disconnect(&driver->connection);
      return -1;
    }
    return 0;
  }
  /* 1 means already connected */
  return 1;
}
static int noit_stomp_submit(iep_thread_driver_t *dr,
                             const char *payload, size_t payloadlen) {
  struct stomp_driver *driver = (struct stomp_driver *)dr;
  apr_pool_t *dummy;
  apr_status_t rc;
  stomp_frame out;

  if(apr_pool_create(&dummy, NULL) != APR_SUCCESS) return -1;

  out.command = "SEND";
  out.headers = apr_hash_make(dummy);
  if (driver->exchange)
    apr_hash_set(out.headers, "exchange",
                 APR_HASH_KEY_STRING, driver->exchange);

  apr_hash_set(out.headers, "destination",
               APR_HASH_KEY_STRING, driver->destination);
  apr_hash_set(out.headers, "ack", APR_HASH_KEY_STRING, "auto");
 
  out.body_length = -1;
  out.body = (char *)payload;
  rc = stomp_write(driver->connection, &out, dummy);
  if(rc != APR_SUCCESS) {
    noitL(noit_error, "STOMP send failed, disconnecting\n");
    if(driver->connection) stomp_disconnect(&driver->connection);
    driver->connection = NULL;
  }
  apr_pool_destroy(dummy);
  return (rc == APR_SUCCESS) ? 0 : -1;
}

mq_driver_t mq_driver_stomp = {
  noit_stomp_allocate,
  noit_stomp_connect,
  noit_stomp_submit,
  noit_stomp_disconnect,
  noit_stomp_deallocate
};

static int noit_stomp_driver_config(noit_module_generic_t *self, noit_hash_table *o) {
  return 0;
}
static int noit_stomp_driver_onload(noit_image_t *self) {
  return 0;
}

static int noit_stomp_driver_init(noit_module_generic_t *self) {
  apr_initialize();
  atexit(apr_terminate);
  stratcon_iep_mq_driver_register("stomp", &mq_driver_stomp);
  return 0;
}

noit_module_generic_t stomp_driver = {
  {
    NOIT_GENERIC_MAGIC,
    NOIT_GENERIC_ABI_VERSION,
    "stomp_driver",
    "STOMP driver for IEP MQ submission",
    "",
    noit_stomp_driver_onload
  },
  noit_stomp_driver_config,
  noit_stomp_driver_init
};

