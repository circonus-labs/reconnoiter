/*
 * Copyright (c) 2005-2009  Florian Forster
 * Copyright (c) 2009, OmniTI Computer Consulting, Inc.
 * Copyright (c) 2009, Dan Di Spaltro
 * All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library, in file named COPYING.LGPL; if not,
 * write to the Free Software Foundation, Inc., 59 Temple Place,
 * Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "noit_defines.h"

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <math.h>
#include <ctype.h>
#ifdef HAVE_SYS_FILIO_H
#include <sys/filio.h>
#endif
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <inttypes.h>
#include <arpa/inet.h>

#include "noit_module.h"
#include "noit_check.h"
#include "noit_check_tools.h"
#include "utils/noit_log.h"
#include "utils/noit_hash.h"


static noit_log_stream_t nlerr = NULL;
static noit_log_stream_t nldeb = NULL;
static int __collectd_initialize_once = 0;


typedef struct _mod_config {
  noit_hash_table *options;
  noit_hash_table target_sessions;
  int ipv4_fd;
  int ipv6_fd;
} collectd_mod_config_t;

typedef struct collectd_closure_s {
  char *username;
  char *secret;
  int security_level;
  EVP_CIPHER_CTX ctx; 
  stats_t current;
  int stats_count;
  int ntfy_count;
} collectd_closure_t;

#define cmp_plugin(vl, val) \
  (strncmp(vl->plugin, val, sizeof(val)) == 0) 

#define cmp_type(vl, plugin, type) \
  (cmp_plugin(vl, plugin) && \
   cmp_plugin(vl, type)) 


/**
 *
 * Collectd Structs
 *
 *
 **/

union meta_value_u
{
  char    *mv_string;
  int64_t  mv_signed_int;
  uint64_t mv_unsigned_int;
  double   mv_double;
  _Bool    mv_boolean;
};
typedef union meta_value_u meta_value_t;

struct meta_entry_s;
typedef struct meta_entry_s meta_entry_t;
struct meta_entry_s
{
  char         *key;
  meta_value_t  value;
  int           type;
  meta_entry_t *next;
};

struct meta_data_s
{
  meta_entry_t   *head;
  pthread_mutex_t lock;
};

typedef struct meta_data_s meta_data_t;


#define NET_DEFAULT_V4_ADDR "239.192.74.66"
#define NET_DEFAULT_V6_ADDR "ff18::efc0:4a42"
#define NET_DEFAULT_PORT    "25826"

#define TYPE_HOST            0x0000
#define TYPE_TIME            0x0001
#define TYPE_PLUGIN          0x0002
#define TYPE_PLUGIN_INSTANCE 0x0003
#define TYPE_TYPE            0x0004
#define TYPE_TYPE_INSTANCE   0x0005
#define TYPE_VALUES          0x0006
#define TYPE_INTERVAL        0x0007

/* Types to transmit notifications */
#define TYPE_MESSAGE         0x0100
#define TYPE_SEVERITY        0x0101

#define TYPE_SIGN_SHA256     0x0200
#define TYPE_ENCR_AES256     0x0210

#define DATA_MAX_NAME_LEN 64

#define NOTIF_MAX_MSG_LEN 256
#define NOTIF_FAILURE 1
#define NOTIF_WARNING 2
#define NOTIF_OKAY    4

#define sfree(ptr) \
  do { \
    if((ptr) != NULL) { \
      free(ptr); \
    } \
    (ptr) = NULL; \
  } while (0)

#define sstrncpy(a, b, c) strncpy(a, b, c)

#define SECURITY_LEVEL_NONE     0
#define SECURITY_LEVEL_SIGN    1
#define SECURITY_LEVEL_ENCRYPT 2

#define DS_TYPE_COUNTER  0
#define DS_TYPE_GAUGE    1
#define DS_TYPE_DERIVE   2
#define DS_TYPE_ABSOLUTE 3

#define DS_TYPE_TO_STRING(t) (t == DS_TYPE_COUNTER)     ? "counter"  : \
        (t == DS_TYPE_GAUGE)    ? "gauge"    : \
        (t == DS_TYPE_DERIVE)   ? "derive"   : \
        (t == DS_TYPE_ABSOLUTE) ? "absolute" : \
        "unknown"

#define BUFF_SIZE 1024

typedef unsigned long long counter_t;
typedef double gauge_t;
typedef int64_t derive_t;
typedef uint64_t absolute_t;
int  interval_g;

union value_u
{
  counter_t  counter;
  gauge_t    gauge;
  derive_t   derive;
  absolute_t absolute;
};
typedef union value_u value_t;

struct value_list_s
{
  value_t *values;
  int      values_len;
  time_t   time;
  int      interval;
  char     host[DATA_MAX_NAME_LEN];
  char     plugin[DATA_MAX_NAME_LEN];
  char     plugin_instance[DATA_MAX_NAME_LEN];
  char     type[DATA_MAX_NAME_LEN];
  char     type_instance[DATA_MAX_NAME_LEN];
  meta_data_t *meta;
  uint8_t  *types;
};
typedef struct value_list_s value_list_t;


enum notification_meta_type_e
{
  NM_TYPE_STRING,
  NM_TYPE_SIGNED_INT,
  NM_TYPE_UNSIGNED_INT,
  NM_TYPE_DOUBLE,
  NM_TYPE_BOOLEAN
};

typedef struct notification_meta_s
{
  char name[DATA_MAX_NAME_LEN];
  enum notification_meta_type_e type;
  union
  {
    const char *nm_string;
    int64_t nm_signed_int;
    uint64_t nm_unsigned_int;
    double nm_double;
    bool nm_boolean;
  } nm_value;
  struct notification_meta_s *next;
} notification_meta_t;

typedef struct notification_s
{
  int    severity;
  time_t time;
  char   message[NOTIF_MAX_MSG_LEN];
  char   host[DATA_MAX_NAME_LEN];
  char   plugin[DATA_MAX_NAME_LEN];
  char   plugin_instance[DATA_MAX_NAME_LEN];
  char   type[DATA_MAX_NAME_LEN];
  char   type_instance[DATA_MAX_NAME_LEN];
  notification_meta_t *meta;
} notification_t;



