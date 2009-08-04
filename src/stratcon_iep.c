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
#include "eventer/eventer.h"
#include "utils/noit_log.h"
#include "utils/noit_b64.h"
#include "noit_jlog_listener.h"
#include "stratcon_jlog_streamer.h"
#include "stratcon_datastore.h"
#include "stratcon_iep.h"
#include "noit_conf.h"
#include "noit_check.h"
#include "noit_xml.h"

#include <unistd.h>
#include <sys/fcntl.h>
#ifdef HAVE_SYS_FILIO_H
#include <sys/filio.h>
#endif
#include <assert.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlsave.h>
#ifdef OPENWIRE
#include "amqcs.h"
#else
#include "stomp/stomp.h"
#endif

eventer_jobq_t iep_jobq;
static noit_log_stream_t noit_iep = NULL;

struct iep_thread_driver {
#ifdef OPENWIRE
  amqcs_connect_options connect_options;
  amqcs_connection *connection;
#else
  stomp_connection *connection;
#endif
  apr_pool_t *pool;
  char* exchange;
};
pthread_key_t iep_connection;

struct iep_job_closure {
  char *line;       /* This is a copy and gets trashed during processing */
  char *remote;
  xmlDocPtr doc;
  char *doc_str;
  apr_pool_t *pool;
};

static void
start_iep_daemon();

static int
bust_to_parts(char *in, char **p, int len) {
  int cnt = 0;
  char *s = in;
  while(cnt < len) {
    p[cnt++] = s;
    while(*s && *s != '\t') s++;
    if(!*s) break;
    *s++ = '\0';
  }
  while(*s) s++; /* Move to end */
  if(s > in && *(s-1) == '\n') *(s-1) = '\0'; /* chomp */
  return cnt;
}

#define ADDCHILD(a,b) \
  xmlNewTextChild(root, NULL, (xmlChar *)(a), (xmlChar *)(b))
#define NEWDOC(xmldoc,n,stanza) do { \
  xmlNodePtr root; \
  xmldoc = xmlNewDoc((xmlChar *)"1.0"); \
  root = xmlNewDocNode(xmldoc, NULL, (xmlChar *)(n), NULL); \
  xmlDocSetRootElement(xmldoc, root); \
  stanza \
} while(0)


static xmlDocPtr
stratcon_iep_doc_from_status(char *data, char *remote) {
  xmlDocPtr doc;
  char *parts[7];
  if(bust_to_parts(data, parts, 7) != 7) return NULL;
  /* 'S' TIMESTAMP UUID STATE AVAILABILITY DURATION STATUS_MESSAGE */
  NEWDOC(doc, "NoitStatus",
         {
           ADDCHILD("remote", remote);
           ADDCHILD("id", parts[2]);
           ADDCHILD("state", parts[3]);
           ADDCHILD("availability", parts[4]);
           ADDCHILD("duration", parts[5]);
           ADDCHILD("status", parts[6]);
         });
  return doc;
}

static xmlDocPtr
stratcon_iep_doc_from_check(char *data, char *remote) {
  xmlDocPtr doc;
  char *parts[6];
  if(bust_to_parts(data, parts, 6) != 6) return NULL;
  /* 'C' TIMESTAMP UUID TARGET MODULE NAME */
  NEWDOC(doc, "NoitCheck",
         {
           ADDCHILD("remote", remote);
           ADDCHILD("id", parts[2]);
           ADDCHILD("target", parts[3]);
           ADDCHILD("module", parts[4]);
           ADDCHILD("name", parts[5]);
         });
  return doc;
}

static xmlDocPtr
stratcon_iep_doc_from_metric(char *data, char *remote) {
  xmlDocPtr doc;
  char *parts[6];
  const char *rootname = "NoitMetricNumeric";
  const char *valuename = "value";
  if(bust_to_parts(data, parts, 6) != 6) return NULL;
  /*  'M' TIMESTAMP UUID NAME TYPE VALUE */

  if(*parts[4] == METRIC_STRING) {
    rootname = "NoitMetricText";
    valuename = "message";
  }
  NEWDOC(doc, rootname,
         {
           ADDCHILD("remote", remote);
           ADDCHILD("id", parts[2]);
           ADDCHILD("name", parts[3]);
           ADDCHILD(valuename, parts[5]);
         });
  return doc;
}

static xmlDocPtr
stratcon_iep_doc_from_query(char *data, char *remote) {
  xmlDocPtr doc;
  char *parts[4];
  if(bust_to_parts(data, parts, 4) != 4) return NULL;
  /*  'Q' ID NAME QUERY  */

  NEWDOC(doc, "StratconQuery",
         {
           ADDCHILD("id", parts[1]);
           ADDCHILD("name", parts[2]);
           ADDCHILD("expression", parts[3]);
         });
  return doc;
}

