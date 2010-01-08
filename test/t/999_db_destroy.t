use Test::More tests => 1;
use testconfig;

use strict;

SKIP: {
  skip "no existing test db to cleanup", 1 unless (-d "$NOIT_TEST_DB");
  `pg_ctl -D $NOIT_TEST_DB -w -m immediate stop`;
  `rm -rf $NOIT_TEST_DB`;
  ok(! -d "$NOIT_TEST_DB", 'cleaned up running instance');
}

1;
