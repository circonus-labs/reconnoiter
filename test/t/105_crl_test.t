use Test::More tests => 4;
use XML::LibXML;
use XML::LibXML::XPathContext;
use Time::HiRes qw/usleep/;
use testconfig;
use apiclient;

use strict;
my ($c, @r);
my $xp = XML::LibXML->new();
my $xpc = XML::LibXML::XPathContext->new();

ok(start_noit("105", { logs_debug => { '' => 'false' } }), 'starting noit');
usleep(1000000);
$c = apiclient->new('localhost', $NOIT_API_PORT);
@r = $c->get("/checks/show/f7cea020-f19d-11dd-85a6-cb6d3a2207dc");
is($r[0], 404, 'request works with usable key');

$c = apiclient->new('localhost', $NOIT_API_PORT,
                    { 'key' => '../badclient.key',
                      'cert' => '../badclient.crt' });
@r = $c->get("/checks/show/f7cea020-f19d-11dd-85a6-cb6d3a2207dc");
like($r[1], qr/SSL/, 'request fails with revoked key');
ok(stop_noit, 'stopping noit');

1;
