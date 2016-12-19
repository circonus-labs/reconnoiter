/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
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

#include <mtev_defines.h>

#include <sys/types.h>
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/fcntl.h>
#ifdef HAVE_SYS_FILIO_H
#include <sys/filio.h>
#endif
#include <signal.h>
#include <errno.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>

#include <eventer/eventer.h>
#include <mtev_log.h>
#include <mtev_b64.h>
#include <mtev_conf.h>
#include <mtev_rest.h>

#include "noit_mtev_bridge.h"
#include "noit_jlog_listener.h"
#include "stratcon_jlog_streamer.h"
#include "stratcon_datastore.h"
#include "stratcon_iep.h"
#include "noit_check.h"

eventer_jobq_t *iep_jobq;
static mtev_log_stream_t noit_iep = NULL;
static mtev_log_stream_t noit_iep_debug = NULL;
static mtev_spinlock_t iep_conn_cnt = 0;
static mtev_boolean inject_remote_cn = mtev_false;

static mtev_hash_table mq_drivers;
struct driver_thread_data {
  mq_driver_t *mq_driver;
  struct iep_thread_driver *driver_data;
};
struct driver_list {
  mq_driver_t *mq_driver;
  pthread_key_t iep_connection;
  mtev_conf_section_t section;
  struct driver_list *next;
} *drivers;

static int iep_system_enabled = 1;
int stratcon_iep_get_enabled() { return iep_system_enabled; }
void stratcon_iep_set_enabled(int n) { iep_system_enabled = n; }
static int rest_set_filters(mtev_http_rest_closure_t *restc,
                            int npats, char **pats);


struct iep_job_closure {
  char *line;       /* This is a copy and gets trashed during processing */
  char *remote;
  char *doc_str;
  struct timeval start;
};

static void
start_iep_daemon();

static double
stratcon_iep_age_from_line(char *data, struct timeval now) {
  double n, t;
  if(data && (*data == 'S' || *data == 'M')) {
    if(data[1] != '\t') return 0;
    t = strtod(data + 2, NULL);
    n = (double)now.tv_sec + (double)now.tv_usec / 1000000.0;
    return n - t;
  }
  return 0;
}