/*                      1 1 1 1 1 1 1 1 1 1 2 2 2 2 2 2 2 2 2 2 3 3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-------+-----------------------+-------------------------------+
 * ! Ver.  !                       ! Length                        !
 * +-------+-----------------------+-------------------------------+
 */
struct part_header_s
{
  uint16_t type;
  uint16_t length;
};
typedef struct part_header_s part_header_t;

/*                      1 1 1 1 1 1 1 1 1 1 2 2 2 2 2 2 2 2 2 2 3 3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-------------------------------+-------------------------------+
 * ! Type                          ! Length                        !
 * +-------------------------------+-------------------------------+
 * : (Length - 4) Bytes                                            :
 * +---------------------------------------------------------------+
 */
struct part_string_s
{
  part_header_t *head;
  char *value;
};
typedef struct part_string_s part_string_t;

/*                      1 1 1 1 1 1 1 1 1 1 2 2 2 2 2 2 2 2 2 2 3 3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-------------------------------+-------------------------------+
 * ! Type                          ! Length                        !
 * +-------------------------------+-------------------------------+
 * : (Length - 4 == 2 || 4 || 8) Bytes                             :
 * +---------------------------------------------------------------+
 */
struct part_number_s
{
  part_header_t *head;
  uint64_t *value;
};
typedef struct part_number_s part_number_t;

/*                      1 1 1 1 1 1 1 1 1 1 2 2 2 2 2 2 2 2 2 2 3 3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-------------------------------+-------------------------------+
 * ! Type                          ! Length                        !
 * +-------------------------------+---------------+---------------+
 * ! Num of values                 ! Type0         ! Type1         !
 * +-------------------------------+---------------+---------------+
 * ! Value0                                                        !
 * !                                                               !
 * +---------------------------------------------------------------+
 * ! Value1                                                        !
 * !                                                               !
 * +---------------------------------------------------------------+
 */
struct part_values_s
{
  part_header_t *head;
  uint16_t *num_values;
  uint8_t  *values_types;
  value_t  *values;
};
typedef struct part_values_s part_values_t;

/*                      1 1 1 1 1 1 1 1 1 1 2 2 2 2 2 2 2 2 2 2 3 3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-------------------------------+-------------------------------+
 * ! Type                          ! Length                        !
 * +-------------------------------+-------------------------------+
 * ! Hash (Bits   0 -  31)                                         !
 * : :                                                             :
 * ! Hash (Bits 224 - 255)                                         !
 * +---------------------------------------------------------------+
 */
/* Minimum size */
#define PART_SIGNATURE_SHA256_SIZE 36
struct part_signature_sha256_s
{
  part_header_t head;
  unsigned char hash[32];
  char *username;
};
typedef struct part_signature_sha256_s part_signature_sha256_t;

/*                      1 1 1 1 1 1 1 1 1 1 2 2 2 2 2 2 2 2 2 2 3 3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-------------------------------+-------------------------------+
 * ! Type                          ! Length                        !
 * +-------------------------------+-------------------------------+
 * ! Original length               ! Padding (0 - 15 bytes)        !
 * +-------------------------------+-------------------------------+
 * ! Hash (Bits   0 -  31)                                         !
 * : :                                                             :
 * ! Hash (Bits 128 - 159)                                         !
 * +---------------------------------------------------------------+
 */
/* Minimum size */
#define PART_ENCRYPTION_AES256_SIZE 42
struct part_encryption_aes256_s
{
  part_header_t head;
  uint16_t username_length;
  char *username;
  unsigned char iv[16];
  /* <encrypted> */
  unsigned char hash[20];
  /*   <payload /> */
  /* </encrypted> */
};
typedef struct part_encryption_aes256_s part_encryption_aes256_t;

struct receive_list_entry_s
{
  char data[BUFF_SIZE];
  int  data_len;
  int  fd;
  struct receive_list_entry_s *next;
};
typedef struct receive_list_entry_s receive_list_entry_t;


/**
 *
 *  END Collectd Structs
 *
 */

static unsigned long long collectd_ntohll (unsigned long long n)
{
#if BYTE_ORDER == BIG_ENDIAN
  return (n);
#else
  return (((unsigned long long) ntohl (n)) << 32) + ntohl (n >> 32);
#endif
} /* unsigned long long collectd_ntohll */

#define ntohd(d) (d)


/* Forward declare this so the parse function can use it */
static int queue_values(collectd_closure_t *ccl, 
  noit_module_t *self, noit_check_t *check, value_list_t *vl);
static int queue_notifications(collectd_closure_t *ccl, 
  noit_module_t *self, noit_check_t *check, notification_t *n);


static EVP_CIPHER_CTX* network_get_aes256_cypher (collectd_closure_t *ccl, /* {{{ */
    const void *iv, size_t iv_size, const char *username)
{

  if (ccl->username == NULL && ccl->secret == NULL)
    return (NULL);
  else
  {
    EVP_CIPHER_CTX *ctx_ptr;
    EVP_MD_CTX ctx_md;
    unsigned char password_hash[32];
    unsigned int length = 0;
    int success;

    ctx_ptr = &ccl->ctx;

    EVP_DigestInit(&ctx_md, EVP_sha256());
    EVP_DigestUpdate(&ctx_md, ccl->secret, strlen(ccl->secret));
    EVP_DigestFinal(&ctx_md, password_hash, &length); 
    EVP_MD_CTX_cleanup(&ctx_md);

    assert(length <= 32);

    success = EVP_DecryptInit(ctx_ptr, EVP_aes_256_ofb(), password_hash, iv);
    if (success != 1)
    {
      noitL(noit_error, "collectd: EVP_DecryptInit returned: %d\n",
          success);
      return (NULL);
    }
    return (ctx_ptr);
  }
} /* }}} int network_get_aes256_cypher */

