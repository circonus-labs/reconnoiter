local ffi = require('ffi');

ffi.cdef[[
typedef enum { mtev_false = 0, mtev_true } mtev_boolean;
typedef unsigned char uuid_t[16];
typedef enum {
  METRIC_ABSENT = 0,
  METRIC_GUESS = '0',
  METRIC_INT32 = 'i',
  METRIC_UINT32 = 'I',
  METRIC_INT64 = 'l',
  METRIC_UINT64 = 'L',
  METRIC_DOUBLE = 'n',
  METRIC_STRING = 's'
} noit_metric_type_t;

typedef struct {
  char *metric_name;
  noit_metric_type_t metric_type;
  union {
    double *n;
    int32_t *i;
    uint32_t *I;
    int64_t *l;
    uint64_t *L;
    char *s;
    void *vp; /* used for clever assignments */
  } metric_value;
  mtev_boolean logged;
  unsigned long accumulator;
} noit_metric_t;

typedef enum {
  MESSAGE_TYPE_C = 'C',
  MESSAGE_TYPE_D = 'D',
  MESSAGE_TYPE_S = 'S',
  MESSAGE_TYPE_H = 'H',
  MESSAGE_TYPE_M = 'M'
} noit_message_type;

typedef struct metric_id_t {
  uuid_t id;
  const char *name;
  int name_len;
} noit_metric_id_t;

typedef struct {
  uint64_t whence_ms; /* when this was recieved */
  noit_metric_type_t type; /* the type of the following data item */
  mtev_boolean is_null;
  union {
    int32_t v_int32;
    uint32_t v_uint32;
    int64_t v_int64;
    uint64_t v_uint64;
    double v_double;
    char *v_string;
  } value; /* the data itself */
  mtev_boolean is_null;
} noit_metric_value_t;

typedef struct metric_message_t {
  noit_metric_id_t id;
  noit_metric_value_t value;
  noit_message_type type;
  char* original_message;
} noit_metric_message_t;

typedef struct {
  noit_metric_type_t type;
  uint16_t count;
  uint8_t stddev_present;
  float stddev;
  union {
    int32_t v_int32;
    uint32_t v_uint32;
    int64_t v_int64;
    uint64_t v_uint64;
    double v_double;
  } value;
  float derivative;
  float derivative_stddev;
  float counter;
  float counter_stddev;
} nnt_multitype;

typedef struct {
  noit_metric_value_t last_value;
  nnt_multitype accumulated;
  int drun;
  int crun;
  uint64_t first_value_time_ms;
} noit_numeric_rollup_accu;

void noit_metric_rollup_accumulate_numeric(noit_numeric_rollup_accu* accumulator, noit_metric_value_t* value);

]]
return ffi.load("libnoit") 
