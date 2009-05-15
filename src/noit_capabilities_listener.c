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
#include "noit_listener.h"
#include "utils/noit_hash.h"
#include "utils/noit_log.h"
#include "utils/noit_sem.h"
#include "noit_capabilities_listener.h"
#include "noit_module.h"
#include "noit_check.h"
#include "noit_xml.h"

#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>

#include <libxml/xmlsave.h>
#include <libxml/tree.h>

typedef struct noit_capsvc_closure {
  char *buff;
  size_t written;
  size_t towrite;
} noit_capsvc_closure_t;

void
noit_capabilities_listener_init() {
  eventer_name_callback("capabilities_transit/1.0", noit_capabilities_handler);
  noit_control_dispatch_delegate(noit_control_dispatch,
                                 NOIT_CAPABILITIES_SERVICE,
                                 noit_capabilities_handler);
}

int
noit_capabilities_handler(eventer_t e, int mask, void *closure,
                          struct timeval *now) {
  int newmask = EVENTER_WRITE | EVENTER_EXCEPTION;
  acceptor_closure_t *ac = closure;
  noit_capsvc_closure_t *cl = ac->service_ctx;

  if(mask & EVENTER_EXCEPTION) {
socket_error:
    /* Exceptions cause us to simply snip the connection */
cleanup_shutdown:
    eventer_remove_fd(e->fd);
    e->opset->close(e->fd, &newmask, e);
    if(cl) {
      if(cl->buff) free(cl->buff);
      free(cl);
    }
    if(ac) acceptor_closure_free(ac);
    return 0;
  }

  if(!ac->service_ctx) {
    char vbuff[128];
    noit_hash_table *lc;
    noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
    const char *k;
    int klen;
    void *data;

    xmlDocPtr xmldoc;
    xmlNodePtr root, cmds;

    cl = ac->service_ctx = calloc(1, sizeof(*cl));
    /* fill out capabilities */
    noit_build_version(vbuff, sizeof(vbuff));

    /* Create an XML Document */
    xmldoc = xmlNewDoc((xmlChar *)"1.0");
    root = xmlNewDocNode(xmldoc, NULL, (xmlChar *)"noitd_capabilities", NULL);
    xmlDocSetRootElement(xmldoc, root);

    /* Fill in the document */
    xmlNewTextChild(root, NULL, (xmlChar *)"version", (xmlChar *)vbuff);

    cmds = xmlNewNode(NULL, (xmlChar *)"services");
    xmlAddChild(root, cmds);
    lc = noit_listener_commands();
    while(noit_hash_next(lc, &iter, &k, &klen, &data)) {
      xmlNodePtr cnode;
      char hexcode[10];
      const char *name;
      eventer_func_t *f = (eventer_func_t *)k;
      noit_hash_table *sc = (noit_hash_table *)data;
      noit_hash_iter sc_iter = NOIT_HASH_ITER_ZERO;
      const char *sc_k;
      int sc_klen;
      void *sc_data;

      name = eventer_name_for_callback(*f);
      cnode = xmlNewNode(NULL, (xmlChar *)"service");
      xmlSetProp(cnode, (xmlChar *)"name", name ? (xmlChar *)name : NULL);
      if(*f == ac->dispatch)
        xmlSetProp(cnode, (xmlChar *)"connected", (xmlChar *)"true");
      xmlAddChild(cmds, cnode);
      while(noit_hash_next(sc, &sc_iter, &sc_k, &sc_klen, &sc_data)) {
        xmlNodePtr scnode;
        char *name_copy, *version = NULL;
        eventer_func_t *f = (eventer_func_t *)sc_data;

        snprintf(hexcode, sizeof(hexcode), "0x%08x", *((u_int32_t *)sc_k));
        name = eventer_name_for_callback(*f);
        name_copy = strdup(name ? name : "[[unknown]]");
        version = strchr(name_copy, '/');
        if(version) *version++ = '\0';

        scnode = xmlNewNode(NULL, (xmlChar *)"command");
        xmlSetProp(scnode, (xmlChar *)"name", (xmlChar *)name_copy);
        if(version)
          xmlSetProp(scnode, (xmlChar *)"version", (xmlChar *)version);
        xmlSetProp(scnode, (xmlChar *)"code", (xmlChar *)hexcode);
        xmlAddChild(cnode, scnode);
        free(name_copy);
      }
    }

    /* Write it out to a buffer and copy it for writing */
    cl->buff = noit_xmlSaveToBuffer(xmldoc);
    cl->towrite = strlen(cl->buff);

    /* Clean up after ourselves */
    xmlFreeDoc(xmldoc);
  }

  while(cl->towrite > cl->written) {
    int len;
    while((len = e->opset->write(e->fd, cl->buff + cl->written,
                                 cl->towrite - cl->written,
                                 &newmask, e)) == -1 && errno == EINTR);
    if(len < 0) {
      if(errno == EAGAIN) return newmask | EVENTER_EXCEPTION;
      goto socket_error;
    }
    cl->written += len;
  }
  goto cleanup_shutdown;

  return 0;
}