static int parse_part_values (void **ret_buffer, size_t *ret_buffer_len,
    value_t **ret_values, uint8_t **ret_types, int *ret_num_values)
{
  char *buffer = *ret_buffer;
  size_t buffer_len = *ret_buffer_len;

  uint16_t tmp16;
  size_t exp_size;
  int   i;

  uint16_t pkg_length;
  uint16_t pkg_type;
  uint16_t pkg_numval;

  uint8_t *pkg_types;
  value_t *pkg_values;

  if (buffer_len < 15)
  {
    noitL(noit_error,"collectd: packet is too short: "
        "buffer_len = %zu\n", buffer_len);
    return (-1);
  }

  memcpy ((void *) &tmp16, buffer, sizeof (tmp16));
  buffer += sizeof (tmp16);
  pkg_type = ntohs (tmp16);

  memcpy ((void *) &tmp16, buffer, sizeof (tmp16));
  buffer += sizeof (tmp16);
  pkg_length = ntohs (tmp16);

  memcpy ((void *) &tmp16, buffer, sizeof (tmp16));
  buffer += sizeof (tmp16);
  pkg_numval = ntohs (tmp16);

  assert (pkg_type == TYPE_VALUES);

  exp_size = 3 * sizeof (uint16_t)
    + pkg_numval * (sizeof (uint8_t) + sizeof (value_t));
  if ((buffer_len < 0) || (buffer_len < exp_size))
  {
    noitL(noit_error, "collectd: parse_part_values: "
        "Packet too short: "
        "Chunk of size %zu expected, "
        "but buffer has only %zu bytes left.\n",
        exp_size, buffer_len);
    return (-1);
  }

  if (pkg_length != exp_size)
  {
    noitL(noit_debug, "collectd: parse_part_values: "
        "Length and number of values "
        "in the packet don't match.\n");
    return (-1);
  }

  pkg_types = (uint8_t *) malloc (pkg_numval * sizeof (uint8_t));
  pkg_values = (value_t *) malloc (pkg_numval * sizeof (value_t));
  if ((pkg_types == NULL) || (pkg_values == NULL))
  {
    sfree (pkg_types);
    sfree (pkg_values);
    noitL(noit_error, "collectd: parse_part_values: malloc failed.\n");
    return (-1);
  }

  memcpy ((void *) pkg_types, (void *) buffer, pkg_numval * sizeof (uint8_t));
  buffer += pkg_numval * sizeof (uint8_t);
  memcpy ((void *) pkg_values, (void *) buffer, pkg_numval * sizeof (value_t));
  buffer += pkg_numval * sizeof (value_t);

  for (i = 0; i < pkg_numval; i++)
  {
    switch (pkg_types[i])
    {
      case DS_TYPE_COUNTER:
        pkg_values[i].counter = (counter_t) collectd_ntohll (pkg_values[i].counter);
        break;

      case DS_TYPE_GAUGE:
        pkg_values[i].gauge = (gauge_t) ntohd (pkg_values[i].gauge);
        break;

      case DS_TYPE_DERIVE:
        pkg_values[i].derive = (derive_t) collectd_ntohll (pkg_values[i].derive);
        break;

      case DS_TYPE_ABSOLUTE:
        pkg_values[i].absolute = (absolute_t) collectd_ntohll (pkg_values[i].absolute);
        break;

      default:
        sfree (pkg_types);
        sfree (pkg_values);
        noitL(noit_debug, "collectd: parse_part_values: "
      "Don't know how to handle data source type %"PRIu8 "\n",
      pkg_types[i]);
        return (-1);
    } /* switch (pkg_types[i]) */
  }

  *ret_buffer     = buffer;
  *ret_buffer_len = buffer_len - pkg_length;
  *ret_num_values = pkg_numval;
  *ret_types      = pkg_types;
  *ret_values     = pkg_values;


  return (0);
} /* int parse_part_values */

static int parse_part_number (void **ret_buffer, size_t *ret_buffer_len,
    uint64_t *value)
{
  char *buffer = *ret_buffer;
  size_t buffer_len = *ret_buffer_len;

  uint16_t tmp16;
  uint64_t tmp64;
  size_t exp_size = 2 * sizeof (uint16_t) + sizeof (uint64_t);

  uint16_t pkg_length;
  uint16_t pkg_type;

  if ((buffer_len < 0) || ((size_t) buffer_len < exp_size))
  {
    noitL(noit_error, "collectd: parse_part_number: "
        "Packet too short: "
        "Chunk of size %zu expected, "
        "but buffer has only %zu bytes left.\n",
        exp_size, buffer_len);
    return (-1);
  }

  memcpy ((void *) &tmp16, buffer, sizeof (tmp16));
  buffer += sizeof (tmp16);
  pkg_type = ntohs (tmp16);

  memcpy ((void *) &tmp16, buffer, sizeof (tmp16));
  buffer += sizeof (tmp16);
  pkg_length = ntohs (tmp16);

  memcpy ((void *) &tmp64, buffer, sizeof (tmp64));
  buffer += sizeof (tmp64);
  *value = collectd_ntohll (tmp64);

  *ret_buffer = buffer;
  *ret_buffer_len = buffer_len - pkg_length;

  return (0);
} /* int parse_part_number */

static int parse_part_string (void **ret_buffer, size_t *ret_buffer_len,
    char *output, int output_len)
{
  char *buffer = *ret_buffer;
  size_t buffer_len = *ret_buffer_len;

  uint16_t tmp16;
  size_t header_size = 2 * sizeof (uint16_t);

  uint16_t pkg_length;
  uint16_t pkg_type;

  if ((buffer_len < 0) || (buffer_len < header_size))
  {
    noitL(noit_error, "collectd: parse_part_string: "
        "Packet too short: "
        "Chunk of at least size %zu expected, "
        "but buffer has only %zu bytes left.\n",
        header_size, buffer_len);
    return (-1);
  }

  memcpy ((void *) &tmp16, buffer, sizeof (tmp16));
  buffer += sizeof (tmp16);
  pkg_type = ntohs (tmp16);

  memcpy ((void *) &tmp16, buffer, sizeof (tmp16));
  buffer += sizeof (tmp16);
  pkg_length = ntohs (tmp16);

  /* Check that packet fits in the input buffer */
  if (pkg_length > buffer_len)
  {
    noitL(noit_error, "collectd: parse_part_string: "
        "Packet too big: "
        "Chunk of size %"PRIu16" received, "
        "but buffer has only %zu bytes left.\n",
        pkg_length, buffer_len);
    return (-1);
  }

  /* Check that pkg_length is in the valid range */
  if (pkg_length <= header_size)
  {
    noitL(noit_error, "collectd: parse_part_string: "
        "Packet too short: "
        "Header claims this packet is only %hu "
        "bytes long.\n", pkg_length);
    return (-1);
  }

  /* Check that the package data fits into the output buffer.
   * The previous if-statement ensures that:
   * `pkg_length > header_size' */
  if ((output_len < 0)
      || ((size_t) output_len < ((size_t) pkg_length - header_size)))
  {
    noitL(noit_error, "collectd: parse_part_string: "
        "Output buffer too small.\n");
    return (-1);
  }

  /* All sanity checks successfull, let's copy the data over */
  output_len = pkg_length - header_size;
  memcpy ((void *) output, (void *) buffer, output_len);
  buffer += output_len;

  /* For some very weird reason '\0' doesn't do the trick on SPARC in
   * this statement. */
  if (output[output_len - 1] != 0)
  {
    noitL(noit_error, "collectd: parse_part_string: "
        "Received string does not end "
        "with a NULL-byte.\n");
    return (-1);
  }

  *ret_buffer = buffer;
  *ret_buffer_len = buffer_len - pkg_length;

  return (0);
} /* int parse_part_string */


