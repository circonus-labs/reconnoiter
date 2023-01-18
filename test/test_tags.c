#include <stdio.h>
#include <string.h>
#include "noit_metric.h"
#include "noit_metric_tag_search.h"
#include "noit_message_decoder.h"
#include "libnoit.h"
#include <mtev_hash.h>
#include <mtev_b64.h>
#include <mtev_perftimer.h>
#include <assert.h>
#include <sys/time.h>

bool benchmark = false;
const int BENCH_ITERS = 1000000;

const char *tcpairs[][2] = {

  { "/transmissions`latency|ST[b\"bjo6Og==\":b\"YT1i\",customer:noone,node:j.mta2vrest.cc.aws-usw2a.prd.acme,cluster:mta2]",
    "/transmissions`latency|ST[cluster:mta2,customer:noone,b\"bjo6Og==\":a=b,node:j.mta2vrest.cc.aws-usw2a.prd.acme]" },

  { "metric_name|ST[abcd]",
    "metric_name|ST[abcd:]" },

  { "woop|ST[a:b,c:d]|MT{foo:bar}|ST[c:d,e:f,a:b]",
    "woop|ST[a:b,c:d,e:f]|MT{foo:bar}" },

  { "woop|ST[a:b,e,c:d]|MT{foo:bar}",
    "woop|ST[a:b,c:d,e:]|MT{foo:bar}" },

  { "woop|ST[a:b,e:,c:d]|MT{foo:bar}",
    "woop|ST[a:b,c:d,e:]|MT{foo:bar}" },

  { "woop|ST[a:b,c:d]|MT{foo:bar}|ST[e]",
    "woop|ST[a:b,c:d,e:]|MT{foo:bar}" },

  { "woop|ST[a:b,c:d]|MT{foo:bar}|ST[e:]",
    "woop|ST[a:b,c:d,e:]|MT{foo:bar}" },

  { "simple string with spaces",
    "simple string with spaces" },

  { "trailing space|ST[foo:bar] ", NULL },
  { "colonfest|ST[f:o:ba::::r:]", "colonfest|ST[f:o:ba::::r:]" },

  { "  a\n\t stupid\bbad person did this \t\r\n|ST[foo:bar]",
    "a   stupid bad person did this|ST[foo:bar]" },

  { "blank_values|ST[a:b,c:]|MT{foo:bar}|ST[e:f,a:b]",
    "blank_values|ST[a:b,c:,e:f]|MT{foo:bar}" },

  /* long val */
  { "metric|ST[short:tag,superlongtagthatismorethan256characterstotal:xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx,another:acceptabletag]",
    "metric|ST[another:acceptabletag,short:tag,superlongtagthatismorethan256characterstotal:xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx_tldr_8e2bd46fa751af2d2b093c2f5465f786cbe2b8a9]" },

  /* long cat and long val */
  { "metric|ST[short:tag,superlongtagcatthatismorethan256characterstotalyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyy:xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx,another:acceptabletag]",
    "metric|ST[another:acceptabletag,short:tag,superlongtagcatthatismorethan256characterstotalyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyy_tldr_2584f4690f561d00c607ad5333d7c31a3f06664c:xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx_tldr_8e2bd46fa751af2d2b093c2f5465f786cbe2b8a9]" },

  /* long (but not too long) cat */
  { "metric|ST[short:tag,superlongtagcatthatismorethan256characterstotalyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyy:xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx,another:acceptabletag]",
    "metric|ST[another:acceptabletag,short:tag,superlongtagcatthatismorethan256characterstotalyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyy_tldr_ce6340dfd8ee2343280ad9723eb4d54d87ebc435:xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx]" },

};

const struct {
  const char *name;
  const char *input;
  const char *output;
  int rval;
  mtev_boolean allocd;
} testlines[] = {
  {
    "test1",
    "H1\t1525385460.000\tpush`httptrap`c_933_247631::httptrap`43e5c324-44c2-4877-a625-3b4c8230f2eb\tSuperSimpleMetricName|ST[a:b,c:d]\tAAUK/wACDP8AARH/AAEa/wABVP8AAQ==",
    "SuperSimpleMetricName|ST[a:b,c:d]",
    1, mtev_false
  },
  {
    "test2 (unordered and overly encoded)",
    "H1\t1525385460.000\tpush`httptrap`c_933_247631::httptrap`43e5c324-44c2-4877-a625-3b4c8230f2eb\t/transmissions`latency|ST[b\"bjo6Og==\":b\"YT1i\",customer:noone,node:j.mta2vrest.cc.aws-usw2a.prd.acme,cluster:mta2]\tAAUK/wACDP8AARH/AAEa/wABVP8AAQ==",
    "/transmissions`latency|ST[cluster:mta2,customer:noone,b\"bjo6Og==\":a=b,node:j.mta2vrest.cc.aws-usw2a.prd.acme]",
    1, mtev_true
  },
  {
    "test3 (invalid tag)",
    "H1\t1525385460.000\tpush`httptrap`c_933_247631::httptrap`43e5c324-44c2-4877-a625-3b4c8230f2eb\t/transmissions`latency|ST[b\"bjo6Og==\":b\"YT1i\",c{ustomer:noone,node:j.mta2vrest.cc.aws-usw2a.prd.acme,cluster:mta2]\tAAUK/wACDP8AARH/AAEa/wABVP8AAQ==",
    NULL,
    -7, mtev_false
  },
  {
    "test4 (spacefest)",
    "H1\t1525385460.000\tpush`httptrap`c_933_247631::httptrap`43e5c324-44c2-4877-a625-3b4c8230f2eb\t \bSuperSpacey\r\n|ST[a:b,c:d]\tAAUK/wACDP8AARH/AAEa/wABVP8AAQ==",
    "SuperSpacey|ST[a:b,c:d]",
    1, mtev_true
  },
  {
    "test5",
    "M\t127.0.0.1\t1526493506.214\tunknown`fault`c_1_77`4766c496-2173-4f60-9607-6449d29cac56\tfoo|ST[color:orange]\ti\t100\n",
    "foo|ST[color:orange]",
    1, mtev_false
   },
};

