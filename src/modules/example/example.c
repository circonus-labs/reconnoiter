/* Your copyright and license data here */

#include "noit_defines.h"
#include "noit_module.h"
#include "noit_check.h"
#include "noit_check_tools.h"

static noit_log_stream_t nlerr = NULL;
static noit_log_stream_t nldeb = NULL;


static int example_onload(noit_image_t *self) {
  if(NULL == (nlerr=noit_log_stream_find("error/example"))) nlerr = noit_error;
  if(NULL == (nldeb=noit_log_stream_find("debug/example"))) nldeb = noit_debug;
  return 0;
}

static int example_config(noit_module_t *self, noit_hash_table *options) {
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
  struct timeval diff;

  BAIL_ON_RUNNING_CHECK(check);
  check->flags |= NP_RUNNING;

  noit_hash_retrieve(check->config, "limit", strlen("limit"), (void **)&limit);
  ci->limit = atoi(limit);

  if(check->stats.inprogress.status) free(check->stats.inprogress.status);
  noit_check_stats_clear(check, &check->stats.inprogress); 
  gettimeofday(&check->stats.inprogress.whence, NULL);
  sub_timeval(check->stats.inprogress.whence, check->last_fire_time, &diff);
  check->stats.inprogress.duration = diff.tv_sec * 1000 + diff.tv_usec / 1000;
  check->stats.inprogress.available = NP_AVAILABLE;
  check->stats.inprogress.status = strdup("hello world");

  if(ci->limit) {
    int value = (int)(lrand48() % ci->limit);
    noit_stats_set_metric(check, &check->stats.inprogress,
                          "random", METRIC_INT32, &value);
    check->stats.inprogress.state = NP_GOOD;
  }
  else {
    noit_stats_set_metric(check, &check->stats.inprogress,
                          "random", METRIC_INT32, NULL);
    check->stats.inprogress.state = NP_BAD;
  }

  noit_check_set_stats(check, &check->stats.inprogress);
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
