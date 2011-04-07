# Before `make install' is performed this script should be runnable with
# `make test'. After `make install' it should work as `perl JLog.t'

#########################

use Fcntl;
use Test::More tests => 7;
BEGIN { 
  use_ok('JLog::Writer');
  use_ok('JLog::Reader');
};

my $sub = "testsubscriber";
my $log = "foo.jlog";

system("rm -rf $log");
my $w = JLog::Writer->new($log, O_CREAT|O_EXCL, 1024);
ok !$w->alter_journal_size(1024), "set journal size should fail";

$w->add_subscriber($sub);
$w->open;
foreach (1 ... 3) {
  $w->write("foo $_"); 
}
$w->close;

my $r = JLog::Reader->new($log);

my @subs = $r->list_subscribers;
is_deeply(\@subs, [ $sub ], 'list_subscribers');

$r->open($sub);

my @lines;
my $i;

while(my $line = $r->read) {
  push @lines, $line;
  $i++;
}
is_deeply(\@lines, [ 'foo 1', 'foo 2', 'foo 3' ], "in == out");

# checkpoint
undef $r;

$r = JLog::Reader->new($log);
$r->open($sub);
@lines = ();
$i = 0;
while(my $line = $r->read) {
  push @lines, $line;
  $i++;
}
is_deeply(\@lines, [ 'foo 1', 'foo 2', 'foo 3' ], "in == out");

$r->checkpoint;

$r = JLog::Reader->new($log);
$r->open($sub);
is $r->read, undef, "our checkpoint cleared things";