struct statement_node {
  char *id;
  char *statement;
  char *provides;
  int marked; /* helps with identifying cycles */
  int nrequires;
  struct statement_node **requires;
};
static void
statement_node_free(void *vstmt) {
  struct statement_node *stmt = vstmt;
  if(stmt->id) free(stmt->id);
  if(stmt->statement) free(stmt->statement);
  if(stmt->provides) free(stmt->provides);
  if(stmt->requires) free(stmt->requires);
}
static void
mq_command_free(void *command) {
  mq_command_t *cmd = (mq_command_t*)command;
  int i;
  if (cmd->check.uuid) {
    xmlFree(cmd->check.uuid);
  }
  if (cmd->check.metric_count > 0) {
    for (i=0; i < cmd->check.metric_count; i++) {
      if (!cmd->check.metrics[i]) break;
      xmlFree(cmd->check.metrics[i]);
    }
    free(cmd->check.metrics);
  }
}
static int
stmt_mark_dag(struct statement_node *stmt, int mgen) {
  int i;
  mtevAssert(stmt->marked <= mgen);
  if(stmt->marked == mgen) return -1;
  if(stmt->marked > 0) return 0; /* validated in a previous sweep */
  stmt->marked = mgen;
  for(i=0; i<stmt->nrequires; i++)
    if(stmt_mark_dag(stmt->requires[i], mgen) < 0) return -1;
  return 0;
}
static void
submit_statement_node(struct statement_node *stmt) {
  int line_len, i;
  char *line, *cp;

  if(stmt->marked) return;
  for(i=0; i<stmt->nrequires; i++)
    submit_statement_node(stmt->requires[i]);

  line_len = 3 /* 2 tabs + \0 */ +
             1 /* 'D' */ + 1 /* '\n' */ +
             strlen(stmt->id) + strlen(stmt->statement);
  line = malloc(line_len);
  snprintf(line, line_len, "D\t%s\t%s\n", stmt->id, stmt->statement);
  cp = line;
  while(cp[0] && cp[1]) {
    if(*cp == '\n') *cp = ' ';
    cp++;
  }
  mtevL(noit_iep, "submitting statement: %s\n", line);
  stratcon_iep_line_processor(DS_OP_INSERT, NULL, NULL, line, NULL);
  stmt->marked = 1;
}
void stratcon_iep_submit_statements() {
  int i, cnt = 0;
  mtev_conf_section_t *statement_configs;
  char path[256];
  struct statement_node *stmt;
  void *vstmt;
  mtev_hash_table stmt_by_id;
  mtev_hash_table stmt_by_provider;
  mtev_hash_iter iter = MTEV_HASH_ITER_ZERO;
  const char *key;
  int klen, mgen = 0;

  mtev_hash_init(&stmt_by_id);
  mtev_hash_init(&stmt_by_provider);

  snprintf(path, sizeof(path), "/stratcon/iep/queries[@master=\"stratcond\"]//statement");
  statement_configs = mtev_conf_get_sections(NULL, path, &cnt);
  mtevL(noit_debug, "Found %d %s stanzas\n", cnt, path);

  /* Phase 1: sweep in all the statements */
  for(i=0; i<cnt; i++) {
    char id[UUID_STR_LEN+1];
    char provides[256];
    char *statement;

    if(!mtev_conf_get_stringbuf(statement_configs[i],
                                "self::node()/@id",
                                id, sizeof(id))) {
      mtevL(noit_iep, "No uuid specified in query\n");
      continue;
    }
    if(!mtev_conf_get_stringbuf(statement_configs[i],
                                "ancestor-or-self::node()/@provides",
                                provides, sizeof(provides))) {
      provides[0] = '\0';
    }
    if(!mtev_conf_get_string(statement_configs[i], "self::node()/epl",
                             &statement)) {
      mtevL(noit_iep, "No contents specified in statement\n");
      continue;
    }
    stmt = calloc(1, sizeof(*stmt));
    stmt->id = strdup(id);
    stmt->statement = statement;
    stmt->provides = provides[0] ? strdup(provides) : NULL;
    if(!mtev_hash_store(&stmt_by_id, stmt->id, strlen(stmt->id), stmt)) {
      mtevL(noit_iep, "Duplicate statement id: %s\n", stmt->id);
      exit(-1);
    }
    if(stmt->provides) {
      if(!mtev_hash_store(&stmt_by_provider, stmt->provides,
                          strlen(stmt->provides), stmt)) {
        mtevL(noit_iep, "Two statements provide: '%s'\n", stmt->provides);
        exit(-1);
      }
    }
  }

  /* Phase 2: load the requires graph */
  for(i=0; i<cnt; i++) {
    char id[UUID_STR_LEN+1];
    int rcnt, j;
    char *requires;
    mtev_conf_section_t *reqs;

    if(!mtev_conf_get_stringbuf(statement_configs[i],
                                "self::node()/@id",
                                id, sizeof(id))) {
      mtevL(noit_iep, "No uuid specified in query\n");
      continue;
    }
    if(!mtev_hash_retrieve(&stmt_by_id, id, strlen(id), &vstmt)) {
      mtevL(noit_iep, "Cannot find statement: %s\n", id);
      exit(-1);
    }
    stmt = vstmt;
    reqs = mtev_conf_get_sections(statement_configs[i],
                                  "self::node()/requires", &rcnt);
    if(rcnt > 0) {
      stmt->requires = malloc(rcnt * sizeof(*(stmt->requires)));
      for(j=0; j<rcnt; j++) {
        void *vrstmt;
        if(!mtev_conf_get_string(reqs[j], "self::node()",
                                 &requires) || requires[0] == '\0') {
          continue;
        }
        if(!mtev_hash_retrieve(&stmt_by_provider, requires, strlen(requires),
                               &vrstmt)) {
          mtevL(noit_iep,
                "Statement %s requires %s which no one provides.\n",
                stmt->id, requires);
          exit(-1);
        }
        stmt->requires[stmt->nrequires++] = vrstmt;
      }
    }
  }

  /* Phase 3: Recursive sweep and mark to detect cycles.
     We're walking the graph backwards here from dependent to provider,
     but a cycle is a cycle, so this validates the graph. */
  while(mtev_hash_next(&stmt_by_id, &iter, &key, &klen, &vstmt)) {
    stmt = vstmt;
    if(stmt_mark_dag(stmt, ++mgen) < 0) {
      mtevL(noit_iep, "Statement %s has a cyclic requirement\n", stmt->id);
      exit(-1);
    }
  }

  /* Phase 4: clean the markings */
  memset(&iter, 0, sizeof(iter));
  while(mtev_hash_next(&stmt_by_id, &iter, &key, &klen, &vstmt)) {
    stmt = vstmt;
    stmt->marked = 0;
  }

  /* Phase 5: do the load */
  memset(&iter, 0, sizeof(iter));
  while(mtev_hash_next(&stmt_by_id, &iter, &key, &klen, &vstmt)) {
    stmt = vstmt;
    submit_statement_node(stmt);
  }

  mtev_hash_destroy(&stmt_by_provider, NULL, NULL);
  mtev_hash_destroy(&stmt_by_id, NULL, statement_node_free);
  free(statement_configs);
}