const char *testtags[][2] = {
  { "b\"Zm9vOmJhcltzdHVmZl0=\":value", "foo:bar[stuff]\037value" },
  { "b\"Zm9vOmJhcltzdHVmZl0=\":b\"Zm9vOmJhcltzdHVmZl0=\"", "foo:bar[stuff]\037foo:bar[stuff]" },
  { "category:b\"Zm9vOmJhcltzdHVmZl0=\"", "category\037foo:bar[stuff]" },
  { "category:value", "category\037value" }
};

struct Matches {
  const char *tagstring;
  struct {
    const char *query;
    mtev_boolean match;
    uint64_t bench_ns;
  } queries[14];
};

struct Matches testmatches[] = {
  { 
    "__name:f1.f2.f3.f4.f5.f6",
    {
      // f1.{f2,foo}.f{3,4,5}.*.*.f6 (encoded on next line)
      { "and(__name:b[graphite]\"ZjEue2YyLGZvb30uZnszLDQsNX0uKi4qLmY2\")", mtev_true },
      { "and(__name:[graphite]f1.**.f6)", mtev_true },
      { "and(__name:[graphite]f1.*.f6)", mtev_false },
      { "and(__name:[graphite]f2.**.f6)", mtev_false },
      { "and(__name:[graphite]f1.**)", mtev_true },
      { "and(__name:[graphite]f1.f2.*.f4.f5.f6)", mtev_true },
      { "and(__name:[graphite]f1.f2.f3.f4.f5.f6)", mtev_true },
      { NULL, 0 }
    }
  },
  {
    "foo:bar,b\"c29tZTpzdHVmZltoZXJlXQ==\":/value,empty:",
    {
      { "and(foo:bar)", mtev_true },
      { "and(foo:/)", mtev_false },
      { "and(/^f(o{2})$/:/(b|a){2,2}r/)", mtev_true },
      { "and(foo:bar,b\"c29tZTpzdHVmZltoZXJlXQ==\":/value)", mtev_true },
      { "and(b/c29tZS4q/:/value)", mtev_true },
      { "and(quux:/value)", mtev_false },
      { "and([exact]\"some:stuff[here]\":/value)", mtev_true },
      { "and(empty:/^$/)", mtev_true },
      { "and(empty:)", mtev_true },
      { "and(empty)", mtev_true },
      { "not(empty:/^$/)", mtev_false },
      { "not(empty:)", mtev_false },
      { "not(empty)", mtev_false },
      { NULL, 0 }
    }
  },
  {
    "b\"KGZvbyk=\":bar",
    {
      { "and(*:*)", mtev_true },
      { "and(*:bar)", mtev_true },
      { "and(foo:bar)", mtev_false },
      { "and(*:/ba(.?)r/)", mtev_true },
      { "and(b\"KGZvbyk=\":bar)", mtev_true },
      { "and(b/XChm/:bar)", mtev_true },
      { NULL, 0 }
    }
  },
  {
    "b\"W2Jhcl0=\":b\"Kipmb28qKg==\"",
    {
      { "and(b\"W2Jhcl0=\":*)", mtev_true },
      { "and(*:b!Kipmb28qKg==!)", mtev_true },
      { NULL, 0 }
    }
  },
  {
    "b\"Kipmb28qKg==\":b\"W2Jhcl0=\"",
    {
      { "and(*:b\"W2Jhcl0=\")", mtev_true },
      { "and(b\"Kipmb28qKg==\":*)", mtev_true },
      { NULL, 0 }
    }
  },
  {
    "b\"P2Zvbz8=\":b\"W2Jhcl0=\"",
    {
      { "and(*:b\"W2Jhcl0=\")", mtev_true },
      { "and(b\"P2Yq\":*)", mtev_true },
      { NULL, 0 }
    }
  },
  {
    "b\"Lz9oaXN0b2dyYW0vKC4rKSQp\":quux",
    {
      { "and(*:*)", mtev_true },
      { "and(hint(*:*))", mtev_true },
      { "and(hint(*:*,index:none))", mtev_true },
      { "hint(and(b!Lz9oaXN0b2dyYW0vKC4rKSQp!:quux),foo:bar)", mtev_true },
      { NULL, 0 }
    }
  },
  {
    "b\"L2Zvby8oXig/OlswLTldezJ9LSkvKC4rKSQp\":bar",
    {
      { "and(*:bar)", mtev_true },
      { "and(b\"L2Zvby8oXig/OlswLTldezJ9LSkvKC4rKSQp\":bar)", mtev_false },
      { "and(b!L2Zvby8oXig/OlswLTldezJ9LSkvKC4rKSQp!:bar)", mtev_true },
      { NULL, 0 }
    }
  },
  {
    "tag:this_tag_pair_is_too_long_0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000257",
    {
      { "and(tag:*too_long*)", mtev_false },
      { NULL, 0 }
    }
  },
  {
    "tag1:value1,"
    "tag2:value2,"
    "tag3:this_is_the_max_length_of_a_tag_pair_000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000256,"
    "tag4:value4",
    {
      { "and(tag1:value1)", mtev_true },
      { "and(tag2:value2)", mtev_true },
      { "and(tag3:*max_length*)", mtev_true },
      { "and(tag3:*256)", mtev_true },
      { "and(tag4:value4)", mtev_true },
      { NULL, 0 }
    }
  },
  { 
    "tag1:value1,"
    "tag2:value2,"
    "tag3:this_is_the_max_length_of_a_tag_pair_000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000256,"
    "tag4:this_tag_pair_is_too_long_000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000257,"
    "tag5:value5",
    {
      { "and(tag1:value1)", mtev_true },
      { "and(tag2:value2)", mtev_true },
      { "and(tag3:*max_length*)", mtev_true },
      { "and(tag3:*256)", mtev_true },
      { "and(tag4:*too_long*)", mtev_false },
      { "and(tag5:value5)", mtev_true },
      { NULL, 0 }
    }
  }
};

