use Test::More tests => 10;
use XML::LibXML;
use XML::LibXML::XPathContext;
use testconfig;
use Time::HiRes qw/gettimeofday tv_interval usleep/;
use apiclient;

use strict;
my $xp = XML::LibXML->new();
my $xpc = XML::LibXML::XPathContext->new();
my @r;
my $doc;
my $codes = {};

ok(start_noit("103", { logs_debug => { '' => 'false' } }), 'starting noit');
my $c = apiclient->new('localhost', $NOIT_API_PORT, "noit-test");
my $req_time = [gettimeofday];
@r = $c->capabilities();
my $answer_time = [gettimeofday];
is($r[0], 0, 'capa');
$doc = $xp->parse_string($r[1]);
foreach ($xpc->findnodes('/noit_capabilities/services/service[@name="control_dispatch"]/command', $doc)) {
  $codes->{$_->getAttribute("code")} = $_->getAttribute("name");
}
is_deeply($codes,
          { '0xfa57feed' => 'livestream_transit'
          , '0x43415041' => 'capabilities_transit'
          , '0x44454c45' => 'noit_wire_rest_api'
          , '0x47455420' => 'noit_wire_rest_api'
          , '0x48454144' => 'noit_wire_rest_api'
          , '0x4d455247' => 'noit_wire_rest_api'
          , '0x504f5354' => 'noit_wire_rest_api'
          , '0x50555420' => 'noit_wire_rest_api'
          , '0x52455645' => 'reverse_accept',
          , '0x7e66feed' => 'log_transit'
          , '0xda7afeed' => 'log_transit'
          },
          'commands available');

# Time check
my $remote_time = $xpc->findvalue('/noit_capabilities/current_time', $doc);
my $request_duration = tv_interval($req_time, $answer_time);
$remote_time += $request_duration/2.0;
my $answer_time_sec = $answer_time->[0] + $answer_time->[1]/1000000.0;
my $time_error = abs($remote_time - $answer_time_sec);
cmp_ok($time_error, '<', $request_duration, 'time skew check');

$c = apiclient->new('localhost', $NOIT_API_PORT, 'noit-test');
@r = $c->get("/checks/show/f7cea020-f19d-11dd-85a6-cb6d3a2207dc");
is($r[0], 404, 'get checks');

@r = $c->put("/checks/set/f7cea020-f19d-11dd-85a6-cb6d3a2207dc",
        qq{<?xml version="1.0" encoding="utf8"?>
<check><attributes><target>127.0.0.1</target><period>5000</period><timeout>1000</timeout><name>selfcheck</name><filterset>allowall</filterset><module>selfcheck</module></attributes><config/></check>});

is($r[0], 200, 'add selfcheck');
$doc = $xp->parse_string($r[1]);
is($xpc->findvalue('/check/attributes/uuid', $doc), 'f7cea020-f19d-11dd-85a6-cb6d3a2207dc', 'saved');

usleep(2000000);
@r = $c->get("/checks/show/f7cea020-f19d-11dd-85a6-cb6d3a2207dc");
is($r[0], 200, 'get checks');
$doc = $xp->parse_string($r[1]);
is($xpc->findvalue('/check/state/state', $doc), 'good', 'results');

ok(stop_noit, 'stopping noit');
1;