void stratcon_iep_submit_queries() {
  int i, cnt = 0;
  mtev_conf_section_t *query_configs;
  char path[256];

  snprintf(path, sizeof(path), "/stratcon/iep/queries[@master=\"stratcond\"]//query");
  query_configs = mtev_conf_get_sections(NULL, path, &cnt);
  mtevL(noit_debug, "Found %d %s stanzas\n", cnt, path);
  for(i=0; i<cnt; i++) {
    char id[UUID_STR_LEN+1];
    char topic[256];
    char *query;
    char *line;
    int line_len;

    if(!mtev_conf_get_stringbuf(query_configs[i],
                                "self::node()/@id",
                                id, sizeof(id))) {
      mtevL(noit_iep, "No uuid specified in query\n");
      continue;
    }
    if(!mtev_conf_get_stringbuf(query_configs[i],
                                "ancestor-or-self::node()/@topic",
                                topic, sizeof(topic))) {
      mtevL(noit_iep, "No topic specified in query\n");
      continue;
    }
    if(!mtev_conf_get_string(query_configs[i], "self::node()/epl",
                             &query)) {
      mtevL(noit_iep, "No contents specified in query\n");
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
    stratcon_iep_line_processor(DS_OP_INSERT, NULL, NULL, line, NULL);
  }
  free(query_configs);
}

static struct driver_thread_data *
connect_iep_driver(struct driver_list *d) {
  int rc;
  struct driver_thread_data *data;
  data = pthread_getspecific(d->iep_connection);
  if(!data) {
    data = calloc(1, sizeof(*data));
    data->mq_driver = d->mq_driver;
    pthread_setspecific(d->iep_connection, data);
  }
  if(!data->driver_data)
    data->driver_data = data->mq_driver->allocate(d->section);
  rc = data->mq_driver->connect(data->driver_data);
  if(rc < 0) return NULL;
  if(rc == 0) {
    /* Initial connect */
    /* TODO: this should be requested by Esper, not blindly pushed */
    stratcon_iep_submit_statements();
    stratcon_datastore_iep_check_preload();
    stratcon_iep_submit_queries();
  }

  return data;
}

static int
setup_iep_connection_callback(eventer_t e, int mask, void *closure,
                              struct timeval *now) {
  mtev_spinlock_unlock(&iep_conn_cnt);
  stratcon_iep_line_processor(DS_OP_INSERT, NULL, NULL, NULL, NULL);
  return 0;
}

static void
setup_iep_connection_later(int seconds) {
  eventer_t newe;
  if(!mtev_spinlock_trylock(&iep_conn_cnt)) return;
  newe = eventer_alloc();
  mtev_gettimeofday(&newe->whence, NULL);
  newe->whence.tv_sec += seconds;
  newe->mask = EVENTER_TIMER;
  newe->callback = setup_iep_connection_callback;
  newe->closure = NULL;
  eventer_add(newe);
}

static int
stratcon_iep_submitter(eventer_t e, int mask, void *closure,
                       struct timeval *now) {
  double age;
  struct iep_job_closure *job = closure;
  char *line;
  struct timeval diff;
  /* We only play when it is an asynch event */
  if(!(mask & EVENTER_ASYNCH_WORK)) return 0;

  if(mask & EVENTER_ASYNCH_CLEANUP) {
    /* free all the memory associated with the batch */
    if(job) {
      if(job->line) free(job->line);
      if(job->remote) free(job->remote);
      if(job->doc_str) free(job->doc_str);
      free(job);
    }
    return 0;
  }

  /* If we're greater than 30 seconds old,
     just quit. */
  sub_timeval(*now, job->start, &diff);
  if (diff.tv_sec >= 30) {
    mtevL(noit_debug, "Skipping event from %s - waiting in eventer for more than 30 seconds\n",
                        job->remote ? job->remote : "(null)");
    return 0;
  }

  if(!job->line || job->line[0] == '\0') return 0;

  if((age = stratcon_iep_age_from_line(job->line, *now)) > 60) {
    mtevL(noit_debug, "Skipping old event from %s, %f seconds old.\n",
          job->remote ? job->remote : "(null)", age);
    return 0;
  }
  /* Submit */
  int line_len = strlen(job->line);
  int remote_len = strlen(job->remote);
  const char *toff = strchr(job->line, '\t');
  int token_off = 2;
  if(toff) token_off = toff - job->line + 1;

  line = (char*)calloc(line_len + 1 /* \t */ + remote_len + 2, 1);
  strncpy(line, job->line, token_off);
  strncat(line, job->remote, remote_len);
  strncat(line, "\t", 1);
  strncat(line, job->line + token_off, line_len - token_off);
  job->doc_str = line;

  for(struct driver_list *d = drivers; d; d = d->next) {
    struct driver_thread_data *tls = connect_iep_driver(d);
    if(tls && tls->driver_data) {
      if(tls->mq_driver->submit(tls->driver_data, job->doc_str,
                                line_len + remote_len + 1) != 0) {
        mtevL(noit_debug, "failed to MQ submit.\n");
      }
    }
  }
  return 0;
}

void
stratcon_iep_line_processor(stratcon_datastore_op_t op,
                            struct sockaddr *remote, const char *remote_cn,
                            void *operand, eventer_t completion) {
  int len;
  char remote_str[256];
  struct iep_job_closure *jc;
  eventer_t newe;
  /* We only care about inserts */

  if(op == DS_OP_CHKPT) {
    if(completion) eventer_add(completion);
    return;
  }
  if(op != DS_OP_INSERT) return;

  if(inject_remote_cn) {
    if(remote_cn == NULL) remote_cn = "default";
    strlcpy(remote_str, remote_cn, sizeof(remote_str));
  }
  else {
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
          snprintf(remote_str, sizeof(remote_str), "%s", ((struct sockaddr_un *)remote)->sun_path);
          break;
      }
    }
  }

  /* process operand and push onto queue */
  newe = eventer_alloc();
  newe->thr_owner = eventer_choose_owner(0);
  newe->mask = EVENTER_ASYNCH;
  newe->callback = stratcon_iep_submitter;
  jc = calloc(1, sizeof(*jc));
  jc->line = operand;
  jc->remote = strdup(remote_str);
  mtev_gettimeofday(&jc->start, NULL);
  newe->closure = jc;

  eventer_add_asynch(iep_jobq, newe);
}