static xmlDocPtr
stratcon_iep_doc_from_querystop(char *data, char *remote) {
  xmlDocPtr doc;
  char *parts[2];
  if(bust_to_parts(data, parts, 2) != 2) return NULL;
  /*  'Q' ID */

  NEWDOC(doc, "StratconQueryStop",
         {
           xmlNodeSetContent(root, (xmlChar *)parts[1]);
         });
  return doc;
}

static xmlDocPtr
stratcon_iep_doc_from_line(char *data, char *remote) {
  if(data) {
    switch(*data) {
      case 'C': return stratcon_iep_doc_from_check(data, remote);
      case 'S': return stratcon_iep_doc_from_status(data, remote);
      case 'M': return stratcon_iep_doc_from_metric(data, remote);
      case 'Q': return stratcon_iep_doc_from_query(data, remote);
      case 'q': return stratcon_iep_doc_from_querystop(data, remote);
    }
  }
  return NULL;
}

static float
stratcon_iep_age_from_line(char *data, struct timeval now) {
  float n, t;
  if(data && (*data == 'S' || *data == 'M')) {
    if(data[1] != '\t') return 0;
    t = strtof(data + 2, NULL);
    n = (float)now.tv_sec + (float)now.tv_usec / 1000000.0;
    return n - t;
  }
  return 0;
}

void stratcon_iep_submit_queries() {
  int i, cnt = 0;
  noit_conf_section_t *query_configs;
  char path[256];

  snprintf(path, sizeof(path), "/stratcon/iep/queries//query");
  query_configs = noit_conf_get_sections(NULL, path, &cnt);
  noitL(noit_debug, "Found %d %s stanzas\n", cnt, path);
  for(i=0; i<cnt; i++) {
    char id[UUID_STR_LEN];
    char topic[256];
    char *query;
    char *line;
    int line_len;

    if(!noit_conf_get_stringbuf(query_configs[i],
                                "self::node()/@id",
                                id, sizeof(id))) {
      noitL(noit_iep, "No uuid specified in query\n");
      continue;
    }
    if(!noit_conf_get_stringbuf(query_configs[i],
                                "ancestor-or-self::node()/@topic",
                                topic, sizeof(topic))) {
      noitL(noit_iep, "No topic specified in query\n");
      continue;
    }
    if(!noit_conf_get_string(query_configs[i], "self::node()",
                             &query)) {
      noitL(noit_iep, "No contents specified in query\n");
      continue;
    }
    line_len = 4 /* 3 tabs + \0 */ +
               1 /* 'Q' */ + 1 /* '\n' */ +
               strlen(id) + strlen(topic) + strlen(query);
    line = malloc(line_len);
    snprintf(line, line_len, "Q\t%s\t%s\t%s\n", id, topic, query);
    free(query);
    query = line;
    while(query[0] && query[1]) {
      if(*query == '\n') *query = ' ';
      query++;
    }
    stratcon_iep_line_processor(DS_OP_INSERT, NULL, line);
  }
  free(query_configs);
}

static
struct iep_thread_driver *stratcon_iep_get_connection() {
  apr_status_t rc;
  struct iep_thread_driver *driver;
  driver = pthread_getspecific(iep_connection);
  if(!driver) {
    driver = calloc(1, sizeof(*driver));
    pthread_setspecific(iep_connection, driver);
  }

  if(!driver->pool) {
    if(apr_pool_create(&driver->pool, NULL) != APR_SUCCESS) return NULL;
  }

