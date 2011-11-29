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
#include "noit_xml.h"
#include "utils/noit_log.h"
#include "utils/noit_mkdir.h"
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

struct noit_xml_buffer_ptr {
  char *buff;
  int raw_len;
  int len;
  int allocd;
};
static int
noit_xml_save_writer(void *vstr, const char *buffer, int len) {
  struct noit_xml_buffer_ptr *clv = vstr;
  if(!clv->buff) {
    clv->allocd = 8192;
    clv->buff = malloc(clv->allocd);
  }
  while(len + clv->len > clv->allocd) {
    char *newbuff;
    int newsize = clv->allocd;
    newsize <<= 1;
    newbuff = realloc(clv->buff, newsize);
    if(!newbuff) {
      return -1;
    }
    clv->allocd = newsize;
    clv->buff = newbuff;
  }
  memcpy(clv->buff + clv->len, buffer, len);
  clv->len += len;
  return len;
}
static int
noit_xml_save_closer(void *vstr) {
  struct noit_xml_buffer_ptr *clv = vstr;
  if(clv->buff == NULL) return 0;
  clv->buff[clv->len] = '\0';
  return 0;
}

char *
noit_xmlSaveToBuffer(xmlDocPtr doc) {
  xmlOutputBufferPtr out;
  xmlCharEncodingHandlerPtr enc;
  struct noit_xml_buffer_ptr buf = { NULL,0,0,0 };

  enc = xmlGetCharEncodingHandler(XML_CHAR_ENCODING_UTF8);
  out = xmlOutputBufferCreateIO(noit_xml_save_writer,
                                noit_xml_save_closer,
                                &buf, enc);
  assert(out);
  xmlSaveFormatFileTo(out, doc, "utf8", 1);
  return buf.buff;
}

int
noit_xmlSaveToFile(xmlDocPtr doc, const char *filename) {
  int rv = -1, fd, have_backup = 0, once;
  char tmpfile[PATH_MAX], bakfile[PATH_MAX];
  xmlOutputBufferPtr out;
  xmlCharEncodingHandlerPtr enc;

  snprintf(tmpfile, sizeof(tmpfile), "%s.tmp", filename);
  snprintf(bakfile, sizeof(bakfile), "%s.bak", filename);
  once = 1;
 mkbkup:
  if(link(filename, bakfile) == 0) {
    unlink(filename);
    have_backup = 1;
  }
  else if (errno != ENOENT) {
    if(!once) return -1;
    once = 0;
    if(errno == EEXIST) {
      unlink(bakfile);
      goto mkbkup;
    }
    noitL(noit_error, "Cannot create backup for %s: %s\n",
          filename, strerror(errno));
    return -1;
  }
  else {
    noitL(noit_debug, "link(%s,%s) -> %s\n", filename, bakfile, strerror(errno));
  }

 retry:
  fd = open(tmpfile, O_WRONLY | O_CREAT | O_EXCL, 0640);
  if(fd < 0) {
    if(!once) return -1;
    once = 0;
    if(errno == ENOENT) {
      if(mkdir_for_file(tmpfile, 0750) == 0) goto retry;
    }
    if(errno == EEXIST) {
      unlink(tmpfile);
      goto retry;
    }
    return -1;
  }
  enc = xmlGetCharEncodingHandler(XML_CHAR_ENCODING_UTF8);
  out = xmlOutputBufferCreateFd(fd, enc);
  assert(out);
  xmlSaveFormatFileTo(out, doc, "utf8", 1);
  close(fd);
  rv = 0;
  if(link(tmpfile, filename)) {
    rv = -1;
    if(link(bakfile, filename) != 0) have_backup = 0;
  }
  unlink(tmpfile);
  if(have_backup)
    unlink(bakfile);
  return rv;
}