struct Matches implicit_testmatches[] = {
  { 
    "__name:this_is_the_max_length_of_an_implicit_tag_pair_00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000004103",
    {
      { "and(__name:this_is*)", mtev_true },
      { "and(__name:*tag_pair*)", mtev_true },
      { "and(__name:*04103)", mtev_true },
      { "and(__name:*value_not_in_name*)", mtev_false },
      { NULL, 0 }
    }
  },
  { 
    "__name:this_implicit_tag_pair_is_too_long_00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000004104",
    {
      { "and(__name:*too_long*)", mtev_false },
      { NULL, 0 }
    }
  },
  { 
    "__name:this_is_the_max_length_of_an_implicit_tag_pair_00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000004103,"
    "__name:this_implicit_tag_pair_is_too_long_00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000004104,"
    "__check_uuid:b946274b-183d-4553-814a-ada8130c560d,",
    {
      { "and(__name:this_is*)", mtev_true },
      { "and(__name:*tag_pair*)", mtev_true },
      { "and(__name:*04103)", mtev_true },
      { "and(__name:*value_not_in_name*)", mtev_false },
      { "and(__name:*too_long*)", mtev_false },
      { "and(__check_uuid:*b946274b*)", mtev_true },
      { NULL, 0 }
    }
  }
};

const char *testqueries[] = {
  "and(*:*)",
  "and(foo:bar)",
  "and(or(foo:bar,c:d),e:f)",
  "and(a:,foo:bar)",
  "and(a:,foo:bar)",
  "and(b\"ZmFydHM=\":b\"\",foo:bar)",
  "and(b\"cm91dGU=\":b\"L2Zvbz9iYXI9cXV1eA==\",foo:bar)",
  "and(b\"cm91dGU=\":b!L2Zvbz9iYXI9cXV1eA==!,foo:bar)",
  "and(b\"cGF0aA==\":b\"P2Zvby4qLmJhcj8=\",foo:bar)",
  "and(b\"cGF0aA==\":b!P2Zvby4qLmJhcj8=!,foo:bar)"
};

int ntest = 0;
int failures = 0;
#define test_assert(a) do { \
  ntest++; \
  if(a) { \
    printf("ok - %d (%s) line %u\n", ntest, #a, __LINE__); \
  } else { \
    printf("not ok - %d (%s) line %u\n", ntest, #a, __LINE__); \
    abort(); \
    failures++; \
  } \
} while(0)

#define test_assert_namef(valid, fmt, args...) do { \
  int __valid = valid; \
  ntest++; \
  if(!__valid) failures++; \
  printf("%s - %d (", __valid ? "ok" : "not ok", ntest); \
  printf(fmt, args); \
  printf(") line %u\n", __LINE__); \
  if(!__valid) abort(); \
} while(0)

void test_tag_decode()
{
  char decoded[512] = {0};
  
  for(int i = 0; i < sizeof(testtags) / sizeof(*testtags); i++) {
    const char *encoded = testtags[i][0];
    int rval  = noit_metric_tagset_decode_tag(decoded, sizeof(decoded),
					      encoded, strlen(encoded));
    test_assert_namef(rval > 0, "'%s' is valid tag", encoded);

    int eq = strncmp(testtags[i][1], decoded, rval);
    test_assert_namef(eq == 0, "'%s' equals '%s'", testtags[i][1], decoded);
  }
}

