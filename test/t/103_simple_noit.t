use Test::More tests => 6;
use XML::LibXML;
use XML::LibXML::XPathContext;
use testconfig;
use apiclient;

use strict;
my $xp = XML::LibXML->new();
my $xpc = XML::LibXML::XPathContext->new();

ok(start_noit("103", { logs_debug => { '' => 'false' } }), 'starting noit');
sleep(1);
my $c = apiclient->new('localhost', $NOIT_API_PORT);
my @r = $c->get("/checks/show/f7cea020-f19d-11dd-85a6-cb6d3a2207dc");
is($r[0], 404, 'get checks');

@r = $c->put("/checks/set/f7cea020-f19d-11dd-85a6-cb6d3a2207dc",
        qq{<?xml version="1.0" encoding="utf8"?>
<check><attributes><target>127.0.0.1</target><period>5000</period><timeout>1000</timeout><name>selfcheck</name><filterset/><module>selfcheck</module></attributes><config/></check>});

is($r[0], 200, 'add selfcheck');
my $doc = $xp->parse_string($r[1]);
is($xpc->findvalue('/check/attributes/uuid', $doc), 'f7cea020-f19d-11dd-85a6-cb6d3a2207dc', 'saved');

sleep(1);
@r = $c->get("/checks/show/f7cea020-f19d-11dd-85a6-cb6d3a2207dc");
is($r[0], 200, 'get checks');
$doc = $xp->parse_string($r[1]);
is($xpc->findvalue('/check/state/state', $doc), 'good', 'results');

1;
