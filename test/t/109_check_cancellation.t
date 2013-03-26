use Test::More tests => 58;
use XML::LibXML;
use XML::LibXML::XPathContext;
use testconfig;
use Time::HiRes qw/gettimeofday tv_interval usleep/;
use apiclient;

use strict;
my $xp = XML::LibXML->new();
my $xpc = XML::LibXML::XPathContext->new();
my (@r, $fh, $doc, $uuid, $c, $prefix);
my $codes = {};

sub boot($) {
  my $name = shift;
  my $suffix = $name ? ".$name" : "";
  ok(start_noit("109$suffix", { logs_debug => { '' => 'false' },
                                eventer_config => { default_queue_threads => 2 } }),
     "$name starting noit");
}

sub check_def($$;$$) {
    my $method = shift;
    my $is = shift;
    my $period = shift || 1000;
    my $timeout = shift || int($period/2);
    $is = $is ? "true" : "false";
    my $name = rand();
    return qq{<?xml version="1.0" encoding="utf8"?><check><attributes><target>127.0.0.1</target><period>$period</period><timeout>$timeout</timeout><name>$name</name><filterset>allowall</filterset><module>test_abort</module></attributes><config><method>$method</method><ignore_signals>$is</ignore_signals></config></check>};
}

SKIP: {
  skip "$^O doesn't support iterruptable", 12
    if $^O =~ /^(?:solaris|linux)$/;
boot("default");
usleep(1000000);
$c = apiclient->new('localhost', $NOIT_API_PORT);

$uuid = '6bbdf85c-3c86-11e0-9160-4fdcf11a743f';
$prefix = 'default, interruptable';
@r = $c->put("/checks/set/$uuid", check_def("default", 0));
is($r[0], 200, "$prefix set");
$doc = $xp->parse_string($r[1]);
is($xpc->findvalue('/check/attributes/uuid', $doc), $uuid, "$prefix saved");
usleep(4000000);
@r = $c->get("/checks/show/$uuid");
is($r[0], 200, "$prefix get checks");
$doc = $xp->parse_string($r[1]);
# this is good because we woke up
is($xpc->findvalue('/check/state/state', $doc), 'good', "$prefix results");
@r = $c->delete("/checks/delete/$uuid");
is($r[0], 200, "$prefix delete");
@r = $c->get("/checks/show/$uuid");
is($r[0], 404, "$prefix gone");

$uuid = 'a717e016-3c95-11e0-8d77-b33dc4909098';
$prefix = 'default, uninterruptable';
@r = $c->put("/checks/set/$uuid", check_def("default", 1));
is($r[0], 200, "$prefix set");
$doc = $xp->parse_string($r[1]);
is($xpc->findvalue('/check/attributes/uuid', $doc), $uuid, "$prefix saved");
usleep(4000000);
# Shall have exploded by now.
eval { @r = $c->get("/checks/show/$uuid"); die $r[1] unless $r[0] == 200; };
isnt($@, '', "$prefix get checks fails as expected");
$fh = get_noit_log();
while(<$fh>) {
  if(/Assertion/ && /check->flags & (0x00000001|NP_RUNNING)/ &&
     /test_abort/) {
    $fh->close;
    ok(1, "$prefix: found assertion");
    last;
  }
}
if($fh->opened) { $fh->close; ok(0, "$prefix: found assertion"); }
ok(0 == stop_noit(), "$prefix shutdown (already happened)");
}

SKIP: {
  skip "$^O doesn't support uniterruptable", 6
    if $^O =~ /^(?:solaris)$/;
boot("pending_abort");
usleep(1000000);
$c = apiclient->new('localhost', $NOIT_API_PORT);

$prefix = 'deferred, uninterruptable';
foreach $uuid ('10e23ee2-3ca5-11e0-8049-c77271aac681',
               '28d346fe-3ca5-11e0-aaa2-23d183227952') {
  @r = $c->put("/checks/set/$uuid", check_def("deferred", 1, 10000, 500));
  is($r[0], 200, "$prefix set [$uuid]");
}
usleep(4000000);
# Shall have exploded by now.
$uuid = '5d2e9042-3ca6-11e0-be34-d3dd9fd294cc';
eval { @r = $c->put("/checks/set/$uuid", check_def("deferred", 1, 10000, 500)); die $r[1] unless $r[0] == 200; };
$uuid = '32aeb706-3cac-11e0-b130-9be389e1121e';
eval { @r = $c->put("/checks/set/$uuid", check_def("deferred", 1, 10000, 500)); die $r[1] unless $r[0] == 200; };
isnt($@, '', "$prefix get checks fails as expected");
$fh = get_noit_log();
while(<$fh>) {
  if(/jobq_queue.*induced.*game over/) {
    $fh->close;
    ok(1, "$prefix: found pending_cancels failure");
    last;
  }
}
if($fh->opened) { $fh->close; ok(0, "$prefix: found pending_cancels failure"); }
ok(0 == stop_noit(), "$prefix shutdown (already happened)");
}