void test_ast_decode()
{
  int erroroffset;
  noit_metric_tag_search_ast_t *ast, *not;
  const char *query;
  char *unparse;

  /* simple test */
  query = "and(foo:bar)";
  ast = noit_metric_tag_search_parse(query, &erroroffset);
  if(ast) {
    unparse = noit_metric_tag_search_unparse(ast);
    test_assert_namef(ast != NULL, "'%s' -> '%s'", query, unparse);
    free(unparse);
    test_assert(noit_metric_tag_search_get_op(ast) == OP_AND_ARGS);
    test_assert(noit_metric_tag_search_get_op(noit_metric_tag_search_get_arg(ast,0)) == OP_MATCH);
    test_assert(strcmp(noit_var_val(noit_metric_tag_search_get_cat(noit_metric_tag_search_get_arg(ast,0))),"foo") == 0);
    test_assert(strcmp(noit_var_val(noit_metric_tag_search_get_name(noit_metric_tag_search_get_arg(ast,0))),"bar") == 0);
    noit_metric_tag_search_free(ast);
  }
  else test_assert_namef(ast != NULL, "parsing error at %d in '%s'", erroroffset, query);

  /* simple test with colons in value */
  query = "and(foo:bar:baz)";
  ast = noit_metric_tag_search_parse(query, &erroroffset);
  if(ast) {
    unparse = noit_metric_tag_search_unparse(ast);
    test_assert_namef(ast != NULL, "'%s' -> '%s'", query, unparse);
    free(unparse);
    test_assert(noit_metric_tag_search_get_op(ast) == OP_AND_ARGS);
    test_assert(noit_metric_tag_search_get_op(noit_metric_tag_search_get_arg(ast,0)) == OP_MATCH);
    test_assert(strcmp(noit_var_val(noit_metric_tag_search_get_cat(noit_metric_tag_search_get_arg(ast,0))),"foo") == 0);
    test_assert(strcmp(noit_var_val(noit_metric_tag_search_get_name(noit_metric_tag_search_get_arg(ast,0))),"bar:baz") == 0);
    noit_metric_tag_search_free(ast);
  }
  else test_assert_namef(ast != NULL, "parsing error at %d in '%s'", erroroffset, query);
  
  /* half / test */
  query = "and(/endpoint`latency:/bar)";
  ast = noit_metric_tag_search_parse(query, &erroroffset);
  if(ast) {
    unparse = noit_metric_tag_search_unparse(ast);
    test_assert_namef(ast != NULL, "'%s' -> '%s'", query, unparse);
    free(unparse);
    test_assert(noit_metric_tag_search_get_op(ast) == OP_AND_ARGS);
    test_assert(noit_metric_tag_search_get_op(noit_metric_tag_search_get_arg(ast,0)) == OP_MATCH);
    test_assert(strcmp(noit_var_val(noit_metric_tag_search_get_cat(noit_metric_tag_search_get_arg(ast,0))),"/endpoint`latency") == 0);
    test_assert(strcmp(noit_var_val(noit_metric_tag_search_get_name(noit_metric_tag_search_get_arg(ast,0))),"/bar") == 0);
    noit_metric_tag_search_free(ast);
  }
  else test_assert_namef(ast != NULL, "parsing error at %d in '%s'", erroroffset, query);
  
  /* simple re key test */
  query = "and(/foo/:bar)";
  ast = noit_metric_tag_search_parse(query, &erroroffset);
  if(ast) {
    unparse = noit_metric_tag_search_unparse(ast);
    test_assert_namef(ast != NULL, "'%s' -> '%s'", query, unparse);
    free(unparse);
    test_assert(noit_metric_tag_search_get_op(ast) == OP_AND_ARGS);
    test_assert(noit_metric_tag_search_get_op(noit_metric_tag_search_get_arg(ast,0)) == OP_MATCH);
    test_assert(strcmp(noit_var_impl_name(noit_metric_tag_search_get_cat(noit_metric_tag_search_get_arg(ast,0))),"re") == 0);
    test_assert(strcmp(noit_var_val(noit_metric_tag_search_get_name(noit_metric_tag_search_get_arg(ast,0))),"bar") == 0);
    noit_metric_tag_search_free(ast);
  }
  else test_assert_namef(ast != NULL, "parsing error at %d in '%s'", erroroffset, query);

  /* simple re val test */
  query = "and(foo:/bar/)";
  ast = noit_metric_tag_search_parse(query, &erroroffset);
  if(ast) {
    unparse = noit_metric_tag_search_unparse(ast);
    test_assert_namef(ast != NULL, "'%s' -> '%s'", query, unparse);
    free(unparse);
    test_assert(noit_metric_tag_search_get_op(ast) == OP_AND_ARGS);
    test_assert(noit_metric_tag_search_get_op(noit_metric_tag_search_get_arg(ast,0)) == OP_MATCH);
    test_assert(strcmp(noit_var_val(noit_metric_tag_search_get_cat(noit_metric_tag_search_get_arg(ast,0))),"foo") == 0);
    test_assert(strcmp(noit_var_impl_name(noit_metric_tag_search_get_name(noit_metric_tag_search_get_arg(ast,0))),"re") == 0);
    noit_metric_tag_search_free(ast);
  }
  else test_assert_namef(ast != NULL, "parsing error at %d in '%s'", erroroffset, query);
  
  /* base64 fixed category */
  query = "and(foo:bar,not(b\"c29tZTpzdHVmZltoZXJlXQ==\":value))";
  ast = noit_metric_tag_search_parse(query, &erroroffset);
  if(ast) {
    unparse = noit_metric_tag_search_unparse(ast);
    test_assert_namef(ast != NULL, "'%s' -> '%s'", query, unparse);
    free(unparse);
    test_assert(noit_metric_tag_search_get_op(ast) == OP_AND_ARGS);
    test_assert(noit_metric_tag_search_get_op(noit_metric_tag_search_get_arg(ast,0)) == OP_MATCH);
    test_assert(strcmp(noit_var_val(noit_metric_tag_search_get_cat(noit_metric_tag_search_get_arg(ast,0))),"foo") == 0);
    test_assert(strcmp(noit_var_val(noit_metric_tag_search_get_name(noit_metric_tag_search_get_arg(ast,0))),"bar") == 0);
    test_assert(noit_metric_tag_search_get_op(noit_metric_tag_search_get_arg(ast,1)) == OP_NOT_ARGS);
    not = noit_metric_tag_search_get_arg(noit_metric_tag_search_get_arg(ast,1),0);
    test_assert(not != NULL);
    test_assert(strcmp(noit_var_val(noit_metric_tag_search_get_cat(not)),"some:stuff[here]") == 0);
    test_assert(strcmp(noit_var_val(noit_metric_tag_search_get_name(not)),"value") == 0);
    noit_metric_tag_search_free(ast);
  }
  else test_assert_namef(ast != NULL, "parsing error at %d in '%s'", erroroffset, query);

  /* base64 regex parse */
  query = " and( foo:bar, not( b/c29tZS4q/:value))";
  ast = noit_metric_tag_search_parse(query, &erroroffset);
  if(ast) {
    unparse = noit_metric_tag_search_unparse(ast);
    test_assert_namef(ast != NULL, "'%s' -> '%s'", query, unparse);
    free(unparse);
    test_assert(noit_metric_tag_search_get_op(ast) == OP_AND_ARGS);
    test_assert(noit_metric_tag_search_get_op(noit_metric_tag_search_get_arg(ast,0)) == OP_MATCH);
    test_assert(strcmp(noit_var_val(noit_metric_tag_search_get_cat(noit_metric_tag_search_get_arg(ast,0))),"foo") == 0);
    test_assert(strcmp(noit_var_val(noit_metric_tag_search_get_name(noit_metric_tag_search_get_arg(ast,0))),"bar") == 0);
    test_assert(noit_metric_tag_search_get_op(noit_metric_tag_search_get_arg(ast,1)) == OP_NOT_ARGS);
    not = noit_metric_tag_search_get_arg(noit_metric_tag_search_get_arg(ast,1),0);
    test_assert(not != NULL);
    test_assert(strcmp(noit_var_val(noit_metric_tag_search_get_cat(not)),"some.*") == 0);
    test_assert(strcmp(noit_var_impl_name(noit_metric_tag_search_get_cat(not)),"re") == 0);
    test_assert(strcmp(noit_var_val(noit_metric_tag_search_get_name(not)),"value") == 0);
    noit_metric_tag_search_free(ast);
  }
  else test_assert_namef(ast != NULL, "parsing error at %d in '%s'", erroroffset, query);

  /* wildcard category parse */
  query = "and(*:bar)";
  ast = noit_metric_tag_search_parse(query, &erroroffset);
  if(ast) {
    unparse = noit_metric_tag_search_unparse(ast);
    test_assert_namef(ast != NULL, "'%s' -> '%s'", query, unparse);
    free(unparse);
    test_assert(noit_metric_tag_search_get_op(ast) == OP_AND_ARGS);
    test_assert(noit_metric_tag_search_get_op(noit_metric_tag_search_get_arg(ast,0)) == OP_MATCH);
    test_assert(strcmp(noit_var_impl_name(noit_metric_tag_search_get_cat(noit_metric_tag_search_get_arg(ast,0))),"default") == 0);
    test_assert(strcmp(noit_var_val(noit_metric_tag_search_get_name(noit_metric_tag_search_get_arg(ast,0))),"bar") == 0);
    noit_metric_tag_search_free(ast);
  }
  else test_assert_namef(ast != NULL, "parsing error at %d in '%s'", erroroffset, query);

  /* wildcard category parse */
  query = "and(f*:bar)";
  ast = noit_metric_tag_search_parse(query, &erroroffset);
  if(ast) {
    unparse = noit_metric_tag_search_unparse(ast);
    test_assert_namef(ast != NULL, "'%s' -> '%s'", query, unparse);
    free(unparse);
    test_assert(noit_metric_tag_search_get_op(ast) == OP_AND_ARGS);
    test_assert(noit_metric_tag_search_get_op(noit_metric_tag_search_get_arg(ast,0)) == OP_MATCH);
    test_assert(strcmp(noit_var_impl_name(noit_metric_tag_search_get_cat(noit_metric_tag_search_get_arg(ast,0))),"default") == 0);
    test_assert(strcmp(noit_var_val(noit_metric_tag_search_get_name(noit_metric_tag_search_get_arg(ast,0))),"bar") == 0);
    noit_metric_tag_search_free(ast);
  }
  else test_assert_namef(ast != NULL, "parsing error at %d in '%s'", erroroffset, query);

  /* wildcard value parse */
  query = "and(foo:b*r)";
  ast = noit_metric_tag_search_parse(query, &erroroffset);
  if(ast) {
    unparse = noit_metric_tag_search_unparse(ast);
    test_assert_namef(ast != NULL, "'%s' -> '%s'", query, unparse);
    free(unparse);
    test_assert(noit_metric_tag_search_get_op(ast) == OP_AND_ARGS);
    test_assert(noit_metric_tag_search_get_op(noit_metric_tag_search_get_arg(ast,0)) == OP_MATCH);
    test_assert(strcmp(noit_var_val(noit_metric_tag_search_get_cat(noit_metric_tag_search_get_arg(ast,0))),"foo") == 0);
    test_assert(strcmp(noit_var_impl_name(noit_metric_tag_search_get_name(noit_metric_tag_search_get_arg(ast,0))),"default") == 0);
    noit_metric_tag_search_free(ast);
  }
  else test_assert_namef(ast != NULL, "parsing error at %d in '%s'", erroroffset, query);

}