  if(!driver->connection) {
    int port;
    char hostname[128];
    if(!noit_conf_get_int(NULL, "/stratcon/iep/stomp/port", &port))
      port = 61613;
    if(!noit_conf_get_stringbuf(NULL, "/stratcon/iep/stomp/hostname",
                                hostname, sizeof(hostname)))
      strlcpy(hostname, "127.0.0.1", sizeof(hostname));
#ifdef OPENWIRE
    memset(&driver->connect_options, 0, sizeof(driver->connect_options));
    strlcpy(driver->connect_options.hostname, hostname,
            sizeof(driver->connect_options.hostname));
    driver->connect_options.port = port;
    if(amqcs_connect(&driver->connection, &driver->connect_options,
                     driver->pool) != APR_SUCCESS) {
      noitL(noit_error, "MQ connection failed\n");
      return NULL;
    }
#else
    if(stomp_connect(&driver->connection, hostname, port,
                     driver->pool)!= APR_SUCCESS) {
      noitL(noit_error, "MQ connection failed\n");
      stomp_disconnect(&driver->connection);
      return NULL;
    }

    {
      stomp_frame frame;
      char username[128];
      char password[128];
      char* exchange = malloc(128);
      frame.command = "CONNECT";
      frame.headers = apr_hash_make(driver->pool);
      // This is for RabbitMQ Support
      if((noit_conf_get_stringbuf(NULL, "/stratcon/iep/stomp/username",
                                  username, sizeof(username))) &&
         (noit_conf_get_stringbuf(NULL, "/stratcon/iep/stomp/password",
                                  password, sizeof(password))))
      {
        apr_hash_set(frame.headers, "login", APR_HASH_KEY_STRING, username);
        apr_hash_set(frame.headers, "passcode", APR_HASH_KEY_STRING, password);
      }


      // This is for RabbitMQ support
      driver->exchange = NULL;
      if(noit_conf_get_stringbuf(NULL, "/stratcon/iep/stomp/exchange",
                                  exchange, 128))
      {
        if (!driver->exchange)
          driver->exchange = exchange;
        else
          free(exchange);
        apr_hash_set(frame.headers, "exchange", APR_HASH_KEY_STRING, driver->exchange);
      }



/*
      We don't use login/pass
      apr_hash_set(frame.headers, "login", APR_HASH_KEY_STRING, "");
      apr_hash_set(frame.headers, "passcode", APR_HASH_KEY_STRING, "");
*/
      frame.body = NULL;
      frame.body_length = -1;
      rc = stomp_write(driver->connection, &frame, driver->pool);
      if(rc != APR_SUCCESS) {
        noitL(noit_error, "MQ STOMP CONNECT failed, %d\n", rc);
        stomp_disconnect(&driver->connection);
        return NULL;
      }
    }  
    {
      stomp_frame *frame;
      rc = stomp_read(driver->connection, &frame, driver->pool);
      if (rc != APR_SUCCESS) {
        noitL(noit_error, "MQ STOMP CONNECT bad response, %d\n", rc);
        stomp_disconnect(&driver->connection);
        return NULL;
      }
     }
#endif
     stratcon_datastore_iep_check_preload();
     stratcon_iep_submit_queries();
  }

  return driver;
}

static int
stratcon_iep_submitter(eventer_t e, int mask, void *closure,
                       struct timeval *now) {
  float age;
  struct iep_job_closure *job = closure;
  /* We only play when it is an asynch event */
  if(!(mask & EVENTER_ASYNCH_WORK)) return 0;

  if(mask & EVENTER_ASYNCH_CLEANUP) {
    /* free all the memory associated with the batch */
    if(job) {
      if(job->line) free(job->line);
      if(job->remote) free(job->remote);
      if(job->doc_str) free(job->doc_str);
      if(job->doc) xmlFreeDoc(job->doc);
      if(job->pool) apr_pool_destroy(job->pool);
      free(job);
    }
    return 0;
  }
  if(!job->line || job->line[0] == '\0') return 0;

  if((age = stratcon_iep_age_from_line(job->line, *now)) > 60) {
    noitL(noit_debug, "Skipping old event %f second old.\n", age);
    return 0;
  }
  job->doc = stratcon_iep_doc_from_line(job->line, job->remote);
  if(job->doc) {
    job->doc_str = noit_xmlSaveToBuffer(job->doc);
    if(job->doc_str) {
      /* Submit */
      struct iep_thread_driver *driver;
      driver = stratcon_iep_get_connection();
      if(driver && driver->pool && driver->connection) {
        apr_status_t rc;
#ifdef OPENWIRE
        ow_ActiveMQQueue *dest;
        ow_ActiveMQTextMessage *message;

        apr_pool_create(&job->pool, driver->pool);
        message = ow_ActiveMQTextMessage_create(job->pool);
        message->content =
          ow_byte_array_create_with_data(job->pool,strlen(job->doc_str),
                                         job->doc_str);
        dest = ow_ActiveMQQueue_create(job->pool);
        dest->physicalName = ow_string_create_from_cstring(job->pool,"TEST.QUEUE");         
        rc = amqcs_send(driver->connection,
                        (ow_ActiveMQDestination*)dest,
                        (ow_ActiveMQMessage*)message,
                        1,4,0,job->pool);
        if(rc != APR_SUCCESS) {
          noitL(noit_error, "MQ send failed, disconnecting\n");
          if(driver->connection) amqcs_disconnect(&driver->connection);
          driver->connection = NULL;
        }
#else
        stomp_frame out;

        apr_pool_create(&job->pool, driver->pool);

        out.command = "SEND";
        out.headers = apr_hash_make(job->pool);
        if (driver->exchange)
          apr_hash_set(out.headers, "exchange", APR_HASH_KEY_STRING, driver->exchange);

        apr_hash_set(out.headers, "destination", APR_HASH_KEY_STRING, "/queue/noit.firehose");
        apr_hash_set(out.headers, "ack", APR_HASH_KEY_STRING, "auto");
      
        out.body_length = -1;
        out.body = job->doc_str;
        rc = stomp_write(driver->connection, &out, job->pool);
        if(rc != APR_SUCCESS) {
          noitL(noit_error, "STOMP send failed, disconnecting\n");
          if(driver->connection) stomp_disconnect(&driver->connection);
          driver->connection = NULL;
        }
#endif
      }
      else {
        noitL(noit_error, "Not submitting event, no MQ\n");
      }
    }
  }
  else {
    noitL(noit_iep, "no iep handler for: '%s'\n", job->line);
  }
  return 0;
}

