use Test::More tests => 4;
my @progs = qw/pg_ctl initdb psql rm/;

use strict;

foreach my $prog (@progs) {
  my $found = 0;
  foreach my $dir (split /:/, $ENV{PATH}) {
    $found = $dir if -x "$dir/$prog";
  }
  BAIL_OUT("$prog is required, but not in your path") unless($found);
  ok($found, "found $prog ($found)");
}

1;