#define PP_SIGNED    0x01
#define PP_ENCRYPTED 0x02

#define BUFFER_READ(p,s) do { \
  memcpy ((p), buffer + buffer_offset, (s)); \
  buffer_offset += (s); \
} while (0)



// Forward declare
static int parse_packet (
    collectd_closure_t *ccl, noit_module_t *self, noit_check_t *check, 
    void *buffer, size_t buffer_size, int flags);


static int parse_part_sign_sha256 (collectd_closure_t *ccl, noit_module_t *self, 
    noit_check_t *check, void **ret_buffer, size_t *ret_buffer_len, int flags)
{
  unsigned char *buffer;
  size_t buffer_len;
  size_t buffer_offset;

  size_t username_len;
  part_signature_sha256_t pss;
  uint16_t pss_head_length;
  unsigned char hash[sizeof (pss.hash)];

  unsigned char *hash_ptr;
  unsigned int length, err;

  buffer = *ret_buffer;
  buffer_len = *ret_buffer_len;
  buffer_offset = 0;

  if (ccl->username == NULL)
  {
    noitL(noit_debug, "collectd: Received signed network packet but can't verify "
        "it because no user has been configured. Will accept it.\n");
    return (0);
  }

  /* Check if the buffer has enough data for this structure. */
  if (buffer_len <= PART_SIGNATURE_SHA256_SIZE)
    return (-ENOMEM);

  /* Read type and length header */
  BUFFER_READ (&pss.head.type, sizeof (pss.head.type));
  BUFFER_READ (&pss.head.length, sizeof (pss.head.length));
  pss_head_length = ntohs (pss.head.length);

  /* Check if the `pss_head_length' is within bounds. */
  if ((pss_head_length <= PART_SIGNATURE_SHA256_SIZE)
      || (pss_head_length > buffer_len))
  {
    noitL(noit_error, "collectd: HMAC-SHA-256 with invalid length received.\n");
    return (-1);
  }

  /* Copy the hash. */
  BUFFER_READ (pss.hash, sizeof (pss.hash));

  /* Calculate username length (without null byte) and allocate memory */
  username_len = pss_head_length - PART_SIGNATURE_SHA256_SIZE;
  pss.username = malloc (username_len + 1);
  if (pss.username == NULL)
    return (-ENOMEM);

  /* Read the username */
  BUFFER_READ (pss.username, username_len);
  pss.username[username_len] = 0;

  assert (buffer_offset == pss_head_length);

  /* Match up the username with the expected username */
  if (strcmp(ccl->username, pss.username) != 0)
  {
    noitL(noit_error, "collectd: User: %s and Given User: %s don't match\n", ccl->username, pss.username);
    sfree (pss.username);
    return (-ENOENT);
  }

  /* Create a hash device and check the HMAC */
  hash_ptr = HMAC(EVP_sha256(), ccl->secret, strlen(ccl->secret),
      buffer     + PART_SIGNATURE_SHA256_SIZE,
      buffer_len - PART_SIGNATURE_SHA256_SIZE,
      hash,         &length);
  if (hash_ptr == NULL)
  {
    noitL(noit_error, "collectd: Creating HMAC-SHA-256 object failed: %d\n",
        err);
    sfree (pss.username);
    return (-1);
  }

  /* Clean up */
  sfree (pss.username);

  if (memcmp (pss.hash, hash, sizeof (pss.hash)) != 0)
  {
    noitL(noit_error, "collectd: Verifying HMAC-SHA-256 signature failed: "
        "Hash mismatch.\n");
  }
  else
  {
    parse_packet (ccl, self, check, buffer + buffer_offset, buffer_len - buffer_offset,
        flags | PP_SIGNED);
  }

  *ret_buffer = buffer + buffer_len;
  *ret_buffer_len = 0;

  return (0);
} /* }}} int parse_part_sign_sha256 */
/* #endif HAVE_LIBGCRYPT */