SKIP: {
  skip "$^O doesn't support deferred", 12
    if $^O =~ /^(?:solaris|linux)$/;
boot("deferred");
usleep(1000000);
$c = apiclient->new('localhost', $NOIT_API_PORT);

$uuid = '4e577e3e-3ca2-11e0-b9fe-cfa5216d0ae6';
$prefix = 'deferred, interruptable';
@r = $c->put("/checks/set/$uuid", check_def("deferred", 0));
is($r[0], 200, "$prefix set");
$doc = $xp->parse_string($r[1]);
is($xpc->findvalue('/check/attributes/uuid', $doc), $uuid, "$prefix saved");
usleep(4000000);
@r = $c->get("/checks/show/$uuid");
is($r[0], 200, "$prefix get checks");
$doc = $xp->parse_string($r[1]);
is($xpc->findvalue('/check/state/state', $doc), 'bad', "$prefix result");
@r = $c->delete("/checks/delete/$uuid");
is($r[0], 200, "$prefix delete");
@r = $c->get("/checks/show/$uuid");
is($r[0], 404, "$prefix gone");

SKIP: {
  skip "$^O doesn't support deferred, uninterruptable", 5
    if $^O =~ /^(?:linux)$/;
$uuid = '766fb7a0-3ca3-11e0-a725-37c58e6f62dd';
$prefix = 'deferred, uninterruptable';
@r = $c->put("/checks/set/$uuid", check_def("default", 1));
is($r[0], 200, "$prefix set");
$doc = $xp->parse_string($r[1]);
is($xpc->findvalue('/check/attributes/uuid', $doc), $uuid, "$prefix saved");
usleep(4000000);
# Shall have exploded by now.
eval { @r = $c->get("/checks/show/$uuid"); die $r[1] unless $r[0] == 200; };
isnt($@, '', "$prefix get checks fails as expected");
$fh = get_noit_log();
while(<$fh>) {
  if(/Assertion/ && /check->flags & (0x00000001|NP_RUNNING)/ &&
     /test_abort/) {
    $fh->close;
    ok(1, "$prefix: found assertion");
    last;
  }
}
if($fh->opened) { $fh->close; ok(0, "$prefix: found assertion"); }
}
ok(0 == stop_noit(), "$prefix shutdown (already happened)");
}

boot("evil");
usleep(1000000);
$c = apiclient->new('localhost', $NOIT_API_PORT);

$uuid = 'd62c889e-3ca3-11e0-95f5-1fe23931d67b';
$prefix = 'evil, interruptable';
@r = $c->put("/checks/set/$uuid", check_def("evil", 0));
is($r[0], 200, "$prefix set");
$doc = $xp->parse_string($r[1]);
is($xpc->findvalue('/check/attributes/uuid', $doc), $uuid, "$prefix saved");
usleep(4000000);
@r = $c->get("/checks/show/$uuid");
is($r[0], 200, "$prefix get checks");
$doc = $xp->parse_string($r[1]);
is($xpc->findvalue('/check/state/state', $doc), 'bad', "$prefix result");
@r = $c->delete("/checks/delete/$uuid");
is($r[0], 200, "$prefix delete");
@r = $c->get("/checks/show/$uuid");
is($r[0], 404, "$prefix gone");

$uuid = 'e169c230-3ca3-11e0-9aa6-ffa78466723c';
$prefix = 'evil, uninterruptable';
@r = $c->put("/checks/set/$uuid", check_def("evil", 1));
is($r[0], 200, "$prefix set");
$doc = $xp->parse_string($r[1]);
is($xpc->findvalue('/check/attributes/uuid', $doc), $uuid, "$prefix saved");
usleep(4000000);
# Evil "works"...
# while dangerous it should reliably work for the test_abort module
@r = $c->get("/checks/show/$uuid");
is($r[0], 200, "$prefix get checks");
$doc = $xp->parse_string($r[1]);
is($xpc->findvalue('/check/state/state', $doc), 'bad', "$prefix result");
@r = $c->delete("/checks/delete/$uuid");
is($r[0], 200, "$prefix delete");
@r = $c->get("/checks/show/$uuid");
is($r[0], 404, "$prefix gone");
ok(stop_noit(), "$prefix shutdown");

boot("asynch");
usleep(1000000);
$c = apiclient->new('localhost', $NOIT_API_PORT);

$prefix = 'asynch, interruptable';
$uuid = '92f56c20-3ca4-11e0-acbe-a3a119ca7850';
@r = $c->put("/checks/set/$uuid", check_def("asynch", 0));
is($r[0], 200, "$prefix set");
$doc = $xp->parse_string($r[1]);
is($xpc->findvalue('/check/attributes/uuid', $doc), $uuid, "$prefix saved");
usleep(4000000);
@r = $c->get("/checks/show/$uuid");
is($r[0], 200, "$prefix get checks");
$doc = $xp->parse_string($r[1]);
is($xpc->findvalue('/check/state/state', $doc), 'bad', "$prefix result");
@r = $c->delete("/checks/delete/$uuid");
is($r[0], 200, "$prefix delete");
@r = $c->get("/checks/show/$uuid");
is($r[0], 404, "$prefix gone");

$uuid = '9f1bdd2c-3ca4-11e0-b6fe-7fee9b8a4257';
$prefix = 'asynch, uninterruptable';
SKIP: {
  skip "$^O doesn't support cancel_asynchronous", 6
    if $^O =~ /^(?:darwin)$/;

  @r = $c->put("/checks/set/$uuid", check_def("asynch", 1));
  is($r[0], 200, "$prefix set");
  $doc = $xp->parse_string($r[1]);
  is($xpc->findvalue('/check/attributes/uuid', $doc), $uuid, "$prefix saved");
  usleep(4000000);
  @r = $c->get("/checks/show/$uuid");
  is($r[0], 200, "$prefix get checks");
  $doc = $xp->parse_string($r[1]);
  is($xpc->findvalue('/check/state/state', $doc), 'bad', "$prefix result");
  @r = $c->delete("/checks/delete/$uuid");
  is($r[0], 200, "$prefix delete");
  @r = $c->get("/checks/show/$uuid");
  is($r[0], 404, "$prefix gone");
};
ok(stop_noit(), "$prefix shutdown");

1;