void
stratcon_iep_line_processor(stratcon_datastore_op_t op,
                            struct sockaddr *remote, void *operand) {
  int len;
  char remote_str[128];
  struct iep_job_closure *jc;
  eventer_t newe;
  struct timeval __now, iep_timeout = { 20L, 0L };
  /* We only care about inserts */

  if(op == DS_OP_CHKPT) {
    eventer_add((eventer_t) operand);
    return;
  }
  if(op != DS_OP_INSERT) return;

  snprintf(remote_str, sizeof(remote_str), "%s", "0.0.0.0");
  if(remote) {
    switch(remote->sa_family) {
      case AF_INET:
        len = sizeof(struct sockaddr_in);
        inet_ntop(remote->sa_family, &((struct sockaddr_in *)remote)->sin_addr,
                  remote_str, len);
        break;
      case AF_INET6:
       len = sizeof(struct sockaddr_in6);
        inet_ntop(remote->sa_family, &((struct sockaddr_in6 *)remote)->sin6_addr,
                  remote_str, len);
       break;
      case AF_UNIX:
        len = SUN_LEN(((struct sockaddr_un *)remote));
        snprintf(remote_str, sizeof(remote_str), "%s", ((struct sockaddr_un *)remote)->sun_path);
        break;
    }
  }

  /* process operand and push onto queue */
  gettimeofday(&__now, NULL);
  newe = eventer_alloc();
  newe->mask = EVENTER_ASYNCH;
  add_timeval(__now, iep_timeout, &newe->whence);
  newe->callback = stratcon_iep_submitter;
  jc = calloc(1, sizeof(*jc));
  jc->line = operand;
  jc->remote = strdup(remote_str);
  newe->closure = jc;

  eventer_add_asynch(&iep_jobq, newe);
}

static void connection_destroy(void *vd) {
  struct iep_thread_driver *driver = vd;
#ifdef OPENWIRE
  if(driver->connection) amqcs_disconnect(&driver->connection);
#else
  if(driver->connection) stomp_disconnect(&driver->connection);
  if(driver->exchange) free(driver->exchange);
#endif
  if(driver->pool) apr_pool_destroy(driver->pool);
  free(driver);
}

jlog_streamer_ctx_t *
stratcon_jlog_streamer_iep_ctx_alloc(void) {
  jlog_streamer_ctx_t *ctx;
  ctx = stratcon_jlog_streamer_ctx_alloc();
  ctx->jlog_feed_cmd = htonl(NOIT_JLOG_DATA_TEMP_FEED);
  ctx->push = stratcon_iep_line_processor;
  return ctx;
}

struct iep_daemon_info {
  pid_t child;
  int stdin_pipe[2];
  int stderr_pipe[2];
  char *directory;
  char *command;
};

static void
iep_daemon_info_free(struct iep_daemon_info *info) {
  if(!info) return;
  if(info->directory) free(info->directory);
  if(info->command) free(info->command);
  if(info->stdin_pipe[0] >= 0) close(info->stdin_pipe[0]);
  if(info->stdin_pipe[1] >= 0) close(info->stdin_pipe[1]);
  if(info->stderr_pipe[0] >= 0) close(info->stderr_pipe[0]);
  if(info->stderr_pipe[1] >= 0) close(info->stderr_pipe[1]);
  free(info);
}

