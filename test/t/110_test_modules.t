use Test::More tests => 8;
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

ok(start_noit("110",
  {
    generics => { 'check_test' => { image => 'check_test' } },
    modules => { 'dns' => { image => 'dns' },
                 'interp' => { loader => 'lua', object => 'noit.test.interp' } },
    logs_debug => { '' => 'false' }
  }), 'starting noit');

my $checkno = 0;
sub mkcheckxml {
   my $target = shift;
   my $module = shift;
   my $config = shift || {};
   $checkno++;
   my $configxml = join("\n", map { "<$_>$config->{$_}</$_>"; } (keys %$config));
   qq{<?xml version="1.0" encoding="utf8"?>
<check><attributes><target>$target</target><period>5000</period><timeout>1000</timeout><name>test.$checkno</name><filterset>allowall</filterset><module>$module</module></attributes><config>$configxml</config></check>}
}

my ($c, $metrics, $expected);
foreach(qw/broken copy name module inaddrarpa reverseip ccns
           randint randuuid randbroken/) {
  $expected->{$_} = 'SUCCESS';
}

$c = apiclient->new('localhost', $NOIT_API_PORT, 'noit-test');
@r = $c->post("/checks/test", mkcheckxml('192.168.19.12', 'interp', { key => "foofoo" }));
is($r[0], 200, 'test interp ipv4');
$doc = $xp->parse_string($r[1]);
is($xpc->findvalue('/check/state/state', $doc), 'good', 'successful run');
$metrics = {};
foreach ($xpc->findnodes('/check/state/metrics/metric', $doc)) {
  ($metrics->{$_->getAttribute("name")} = $_->textContent()) =~ s/:.+//;
}
is_deeply($metrics, $expected, 'ipv4 metrics passed');

@r = $c->post("/checks/test", mkcheckxml('fe80::7ed1:c3ff:fedc:ddf7', 'interp', { key => "quuxer" }));
is($r[0], 200, 'test interp ipv6');
$doc = $xp->parse_string($r[1]);
is($xpc->findvalue('/check/state/state', $doc), 'good', 'successful run');
$metrics = {};
foreach ($xpc->findnodes('/check/state/metrics/metric', $doc)) {
  ($metrics->{$_->getAttribute("name")} = $_->textContent()) =~ s/:.+//;
}
is_deeply($metrics, $expected, 'ipv6 metrics passed');

ok(stop_noit, 'stopping noit');
1;
