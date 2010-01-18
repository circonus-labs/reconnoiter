/*
 * Copyright (c) 2010, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *    * Neither the name OmniTI Computer Consulting, Inc. nor the names
 *      of its contributors may be used to endorse or promote products
 *      derived from this software without specific prior written
 *      permission.
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

#include "noit_config.h"
#include "utils/noit_getip.h"
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>

int
noit_getip_ipv4(struct in_addr remote, struct in_addr *local) {
  int s;
  struct sockaddr_in r, l;
  socklen_t llen;
  s = socket(PF_INET, SOCK_DGRAM, 17); /* 17 is UDP */
  if(s < 0) return -1;
  memset(&r, 0, sizeof(r));
  r.sin_port = htons(53); /* DNS port, it's fairly standard. */
  r.sin_addr = remote;
  r.sin_family = AF_INET;
  if(connect(s, (struct sockaddr *)&r, sizeof(r)) < 0) {
    close(s);
    return -1;
  }
  llen = sizeof(l);
  if(getsockname(s, (struct sockaddr *)&l, &llen) < 0) {
    close(s);
    return -1;
  }
  close(s);
  *local = l.sin_addr;
  return 0;
}