void test_tag_match()
{
  int erroroffset;
  noit_metric_tagset_t tagset = {};
  noit_metric_tagset_builder_t builder;
  noit_metric_tag_search_ast_t *ast;
  mtev_boolean match;
  char *canonical;

  for(int i = 0; i < sizeof(testmatches) / sizeof(*testmatches); i++) {
    test_assert_namef(true, "testing tagset '%s'", testmatches[i].tagstring);
    noit_metric_tagset_builder_start(&builder);
    noit_metric_tagset_builder_add_many(&builder, testmatches[i].tagstring, strlen(testmatches[i].tagstring));
    memset(&tagset, 0, sizeof(tagset));
    noit_metric_tagset_builder_end(&builder, &tagset, &canonical);

    for(int j = 0; testmatches[i].queries[j].query != NULL; j++) {
      ast = noit_metric_tag_search_parse_lazy(testmatches[i].queries[j].query, &erroroffset);
      if(ast) {
        if(benchmark) {
          mtev_perftimer_t btimer;
          mtev_perftimer_start(&btimer);
          for(int b=0; b<BENCH_ITERS; b++) {
            match = noit_metric_tag_search_evaluate_against_tags(ast, &tagset);
          }
          testmatches[i].queries[j].bench_ns = mtev_perftimer_elapsed(&btimer);
        } else {
          match = noit_metric_tag_search_evaluate_against_tags(ast, &tagset);
        }
        test_assert_namef(match == testmatches[i].queries[j].match, "'%s' %s", testmatches[i].queries[j].query, match ? "matches" : "doesn't match");
        /* clone if only to test cloning */
        noit_metric_tag_search_free(noit_metric_tag_search_clone(ast));
        noit_metric_tag_search_free(ast);
      } else {
        test_assert_namef(ast != NULL, "parsing error at %d in '%s'", erroroffset, testmatches[i].queries[j].query);
      }
    }
    free(tagset.tags);
  }
}

void test_implicit_tag_match() {
  int erroroffset;
  noit_metric_tagset_t tagset = {};
  noit_metric_tag_search_ast_t *ast;
  mtev_boolean match;
  char *canonical;

  for (int i = 0;
       i < sizeof(implicit_testmatches) / sizeof(*implicit_testmatches); i++) {
    test_assert_namef(true, "testing tagset '%s'",
                      implicit_testmatches[i].tagstring);
    memset(&tagset, 0, sizeof(tagset));
    noit_metric_add_implicit_tags_to_tagset(
        implicit_testmatches[i].tagstring,
        strlen(implicit_testmatches[i].tagstring), &tagset, &canonical);

    for (int j = 0; implicit_testmatches[i].queries[j].query != NULL; j++) {
      ast = noit_metric_tag_search_parse_lazy(
          implicit_testmatches[i].queries[j].query, &erroroffset);
      if (ast) {
        if (benchmark) {
          mtev_perftimer_t btimer;
          mtev_perftimer_start(&btimer);
          for (int b = 0; b < BENCH_ITERS; b++) {
            match = noit_metric_tag_search_evaluate_against_tags(ast, &tagset);
          }
          implicit_testmatches[i].queries[j].bench_ns =
              mtev_perftimer_elapsed(&btimer);
        } else {
          match = noit_metric_tag_search_evaluate_against_tags(ast, &tagset);
        }
        test_assert_namef(match == implicit_testmatches[i].queries[j].match,
                          "'%s' %s", implicit_testmatches[i].queries[j].query,
                          match ? "matches" : "doesn't match");
        /* clone if only to test cloning */
        noit_metric_tag_search_free(noit_metric_tag_search_clone(ast));
        noit_metric_tag_search_free(ast);
      } else {
        test_assert_namef(ast != NULL, "parsing error at %d in '%s'",
                          erroroffset,
                          implicit_testmatches[i].queries[j].query);
      }
    }
    free(tagset.tags);
  }
}

void test_canon(const char *in, const char *expect) {
  int len;
  char buff[MAX_METRIC_TAGGED_NAME];
  len = noit_metric_canonicalize(in, strlen(in), buff, sizeof(buff), mtev_true);
  if(len < 0) test_assert_namef(expect == NULL, "canon(%s) -> NULL (%d)", in, len);
  else test_assert_namef(expect && !strcmp(expect, buff), "canon(%s) -> %s", expect, buff);
}

