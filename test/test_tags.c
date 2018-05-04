#include <stdio.h>
#include "noit_metric.h"
#include "noit_metric_tag_search.h"
#include "noit_message_decoder.h"
#include <assert.h>
#include <sys/time.h>

int ntest = 0;
int failures = 0;
#define test_assert(a) do { \
  ntest++; \
  if(a) { \
    printf("ok - %d (%s)\n", ntest, #a); \
  } else { \
    printf("not ok - %d (%s)\n", ntest, #a); \
    failures++; \
  } \
} while(0)

void test_tag_decode()
{
  char decoded[512] = {0};
  
  /* echo -n "foo:bar[stuff]" | base64 */
  const char *encoded = "b\"Zm9vOmJhcltzdHVmZl0=\":value";
  int rval  = noit_metric_tagset_decode_tag(decoded, sizeof(decoded),
					    encoded, strlen(encoded));
  test_assert(rval > 0);

  int eq = strncmp("foo:bar[stuff]\037value", decoded, rval);
  test_assert(eq == 0);

  encoded = "b\"Zm9vOmJhcltzdHVmZl0=\":b\"Zm9vOmJhcltzdHVmZl0=\"";
  rval = noit_metric_tagset_decode_tag(decoded, sizeof(decoded),
				       encoded, strlen(encoded));
  test_assert(rval > 0);

  eq = strncmp("foo:bar[stuff]\037foo:bar[stuff]", decoded, rval);
  test_assert(eq == 0);

  encoded = "category:b\"Zm9vOmJhcltzdHVmZl0=\"";
  rval = noit_metric_tagset_decode_tag(decoded, sizeof(decoded),
				       encoded, strlen(encoded));
  test_assert(rval > 0);

  eq = strncmp("category\037foo:bar[stuff]", decoded, rval);
  test_assert(eq == 0);

  /* normal tags should just pass through with copies */
  encoded = "category:value";
  rval = noit_metric_tagset_decode_tag(decoded, sizeof(decoded),
				       encoded, strlen(encoded));
  test_assert(rval > 0);

  eq = strncmp("category\037value", decoded, rval);
  test_assert(eq == 0);

}

void test_ast_decode()
{
  int erroroffset;

  /* simple test */
  noit_metric_tag_search_ast_t *ast = noit_metric_tag_search_parse("and(foo:bar)", &erroroffset);
  test_assert(ast != NULL);
  test_assert(ast->operation == OP_AND_ARGS);
  test_assert(ast->contents.args.node[0]->operation == OP_MATCH);
  test_assert(strcmp(ast->contents.args.node[0]->contents.spec.cat.str,"foo") == 0);
  test_assert(strcmp(ast->contents.args.node[0]->contents.spec.name.str,"bar") == 0);
  noit_metric_tag_search_free(ast);
  
  /* base64 fixed category */
  ast = noit_metric_tag_search_parse("and(foo:bar,not(b\"c29tZTpzdHVmZltoZXJlXQ==\":value))", &erroroffset);
  test_assert(ast != NULL);
  test_assert(ast->operation == OP_AND_ARGS);
  test_assert(ast->contents.args.node[0]->operation == OP_MATCH);
  test_assert(strcmp(ast->contents.args.node[0]->contents.spec.cat.str,"foo") == 0);
  test_assert(strcmp(ast->contents.args.node[0]->contents.spec.name.str,"bar") == 0);
  test_assert(ast->contents.args.node[1]->operation == OP_NOT_ARGS);
  noit_metric_tag_search_ast_t *not = ast->contents.args.node[1]->contents.args.node[0];
  test_assert(not != NULL);
  test_assert(strcmp(not->contents.spec.cat.str,"some:stuff[here]") == 0);
  test_assert(strcmp(not->contents.spec.name.str,"value") == 0);
  noit_metric_tag_search_free(ast);

  /* base64 regex parse */
  ast = noit_metric_tag_search_parse("and(foo:bar,not(b/c29tZS4q/:value))", &erroroffset);
  test_assert(ast != NULL);
  test_assert(ast->operation == OP_AND_ARGS);
  test_assert(ast->contents.args.node[0]->operation == OP_MATCH);
  test_assert(strcmp(ast->contents.args.node[0]->contents.spec.cat.str,"foo") == 0);
  test_assert(strcmp(ast->contents.args.node[0]->contents.spec.name.str,"bar") == 0);
  test_assert(ast->contents.args.node[1]->operation == OP_NOT_ARGS);
  not = ast->contents.args.node[1]->contents.args.node[0];
  test_assert(not != NULL);
  test_assert(strcmp(not->contents.spec.cat.str,"some.*") == 0);
  test_assert(not->contents.spec.cat.re != NULL);
  test_assert(strcmp(not->contents.spec.name.str,"value") == 0);
  noit_metric_tag_search_free(ast);
  
}

void test_tag_match()
{
  int erroroffset;
  noit_metric_tagset_t tagset;
  noit_metric_tagset_builder_t builder;
  noit_metric_tagset_builder_start(&builder);

  const char *tagstring = "foo:bar,b\"c29tZTpzdHVmZltoZXJlXQ==\":value";
  noit_metric_tagset_builder_add_many(&builder, tagstring, strlen(tagstring));
  char *canonical;
  noit_metric_tagset_builder_end(&builder, &tagset, &canonical);
  
  /* simple test */
  noit_metric_tag_search_ast_t *ast = noit_metric_tag_search_parse("and(foo:bar)", &erroroffset);
  mtev_boolean match = noit_metric_tag_search_evaluate_against_tags(ast, &tagset);
  test_assert(match == mtev_true);
  noit_metric_tag_search_free(ast);

  ast = noit_metric_tag_search_parse("and(foo:bar,b\"c29tZTpzdHVmZltoZXJlXQ==\":value)", &erroroffset);
  match = noit_metric_tag_search_evaluate_against_tags(ast, &tagset);
  test_assert(match == mtev_true);
  noit_metric_tag_search_free(ast);

  ast = noit_metric_tag_search_parse("and(b/c29tZS4q/:value)", &erroroffset);
  match = noit_metric_tag_search_evaluate_against_tags(ast, &tagset);
  test_assert(match == mtev_true);
  noit_metric_tag_search_free(ast);

  ast = noit_metric_tag_search_parse("and(quux:value)", &erroroffset);
  match = noit_metric_tag_search_evaluate_against_tags(ast, &tagset);
  test_assert(match == mtev_false);
  noit_metric_tag_search_free(ast);
}

void metric_parsing(void) {
  int len;
  char buff[NOIT_TAG_MAX_PAIR_LEN], dbuff[NOIT_TAG_MAX_PAIR_LEN], ebuff[NOIT_TAG_MAX_PAIR_LEN];

  snprintf(buff, sizeof(buff), "@#lkm45lsnd:kljnmsdkflnsdf:kjnsdkfjnsdf");
  len = noit_metric_tagset_decode_tag(dbuff, sizeof(dbuff), buff, strlen(buff));
  test_assert(len > 0);
  len = noit_metric_tagset_encode_tag(ebuff, sizeof(ebuff), dbuff, len);
  test_assert(len > 0);
  test_assert(strcmp(buff, ebuff) == 0);

  snprintf(buff, sizeof(buff), "b\"Zm9v\":http://12.3.3.4:80/this?is=it");
  len = noit_metric_tagset_decode_tag(dbuff, sizeof(dbuff), buff, strlen(buff));
  test_assert(len > 0);
  len = noit_metric_tagset_encode_tag(ebuff, sizeof(ebuff), dbuff, len);
  test_assert(len > 0);
  test_assert(strcmp("foo:http://12.3.3.4:80/this?is=it", ebuff) == 0);

  char *hbuff;
  noit_metric_message_t message;
  int rval;

  hbuff = "H1\t1525385460.000\tpush`httptrap`c_933_247631::httptrap`43e5c324-44c2-4877-a625-3b4c8230f2eb\tSuperSimpleMetricName|ST[a:b,c:d]\tAAUK/wACDP8AARH/AAEa/wABVP8AAQ==";
  memset(&message, 0, sizeof(message));
  message.original_message = hbuff;
  rval = noit_message_decoder_parse_line(&message, 0);
  test_assert(message.id.alloc_name == NULL);
  test_assert(rval == 1);
  noit_metric_message_clear(&message);

  hbuff = "H1\t1525385460.000\tpush`httptrap`c_933_247631::httptrap`43e5c324-44c2-4877-a625-3b4c8230f2eb\t/transmissions`latency|ST[b\"bjo6Og==\":b\"YT1i\",customer:noone,node:j.mta2vrest.cc.aws-usw2a.prd.acme,cluster:mta2]\tAAUK/wACDP8AARH/AAEa/wABVP8AAQ==";
  memset(&message, 0, sizeof(message));
  message.original_message = hbuff;
  rval = noit_message_decoder_parse_line(&message, 0);
  test_assert(message.id.alloc_name);
  test_assert(rval == 1);
  noit_metric_message_clear(&message);

  /* This one has a stray { in one of the tag keys. */  
  hbuff = "H1\t1525385460.000\tpush`httptrap`c_933_247631::httptrap`43e5c324-44c2-4877-a625-3b4c8230f2eb\t/transmissions`latency|ST[b\"bjo6Og==\":b\"YT1i\",c{ustomer:noone,node:j.mta2vrest.cc.aws-usw2a.prd.acme,cluster:mta2]\tAAUK/wACDP8AARH/AAEa/wABVP8AAQ==";
  memset(&message, 0, sizeof(message));
  message.original_message = hbuff;
  rval = noit_message_decoder_parse_line(&message, 0);
  test_assert(message.id.alloc_name == NULL);
  test_assert(rval < 0);
  noit_metric_message_clear(&message);

  hbuff = strdup("/transmissions`latency|ST[b\"bjo6Og==\":b\"YT1i\",customer:noone,node:j.mta2vrest.cc.aws-usw2a.prd.acme,cluster:mta2]");
  len = noit_metric_canonicalize(hbuff, strlen(hbuff), hbuff, strlen(hbuff), mtev_true);
  test_assert(len > 0);
  test_assert(!strcmp(hbuff, "/transmissions`latency|ST[cluster:mta2,customer:noone,b\"bjo6Og==\":a=b,node:j.mta2vrest.cc.aws-usw2a.prd.acme]"));
  free(hbuff);

  hbuff = strdup("woop|ST[a:b,c:d]|MT{foo:bar}|ST[c:d,e:f,a:b]");
  len = noit_metric_canonicalize(hbuff, strlen(hbuff), hbuff, strlen(hbuff), mtev_true);
  test_assert(len > 0);
  test_assert(!strcmp(hbuff, "woop|ST[a:b,c:d,e:f]|MT{foo:bar}"));
  free(hbuff);

}

void loop(char *str) {
  int len;
  const int nloop = 1000000;
  struct timeval start, end;
  char *hbuff = strdup(str);
  int hlen = strlen(hbuff);
  char obuff[MAX_METRIC_TAGGED_NAME];
  gettimeofday(&start, NULL);
  for(int i=0; i<nloop; i++) {
    len = noit_metric_canonicalize(hbuff, hlen, obuff, sizeof(obuff), mtev_true);
    assert(len > 0);
  }
  gettimeofday(&end, NULL);
  double elapsed = sub_timeval_d(end, start);
  free(hbuff);
  printf("canonicalize('%s') -> %f ns/op\n", str, (elapsed * 1000000000.0) / (double)nloop);
}
int main(int argc, const char **argv)
{
  test_tag_decode();
  test_ast_decode();
  metric_parsing();
  loop("woop|ST[a:b,c:d]|MT{foo:bar}|ST[c:d,e:f,a:b]");
  loop("testing_this|ST[cluster:mta2,customer:noone,b\"bjo6Og==\":a=b,node:j.mta2vrest.prd.acme]");
  loop("testing_this_long_untagged_metric");
  return !(failures == 0);
}
