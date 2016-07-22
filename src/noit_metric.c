#include "noit_metric.h"

#include <mtev_json_object.h>
#include <circllhist.h>

#include <stdio.h>

void
noit_metric_to_json(noit_metric_message_t *metric, char **json, size_t *len, mtev_boolean include_original)
{
  if (json == NULL) {
    *len = 0;
    return;
  }

  struct mtev_json_object *o = mtev_json_object_new_object();

  char type[2] = {0};
  sprintf(type, "%c", metric->type);
  mtev_json_object_object_add(o, "type", mtev_json_object_new_string(type));
  struct mtev_json_object *whence = mtev_json_object_new_int(0);
  mtev_json_object_set_int_overflow(whence, mtev_json_overflow_uint64);
  mtev_json_object_set_uint64(whence, metric->value.whence_ms);
  mtev_json_object_object_add(o, "timestamp_ms", whence);

  char uuid_str[UUID_PRINTABLE_STRING_LENGTH] = {0};
  uuid_unparse_lower(metric->id.id, uuid_str);

  mtev_json_object_object_add(o, "check_uuid", mtev_json_object_new_string(uuid_str));

  if (metric->value.type == METRIC_NULL || metric->value.type == METRIC_ABSENT) {
    mtev_json_object_object_add(o, "value_type", NULL);
  } else {
    char value_type[2] = {0};
    sprintf(value_type, "%c", metric->value.type);
    mtev_json_object_object_add(o, "value_type", mtev_json_object_new_string(value_type));
  }

  if (metric->type == MESSAGE_TYPE_M) {
    char name[metric->id.name_len + 1];
    strncpy(name, metric->id.name, metric->id.name_len);
    name[metric->id.name_len] = '\0';
    struct mtev_json_object *int_value = mtev_json_object_new_int(metric->value.value.v_int32);

    switch (metric->value.type) {
    case METRIC_GUESS:
    case METRIC_INT32:
      mtev_json_object_object_add(o, name, int_value);
      break;

    case METRIC_UINT32:
      mtev_json_object_set_int_overflow(int_value, mtev_json_overflow_uint64);
      mtev_json_object_set_uint64(int_value, metric->value.value.v_uint32);
      mtev_json_object_object_add(o, name, int_value);
      break;

    case METRIC_INT64:
      mtev_json_object_set_int_overflow(int_value, mtev_json_overflow_int64);
      mtev_json_object_set_int64(int_value, metric->value.value.v_int64);
      mtev_json_object_object_add(o, name, int_value);
      break;

    case METRIC_UINT64:
      mtev_json_object_set_int_overflow(int_value, mtev_json_overflow_uint64);
      mtev_json_object_set_uint64(int_value, metric->value.value.v_uint64);
      mtev_json_object_object_add(o, name, int_value);
      break;

    case METRIC_DOUBLE:
      {
        mtev_json_object_object_add(o, name, mtev_json_object_new_double(metric->value.value.v_double));
        break;

      }
    case METRIC_STRING:
      mtev_json_object_object_add(o, name, mtev_json_object_new_string(metric->value.value.v_string));
      break;
    default:
      mtev_json_object_object_add(o, name, NULL);
      break;
    }
  } else if (metric->type == MESSAGE_TYPE_S) {
    char *status = strdup(metric->id.name);
    const char *field = strtok(status, "\t");
    if (field != NULL) {
      mtev_json_object_object_add(o, "state", mtev_json_object_new_string(field));
    }
    field = strtok(NULL, "\t");
    if (field != NULL) {
      mtev_json_object_object_add(o, "available", mtev_json_object_new_string(field));
    }
    field = strtok(NULL, "\t");
    if (field != NULL) {
      mtev_json_object_object_add(o, "duration", mtev_json_object_new_int(atoi(field)));
    }
    field = strtok(NULL, "\t");
    if (field != NULL) {
      mtev_json_object_object_add(o, "status", mtev_json_object_new_string(field));
    }
  } else if (metric->type == MESSAGE_TYPE_H) {
    histogram_t *histo = hist_alloc();
    ssize_t s = hist_deserialize_b64(histo, metric->value.value.v_string, strlen(metric->value.value.v_string));
    if (s > 0) {
      struct mtev_json_object *histogram = mtev_json_object_new_array();
      for (int i = 0; i < hist_bucket_count(histo); i++) {
        /* for each bvs in the histogram, create a "bucket" object in the json */
        struct mtev_json_object *bucket = mtev_json_object_new_object();
        double b = 0.0;
        u_int64_t bc = 0;
        hist_bucket_idx(histo, i, &b, &bc);
        mtev_json_object_object_add(bucket, "bucket",
                                    mtev_json_object_new_double(b));
        struct mtev_json_object *count_o = mtev_json_object_new_int(0);
        mtev_json_object_set_int_overflow(count_o, mtev_json_overflow_uint64);
        mtev_json_object_set_int64(count_o, bc);

        mtev_json_object_object_add(bucket, "count", count_o);
        mtev_json_object_array_add(histogram, bucket);
      }
      hist_free(histo);
      char name[metric->id.name_len + 1];
      strncpy(name, metric->id.name, metric->id.name_len);
      name[metric->id.name_len] = '\0';
      mtev_json_object_object_add(o, name, histogram);
    }
  }

  const char *j = mtev_json_object_to_json_string(o);
  *json = strdup(j);
  *len = strlen(j);
  mtev_json_object_put(o);
}
