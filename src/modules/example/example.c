/* Your copyright and license data here */

#include "mtev_defines.h"
#include "noit_module.h"
#include "noit_check.h"
#include "noit_check_tools.h"

static mtev_log_stream_t nlerr = NULL;
static mtev_log_stream_t nldeb = NULL;


static int example_onload(mtev_image_t *self) {
  if(NULL == (nlerr=mtev_log_stream_find("error/example"))) nlerr = mtev_error;
  if(NULL == (nldeb=mtev_log_stream_find("debug/example"))) nldeb = mtev_debug;
  return 0;
}

static int example_config(noit_module_t *self, mtev_hash_table *options) {
  return 0;
}

static int example_init(noit_module_t *self) {
  return 0;
}

struct example_check_info {
  noit_check_t *check;
  noit_module_t *self;
  int limit;
};

static int example_initiate(noit_module_t *self, noit_check_t *check,
                            noit_check_t *cause) {
  struct example_check_info *ci = check->closure;
  const char *limit = "0";
  struct timeval now, diff;

  BAIL_ON_RUNNING_CHECK(check);
  check->flags |= NP_RUNNING;

  mtev_hash_retrieve(check->config, "limit", strlen("limit"), (void **)&limit);
  ci->limit = atoi(limit);

  gettimeofday(&now, NULL);
  sub_timeval(now, check->last_fire_time, &diff);
  noit_stats_set_whence(check, &now);
  noit_stats_set_duration(check, diff.tv_sec * 1000 + diff.tv_usec / 1000);
  noit_stats_set_available(check, NP_AVAILABLE);
  noit_stats_set_status(check, "hello world");

  if(ci->limit) {
    int value = (int)(lrand48() % ci->limit);
    noit_stats_set_metric(check, "random", METRIC_INT32, &value);
    noit_stats_set_state(check, NP_GOOD);
  }
  else {
    noit_stats_set_metric(check, "random", METRIC_INT32, NULL);
    noit_stats_set_state(check, NP_BAD);
  }

  noit_check_set_stats(check);
  check->flags &= ~NP_RUNNING;

  return 0;
}

static int example_initiate_check(noit_module_t *self, noit_check_t *check,
                                  int once, noit_check_t *cause) {
  struct example_check_info *ci;
  if(!check->closure)
    check->closure = calloc(1, sizeof(struct example_check_info));
  ci = check->closure;
  ci->check = check;
  ci->self = self;
  INITIATE_CHECK(example_initiate, self, check, cause);
  return 0;
}

static void example_cleanup(noit_module_t *self, noit_check_t *check) {
  struct example_check_info *ci = check->closure;
  if(ci) {
    free(ci);
  }
}

#include "example.xmlh"
noit_module_t example = {
  {
    .magic = NOIT_MODULE_MAGIC,
    .version = NOIT_MODULE_ABI_VERSION,
    .name = "example",
    .description = "a module to demonstrat module development",
    .xml_description = example_xml_description,
    .onload = example_onload
  },
  example_config,
  example_init,
  example_initiate_check,
  example_cleanup
};
