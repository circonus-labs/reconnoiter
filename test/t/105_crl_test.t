use Test::More tests => 3;
use XML::LibXML;
use XML::LibXML::XPathContext;
use testconfig;
use apiclient;

use strict;
my ($c, @r);
my $xp = XML::LibXML->new();
my $xpc = XML::LibXML::XPathContext->new();

ok(start_noit("105", { logs_debug => { '' => 'false' } }), 'starting noit');
sleep(1);
$c = apiclient->new('localhost', $NOIT_API_PORT);
@r = $c->get("/checks/show/f7cea020-f19d-11dd-85a6-cb6d3a2207dc");
is($r[0], 404, 'request works with usable key');

$c = apiclient->new('localhost', $NOIT_API_PORT,
                    { 'key' => '../badclient.key',
                      'cert' => '../badclient.crt' });
eval {
    @r = $c->get("/checks/show/f7cea020-f19d-11dd-85a6-cb6d3a2207dc");
};
like($@, qr/SSL/, 'request fails with revoked key');

1;
