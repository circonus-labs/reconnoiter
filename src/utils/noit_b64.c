/*
 * Copyright (c) 2005-2007, OmniTI Computer Consulting, Inc.
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
#include "noit_b64.h"
#include <ctype.h>

static const char __b64[] = {
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
  'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
  'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
  'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
  '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/', 0x00 };

int
noit_b64_decode(const char *src, size_t src_len,
                unsigned char *dest, size_t dest_len) {
  const unsigned char *cp = (unsigned char *)src;
  unsigned char *dcp = dest;
  unsigned char ch, in[4], out[3];
  int ib = 0, ob = 3, needed = (((src_len / 4) * 3) - 2);

  if(dest_len < needed) return 0;
  while(cp <= ((unsigned char *)src+src_len)) {
    if((*cp >= 'A') && (*cp <= 'Z')) ch = *cp - 'A';
    else if((*cp >= 'a') && (*cp <= 'z')) ch = *cp - 'a' + 26;
    else if((*cp >= '0') && (*cp <= '9')) ch = *cp - '0' + 52;
    else if(*cp == '+') ch = 62;
    else if(*cp == '/') ch = 63;
    else if(*cp == '=') ch = 0xff;
    else if(isspace((int)*cp)) { cp++; continue; }
    else break;
    cp++;
    if(ch == 0xff) {
      if(ib == 0) break;
      if(ib == 1 || ib == 2) ob = 1;
      else ob = 2;
      ib = 3;
    }
    in[ib++] = ch;
    if(ib == 4) {
      out[0] = (in[0] << 2) | ((in[1] & 0x30) >> 4);
      out[1] = ((in[1] & 0x0f) << 4) | ((in[2] & 0x3c) >> 2);
      out[2] = ((in[2] & 0x03) << 6) | (in[3] & 0x3f);
      for(ib = 0; ib < ob; ib++)
        *dcp++ = out[ib];
      ib = 0;
    }
  }
  return dcp - (unsigned char *)dest;
}

int
noit_b64_encode(const unsigned char *src, size_t src_len,
                char *dest, size_t dest_len) {
  const unsigned char *bptr = src;
  char *eptr = dest;
  int len = src_len;
  int n = (((src_len + 2) / 3) * 4);

  if(dest_len < n) return 0;

  while(len > 2) {
    *eptr++ = __b64[bptr[0] >> 2];
    *eptr++ = __b64[((bptr[0] & 0x03) << 4) + (bptr[1] >> 4)];
    *eptr++ = __b64[((bptr[1] & 0x0f) << 2) + (bptr[2] >> 6)];
    *eptr++ = __b64[bptr[2] & 0x3f];
    bptr += 3;
    len -= 3;
  }
  if(len != 0) {
    *eptr++ = __b64[bptr[0] >> 2];
    if(len > 1) {
      *eptr++ = __b64[((bptr[0] & 0x03) << 4) + (bptr[1] >> 4)];
      *eptr++ = __b64[(bptr[1] & 0x0f) << 2];
      *eptr++ = '=';
    } else {
      *eptr++ = __b64[(bptr[0] & 0x03) << 4];
      *eptr++ = '=';
      *eptr++ = '=';
    }
  }
  return n;
}

