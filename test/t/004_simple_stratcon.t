use Test::More tests => 9;
use XML::LibXML;
use XML::LibXML::XPathContext;
use testconfig;
use apiclient;

use strict;
my $xp = XML::LibXML->new();
my $xpc = XML::LibXML::XPathContext->new();
my $doc;

ok(start_stratcon("004"), 'starting stratcon');
sleep(1);
my $c = apiclient->new('localhost', $STRATCON_API_PORT);
my @r = $c->get("/noits/show");
is($r[0], 200, 'show noits');
$doc = $xp->parse_string($r[1]);
my @nonoits = $xpc->findnodes("/noits/noit", $doc);
is(@nonoits, 0, 'no noits');
@r = $c->put("/noits/set/127.0.0.1:$NOIT_API_PORT");
is($r[0], 200, 'add noit');
@r = $c->get("/noits/show");
is($r[0], 200, 'show noits');
$doc = $xp->parse_string($r[1]);
my @noits = $xpc->findnodes("/noits/noit", $doc);
is(@noits, 2, 'noits added');
@r = $c->delete("/noits/delete/127.0.0.1:$NOIT_API_PORT");
is($r[0], 200, 'delete noit');
@r = $c->get("/noits/show");
is($r[0], 200, 'show noits');
$doc = $xp->parse_string($r[1]);
@nonoits = $xpc->findnodes("/noits/noit", $doc);
is(@nonoits, 0, 'removed noit');

1;