static void connection_destroy(void *vd) {
  struct driver_thread_data *data = vd;
  data->mq_driver->disconnect(data->driver_data);
  data->mq_driver->deallocate(data->driver_data);
  free(data);
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
    kill(info->child, SIGKILL);
    if(waitpid(info->child, &rv, 0) != info->child) {
      mtevL(noit_iep, "Failed to reap IEP daemon\n");
      exit(-1);
    }
    mtevL(noit_iep, "IEP daemon is done, starting a new one\n");
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
    mtevAssert(len < sizeof(buff));
    buff[len] = '\0';
    mtevL(noit_iep_debug, "%s", buff);
  }
}

static void
start_iep_daemon() {
  eventer_t newe;
  struct iep_daemon_info *info;
  char *cmd = NULL;

  if(!mtev_conf_get_string(NULL, "/stratcon/iep/start/@command",
                           &cmd)) {
    mtevL(noit_iep, "No IEP start command provided.  You're on your own.\n");
    setup_iep_connection_later(0);
    return;
  }

  info = calloc(1, sizeof(*info));
  info->stdin_pipe[0] = info->stdin_pipe[1] = -1;
  info->stderr_pipe[0] = info->stderr_pipe[1] = -1;
  info->command = cmd;

  if(!mtev_conf_get_string(NULL, "/stratcon/iep/start/@directory",
                           &info->directory))
    info->directory = strdup(".");
  if(pipe(info->stdin_pipe) != 0 ||
     pipe(info->stderr_pipe) != 0) {
    mtevL(noit_iep, "pipe: %s\n", strerror(errno));
    goto bail;
  }
  info->child = fork();
  if(info->child == -1) {
    mtevL(noit_iep, "fork: %s\n", strerror(errno));
    goto bail;
  }
  if(info->child == 0) {
    char *argv[3] = { "run-iep", NULL, NULL };
    int stdout_fileno;

    argv[1] = mtev_conf_config_filename();

    if(chdir(info->directory) != 0) {
      mtevL(noit_iep, "Starting IEP daemon, chdir failed: %s\n",
            strerror(errno));
      exit(-1);
    }

    close(info->stdin_pipe[1]);
    close(info->stderr_pipe[0]);
    dup2(info->stdin_pipe[0], 0);
    dup2(info->stderr_pipe[1], 2);
    stdout_fileno = open("/dev/null", O_WRONLY);
    if(stdout_fileno >= 0) dup2(stdout_fileno, 1);

    exit(execv(info->command, argv));
  }
  /* in the parent */
  close(info->stdin_pipe[0]);
  info->stdin_pipe[0] = -1;
  close(info->stderr_pipe[1]);
  info->stderr_pipe[1] = -1;
  if(eventer_set_fd_nonblocking(info->stderr_pipe[0])) {
    goto bail;
  }

  newe = eventer_alloc();
  newe->fd = info->stderr_pipe[0];
  newe->mask = EVENTER_READ | EVENTER_EXCEPTION;
  newe->callback = stratcon_iep_err_handler;
  newe->closure = info;
  eventer_add(newe);
  info = NULL;

  setup_iep_connection_later(1);

  return;

 bail:
  iep_daemon_info_free(info);
  mtevL(noit_iep, "Failed to start IEP daemon\n");
  exit(-1);
  return;
}