void test_fuzz_canon() {
  for(int encode = 0; encode < 4; encode++) {
    int cnt = 0;
    char buff[4096];
    char _obuff[32678];
    char out[MAX_METRIC_TAGGED_NAME];
    mtev_hash_table unique;
    mtev_hash_init(&unique);
    _obuff[0] = '\0';
    strlcat(_obuff, "metric|ST[", sizeof(_obuff));
    for(int tc=0; tc<4; tc++) {
      if(tc) strlcat(_obuff, ",", sizeof(_obuff));
      int pre_tag_len = strlen(_obuff);
      char cat, val;
      cat = 'a' + (lrand48() % 26);
      val = 'a' + (lrand48() % 26);
    for(int i=10; i<20; i++) {
      _obuff[pre_tag_len] = '\0';
      if(encode & 1) {
        memset(buff, cat, i);
        strlcat(_obuff, "b\"", sizeof(_obuff));
        int len = strlen(_obuff);
        int offset = mtev_b64_encode((unsigned char *)buff, i, _obuff+len, sizeof(_obuff)-len);
        assert(offset > 0);
        _obuff[len + offset] = '\0';
        strlcat(_obuff, "\"", sizeof(_obuff));
      } else {
        memset(buff, cat, i);
        buff[i] = '\0';
        strlcat(_obuff, buff, sizeof(_obuff));
      }
      strlcat(_obuff, ":", sizeof(_obuff));
      int pre_tagval_len = strlen(_obuff);
      for(int j=230; j<260; j++) {
        _obuff[pre_tagval_len] = '\0';
        if(encode & 2) {
          memset(buff, val, j);
          strlcat(_obuff, "b\"", sizeof(_obuff));
          int len = strlen(_obuff);
          int offset = mtev_b64_encode((unsigned char *)buff, j, _obuff+len, sizeof(_obuff)-len);
          assert(offset > 0);
          _obuff[len + offset] = '\0';
          strlcat(_obuff, "\"", sizeof(_obuff));
        } else {
          memset(buff, val, j);
          buff[j] = '\0';
          strlcat(_obuff, buff, sizeof(_obuff));
        }
        strlcat(_obuff, "]", sizeof(_obuff));
        /* We can only operate on metric names up to sizeof(out), so just elide the testing
         * from longer names. */
        if(strlen(_obuff) <= sizeof(out)) {
          char *copy = malloc(strlen(_obuff));
          memcpy(copy, _obuff, strlen(_obuff));
          int len = noit_metric_canonicalize(copy, strlen(_obuff), out, sizeof(out), mtev_true);
          free(copy);
          if(len < 0) {
            test_assert_namef(len > 0, "canonicalization failed %d (%s)", len, _obuff);
            return;
          }
          mtev_hash_replace(&unique, strdup(out), strlen(out), NULL, free, NULL);
          cnt++;
        }
      }
      _obuff[strlen(_obuff)-1] = '\0';
    }
    }
    test_assert_namef(mtev_hash_size(&unique) == cnt, "fuzzed uniquely [%c%c] (%d==%d)",
                      encode & 1 ? 'b' : '-',
                      encode & 2 ? 'b' : '-',
                      mtev_hash_size(&unique), cnt);
    mtev_hash_destroy(&unique, free, NULL);
  }
}

void test_reorder_tags(void) {
  int erroroffset = 0;
  const char *query = "and(a:b,c:d)";
  noit_metric_tag_search_ast_t *ast = noit_metric_tag_search_parse(query, &erroroffset);
  if(ast) {
    const int replace_idx = 1;
    char *unparse = noit_metric_tag_search_unparse(ast);
    test_assert(strcmp("and([exact]a:[exact]b,[exact]c:[exact]d)", unparse) == 0);
    free(unparse);
    test_assert(noit_metric_tag_search_get_nargs(ast) == 2);
    test_assert(strcmp(noit_var_val(noit_metric_tag_search_get_cat(noit_metric_tag_search_get_arg(ast,0))),"a") == 0);
    test_assert(strcmp(noit_var_val(noit_metric_tag_search_get_name(noit_metric_tag_search_get_arg(ast,0))),"b") == 0);
    test_assert(strcmp(noit_var_val(noit_metric_tag_search_get_cat(noit_metric_tag_search_get_arg(ast,1))),"c") == 0);
    test_assert(strcmp(noit_var_val(noit_metric_tag_search_get_name(noit_metric_tag_search_get_arg(ast,1))),"d") == 0);

    noit_metric_tag_search_ast_t *swap = noit_metric_tag_search_get_arg(ast, replace_idx);
    noit_metric_tag_search_set_arg(ast, replace_idx,
                                   noit_metric_tag_search_get_arg(ast, replace_idx - 1));
    noit_metric_tag_search_set_arg(ast, replace_idx - 1, swap);

    test_assert(noit_metric_tag_search_get_nargs(ast) == 2);
    test_assert(strcmp(noit_var_val(noit_metric_tag_search_get_cat(noit_metric_tag_search_get_arg(ast,0))),"c") == 0);
    test_assert(strcmp(noit_var_val(noit_metric_tag_search_get_name(noit_metric_tag_search_get_arg(ast,0))),"d") == 0);
    test_assert(strcmp(noit_var_val(noit_metric_tag_search_get_cat(noit_metric_tag_search_get_arg(ast,1))),"a") == 0);
    test_assert(strcmp(noit_var_val(noit_metric_tag_search_get_name(noit_metric_tag_search_get_arg(ast,1))),"b") == 0);

    unparse = noit_metric_tag_search_unparse(ast);
    test_assert(strcmp("and([exact]c:[exact]d,[exact]a:[exact]b)", unparse) == 0);

    free(unparse);
    noit_metric_tag_search_free(ast);
  } else {
    test_assert_namef(ast != NULL, "parsing error at %d in '%s'", erroroffset, query);
  }
}

void test_line(const char *name, const char *in, const char *expect, int rval_expect, mtev_boolean allocd) {
  noit_metric_message_t message;
  memset(&message, 0, sizeof(message));
  message.original_message = (char *)in;
  message.original_message_len = strlen(in);
  int rval = noit_message_decoder_parse_line(&message, -1);
  test_assert_namef(allocd != (message.id.alloc_name == NULL), "test_line(%s) allocd", name);
  test_assert_namef(rval == rval_expect, "test_line(%s) rval [%d should be %d]", name, rval, rval_expect);
  if(expect != NULL) {
     int v = strlen(expect) == message.id.name_len_with_tags &&
             !memcmp(message.id.name, expect, message.id.name_len_with_tags);
     test_assert_namef(v,
                       "test_line(%s) metric match", name);
     if(v == 0) {
       printf("\nFAILURE:\nRESULT: '%.*s'\nEXPECT: '%s'\n\n",
              (int)message.id.name_len_with_tags, message.id.name, expect);
     }
  }
  else {
    test_assert_namef(message.id.name_len_with_tags == 0 || rval < 0,
                      "test_line(%s) metric null", name);
  }
  noit_metric_message_clear(&message);
}