static int parse_part_encr_aes256 (collectd_closure_t *ccl, noit_module_t *self, 
    noit_check_t *check, void **ret_buffer, size_t *ret_buffer_len,
    int flags)
{
  unsigned char  *buffer = *ret_buffer;
  size_t buffer_len = *ret_buffer_len;
  size_t payload_len;
  size_t part_size;
  size_t buffer_offset;
  int    tmpbufsize;
  unsigned char   *tmpbuf;
  uint16_t username_len;
  part_encryption_aes256_t pea;
  unsigned char hash[sizeof (pea.hash)];
  unsigned int hash_length;

  EVP_CIPHER_CTX *ctx;
  EVP_MD_CTX ctx_md;
  int err;

  /* Make sure at least the header if available. */
  if (buffer_len <= PART_ENCRYPTION_AES256_SIZE)
  {
    noitL(noit_debug, "collectd: parse_part_encr_aes256: "
        "Discarding short packet.\n");
    return (-1);
  }

  buffer_offset = 0;

  /* Copy the unencrypted information into `pea'. */
  BUFFER_READ (&pea.head.type, sizeof (pea.head.type));
  BUFFER_READ (&pea.head.length, sizeof (pea.head.length));

  /* Check the `part size'. */
  part_size = ntohs (pea.head.length);
  if ((part_size <= PART_ENCRYPTION_AES256_SIZE)
      || (part_size > buffer_len))
  {
    noitL(noit_debug, "collectd: parse_part_encr_aes256: "
        "Discarding part with invalid size.\n");
    return (-1);
  }

  /* Read the username */
  BUFFER_READ (&username_len, sizeof (username_len));
  username_len = ntohs (username_len);

  if ((username_len <= 0)
      || (username_len > (part_size - (PART_ENCRYPTION_AES256_SIZE + 1))))
  {
    noitL(noit_debug, "collectd: parse_part_encr_aes256: "
        "Discarding part with invalid username length.\n");
    return (-1);
  }

  assert (username_len > 0);
  pea.username = malloc (username_len + 1);
  if (pea.username == NULL)
    return (-ENOMEM);
  BUFFER_READ (pea.username, username_len);
  pea.username[username_len] = 0;

  /* Last but not least, the initialization vector */
  BUFFER_READ (pea.iv, sizeof (pea.iv));

  /* Make sure we are at the right position */
  assert (buffer_offset == (username_len +
        PART_ENCRYPTION_AES256_SIZE - sizeof (pea.hash)));

  /* Match up the username with the expected username */
  if (strcmp(ccl->username, pea.username) != 0)
  {
    noitL(noit_error, "collectd: Username received and server side username don't match\n");
    sfree (pea.username);
    return (-ENOENT);
  }

  ctx = network_get_aes256_cypher (ccl, pea.iv, sizeof (pea.iv),
      pea.username);
  if (ctx == NULL)
    return (-1);

  payload_len = part_size - (PART_ENCRYPTION_AES256_SIZE + username_len);
  assert (payload_len > 0);
  tmpbuf = malloc(part_size - buffer_offset);

  /* Decrypt the packet */
  err = EVP_DecryptUpdate(ctx,
      tmpbuf, &tmpbufsize,
      buffer    + buffer_offset,
      part_size - buffer_offset);
  if (err != 1)
  {
    noitL(noit_error, "collectd: openssl returned: %d\n", err);
    return (-1);
  }

  assert(part_size - buffer_offset == tmpbufsize);
  /* Make it appear to be in place */
  memcpy(buffer + buffer_offset, tmpbuf, part_size - buffer_offset);
  sfree(tmpbuf);

  /* Read the hash */
  BUFFER_READ (pea.hash, sizeof (pea.hash));

  /* Make sure we're at the right position - again */
  assert (buffer_offset == (username_len + PART_ENCRYPTION_AES256_SIZE));
  assert (buffer_offset == (part_size - payload_len));

  /* Check hash sum */
  memset (hash, 0, sizeof (hash));
  EVP_DigestInit(&ctx_md, EVP_sha1());
  EVP_DigestUpdate(&ctx_md, buffer + buffer_offset, payload_len);
  err = EVP_DigestFinal(&ctx_md, hash, &hash_length);
  if (memcmp (hash, pea.hash, sizeof (hash)) != 0)
  {
    noitL(noit_error, "collectd: Decryption failed: Checksum mismatch.\n");
    return (-1);
  }

  parse_packet (ccl, self, check, buffer + buffer_offset, payload_len,
      flags | PP_ENCRYPTED);

  /* Update return values */
  *ret_buffer =     buffer     + part_size;
  *ret_buffer_len = buffer_len - part_size;
  sfree(pea.username);

  return (0);
} /* }}} int parse_part_encr_aes256 */


#undef BUFFER_READ


