/* Generated by the protocol buffer compiler.  DO NOT EDIT! */

/* Do not generate deprecated warnings for self */
#ifndef PROTOBUF_C_NO_DEPRECATED
#define PROTOBUF_C_NO_DEPRECATED
#endif

#include "bundle.pb-c.h"
void   metric__init
                     (Metric         *message)
{
  static Metric init_value = METRIC__INIT;
  *message = init_value;
}
size_t metric__get_packed_size
                     (const Metric *message)
{
  PROTOBUF_C_ASSERT (message->base.descriptor == &metric__descriptor);
  return protobuf_c_message_get_packed_size ((const ProtobufCMessage*)(message));
}
size_t metric__pack
                     (const Metric *message,
                      uint8_t       *out)
{
  PROTOBUF_C_ASSERT (message->base.descriptor == &metric__descriptor);
  return protobuf_c_message_pack ((const ProtobufCMessage*)message, out);
}
size_t metric__pack_to_buffer
                     (const Metric *message,
                      ProtobufCBuffer *buffer)
{
  PROTOBUF_C_ASSERT (message->base.descriptor == &metric__descriptor);
  return protobuf_c_message_pack_to_buffer ((const ProtobufCMessage*)message, buffer);
}
Metric *
       metric__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data)
{
  return (Metric *)
     protobuf_c_message_unpack (&metric__descriptor,
                                allocator, len, data);
}
void   metric__free_unpacked
                     (Metric *message,
                      ProtobufCAllocator *allocator)
{
  PROTOBUF_C_ASSERT (message->base.descriptor == &metric__descriptor);
  protobuf_c_message_free_unpacked ((ProtobufCMessage*)message, allocator);
}
void   status__init
                     (Status         *message)
{
  static Status init_value = STATUS__INIT;
  *message = init_value;
}
size_t status__get_packed_size
                     (const Status *message)
{
  PROTOBUF_C_ASSERT (message->base.descriptor == &status__descriptor);
  return protobuf_c_message_get_packed_size ((const ProtobufCMessage*)(message));
}
size_t status__pack
                     (const Status *message,
                      uint8_t       *out)
{
  PROTOBUF_C_ASSERT (message->base.descriptor == &status__descriptor);
  return protobuf_c_message_pack ((const ProtobufCMessage*)message, out);
}
size_t status__pack_to_buffer
                     (const Status *message,
                      ProtobufCBuffer *buffer)
{
  PROTOBUF_C_ASSERT (message->base.descriptor == &status__descriptor);
  return protobuf_c_message_pack_to_buffer ((const ProtobufCMessage*)message, buffer);
}
Status *
       status__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data)
{
  return (Status *)
     protobuf_c_message_unpack (&status__descriptor,
                                allocator, len, data);
}
void   status__free_unpacked
                     (Status *message,
                      ProtobufCAllocator *allocator)
{
  PROTOBUF_C_ASSERT (message->base.descriptor == &status__descriptor);
  protobuf_c_message_free_unpacked ((ProtobufCMessage*)message, allocator);
}
void   metadata__init
                     (Metadata         *message)
{
  static Metadata init_value = METADATA__INIT;
  *message = init_value;
}
size_t metadata__get_packed_size
                     (const Metadata *message)
{
  PROTOBUF_C_ASSERT (message->base.descriptor == &metadata__descriptor);
  return protobuf_c_message_get_packed_size ((const ProtobufCMessage*)(message));
}
size_t metadata__pack
                     (const Metadata *message,
                      uint8_t       *out)
{
  PROTOBUF_C_ASSERT (message->base.descriptor == &metadata__descriptor);
  return protobuf_c_message_pack ((const ProtobufCMessage*)message, out);
}
size_t metadata__pack_to_buffer
                     (const Metadata *message,
                      ProtobufCBuffer *buffer)
{
  PROTOBUF_C_ASSERT (message->base.descriptor == &metadata__descriptor);
  return protobuf_c_message_pack_to_buffer ((const ProtobufCMessage*)message, buffer);
}
Metadata *
       metadata__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data)
{
  return (Metadata *)
     protobuf_c_message_unpack (&metadata__descriptor,
                                allocator, len, data);
}
void   metadata__free_unpacked
                     (Metadata *message,
                      ProtobufCAllocator *allocator)
{
  PROTOBUF_C_ASSERT (message->base.descriptor == &metadata__descriptor);
  protobuf_c_message_free_unpacked ((ProtobufCMessage*)message, allocator);
}
void   bundle__init
                     (Bundle         *message)
{
  static Bundle init_value = BUNDLE__INIT;
  *message = init_value;
}
size_t bundle__get_packed_size
                     (const Bundle *message)
{
  PROTOBUF_C_ASSERT (message->base.descriptor == &bundle__descriptor);
  return protobuf_c_message_get_packed_size ((const ProtobufCMessage*)(message));
}
size_t bundle__pack
                     (const Bundle *message,
                      uint8_t       *out)
{
  PROTOBUF_C_ASSERT (message->base.descriptor == &bundle__descriptor);
  return protobuf_c_message_pack ((const ProtobufCMessage*)message, out);
}
size_t bundle__pack_to_buffer
                     (const Bundle *message,
                      ProtobufCBuffer *buffer)
{
  PROTOBUF_C_ASSERT (message->base.descriptor == &bundle__descriptor);
  return protobuf_c_message_pack_to_buffer ((const ProtobufCMessage*)message, buffer);
}
Bundle *
       bundle__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data)
{
  return (Bundle *)
     protobuf_c_message_unpack (&bundle__descriptor,
                                allocator, len, data);
}
void   bundle__free_unpacked
                     (Bundle *message,
                      ProtobufCAllocator *allocator)
{
  PROTOBUF_C_ASSERT (message->base.descriptor == &bundle__descriptor);
  protobuf_c_message_free_unpacked ((ProtobufCMessage*)message, allocator);
}
static const ProtobufCFieldDescriptor metric__field_descriptors[8] =
{
  {
    "name",
    1,
    PROTOBUF_C_LABEL_REQUIRED,
    PROTOBUF_C_TYPE_STRING,
    0,   /* quantifier_offset */
    PROTOBUF_C_OFFSETOF(Metric, name),
    NULL,
    NULL,
    0,            /* packed */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "metricType",
    2,
    PROTOBUF_C_LABEL_REQUIRED,
    PROTOBUF_C_TYPE_INT32,
    0,   /* quantifier_offset */
    PROTOBUF_C_OFFSETOF(Metric, metrictype),
    NULL,
    NULL,
    0,            /* packed */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "valueDbl",
    3,
    PROTOBUF_C_LABEL_OPTIONAL,
    PROTOBUF_C_TYPE_DOUBLE,
    PROTOBUF_C_OFFSETOF(Metric, has_valuedbl),
    PROTOBUF_C_OFFSETOF(Metric, valuedbl),
    NULL,
    NULL,
    0,            /* packed */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "valueI64",
    4,
    PROTOBUF_C_LABEL_OPTIONAL,
    PROTOBUF_C_TYPE_INT64,
    PROTOBUF_C_OFFSETOF(Metric, has_valuei64),
    PROTOBUF_C_OFFSETOF(Metric, valuei64),
    NULL,
    NULL,
    0,            /* packed */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "valueUI64",
    5,
    PROTOBUF_C_LABEL_OPTIONAL,
    PROTOBUF_C_TYPE_UINT64,
    PROTOBUF_C_OFFSETOF(Metric, has_valueui64),
    PROTOBUF_C_OFFSETOF(Metric, valueui64),
    NULL,
    NULL,
    0,            /* packed */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "valueI32",
    6,
    PROTOBUF_C_LABEL_OPTIONAL,
    PROTOBUF_C_TYPE_INT32,
    PROTOBUF_C_OFFSETOF(Metric, has_valuei32),
    PROTOBUF_C_OFFSETOF(Metric, valuei32),
    NULL,
    NULL,
    0,            /* packed */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "valueUI32",
    7,
    PROTOBUF_C_LABEL_OPTIONAL,
    PROTOBUF_C_TYPE_UINT32,
    PROTOBUF_C_OFFSETOF(Metric, has_valueui32),
    PROTOBUF_C_OFFSETOF(Metric, valueui32),
    NULL,
    NULL,
    0,            /* packed */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "valueStr",
    8,
    PROTOBUF_C_LABEL_OPTIONAL,
    PROTOBUF_C_TYPE_STRING,
    0,   /* quantifier_offset */
    PROTOBUF_C_OFFSETOF(Metric, valuestr),
    NULL,
    NULL,
    0,            /* packed */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
};
static const unsigned metric__field_indices_by_name[] = {
  1,   /* field[1] = metricType */
  0,   /* field[0] = name */
  2,   /* field[2] = valueDbl */
  5,   /* field[5] = valueI32 */
  3,   /* field[3] = valueI64 */
  7,   /* field[7] = valueStr */
  6,   /* field[6] = valueUI32 */
  4,   /* field[4] = valueUI64 */
};
static const ProtobufCIntRange metric__number_ranges[1 + 1] =
{
  { 1, 0 },
  { 0, 8 }
};
const ProtobufCMessageDescriptor metric__descriptor =
{
  PROTOBUF_C_MESSAGE_DESCRIPTOR_MAGIC,
  "Metric",
  "Metric",
  "Metric",
  "",
  sizeof(Metric),
  8,
  metric__field_descriptors,
  metric__field_indices_by_name,
  1,  metric__number_ranges,
  (ProtobufCMessageInit) metric__init,
  NULL,NULL,NULL    /* reserved[123] */
};
static const ProtobufCFieldDescriptor status__field_descriptors[4] =
{
  {
    "available",
    1,
    PROTOBUF_C_LABEL_REQUIRED,
    PROTOBUF_C_TYPE_INT32,
    0,   /* quantifier_offset */
    PROTOBUF_C_OFFSETOF(Status, available),
    NULL,
    NULL,
    0,            /* packed */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "state",
    2,
    PROTOBUF_C_LABEL_REQUIRED,
    PROTOBUF_C_TYPE_INT32,
    0,   /* quantifier_offset */
    PROTOBUF_C_OFFSETOF(Status, state),
    NULL,
    NULL,
    0,            /* packed */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "duration",
    3,
    PROTOBUF_C_LABEL_REQUIRED,
    PROTOBUF_C_TYPE_INT32,
    0,   /* quantifier_offset */
    PROTOBUF_C_OFFSETOF(Status, duration),
    NULL,
    NULL,
    0,            /* packed */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "status",
    4,
    PROTOBUF_C_LABEL_REQUIRED,
    PROTOBUF_C_TYPE_STRING,
    0,   /* quantifier_offset */
    PROTOBUF_C_OFFSETOF(Status, status),
    NULL,
    NULL,
    0,            /* packed */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
};
static const unsigned status__field_indices_by_name[] = {
  0,   /* field[0] = available */
  2,   /* field[2] = duration */
  1,   /* field[1] = state */
  3,   /* field[3] = status */
};
static const ProtobufCIntRange status__number_ranges[1 + 1] =
{
  { 1, 0 },
  { 0, 4 }
};
const ProtobufCMessageDescriptor status__descriptor =
{
  PROTOBUF_C_MESSAGE_DESCRIPTOR_MAGIC,
  "Status",
  "Status",
  "Status",
  "",
  sizeof(Status),
  4,
  status__field_descriptors,
  status__field_indices_by_name,
  1,  status__number_ranges,
  (ProtobufCMessageInit) status__init,
  NULL,NULL,NULL    /* reserved[123] */
};
static const ProtobufCFieldDescriptor metadata__field_descriptors[2] =
{
  {
    "key",
    1,
    PROTOBUF_C_LABEL_REQUIRED,
    PROTOBUF_C_TYPE_STRING,
    0,   /* quantifier_offset */
    PROTOBUF_C_OFFSETOF(Metadata, key),
    NULL,
    NULL,
    0,            /* packed */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "value",
    2,
    PROTOBUF_C_LABEL_REQUIRED,
    PROTOBUF_C_TYPE_STRING,
    0,   /* quantifier_offset */
    PROTOBUF_C_OFFSETOF(Metadata, value),
    NULL,
    NULL,
    0,            /* packed */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
};
static const unsigned metadata__field_indices_by_name[] = {
  0,   /* field[0] = key */
  1,   /* field[1] = value */
};
static const ProtobufCIntRange metadata__number_ranges[1 + 1] =
{
  { 1, 0 },
  { 0, 2 }
};
const ProtobufCMessageDescriptor metadata__descriptor =
{
  PROTOBUF_C_MESSAGE_DESCRIPTOR_MAGIC,
  "Metadata",
  "Metadata",
  "Metadata",
  "",
  sizeof(Metadata),
  2,
  metadata__field_descriptors,
  metadata__field_indices_by_name,
  1,  metadata__number_ranges,
  (ProtobufCMessageInit) metadata__init,
  NULL,NULL,NULL    /* reserved[123] */
};
static const ProtobufCFieldDescriptor bundle__field_descriptors[5] =
{
  {
    "status",
    1,
    PROTOBUF_C_LABEL_OPTIONAL,
    PROTOBUF_C_TYPE_MESSAGE,
    0,   /* quantifier_offset */
    PROTOBUF_C_OFFSETOF(Bundle, status),
    &status__descriptor,
    NULL,
    0,            /* packed */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "metrics",
    2,
    PROTOBUF_C_LABEL_REPEATED,
    PROTOBUF_C_TYPE_MESSAGE,
    PROTOBUF_C_OFFSETOF(Bundle, n_metrics),
    PROTOBUF_C_OFFSETOF(Bundle, metrics),
    &metric__descriptor,
    NULL,
    0,            /* packed */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "metadata",
    3,
    PROTOBUF_C_LABEL_REPEATED,
    PROTOBUF_C_TYPE_MESSAGE,
    PROTOBUF_C_OFFSETOF(Bundle, n_metadata),
    PROTOBUF_C_OFFSETOF(Bundle, metadata),
    &metadata__descriptor,
    NULL,
    0,            /* packed */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "period",
    4,
    PROTOBUF_C_LABEL_OPTIONAL,
    PROTOBUF_C_TYPE_UINT32,
    PROTOBUF_C_OFFSETOF(Bundle, has_period),
    PROTOBUF_C_OFFSETOF(Bundle, period),
    NULL,
    NULL,
    0,            /* packed */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "timeout",
    5,
    PROTOBUF_C_LABEL_OPTIONAL,
    PROTOBUF_C_TYPE_UINT32,
    PROTOBUF_C_OFFSETOF(Bundle, has_timeout),
    PROTOBUF_C_OFFSETOF(Bundle, timeout),
    NULL,
    NULL,
    0,            /* packed */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
};
static const unsigned bundle__field_indices_by_name[] = {
  2,   /* field[2] = metadata */
  1,   /* field[1] = metrics */
  3,   /* field[3] = period */
  0,   /* field[0] = status */
  4,   /* field[4] = timeout */
};
static const ProtobufCIntRange bundle__number_ranges[1 + 1] =
{
  { 1, 0 },
  { 0, 5 }
};
const ProtobufCMessageDescriptor bundle__descriptor =
{
  PROTOBUF_C_MESSAGE_DESCRIPTOR_MAGIC,
  "Bundle",
  "Bundle",
  "Bundle",
  "",
  sizeof(Bundle),
  5,
  bundle__field_descriptors,
  bundle__field_indices_by_name,
  1,  bundle__number_ranges,
  (ProtobufCMessageInit) bundle__init,
  NULL,NULL,NULL    /* reserved[123] */
};