void metric_parsing(void) {
  int len;
  char buff[NOIT_TAG_MAX_PAIR_LEN], dbuff[NOIT_TAG_MAX_PAIR_LEN], ebuff[NOIT_TAG_MAX_PAIR_LEN];

  snprintf(buff, sizeof(buff), "@#lkm45lsnd:kljnmsdkflnsdf:kjnsdkfjnsdf");
  len = noit_metric_tagset_decode_tag(dbuff, sizeof(dbuff), buff, strlen(buff));
  test_assert_namef(len > 0, "'%s' -> '%s'", buff, dbuff);
  len = noit_metric_tagset_encode_tag(ebuff, sizeof(ebuff), dbuff, len);
  test_assert_namef(len > 0, "'%s' -> '%s'", dbuff, ebuff);
  test_assert_namef(strcmp(buff, ebuff) == 0, "'%s' equals '%s'", buff, ebuff);

  snprintf(buff, sizeof(buff), "b\"Zm9v\":http://12.3.3.4:80/this?is=it");
  len = noit_metric_tagset_decode_tag(dbuff, sizeof(dbuff), buff, strlen(buff));
  test_assert_namef(len > 0, "'%s' -> '%s'", buff, dbuff);
  len = noit_metric_tagset_encode_tag(ebuff, sizeof(ebuff), dbuff, len);
  test_assert_namef(len > 0, "'%s' -> '%s'", dbuff, ebuff);
  test_assert_namef(strcmp("foo:http://12.3.3.4:80/this?is=it", ebuff) == 0, "'%s' equals 'foo:http://12.3.3.4:80/this?is=it'", ebuff);

  int i;

  for(i=0; i<sizeof(testlines)/sizeof(*testlines); i++) {
    test_line(testlines[i].name, testlines[i].input, testlines[i].output, testlines[i].rval, testlines[i].allocd);
  }

  for(int i=0; i<sizeof(tcpairs)/sizeof(*tcpairs); i++) {
    test_canon(tcpairs[i][0], tcpairs[i][1]);
  }
  test_fuzz_canon();
}

void query_parsing(void) {
  int erroroffset = 0;
  noit_metric_tag_search_ast_t *ast;
  char *unparse;

  for(int i = 0; i < sizeof(testqueries) / sizeof(*testqueries); i++) {
    ast = noit_metric_tag_search_parse(testqueries[i], &erroroffset);
    if(ast) {
      unparse = noit_metric_tag_search_unparse(ast);
      test_assert_namef(ast != NULL, "'%s' -> '%s'", testqueries[i], unparse);
      free(unparse);
      noit_metric_tag_search_free(ast);
    } else {
      test_assert_namef(ast != NULL, "parsing error at %d in '%s'", erroroffset, testqueries[i]);
    }
  }

  const char *query = "hint(and(a:b),foo:bar,index:b\"bm9uZQ==\")";
  ast = noit_metric_tag_search_parse(query, &erroroffset);
  if(ast) {
    unparse = noit_metric_tag_search_unparse(ast);
    test_assert_namef(noit_metric_tag_search_has_hint(ast, "foo", "bar"), "%s missing foo:bar", query);
    test_assert_namef(noit_metric_tag_search_has_hint(ast, "index", "none"), "%s missing index:none", query);
    test_assert_namef(!noit_metric_tag_search_has_hint(ast, "index", "bitmap"), "%s missing index:bitmap", query);
    free(unparse);
    noit_metric_tag_search_free(ast);
  } else {
    test_assert_namef(ast != NULL, "parsing error at %d in '%s'", erroroffset, query);
  }
  test_reorder_tags();
}

void query_argument_swapping(void) {
  int erroroffset = 0;
  const char *query = "and(a:b,c:d,e:f,g:h,i:j)";
  noit_metric_tag_search_ast_t *ast = noit_metric_tag_search_parse(query, &erroroffset);
  if (ast) {
    test_assert(-1 == noit_metric_tag_search_swap(ast, 0, 120));
    test_assert(-1 == noit_metric_tag_search_swap(ast, 120, 0));
    test_assert(-1 == noit_metric_tag_search_swap(ast, -1, 0));
    test_assert(0 == noit_metric_tag_search_swap(ast, 1, 3));
    char *unparse = noit_metric_tag_search_unparse(ast);
    test_assert(strcmp("and([exact]a:[exact]b,[exact]g:[exact]h,[exact]e:[exact]f,[exact]c:[exact]d,[exact]i:[exact]j)", unparse) == 0);
    free(unparse);
    test_assert(0 == noit_metric_tag_search_swap(ast, 0, 0));
    unparse = noit_metric_tag_search_unparse(ast);
    test_assert(strcmp("and([exact]a:[exact]b,[exact]g:[exact]h,[exact]e:[exact]f,[exact]c:[exact]d,[exact]i:[exact]j)", unparse) == 0);
    free(unparse);
    noit_metric_tag_search_free(ast);
  } else {
    test_assert_namef(ast != NULL, "parsing error at %d in '%s'", erroroffset, query);
  }
}