static int parse_packet (/* {{{ */
    collectd_closure_t *ccl, noit_module_t *self, noit_check_t *check,
    void *buffer, size_t buffer_size, int flags)
{
  int status;

  int packet_was_signed = (flags & PP_SIGNED);
  int packet_was_encrypted = (flags & PP_ENCRYPTED);
  int printed_ignore_warning = 0;

#define VALUE_LIST_INIT { NULL, 0, 0, interval_g, "localhost", "", "", "", "", NULL }
  value_list_t vl = VALUE_LIST_INIT;
  notification_t n;

  memset (&vl, '\0', sizeof (vl));
  memset (&n, '\0', sizeof (n));
  status = 0;

  while ((status == 0) && (0 < buffer_size)
      && ((unsigned int) buffer_size > sizeof (part_header_t)))
  {
    uint16_t pkg_length;
    uint16_t pkg_type;

    memcpy ((void *) &pkg_type,
        (void *) buffer,
        sizeof (pkg_type));
    memcpy ((void *) &pkg_length,
        (void *) (buffer + sizeof (pkg_type)),
        sizeof (pkg_length));

    pkg_length = ntohs (pkg_length);
    pkg_type = ntohs (pkg_type);

    if (pkg_length > buffer_size)
      break;
    /* Ensure that this loop terminates eventually */
    if (pkg_length < (2 * sizeof (uint16_t)))
      break;


    if (pkg_type == TYPE_ENCR_AES256)
    {
      status = parse_part_encr_aes256 (ccl, self, check,
          &buffer, &buffer_size, flags);
      if (status != 0)
      {
        noitL(noit_error, "collectd: Decrypting AES256 "
            "part failed "
            "with status %i.\n", status);
        break;
      }
    }
    else if ((ccl->security_level == SECURITY_LEVEL_ENCRYPT)
        && (packet_was_encrypted == 0))
    {
      if (printed_ignore_warning == 0)
      {
        noitL(noit_debug, "collectd: Unencrypted packet or "
            "part has been ignored.\n");
        printed_ignore_warning = 1;
      }
      buffer = ((char *) buffer) + pkg_length;
      continue;
    }
    else if (pkg_type == TYPE_SIGN_SHA256)
    {
      status = parse_part_sign_sha256 (ccl, self, check,
                                        &buffer, &buffer_size, flags);
      if (status != 0)
      {
        noitL(noit_error, "collectd: Verifying HMAC-SHA-256 "
            "signature failed "
            "with status %i.\n", status);
        break;
      }
    }
    else if ((ccl->security_level == SECURITY_LEVEL_SIGN)
        && (packet_was_encrypted == 0)
        && (packet_was_signed == 0))
    {
      if (printed_ignore_warning == 0)
      {
        noitL(noit_debug, "collectd: Unsigned packet or "
            "part has been ignored.\n");
        printed_ignore_warning = 1;
      }
      buffer = ((char *) buffer) + pkg_length;
      continue;
    }
    else if (pkg_type == TYPE_VALUES)
    {
      status = parse_part_values (&buffer, &buffer_size,
          &vl.values, &vl.types, &vl.values_len);

      if (status != 0)
        break;

      if ((vl.time > 0)
          && (strlen (vl.host) > 0)
          && (strlen (vl.plugin) > 0)
          && (strlen (vl.type) > 0))
      {
        queue_values(ccl, self, check, &vl);
      }
      else
      {
        noitL(noit_error,
            "collectd:  NOT dispatching values\n");
      }

      sfree (vl.values);
      sfree (vl.types);
    }
    else if (pkg_type == TYPE_TIME)
    {
      uint64_t tmp = 0;
      status = parse_part_number (&buffer, &buffer_size,
          &tmp);
      if (status == 0)
      {
        vl.time = (time_t) tmp;
        n.time = (time_t) tmp;
      }
    }
    else if (pkg_type == TYPE_INTERVAL)
    {
      uint64_t tmp = 0;
      status = parse_part_number (&buffer, &buffer_size,
          &tmp);
      if (status == 0)
        vl.interval = (int) tmp;
    }
    else if (pkg_type == TYPE_HOST)
    {
      status = parse_part_string (&buffer, &buffer_size,
          vl.host, sizeof (vl.host));
      if (status == 0)
        sstrncpy (n.host, vl.host, sizeof (n.host));
    }
    else if (pkg_type == TYPE_PLUGIN)
    {
      status = parse_part_string (&buffer, &buffer_size,
          vl.plugin, sizeof (vl.plugin));
      if (status == 0)
        sstrncpy (n.plugin, vl.plugin,
            sizeof (n.plugin));
    }
    else if (pkg_type == TYPE_PLUGIN_INSTANCE)
    {
      status = parse_part_string (&buffer, &buffer_size,
          vl.plugin_instance,
          sizeof (vl.plugin_instance));
      if (status == 0)
        sstrncpy (n.plugin_instance,
            vl.plugin_instance,
            sizeof (n.plugin_instance));
    }
    else if (pkg_type == TYPE_TYPE)
    {
      status = parse_part_string (&buffer, &buffer_size,
          vl.type, sizeof (vl.type));
      if (status == 0)
        sstrncpy (n.type, vl.type, sizeof (n.type));
    }
    else if (pkg_type == TYPE_TYPE_INSTANCE)
    {
      status = parse_part_string (&buffer, &buffer_size,
          vl.type_instance,
          sizeof (vl.type_instance));
      if (status == 0)
        sstrncpy (n.type_instance, vl.type_instance,
            sizeof (n.type_instance));
    }
    else if (pkg_type == TYPE_MESSAGE)
    {
      status = parse_part_string (&buffer, &buffer_size,
          n.message, sizeof (n.message));

      if (status != 0)
      {
        /* do nothing */
      }
      else if ((n.severity != NOTIF_FAILURE)
          && (n.severity != NOTIF_WARNING)
          && (n.severity != NOTIF_OKAY))
      {
        noitL(noit_error, "collectd: "
            "Ignoring notification with "
            "unknown severity %i.\n",
            n.severity);
      }
      else if (n.time <= 0)
      {
        noitL(noit_error, "collectd: "
            "Ignoring notification with "
            "time == 0.\n");
      }
      else if (strlen (n.message) <= 0)
      {
        noitL(noit_error, "collectd: "
            "Ignoring notification with "
            "an empty message.\n");
      }
      else
      {
        queue_notifications(ccl, self, check, &n);
        noitL(noit_error, "collectd: "
            "DISPATCH NOTIFICATION\n");
      }
    }
    else if (pkg_type == TYPE_SEVERITY)
    {
      uint64_t tmp = 0;
      status = parse_part_number (&buffer, &buffer_size,
          &tmp);
      if (status == 0)
        n.severity = (int) tmp;
    }
    else
    {
      noitL(noit_error, "collectd: parse_packet: Unknown part"
          " type: 0x%04hx\n", pkg_type);
      buffer = ((char *) buffer) + pkg_length;
    }
  } /* while (buffer_size > sizeof (part_header_t)) */

  return (status);
} /* }}} int parse_packet */


// Not proud of this at all however; I am not sure best how to address this.
static int infer_type(char *buffer, int buffer_len, value_list_t *vl, int index) {
  int len = strlen(buffer);
  strcat(buffer, "`");
  if (cmp_type(vl, "load", "load")) {
    assert(vl->values_len == 3);
    switch (index) {
      case 0:
        strcat(buffer, "1min"); break;
      case 1:
        strcat(buffer, "5min"); break;
      case 2:
        strcat(buffer, "15min"); break;
    }
  } else if (cmp_plugin(vl, "interface")) {
    assert(vl->values_len == 2);
    switch (index) {
      case 0:
        strcat(buffer, "rx"); break;
      case 1:
        strcat(buffer, "tx"); break;
    }
  } else {
    char buf[20];
    snprintf(buf, sizeof(buf), "%d", index);
    strcat(buffer, buf); 
    noitL(noit_debug, "collectd: parsing multiple values" 
        " and guessing on the type for plugin[%s] and type[%s]"
        , vl->plugin, vl->type);
  }
  return len;
}

static void concat_metrics(char *buffer, char* plugin, char* plugin_inst, char* type, char* type_inst) {
  strcpy(buffer, plugin);

  if (strlen(plugin_inst)) {
    strcat(buffer, "`");
    strcat(buffer, plugin_inst);
  }
  if (strlen(type)) {
    strcat(buffer, "`");
    strcat(buffer, type);
  }
  if (strlen(type_inst)) {
    strcat(buffer, "`");
    strcat(buffer, type_inst);
  }
}

static int queue_notifications(collectd_closure_t *ccl, 
      noit_module_t *self, noit_check_t *check, notification_t *n) {
  stats_t current;
  char buffer[DATA_MAX_NAME_LEN*4 + 128];

  noit_check_stats_clear(&current);
  gettimeofday(&current.whence, NULL);
  // Concat all the names together so they fit into the flat noitd model 
  concat_metrics(buffer, n->plugin, n->plugin_instance, n->type, n->type_instance);
  noit_stats_set_metric(&ccl->current, buffer, METRIC_STRING, n->message);
  noit_check_set_stats(self, check, &current);
  noitL(nldeb, "collectd: dispatch_notifications(%s, %s, %s)\n",check->target, buffer, n->message);
  return 0;
}


