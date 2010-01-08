use Test::More tests => 5;
use testconfig;

use strict;

my $conn;

$conn = pg('reconnoiter','reconnoiter');
SKIP: {
  skip 'already created user and db', 1 if($conn);
  `psql -h localhost -p $NOIT_TEST_DB_PORT postgres -f ../../sql/reconnoiter_ddl_dump.sql`;
  is($?, 0, 'loaded schema');
}
$conn = pg('reconnoiter','reconnoiter');
ok($conn, 'connected under new user');
my $s = $conn->prepare("select count(*) from noit.metric_numeric_rollup_config");
ok($s, 'prepared test query');
ok($s->execute(), 'executed test query');
is($s->fetchrow(), 6, 'rollups exist');
undef $conn;

1;