void loop(char *str) {
  int len;
  const int nloop = 100000;
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

void test_tag_at_limit(void) {
  const char *tag_name = "image_id:docker-pullable://repo.abcd.ef/"
    "pipeline-docker-automation/application_framework_project/example-"
    "isolated-service/automation/jtd-test/1-b1b0798069f37ddbbdec9722276152182c2de82a"
    "@sha256:b93831fef47e19c2f96e14b48f8715fc0ec5fd9a3f025ea4e9e5d0a2c6d5255d]";
  const size_t tag_name_len = strlen(tag_name);
  mtev_boolean too_long = mtev_false;
  noit_metric_tag_t tag;
  ssize_t len;
  char dbuff[NOIT_TAG_MAX_PAIR_LEN * 3];

  assert(tag_name_len - 1 == NOIT_TAG_MAX_PAIR_LEN);

  noit_metric_tags_parse_one(tag_name, tag_name_len - 1, &tag, &too_long);
  assert(too_long == mtev_false);

  memset(dbuff, 0xff, NOIT_TAG_MAX_PAIR_LEN * 3);
  len = noit_metric_tagset_decode_tag(dbuff + NOIT_TAG_MAX_PAIR_LEN, NOIT_TAG_MAX_PAIR_LEN, tag.tag, tag.total_size);

  assert(len >= 0);
  assert((uint8_t)dbuff[NOIT_TAG_MAX_PAIR_LEN - 1] == 0xff);
  assert((uint8_t)dbuff[NOIT_TAG_MAX_PAIR_LEN + NOIT_TAG_MAX_PAIR_LEN] == 0xff);
  assert(memcmp(tag_name, dbuff + NOIT_TAG_MAX_PAIR_LEN, NOIT_TAG_MAX_PAIR_LEN) != 0);
  assert((uint8_t)dbuff[NOIT_TAG_MAX_PAIR_LEN] == 'i');
  assert((uint8_t)dbuff[NOIT_TAG_MAX_PAIR_LEN + NOIT_TAG_MAX_PAIR_LEN] == 0xff);

  len = noit_metric_tagset_encode_tag(dbuff + NOIT_TAG_MAX_PAIR_LEN,
    NOIT_TAG_MAX_PAIR_LEN,
    dbuff + NOIT_TAG_MAX_PAIR_LEN,
    NOIT_TAG_MAX_PAIR_LEN);
  assert(len >= 0);
  assert(memcmp(tag_name, dbuff + NOIT_TAG_MAX_PAIR_LEN, NOIT_TAG_MAX_PAIR_LEN) == 0);
}

void test_implicit_tag_at_limit(void) {
  const char *implicit_tag = 
    "__name:this_is_the_max_length_of_an_implicit_tag_pair_00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000004103";
  const size_t implicit_tag_len = strlen(implicit_tag);
  mtev_boolean too_long = mtev_false;
  noit_metric_tag_t tag;
  ssize_t len;
  char dbuff[NOIT_IMPLICIT_TAG_MAX_PAIR_LEN * 3];

  assert(implicit_tag_len == NOIT_IMPLICIT_TAG_MAX_PAIR_LEN);

  noit_metric_tags_parse_one_implicit(implicit_tag, implicit_tag_len, &tag,
                                      &too_long);
  assert(too_long == mtev_false);

  memset(dbuff, 0xff, NOIT_IMPLICIT_TAG_MAX_PAIR_LEN * 3);
  len = noit_metric_tagset_decode_tag(dbuff + NOIT_IMPLICIT_TAG_MAX_PAIR_LEN,
                                      NOIT_IMPLICIT_TAG_MAX_PAIR_LEN, tag.tag,
                                      tag.total_size);

  assert(len >= 0);
  assert((uint8_t)dbuff[NOIT_IMPLICIT_TAG_MAX_PAIR_LEN - 1] == 0xff);
  assert(
      (uint8_t)dbuff[NOIT_IMPLICIT_TAG_MAX_PAIR_LEN + NOIT_IMPLICIT_TAG_MAX_PAIR_LEN] ==
      0xff);
  assert(memcmp(implicit_tag, dbuff + NOIT_IMPLICIT_TAG_MAX_PAIR_LEN,
                NOIT_IMPLICIT_TAG_MAX_PAIR_LEN) != 0);
  assert((uint8_t)dbuff[NOIT_IMPLICIT_TAG_MAX_PAIR_LEN] == '_');
  assert(
      (uint8_t)dbuff[NOIT_IMPLICIT_TAG_MAX_PAIR_LEN + NOIT_IMPLICIT_TAG_MAX_PAIR_LEN] ==
      0xff);

  len = noit_metric_tagset_encode_tag(
      dbuff + NOIT_IMPLICIT_TAG_MAX_PAIR_LEN, NOIT_IMPLICIT_TAG_MAX_PAIR_LEN,
      dbuff + NOIT_IMPLICIT_TAG_MAX_PAIR_LEN, NOIT_IMPLICIT_TAG_MAX_PAIR_LEN);
  assert(len >= 0);
  assert(memcmp(implicit_tag, dbuff + NOIT_IMPLICIT_TAG_MAX_PAIR_LEN,
                NOIT_IMPLICIT_TAG_MAX_PAIR_LEN) == 0);
}

int main(int argc, char * const *argv)
{
  int opt;
  if (argc > 1) {
    for(int i=1; i< argc; i++) {
      int erroroffset;
      noit_metric_tag_search_ast_t *ast = noit_metric_tag_search_parse(argv[i], &erroroffset);
      if(!ast) {
        printf("error at %d\n", erroroffset);
      } else {
        char *str = noit_metric_tag_search_unparse(ast);
        printf("-> %s\n", str);
        free(str);
      }
    }
    exit(0);
  }

  while(-1 != (opt = getopt(argc, argv, "b"))) {
    switch(opt) {
    case 'b': benchmark = true; break;
    default:
      fprintf(stderr, "unknown option: %c\n", opt);
      exit(-2);
    }
  }
  libnoit_init_globals();
  test_tag_decode();
  test_ast_decode();
  test_tag_match();
  test_implicit_tag_match();
  test_tag_at_limit();
  test_implicit_tag_at_limit();
  metric_parsing();
  query_parsing();
  query_argument_swapping();
  printf("\nPerformance:\n====================\n");
  loop("woop|ST[a:b,c:d]|MT{foo:bar}|ST[c:d,e:f,a:b]");
  loop("testing_this|ST[cluster:mta2,customer:noone,b\"bjo6Og==\":a=b,node:j.mta2vrest.prd.acme]");
  loop("testing_this_long_untagged_metric");
  printf("\n%d tests failed.\n", failures);
  if(benchmark) {
    for(int i=0; i < sizeof(testmatches) / sizeof(*testmatches); i++) {
      for(int j = 0; testmatches[i].queries[j].query != NULL; j++) {
        printf("%s on %s -> %f ns/op\n", testmatches[i].queries[j].query, testmatches[i].tagstring,
               (double)testmatches[i].queries[j].bench_ns / (double)BENCH_ITERS);
      }
    }
  }
  return !(failures == 0);
}
