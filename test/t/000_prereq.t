use Test::More tests => 10;
my @progs = qw/pg_ctl initdb psql rm/;

use strict;

use_ok('testconfig');
use_ok('apiclient');
use_ok('stomp');
use_ok('XML::LibXML');
use_ok('JSON');

foreach my $prog (@progs) {
  my $found = 0;
  foreach my $dir (split /:/, $ENV{PATH}) {
    $found = $dir if -x "$dir/$prog";
  }
  BAIL_OUT("$prog is required, but not in your path") unless($found);
  ok($found, "found $prog ($found)");
}
mkdir "logs";
ok(-d "logs", "logs dir exists");

1;
