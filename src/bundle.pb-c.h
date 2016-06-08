/* Generated by the protocol buffer compiler.  DO NOT EDIT! */

#ifndef PROTOBUF_C_bundle_2eproto__INCLUDED
#define PROTOBUF_C_bundle_2eproto__INCLUDED

#include <google/protobuf-c/protobuf-c.h>

#ifdef PROTOBUF_C_BEGIN_DECLS
PROTOBUF_C_BEGIN_DECLS
#else
#ifdef __cplusplus
extern "C" {
#endif
#endif

#if defined(PROTOBUF_C_VERSION_NUMBER)
#define PROTOBUF_C_ASSERT assert
#define PROTOBUF_C_OFFSETOF offsetof
#define PROTOBUF_C_MESSAGE_DESCRIPTOR_MAGIC PROTOBUF_C__MESSAGE_DESCRIPTOR_MAGIC
#include <stdlib.h>
static void *__c_allocator_alloc(void *d, size_t size) {
  return malloc(size);
}
static void __c_allocator_free(void *d, void *p) {
  free(p);
}
static ProtobufCAllocator __c_allocator = {
  .alloc = __c_allocator_alloc,
  .free = __c_allocator_free,
  .allocator_data = NULL
};
#define protobuf_c_system_allocator __c_allocator
#endif

typedef struct _Metric Metric;
typedef struct _Status Status;
typedef struct _Metadata Metadata;
typedef struct _Bundle Bundle;


/* --- enums --- */


/* --- messages --- */

struct  _Metric
{
  ProtobufCMessage base;
  char *name;
  int32_t metrictype;
  protobuf_c_boolean has_valuedbl;
  double valuedbl;
  protobuf_c_boolean has_valuei64;
  int64_t valuei64;
  protobuf_c_boolean has_valueui64;
  uint64_t valueui64;
  protobuf_c_boolean has_valuei32;
  int32_t valuei32;
  protobuf_c_boolean has_valueui32;
  uint32_t valueui32;
  char *valuestr;
};
#define METRIC__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&metric__descriptor) \
    , NULL, 0, 0,0, 0,0, 0,0, 0,0, 0,0, NULL }


struct  _Status
{
  ProtobufCMessage base;
  int32_t available;
  int32_t state;
  int32_t duration;
  char *status;
};
#define STATUS__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&status__descriptor) \
    , 0, 0, 0, NULL }


struct  _Metadata
{
  ProtobufCMessage base;
  char *key;
  char *value;
};
#define METADATA__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&metadata__descriptor) \
    , NULL, NULL }


struct  _Bundle
{
  ProtobufCMessage base;
  Status *status;
  size_t n_metrics;
  Metric **metrics;
  size_t n_metadata;
  Metadata **metadata;
  protobuf_c_boolean has_period;
  uint32_t period;
  protobuf_c_boolean has_timeout;
  uint32_t timeout;
};
#define BUNDLE__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&bundle__descriptor) \
    , NULL, 0,NULL, 0,NULL, 0,0, 0,0 }


/* Metric methods */
void   metric__init
                     (Metric         *message);
size_t metric__get_packed_size
                     (const Metric   *message);
size_t metric__pack
                     (const Metric   *message,
                      uint8_t             *out);
size_t metric__pack_to_buffer
                     (const Metric   *message,
                      ProtobufCBuffer     *buffer);
Metric *
       metric__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   metric__free_unpacked
                     (Metric *message,
                      ProtobufCAllocator *allocator);
/* Status methods */
void   status__init
                     (Status         *message);
size_t status__get_packed_size
                     (const Status   *message);
size_t status__pack
                     (const Status   *message,
                      uint8_t             *out);
size_t status__pack_to_buffer
                     (const Status   *message,
                      ProtobufCBuffer     *buffer);
Status *
       status__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   status__free_unpacked
                     (Status *message,
                      ProtobufCAllocator *allocator);
/* Metadata methods */
void   metadata__init
                     (Metadata         *message);
size_t metadata__get_packed_size
                     (const Metadata   *message);
size_t metadata__pack
                     (const Metadata   *message,
                      uint8_t             *out);
size_t metadata__pack_to_buffer
                     (const Metadata   *message,
                      ProtobufCBuffer     *buffer);
Metadata *
       metadata__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   metadata__free_unpacked
                     (Metadata *message,
                      ProtobufCAllocator *allocator);
/* Bundle methods */
void   bundle__init
                     (Bundle         *message);
size_t bundle__get_packed_size
                     (const Bundle   *message);
size_t bundle__pack
                     (const Bundle   *message,
                      uint8_t             *out);
size_t bundle__pack_to_buffer
                     (const Bundle   *message,
                      ProtobufCBuffer     *buffer);
Bundle *
       bundle__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   bundle__free_unpacked
                     (Bundle *message,
                      ProtobufCAllocator *allocator);
/* --- per-message closures --- */

typedef void (*Metric_Closure)
                 (const Metric *message,
                  void *closure_data);
typedef void (*Status_Closure)
                 (const Status *message,
                  void *closure_data);
typedef void (*Metadata_Closure)
                 (const Metadata *message,
                  void *closure_data);
typedef void (*Bundle_Closure)
                 (const Bundle *message,
                  void *closure_data);

/* --- services --- */


/* --- descriptors --- */

extern const ProtobufCMessageDescriptor metric__descriptor;
extern const ProtobufCMessageDescriptor status__descriptor;
extern const ProtobufCMessageDescriptor metadata__descriptor;
extern const ProtobufCMessageDescriptor bundle__descriptor;

#ifdef PROTOBUF_C_END_DECLS
PROTOBUF_C_END_DECLS
#else
#ifdef __cplusplus
}
#endif
#endif


#endif  /* PROTOBUF_bundle_2eproto__INCLUDED */