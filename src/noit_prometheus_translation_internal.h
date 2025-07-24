#ifndef NOIT_PROMETHEUS_TRANSLATION_INTERNAL_H
#define NOIT_PROMETHEUS_TRANSLATION_INTERNAL_H

#include "noit_prometheus_translation.h"
#include "prometheus.pb-c.h"
#include "prometheus_types.pb-c.h"

/* Any functions that depend on generated Prometheus headers go here; they can be called
 * within reconnoiter, but can't be called by external libnoit users as they may not
 * have the required generated headers */

#ifdef __cplusplus
extern "C" {
#endif

prometheus_metric_name_t *noit_prometheus_metric_name_from_labels(Prometheus__Label **labels,
                                                                  size_t label_count,
                                                                  const char *units,
                                                                  bool coerce_hist);

prometheus_coercion_t noit_prometheus_metric_name_coerce(Prometheus__Label **labels,
                                                         size_t label_count,
                                                         bool do_units,
                                                         bool do_hist,
                                                         const char **allowed_units);
noit_metric_message_t *
noit_prometheus_translate_to_noit_metric_message(prometheus_coercion_t *coercion,
                                                 const int64_t account_id,
                                                 const uuid_t check_uuid,
                                                 const prometheus_metric_name_t *metric_name,
                                                 const Prometheus__Sample *sample);

#ifdef __cplusplus
}
#endif
#endif
