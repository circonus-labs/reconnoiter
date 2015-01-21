use Test::More tests => 5;
use testconfig;
use IPC::Open3;
use IO::File;

use strict;

my $conn;

$conn = pg('reconnoiter','reconnoiter');
SKIP: {
  skip 'already loaded schema', 1 if($conn);
  my $cmd = "psql -h localhost -p $NOIT_TEST_DB_PORT postgres " .
                "-f ../../sql/reconnoiter_ddl_dump.sql";
  my $rdr = IO::File->new(">logs/002_psql.out");
  my $wtr;
  my $pid = open3($wtr, ">&".$rdr->fileno, \*CHLD_ERR, $cmd);
  $wtr->close();
  my $bad = '';
  while(<CHLD_ERR>) {
    $bad .= $_ unless /\b(NOTICE|INFO)\b/;
  }
  close(CHLD_ERR);
  waitpid($pid, 0);
  is($bad, '', 'no errors during schema load');
}
$conn = pg('reconnoiter','reconnoiter');
ok($conn, 'connected under new user');
my $s = $conn->prepare("select count(*) from noit.metric_numeric_rollup_config");
ok($s, 'prepared test query');
ok($s->execute(), 'executed test query');
is($s->fetchrow(), 6, 'rollups exist');
undef $conn;

1;
