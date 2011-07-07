/*
 * Copyright (c) 2011, OmniTI Computer Consulting, Inc.
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
#include "noit_check_log_helpers.h"
#include <zlib.h>
#include "utils/noit_b64.h"
#include "utils/noit_log.h"

int
noit_check_log_bundle_compress_b64(noit_compression_type_t ctype,
                                   const char *buf_in,
                                   unsigned int len_in,
                                   char ** buf_out,
                                   unsigned int * len_out) {
  uLong initial_dlen, dlen;
  char *compbuff, *b64buff;

  // Compress saves 25% of space (ex 470 -> 330)
  switch(ctype) {
    case NOIT_COMPRESS_ZLIB:
      /* Compress */
      initial_dlen = dlen = compressBound((uLong)len_in);
      compbuff = malloc(initial_dlen);
      if(!compbuff) return -1;
      if(Z_OK != compress2((Bytef *)compbuff, &dlen,
                           (Bytef *)buf_in, len_in, 9)) {
        noitL(noit_error, "Error compressing bundled metrics.\n");
        free(compbuff);
        return -1;
      }
      break;
    case NOIT_COMPRESS_NONE:
      // Or don't
      dlen = (uLong)len_in;
      compbuff = (char *)buf_in;
      break;
  }

  /* Encode */
  // Problems with the calculation?
  initial_dlen = ((dlen + 2) / 3 * 4);
  b64buff = malloc(initial_dlen);
  if (!b64buff) {
    if(ctype == NOIT_COMPRESS_ZLIB) free(compbuff);
    return -1;
  }
  dlen = noit_b64_encode((unsigned char *)compbuff, dlen,
                         (char *)b64buff, initial_dlen);
  if(ctype == NOIT_COMPRESS_ZLIB) free(compbuff);
  if(dlen == 0) {
    noitL(noit_error, "Error base64'ing bundled metrics.\n");
    free(b64buff);
    return -1;
  }
  *buf_out = b64buff;
  *len_out = (unsigned int)dlen;
  return 0;
}

int
noit_check_log_bundle_decompress_b64(noit_compression_type_t ctype,
                                     const char *buf_in,
                                     unsigned int len_in,
                                     char *buf_out,
                                     unsigned int len_out) {
  int rv;
  uLong initial_dlen, dlen, rawlen;
  char *compbuff, *rawbuff;

  /* Decode */
  initial_dlen = (((len_in / 4) * 3) - 2);
  compbuff = malloc(initial_dlen);
  if (!compbuff) return -1;
  dlen = noit_b64_decode((char *)buf_in, len_in,
                         (unsigned char *)compbuff, initial_dlen);
  if(dlen == 0) {
    noitL(noit_error, "Error base64'ing bundled metrics.\n");
    free(compbuff);
    return -1;
  }

  switch(ctype) {
    case NOIT_COMPRESS_ZLIB:
      /* Decompress */
      rawlen = len_out;
      if(Z_OK != (rv = uncompress((Bytef *)buf_out, &rawlen,
                                  (Bytef *)compbuff, dlen)) ||
         rawlen != len_out) {
        noitL(noit_error, "Error decompressing bundle: %d (%u != %u).\n",
              rv, (unsigned int)rawlen, (unsigned int)len_out);
        free(compbuff);
        return -1;
      }
      break;
    case NOIT_COMPRESS_NONE:
      // Or don't
      rawlen = (uLong)dlen;
      rawbuff = compbuff;
      if(rawlen != len_out) return -1;
      memcpy(buf_out, rawbuff, rawlen);
      break;
  }

  return 0;
}
