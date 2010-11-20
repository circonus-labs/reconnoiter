use Test::More tests => 17;
use XML::LibXML;
use XML::LibXML::XPathContext;
use testconfig;
use apiclient;
use Data::Dumper;

use strict;
my $uuid = '9c2163aa-f4bd-11df-851b-979bd290a553';
my $xp = XML::LibXML->new();
my $xpc = XML::LibXML::XPathContext->new();

ok(start_noit("108", { logs_debug => { '' => 'false' } }), 'starting noit');
ok(start_stratcon("108", { noits => [ { address => "127.0.0.1", port => "$NOIT_API_PORT" } ] }), 'starting stratcon');
sleep(1);
my $c = apiclient->new('localhost', $NOIT_API_PORT);
my @r = $c->get("/checks/show/$uuid");
is($r[0], 404, 'get checks');

@r = $c->put("/checks/set/$uuid",
        qq{<?xml version="1.0" encoding="utf8"?>
<check><attributes><target>127.0.0.1</target><period>5000</period><timeout>500</timeout><name>selfcheck</name><filterset>allowall</filterset><module>selfcheck</module></attributes><config/></check>});

is($r[0], 200, 'add selfcheck');
my $doc = $xp->parse_string($r[1]);
is($xpc->findvalue('/check/attributes/uuid', $doc), $uuid, 'saved');

sleep(1);
@r = $c->get("/checks/show/$uuid");
is($r[0], 200, 'get checks');
$doc = $xp->parse_string($r[1]);
is($xpc->findvalue('/check/state/state', $doc), 'good', 'results');

ok(1, 'going to sleep 1 seconds for mapping');
sleep(1);

my $conn = pg('reconnoiter','reconnoiter');
ok($conn, 'data store connection');
my $st=$conn->prepare("select sid from stratcon.map_uuid_to_sid where id = ?");
$st->execute($uuid);
my ($sid) = $st->fetchrow();
$st->finish();
ok($sid, 'uuid mapped to sid');
sub do_counts {
  my $artext = $conn->prepare("select count(*) from noit.metric_text_archive ".
                              " where sid = ?");
  my $arnum = $conn->prepare("select count(*) from noit.metric_numeric_archive ".
                             " where sid = ?");
  $artext->execute($sid);
  my ($text_rows) = $artext->fetchrow();
  $arnum->execute($sid);
  my ($numeric_rows) = $arnum->fetchrow();
  return ($text_rows, $numeric_rows);
}

my($st_t, $st_n) = do_counts();

ok(1, 'going to sleep 7 seconds for data to stream');
sleep(7);

my $sc = apiclient->new('localhost', $STRATCON_API_PORT);
@r = $sc->get('/noits/show');
is($r[0], '200', 'get noits');
$doc = $xp->parse_string($r[1]);
cmp_ok($xpc->findvalue('/noits/noit[@type="durable/storage"]/@session_events', $doc), '>', 0, 'durable connection (events)');
cmp_ok($xpc->findvalue('/noits/noit[@type="transient/iep"]/@session_events', $doc), '>', 0, 'iep connection (events)');

ok(1, 'going to wait 2 more seconds for load into postgres');
sleep(2);
my($f_t, $f_n) = do_counts();
cmp_ok($st_t, '<', $f_t, 'text metrics loaded');
cmp_ok($st_n, '<', $f_n, 'numeric metrics loaded');

1;
