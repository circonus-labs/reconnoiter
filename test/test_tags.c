#include "noit_metric.h"
#include "noit_metric_tag_search.h"
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

void test_ast_decode()
{
  int erroroffset;

  /* simple test */
  noit_metric_tag_search_ast_t *ast = noit_metric_tag_search_parse("and(foo:bar)", &erroroffset);
  assert(ast != NULL);
  assert(ast->operation == OP_AND_ARGS);
  assert(ast->contents.args.node[0]->operation == OP_MATCH);
  assert(strcmp(ast->contents.args.node[0]->contents.spec.cat.str,"foo") == 0);
  assert(strcmp(ast->contents.args.node[0]->contents.spec.name.str,"bar") == 0);
  noit_metric_tag_search_free(ast);
  
  /* base64 fixed category */
  ast = noit_metric_tag_search_parse("and(foo:bar,not(b\"c29tZTpzdHVmZltoZXJlXQ==\":value))", &erroroffset);
  assert(ast != NULL);
  assert(ast->operation == OP_AND_ARGS);
  assert(ast->contents.args.node[0]->operation == OP_MATCH);
  assert(strcmp(ast->contents.args.node[0]->contents.spec.cat.str,"foo") == 0);
  assert(strcmp(ast->contents.args.node[0]->contents.spec.name.str,"bar") == 0);
  assert(ast->contents.args.node[1]->operation == OP_NOT_ARGS);
  noit_metric_tag_search_ast_t *not = ast->contents.args.node[1]->contents.args.node[0];
  assert(not != NULL);
  assert(strcmp(not->contents.spec.cat.str,"some:stuff[here]") == 0);
  assert(strcmp(not->contents.spec.name.str,"value") == 0);
  noit_metric_tag_search_free(ast);

  /* base64 regex parse */
  ast = noit_metric_tag_search_parse("and(foo:bar,not(b/c29tZS4q/:value))", &erroroffset);
  assert(ast != NULL);
  assert(ast->operation == OP_AND_ARGS);
  assert(ast->contents.args.node[0]->operation == OP_MATCH);
  assert(strcmp(ast->contents.args.node[0]->contents.spec.cat.str,"foo") == 0);
  assert(strcmp(ast->contents.args.node[0]->contents.spec.name.str,"bar") == 0);
  assert(ast->contents.args.node[1]->operation == OP_NOT_ARGS);
  not = ast->contents.args.node[1]->contents.args.node[0];
  assert(not != NULL);
  assert(strcmp(not->contents.spec.cat.str,"some.*") == 0);
  assert(not->contents.spec.cat.re != NULL);
  assert(strcmp(not->contents.spec.name.str,"value") == 0);
  noit_metric_tag_search_free(ast);
  
}

int main(int argc, const char **argv)
{
  test_tag_decode();
  test_ast_decode();
  return 0;
}