static int queue_values(collectd_closure_t *ccl,
      noit_module_t *self, noit_check_t *check, value_list_t *vl) {
  char buffer[DATA_MAX_NAME_LEN*4 + 4 + 1 + 20];
  int i, len = 0;

  // Concat all the names together so they fit into the flat noitd model 
  concat_metrics(buffer, vl->plugin, vl->plugin_instance, vl->type, vl->type_instance);
  for (i=0; i < vl->values_len; i++) {
  
    // Only infer the type if the amount of values is greater than one
    if (vl->values_len > 1) {
      // Trunc the string  
      if (len > 0)
        buffer[len] = 0;
      len = infer_type(buffer, sizeof(buffer), vl, i);
    }

    switch (vl->types[i])
    {
      case DS_TYPE_COUNTER:
        noit_stats_set_metric(&ccl->current, buffer, METRIC_UINT64, &vl->values[i].counter);
        break;

      case DS_TYPE_GAUGE:
        noit_stats_set_metric(&ccl->current, buffer, METRIC_DOUBLE, &vl->values[i].gauge);
        break;

      case DS_TYPE_DERIVE:
        noit_stats_set_metric(&ccl->current, buffer, METRIC_INT64, &vl->values[i].derive);
        break;

      case DS_TYPE_ABSOLUTE:
        noit_stats_set_metric(&ccl->current, buffer, METRIC_INT64, &vl->values[i].absolute);
        break;

        noitL(noit_debug, "collectd: parse_part_values: "
      "Don't know how to handle data source type %"PRIu8 "\n",
      vl->types[i]);
        return (-1);
    } /* switch (value_types[i]) */
    ccl->stats_count++;
    noitL(nldeb, "collectd: queue_values(%s, %s)\n", buffer, check->target);
  }
  //noit_check_set_stats(self, check, &current);
  return 0;
} 

static void clear_closure(collectd_closure_t *ccl) {
  ccl->stats_count = 0;
  ccl->ntfy_count = 0;
  noit_check_stats_clear(&ccl->current);

}

static int collectd_submit(noit_module_t *self, noit_check_t *check) {
  collectd_closure_t *ccl;
  struct timeval duration;
  if(!check->closure) {
    ccl = check->closure = (void *)calloc(1, sizeof(collectd_closure_t)); 
    memset(ccl, 0, sizeof(collectd_closure_t));
  } else {
    // Don't count the first run
    char human_buffer[256];
    ccl = (collectd_closure_t*)check->closure; 
    gettimeofday(&ccl->current.whence, NULL);
    sub_timeval(ccl->current.whence, check->last_fire_time, &duration);
    ccl->current.duration = duration.tv_sec; // + duration.tv_usec / (1000 * 1000);

    snprintf(human_buffer, sizeof(human_buffer),
             "dur=%d,run=%d,stats=%d,ntfy=%d", ccl->current.duration, 
             check->generation, ccl->stats_count, ccl->ntfy_count);
    noitL(nldeb, "collectd(%s) [%s]\n", check->target, human_buffer);

    // Not sure what to do here
    ccl->current.available = (ccl->ntfy_count > 0 || ccl->stats_count > 0) ? 
        NP_AVAILABLE : NP_UNAVAILABLE;
    ccl->current.state = (ccl->ntfy_count > 0 || ccl->stats_count > 0) ? 
        NP_GOOD : NP_BAD;
    ccl->current.status = human_buffer;
    noit_check_set_stats(self, check, &ccl->current);

    memcpy(&check->last_fire_time, &ccl->current.whence, sizeof(duration));
  }
  clear_closure(ccl);
  return 0;
}

static int noit_collectd_handler(eventer_t e, int mask, void *closure,
                             struct timeval *now) {
  struct sockaddr_in  skaddr;
  char packet[1500];
  int packet_len = sizeof(packet);
  unsigned int from_len;
  char ip_p[INET_ADDRSTRLEN];
  noit_module_t *self = (noit_module_t *)closure;
  collectd_mod_config_t *conf;
  conf = noit_module_get_userdata(self);


  // Get the username and password of the string

  while(1) {
    int inlen;
    noit_check_t *check;
    collectd_closure_t *ccl;
    char *security_buffer; 

    from_len = sizeof(skaddr);

    inlen = recvfrom(e->fd, packet, packet_len, 0,
                     (struct sockaddr *)&skaddr, &from_len);
    gettimeofday(now, NULL); /* set it, as we care about accuracy */

    if(inlen < 0) {
      if(errno == EAGAIN || errno == EINTR) break;
      noitLT(nlerr, now, "collectd: recvfrom: %s\n", strerror(errno));
      break;
    }
    if (!inet_ntop(AF_INET, &(skaddr.sin_addr), ip_p, INET_ADDRSTRLEN)) {
      noitLT(nlerr, now, "collectd: inet_ntop failed: %s\n", strerror(errno));
      break;
    }
    check = noit_poller_lookup_by_name(ip_p, "collectd");
    if (!check) {
      noitL(nlerr, "collectd: No defined check from ip [%s].\n", ip_p);
      break;
    }

    // If its a new check retrieve some values
    if (check->closure == NULL) {
      // TODO: Verify if it could somehow retrieve data before the check closure exists 
      ccl = check->closure = (void *)calloc(1, sizeof(collectd_closure_t)); 
      memset(ccl, 0, sizeof(collectd_closure_t));
    } else {
      ccl = (collectd_closure_t*)check->closure; 
    }
    // Default to NONE
    ccl->security_level = SECURITY_LEVEL_NONE;     
    if (noit_hash_retr_str(check->config, "security_level", strlen("security_level"),
                           (const char**)&security_buffer)) 
    {
      ccl->security_level = atoi(security_buffer);
    }

    // Is this outside to keep updates happening?
    if (!noit_hash_retr_str(check->config, "username", strlen("username"),
                           (const char**)&ccl->username) &&
        !noit_hash_retr_str(conf->options, "username", strlen("username"),
                           (const char**)&ccl->username)) 
    {
      if (ccl->security_level == SECURITY_LEVEL_ENCRYPT) {
        noitL(nlerr, "collectd: no username defined for check.\n");
        goto cleanup;
      } else if (ccl->security_level == SECURITY_LEVEL_SIGN) {
        noitL(nlerr, "collectd: no username defined for check, "
            "will accept any signed packet.\n");
      }
    }

    if (!noit_hash_retr_str(check->config, "secret", strlen("secret"),
                           (const char**)&ccl->secret) &&
        !noit_hash_retr_str(conf->options, "secret", strlen("secret"),
                           (const char**)&ccl->secret)) 
    {
      if (ccl->security_level == SECURITY_LEVEL_ENCRYPT) {
        noitL(nlerr, "collectd: no secret defined for check.\n");
        goto cleanup;
      } else if (ccl->security_level == SECURITY_LEVEL_SIGN) {
        noitL(nlerr, "collectd: no secret defined for check, "
            "will accept any signed packet.\n");
      }
    }

    parse_packet(ccl, self, check, packet, inlen, 0);
  }
  return EVENTER_READ | EVENTER_EXCEPTION;

cleanup:
  return 0;
}


