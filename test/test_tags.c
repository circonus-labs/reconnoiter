#include "noit_metric.h"
#include <assert.h>

void test_tag_decode()
{
  char decoded[512] = {0};
  
  /* echo -n "foo:bar[stuff]" | base64 */
  const char *encoded = "b\"Zm9vOmJhcltzdHVmZl0=\":value";
  int rval  = noit_metric_tagset_decode_tag(decoded, sizeof(decoded),
					    encoded, strlen(encoded));
  assert(rval > 0);

  int eq = strncmp("foo:bar[stuff]\037value", decoded, rval);
  assert(eq == 0);

  encoded = "b\"Zm9vOmJhcltzdHVmZl0=\":b\"Zm9vOmJhcltzdHVmZl0=\"";
  rval = noit_metric_tagset_decode_tag(decoded, sizeof(decoded),
				       encoded, strlen(encoded));
  assert(rval > 0);

  eq = strncmp("foo:bar[stuff]\037foo:bar[stuff]", decoded, rval);
  assert(eq == 0);

  encoded = "category:b\"Zm9vOmJhcltzdHVmZl0=\"";
  rval = noit_metric_tagset_decode_tag(decoded, sizeof(decoded),
				       encoded, strlen(encoded));
  assert(rval > 0);

  eq = strncmp("category\037foo:bar[stuff]", decoded, rval);
  assert(eq == 0);

  /* normal tags should just pass through with copies */
  encoded = "category:value";
  rval = noit_metric_tagset_decode_tag(decoded, sizeof(decoded),
				       encoded, strlen(encoded));
  assert(rval > 0);

  eq = strncmp("category\037value", decoded, rval);
  assert(eq == 0);
}

int main(int argc, const char **argv)
{
  test_tag_decode();
  return 0;
}
