use Test::More tests => 5;
use testconfig;

use strict;

SKIP: {
  skip "no existing test db to cleanup", 1 unless (-d "$NOIT_TEST_DB");
  `pg_ctl -D $NOIT_TEST_DB -w -m immediate stop`;
  `rm -rf $NOIT_TEST_DB`;
  ok(! -d "$NOIT_TEST_DB", 'cleaned up running instance');
}
ok(-r '../../sql/reconnoiter_ddl_dump.sql', 'SQL to load');
ok(`initdb -A trust $NOIT_TEST_DB`);
open (F, ">>$NOIT_TEST_DB/postgresql.conf");
print F "listen_addresses = 'localhost'\n";
print F "unix_socket_directory = '$NOIT_TEST_DB'\n";
print F "port = $NOIT_TEST_DB_PORT\n";
close(F);
system("pg_ctl", "-D", "$NOIT_TEST_DB", "-l", "$NOIT_TEST_DB/serverlog", "-s", "-w", "start");
SKIP: {
  skip "pg_ctl and \$? issues on $^O", 1
    if $^O eq 'darwin' or $^O eq 'linux';
  is($?, 0, 'starting postgres');
}

my $conn = pg();
BAIL_OUT("cannot continue tests without DB") unless($conn);
ok($conn, "connect to database");
undef $conn;

1;