static int
stratcon_iep_err_handler(eventer_t e, int mask, void *closure,
                         struct timeval *now) {
  int len, newmask;
  char buff[4096];
  struct iep_daemon_info *info = (struct iep_daemon_info *)closure;

  if(mask & EVENTER_EXCEPTION) {
    int rv;
   read_error:
    kill(SIGKILL, info->child);
    if(waitpid(info->child, &rv, 0) != info->child) {
      noitL(noit_error, "Failed to reap IEP daemon\n");
      exit(-1);
    }
    noitL(noit_error, "IEP daemon is done, starting a new one\n");
    start_iep_daemon();
    eventer_remove_fd(e->fd);
    iep_daemon_info_free(info);
    return 0;
  }
  while(1) {
    len = e->opset->read(e->fd, buff, sizeof(buff)-1, &newmask, e);
    if(len == -1 && (errno == EAGAIN || errno == EINTR))
      return newmask | EVENTER_EXCEPTION;
    if(len <= 0) goto read_error;
    assert(len < sizeof(buff));
    buff[len] = '\0';
    noitL(noit_iep, "%s", buff);
  }
}

static void
start_iep_daemon() {
  eventer_t newe;
  struct iep_daemon_info *info;

  info = calloc(1, sizeof(*info));
  info->stdin_pipe[0] = info->stdin_pipe[1] = -1;
  info->stderr_pipe[0] = info->stderr_pipe[1] = -1;

  if(!noit_conf_get_string(NULL, "/stratcon/iep/start/@directory",
                           &info->directory))
    info->directory = strdup(".");
  if(!noit_conf_get_string(NULL, "/stratcon/iep/start/@command",
                           &info->command)) {
    noitL(noit_error, "No IEP start command provided.  You're on your own.\n");
    goto bail;
  }
  if(pipe(info->stdin_pipe) != 0 ||
     pipe(info->stderr_pipe) != 0) {
    noitL(noit_error, "pipe: %s\n", strerror(errno));
    goto bail;
  }
  info->child = fork();
  if(info->child == -1) {
    noitL(noit_error, "fork: %s\n", strerror(errno));
    goto bail;
  }
  if(info->child == 0) {
    char *argv[3] = { "run-iep", NULL, NULL };
    int stdout_fileno;

    argv[1] = noit_conf_config_filename();

    if(chdir(info->directory) != 0) {
      noitL(noit_error, "Starting IEP daemon, chdir failed: %s\n",
            strerror(errno));
      exit(-1);
    }

    close(info->stdin_pipe[1]);
    close(info->stderr_pipe[0]);
    dup2(info->stdin_pipe[0], 0);
    dup2(info->stderr_pipe[1], 2);
    stdout_fileno = open("/dev/null", O_WRONLY);
    dup2(stdout_fileno, 1);

    exit(execv(info->command, argv));
  }
  /* in the parent */
  socklen_t on = 1;

  close(info->stdin_pipe[0]);
  info->stdin_pipe[0] = -1;
  close(info->stderr_pipe[1]);
  info->stderr_pipe[1] = -1;
  if(ioctl(info->stderr_pipe[0], FIONBIO, &on)) {
    goto bail;
  }

  newe = eventer_alloc();
  newe->fd = info->stderr_pipe[0];
  newe->mask = EVENTER_READ | EVENTER_EXCEPTION;
  newe->callback = stratcon_iep_err_handler;
  newe->closure = info;
  eventer_add(newe);
  info = NULL;

  return;

 bail:
  if(info) {
    iep_daemon_info_free(info);
  }
  noitL(noit_error, "Failed to start IEP daemon\n");
  exit(-1);
  return;
}

void
stratcon_iep_init() {
  noit_boolean disabled = noit_false;
  apr_initialize();
  atexit(apr_terminate);   

  if(noit_conf_get_boolean(NULL, "/stratcon/iep/@disabled", &disabled) &&
     disabled == noit_true) {
    noitL(noit_error, "IEP system is disabled!\n");
    return;
  }

  noit_iep = noit_log_stream_find("error/iep");
  if(!noit_iep) noit_iep = noit_error;

  start_iep_daemon();

  eventer_name_callback("stratcon_iep_submitter", stratcon_iep_submitter);
  pthread_key_create(&iep_connection, connection_destroy);

  /* start up a thread pool of one */
  memset(&iep_jobq, 0, sizeof(iep_jobq));
  eventer_jobq_init(&iep_jobq, "iep_submitter");
  iep_jobq.backq = eventer_default_backq();
  eventer_jobq_increase_concurrency(&iep_jobq);

  /* setup our live jlog stream */
  stratcon_streamer_connection(NULL, NULL,
                               stratcon_jlog_recv_handler,
                               (void *(*)())stratcon_jlog_streamer_iep_ctx_alloc,
                               NULL,
                               jlog_streamer_ctx_free);
}

