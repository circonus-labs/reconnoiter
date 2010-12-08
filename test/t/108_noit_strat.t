use Test::More tests => 26;
use WWW::Curl::Easy;
use JSON;
use XML::LibXML;
use XML::LibXML::XPathContext;
use testconfig;
use apiclient;
use stomp;
use Data::Dumper;

use strict;
my $uuid_re = qr/^[0-9a-fA-F]{4}(?:[0-9a-fA-F]{4}-){4}[0-9a-fA-F]{12}$/;
my $uuid = '9c2163aa-f4bd-11df-851b-979bd290a553';
my $xp = XML::LibXML->new();
my $xpc = XML::LibXML::XPathContext->new();

my $iep_queries = [
  { id => '52f1f2ec-0275-11e0-a846-b757d1de0f4a',
    topic => 'numeric',
    epl => 'select * from NoitMetricNumeric as r'
  },
  { id => '8f04d54c-0275-11e0-b62c-6fdd90cb8bde',
    topic => 'text',
    epl => 'select * from NoitMetricText as r'
  },
  { id => '95f4ed2e-0275-11e0-bdc7-27110e43915b',
    topic => 'status',
    epl => 'select * from NoitStatus as r'
  },
];

ok(start_noit("108", { logs_debug => { '' => 'false' } }), 'starting noit');
ok(start_stratcon("108", { noits => [ { address => "127.0.0.1", port => "$NOIT_API_PORT" } ], iep => { queries => $iep_queries } }), 'starting stratcon');
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
my $sid = undef;
if($conn) {
  my $st=$conn->prepare("select sid from stratcon.map_uuid_to_sid where id = ?");
  $st->execute($uuid);
  ($sid) = $st->fetchrow();
  $st->finish();
}
ok($sid, 'uuid mapped to sid');

sub do_counts {
  return (0,0) unless $conn;
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

### Test real-time streaming.

my $curl = WWW::Curl::Easy->new;
$curl->setopt(CURLOPT_URL, "http://localhost:$STRATCON_WEB_PORT/data/$uuid\@500");
$curl->setopt(CURLOPT_TIMEOUT, 5);
my $response_body;
$curl->setopt(CURLOPT_WRITEDATA,\$response_body);
my $retcode = 0;
$retcode = $curl->perform;
is($retcode, 28, 'needed to timeout stream');

my @rdata;
my $json_text;
my $test_S = 0;
eval {
  foreach (split(/\R/,$response_body)) {
    if(/^\s*<script id=.*window\.parent\.plot_iframe_data\((\{.*?\})\)/) {
      $json_text = $1;
      my $json = from_json($json_text);
      push @rdata, $json;
      if(!$test_S && $json->{type} eq 'S') {
        like($json->{id}, $uuid_re, 'status line uuid');
        is($json->{check_module}, 'selfcheck', 'status line module');
        $test_S = 1;
      }
    }
  }
};
if(!$test_S) {
  ok(0, "status line uuid");
  ok(0, "status line module");
}
is($@, '', 'json parse errors: ' . ($@ ? $json_text : 'none'));

# There are at least 4 metrics for the self check.
# in 5 seconds - (1 second lag) - jittered start at 500ms period,
# it should run at least 7 times.
cmp_ok(scalar(@rdata), '>=', 7*4, 'streamed data');

my $stomp;
my $payload;
my $json;

$stomp = stomp->new();
$stomp->subscribe('/queue/noit.firehose');
$payload = $stomp->get({timeout => 6});
undef $stomp;
ok($payload, 'firehose traffic');

$stomp = stomp->new();
$stomp->subscribe('/topic/noit.alerts.numeric');
$payload = $stomp->get({timeout => 6});
eval { $json = from_json($payload); };
is($@, '', 'json numeric payload');
undef $stomp;
like($json->{r}->{uuid} || '', $uuid_re, 'numeric match has uuid');
is($json->{r}->{check_module} || '', 'selfcheck', 'modules is set');

1;