static int noit_collectd_initiate_check(noit_module_t *self,
                                        noit_check_t *check,
                                        int once, noit_check_t *cause) {
  /* The idea is to write the collectd stuff to the stats one every period 
   * Then we can warn people if no stats where written in a period of time
   */
  INITIATE_CHECK(collectd_submit, self, check);
  return 0;
}

static int noit_collectd_config(noit_module_t *self, noit_hash_table *options) {
  collectd_mod_config_t *conf;
  conf = noit_module_get_userdata(self);
  if(conf) {
    if(conf->options) {
      noit_hash_destroy(conf->options, free, free);
      free(conf->options);
    }
  }
  else
    conf = calloc(1, sizeof(*conf));
  conf->options = options;
  noit_module_set_userdata(self, conf);
  return 1;
}

static int noit_collectd_onload(noit_image_t *self) {
  if(!nlerr) nlerr = noit_log_stream_find("error/collectd");
  if(!nldeb) nldeb = noit_log_stream_find("debug/collectd");
  if(!nlerr) nlerr = noit_stderr;
  if(!nldeb) nldeb = noit_debug;
  eventer_name_callback("noit_collectd/handler", noit_collectd_handler);
  return 0;
}

static int noit_collectd_init(noit_module_t *self) {
  const char *config_val;
  int sockaddr_len;
  collectd_mod_config_t *conf;
  conf = noit_module_get_userdata(self);
  int portint = 0;
  struct sockaddr_in skaddr;
  const char *host;
  unsigned short port;

  if(!__collectd_initialize_once) {
    __collectd_initialize_once = 1;
  }


  /* Default Collectd port */
  portint = 25826;
  if(noit_hash_retr_str(conf->options,
                         "collectd_port", strlen("collectd_port"),
                         (const char**)&config_val))
    portint = atoi(config_val);


  if(!noit_hash_retr_str(conf->options,
                         "collectd_host", strlen("collectd_host"),
                         (const char**)&host))
    host = "*";

  port = (unsigned short) portint;

  conf->ipv4_fd = conf->ipv6_fd = -1;

  conf->ipv4_fd = socket(PF_INET, SOCK_DGRAM, 0);
  if(conf->ipv4_fd < 0) {
    noitL(noit_error, "collectd: socket failed: %s\n",
          strerror(errno));
  }
  else {
   /*
    socklen_t slen = sizeof(on);
    if(getsockopt(conf->ipv4_fd, SOL_SOCKET, SO_SNDBUF, &on, &slen) == 0) {
      while(on < (1 << 20)) {
        on <<= 1;
        if(setsockopt(conf->ipv4_fd, SOL_SOCKET, SO_SNDBUF,
                      &on, sizeof(on)) != 0) {
          on >>= 1;
          break;
        }
      }
      noitL(noit_debug, ": send buffer set to %d\n", on);
    }
    else
      noitL(noit_error, "Cannot get sndbuf size: %s\n", strerror(errno));
    */
    if(eventer_set_fd_nonblocking(conf->ipv4_fd)) {
      close(conf->ipv4_fd);
      conf->ipv4_fd = -1;
      noitL(noit_error,
            "collectd: could not set socket non-blocking: %s\n",
            strerror(errno));
    }
  }
  skaddr.sin_family = AF_INET;
  skaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  skaddr.sin_port = htons(port);

  sockaddr_len = sizeof(skaddr);
  if(bind(conf->ipv4_fd, (struct sockaddr *)&skaddr, sockaddr_len) < 0) {
    noitL(noit_stderr, "bind failed[%s]: %s\n", host, strerror(errno));
    close(conf->ipv4_fd);
    return -1;
  }

  if(conf->ipv4_fd >= 0) {
    eventer_t newe;
    newe = eventer_alloc();
    newe->fd = conf->ipv4_fd;
    newe->mask = EVENTER_READ | EVENTER_EXCEPTION;
    newe->callback = noit_collectd_handler;
    newe->closure = self;
    eventer_add(newe);
  }
/*
  conf->ipv6_fd = socket(AF_INET6, SOCK_DGRAM, 0);
  if(conf->ipv6_fd < 0) {
    noitL(noit_error, "collectd: socket failed: %s\n",
          strerror(errno));
  }
  else {
    if(eventer_set_fd_nonblocking(conf->ipv6_fd)) {
      close(conf->ipv6_fd);
      conf->ipv6_fd = -1;
      noitL(noit_error,
            "collectd: could not set socket non-blocking: %s\n",
               strerror(errno));
    }
  }
  if(conf->ipv6_fd >= 0) {
    eventer_t newe;
    newe = eventer_alloc();
    newe->fd = conf->ipv6_fd;
    newe->mask = EVENTER_READ;
    newe->callback = ping_icmp_handler;
    newe->closure = self;
    eventer_add(newe);
  }
  */
  noit_module_set_userdata(self, conf);
  return 0;
}

#include "collectd.xmlh"
noit_module_t collectd = {
  {
    NOIT_MODULE_MAGIC,
    NOIT_MODULE_ABI_VERSION,
    "collectd",
    "collectd collection",
    collectd_xml_description,
    noit_collectd_onload
  },
  noit_collectd_config,
  noit_collectd_init,
  noit_collectd_initiate_check,
  NULL /* noit_collectd_cleanup */
};
