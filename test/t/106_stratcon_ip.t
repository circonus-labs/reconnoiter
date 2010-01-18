use Test::More tests => 6;
use XML::LibXML;
use XML::LibXML::XPathContext;
use testconfig;
use apiclient;

use strict;
my $xp = XML::LibXML->new();
my $xpc = XML::LibXML::XPathContext->new();
my $doc;

ok(start_stratcon("106"), 'starting stratcon');
sleep(1);

my $conn = pg('reconnoiter','reconnoiter');
ok($conn, 'connected to db');
my $s = $conn->prepare("select remote_address from stratcon.current_node_config");
ok($s, 'prepared test query');
ok($s->execute(), 'executed test query');
my ($ip) = $s->fetchrow();
like($ip, qr/^(?:\d+\.){3}\d+$/, 'is an ip');
isn't($ip, "0.0.0.0", 'is not 0.0.0.0');
undef $conn;

1;