void
stratcon_iep_mq_driver_register(const char *name, mq_driver_t *d) {
  mtev_hash_replace(&mq_drivers, strdup(name), strlen(name), d, free, NULL);
}

static int rest_set_filters(mtev_http_rest_closure_t *restc,
                            int npats, char **pats) {
  mtev_http_session_ctx *ctx = restc->http_ctx;
  xmlXPathObjectPtr pobj = NULL;
  xmlDocPtr doc = NULL, indoc = NULL;
  xmlNodePtr node, root;
  int error_code = 500, complete = 0, mask = 0, cnt = 0, i, j;
  const char *error = "internal error";
  xmlXPathContextPtr xpath_ctxt;
  char *action = NULL, *uuid = NULL;
  char xpath[1024];
  mq_command_t *commands = NULL;
  bool valid = true;

  if (npats != 0) goto error;

  indoc = rest_get_xml_upload(restc, &mask, &complete);
  if(!complete) return mask;
  if(indoc == NULL) {
    error = "xml parse error";
    goto error;
  }

  xpath_ctxt = xmlXPathNewContext(indoc);
  pobj = xmlXPathEval((xmlChar *)"/mq_metrics/check", xpath_ctxt);

  if(!pobj) {
    error = "xml format incorrect";
    goto error;
  }
  if(pobj->type != XPATH_NODESET) {
    error = "couldn't find xml nodeset";
    goto error;
  }
  cnt = xmlXPathNodeSetGetLength(pobj->nodesetval);
  if (cnt <= 0) {
    error = "no nodes given";
    goto error;
  }

  commands = calloc(cnt, sizeof(mq_command_t));
  if (!commands) goto error;

  for(i=0; i<cnt; i++) {
    xmlXPathObjectPtr metric_obj = NULL;

    if (!valid) break;
    node = xmlXPathNodeSetItem(pobj->nodesetval, i);
    action = (char *)xmlGetProp(node, (xmlChar *)"action");
    uuid = (char *)xmlGetProp(node, (xmlChar *)"uuid");
    if ((action == NULL) || (uuid == NULL)) {
      mtevL(mtev_error, "error parsing %d - need both action and uuid\n", i);
      if (action) xmlFree(action);
      if (uuid) xmlFree(uuid);
      valid = false;
      continue;
    }
    if (!strncmp(action, "set", 3)) {
      commands[i].action = MQ_ACTION_SET;
    }
    else if (!strncmp(action, "forget", 6)) {
      commands[i].action = MQ_ACTION_FORGET;
    }
    else {
      mtevL(mtev_error, "error parsing %d - bad action (%s)\n", i, action);
      if (action) xmlFree(action);
      if (uuid) xmlFree(uuid);
      valid = false;
      continue;
    }
    commands[i].check.uuid = uuid;
    snprintf(xpath, sizeof(xpath), "/mq_metrics/check[%d]/metrics/metric", i+1);
    metric_obj = xmlXPathEval((xmlChar *)xpath, xpath_ctxt);
    if (metric_obj && metric_obj->type == XPATH_NODESET) {
      commands[i].check.metric_count = xmlXPathNodeSetGetLength(metric_obj->nodesetval);
      if (commands[i].check.metric_count > 0) {
        char *metric_name = NULL;
        commands[i].check.metrics = calloc(commands[i].check.metric_count, sizeof(char*));
        for (j=0; j < commands[i].check.metric_count; j++) {
          xmlNodePtr metric_node = NULL;
          if (!valid) break;
          metric_node = xmlXPathNodeSetItem(metric_obj->nodesetval, j);
          metric_name = (char *)xmlGetProp(metric_node, (xmlChar *)"name");
          if (!metric_name) {
            valid = false;
            continue;
          }
          commands[i].check.metrics[j] = metric_name;
        }
      }
    }
  }

  if (!valid) {
    goto error;
  }

  for(struct driver_list *d = drivers; d; d = d->next) {
    if (d->mq_driver) {
      d->mq_driver->set_filters(commands, cnt);
    }
  }

  mtev_http_response_ok(restc->http_ctx, "text/xml");
  mtev_http_response_end(restc->http_ctx);
  goto cleanup;

 error:
  mtev_http_response_standard(ctx, error_code, "ERROR", "text/xml");
  doc = xmlNewDoc((xmlChar *)"1.0");
  root = xmlNewDocNode(doc, NULL, (xmlChar *)"error", NULL);
  xmlDocSetRootElement(doc, root);
  xmlNodeAddContent(root, (xmlChar *)error);
  mtev_http_response_xml(ctx, doc);
  mtev_http_response_end(ctx);

 cleanup:
  if (commands) {
    for (i=0; i<cnt; i++) {
      mq_command_free(&commands[i]);
    }
    free(commands);
  }
  return 0;
}

