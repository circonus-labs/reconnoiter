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

static const char __b32[] = {
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J',
  'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U',
  'V', 'W', 'X', 'Y', 'Z', '2', '3', '4', '5', '6', '7', 0x00 };

static const unsigned char __ub32[] = {
   0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe,
   0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe,
   0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe,
   0xfe, 0xfe, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xff, 0xfe, 0xfe,
   0xfe, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e,
   0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe,
   0xfe, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e,
   0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe,
   0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe,
   0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe,
   0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe,
   0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe,
   0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe,
   0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe,
   0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe,
   0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe };

int
noit_b32_decode(const char *src, size_t src_len,
                unsigned char *dest, size_t dest_len) {
  const unsigned char *cp = (unsigned char *)src;
  unsigned char *dcp = dest;
  unsigned char ch, in[8] = { 0 }, out[5];
  int ib = 0, ob = 3, needed = ((src_len / 8) * 5);

  if(dest_len < needed) return 0;
  while(cp <= ((unsigned char *)src+src_len)) {
    if(isspace((int)*cp)) { cp++; continue; }
    if(*cp == '-') { cp++; continue; }
    ch = __ub32[*cp];
    if(ch == 0xfe) break;
    cp++;
    if(ch == 0xff) {
      if(ib == 0) break;
      if(ib == 1 || ib == 2) ob = 1;
      else if(ib == 3 || ib == 4) ob = 2;
      else if(ib == 4 || ib == 5) ob = 3;
      else ob = 4;
      ib = 7;
    }
    in[ib++] = ch;
    if(ib == 8) {
      out[0] = (in[0] << 3) | (in[1] >> 2);
      out[1] = ((in[1] & 0x3) << 6) | (in[2] << 1) | (in[3] >> 4);
      out[2] = ((in[3] & 0xf) << 4) | (in[4] >> 1);
      out[3] = ((in[4] & 0x1) << 7) | (in[5] << 2) | (in[6] >> 3);
      out[4] = ((in[6] & 0x7) << 5) | in[7];
      for(ib = 0; ib < ob; ib++)
        *dcp++ = out[ib];
      ib = 0;
      memset(in, 0, sizeof(in));
    }
  }
  return dcp - (unsigned char *)dest;
}

int
noit_b32_encode(const unsigned char *src, size_t src_len,
                char *dest, size_t dest_len) {
  const unsigned char *bptr = src;
  char *eptr = dest;
  int len = src_len;
  int i, n = (((src_len + 4) / 5) * 8);

  if(dest_len < n) return 0;

  while(len > 4) {
    *eptr++ = __b32[bptr[0] >> 3];                             /* [ xxxxx000 00000000 00000000 00000000 00000000 ] */
    *eptr++ = __b32[((bptr[0] & 0x07) << 2) + (bptr[1] >> 6)]; /* [ 00000xxx xx000000 00000000 00000000 00000000 ] */
    *eptr++ = __b32[(bptr[1] & 0x3e) >> 1];                    /* [ 00000000 00xxxxx0 00000000 00000000 00000000 ] */
    *eptr++ = __b32[((bptr[1] & 0x1) << 4) + (bptr[2] >> 4)];  /* [ 00000000 0000000x xxxx0000 00000000 00000000 ] */
    *eptr++ = __b32[((bptr[2] & 0xf) << 1) + (bptr[3] >> 7)];  /* [ 00000000 00000000 0000xxxx x0000000 00000000 ] */
    *eptr++ = __b32[(bptr[3] & 0x7c) >> 2];                    /* [ 00000000 00000000 00000000 0xxxxx00 00000000 ] */
    *eptr++ = __b32[((bptr[3] & 0x3) << 3) + (bptr[4] >> 5)];  /* [ 00000000 00000000 00000000 000000xx xxx00000 ] */
    *eptr++ = __b32[bptr[4] & 0x1f];                           /* [ 00000000 00000000 00000000 00000000 000xxxxx ] */
    bptr += 5;
    len -= 5;
  }
  if(len != 0) {
    *eptr++ = __b32[bptr[0] >> 3];
    if(len == 1) {
      *eptr++ = __b32[(bptr[0] & 0x07) << 2];
      for(i=0;i<5;i++) *eptr++ = '=';
    }
    else {
      *eptr++ = __b32[((bptr[0] & 0x07) << 2) + (bptr[1] >> 6)];
      *eptr++ = __b32[(bptr[1] & 0x3e) >> 1];
      if(len == 2) {
        *eptr++ = __b32[(bptr[1] & 0x1) << 4];
        for(i=0;i<3;i++) *eptr++ = '=';
      }
      else {
        *eptr++ = __b32[((bptr[1] & 0x1) << 4) + (bptr[2] >> 4)];
        if(len == 3) {
          *eptr++ = __b32[(bptr[2] & 0xf) << 1];
          *eptr++ = '=';
          *eptr++ = '=';
        }
        else {
          *eptr++ = __b32[((bptr[2] & 0xf) << 1) + (bptr[3] >> 7)];
          *eptr++ = __b32[(bptr[3] & 0x7c) >> 2];
          *eptr++ = __b32[(bptr[3] & 0x3) << 3];
        }
      }
    }
    *eptr = '=';
  }
  return n;
}