void
stratcon_iep_init() {
  mtev_conf_section_t *mqs;
  int i, cnt;
  mtev_boolean disabled = mtev_false;
  char mq_type[128] = "stomp";
  /* Only 32 so we can print out a reasonable length bad value */
  char remote[32] = "ip";
  struct driver_list *newdriver;
  void *vdriver;

  noit_iep = mtev_log_stream_find("error/iep");
  noit_iep_debug = mtev_log_stream_find("debug/iep");
  if(!noit_iep) noit_iep = noit_error;
  if(!noit_iep_debug) noit_iep_debug = noit_debug;

  if(mtev_conf_get_stringbuf(NULL, "/stratcon/iep/@inject_remote", remote, sizeof(remote))) {
    if(strcmp(remote, "ip") && strcmp(remote, "cn")) {
      mtevL(noit_iep, "Bad @remote_inject \"%s\", setting to \"cn\"\n", remote);
      strlcpy(remote, "ip", sizeof(remote));
    }
  }
  if(!strcmp(remote, "ip")) inject_remote_cn = mtev_false;
  else if(!strcmp(remote, "cn")) inject_remote_cn = mtev_true;

  if(mtev_conf_get_boolean(NULL, "/stratcon/iep/@disabled", &disabled) &&
     disabled == mtev_true) {
    mtevL(noit_iep, "IEP system is disabled!\n");
    return;
  }

  mqs = mtev_conf_get_sections(NULL, "/stratcon/iep/mq", &cnt);
  for(i=0; i<cnt; i++) {
    if(!mtev_conf_get_stringbuf(mqs[i], "@type",
                                mq_type, sizeof(mq_type))) {
      mtevL(noit_iep, "You must specify an <mq type=\"...\"> that is valid.\n");
      exit(-2);
    }
    if(!mtev_hash_retrieve(&mq_drivers, mq_type, strlen(mq_type), &vdriver) ||
       vdriver == NULL) {
      mtevL(noit_iep, "Cannot find MQ driver type: %s\n", mq_type);
      mtevL(noit_iep, "Did you forget to load a module?\n");
      exit(-2);
    }
    newdriver = calloc(1, sizeof(*newdriver));
    newdriver->section = mqs[i];
    newdriver->mq_driver = (mq_driver_t *)vdriver;
    pthread_key_create(&newdriver->iep_connection, connection_destroy);
    newdriver->next = drivers;
    drivers = newdriver;
  }
  free(mqs);

  eventer_name_callback("stratcon_iep_submitter", stratcon_iep_submitter);
  eventer_name_callback("stratcon_iep_err_handler", stratcon_iep_err_handler);
  eventer_name_callback("setup_iep_connection_callback", setup_iep_connection_callback);

  /* start up a thread pool of one */
  
  iep_jobq = eventer_jobq_create("iep_submitter");
  eventer_jobq_set_concurrency(iep_jobq, 1);
  eventer_jobq_set_min_max(iep_jobq, 1, 1);

  mtevAssert(mtev_http_rest_register_auth(
    "PUT", "/", "^mq_filters$",
    rest_set_filters, mtev_http_rest_client_cert_auth
  ) == 0);

  start_iep_daemon();

  /* setup our live jlog stream */
  stratcon_streamer_connection(NULL, NULL, "noit",
                               stratcon_jlog_recv_handler,
                               (void *(*)())stratcon_jlog_streamer_iep_ctx_alloc,
                               NULL,
                               jlog_streamer_ctx_free);
}

void
stratcon_iep_init_globals(void) {
  mtev_hash_init(&mq_drivers);
}

