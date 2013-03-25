package testconfig;
use Test::More;
use Fcntl;
use DBI;
use Cwd;
use Exporter 'import';
use Data::Dumper;
use IO::File;
use strict;
use vars qw/@EXPORT/;
sub mkL {
  my $fd = $_[0];
  return sub {} unless $ENV{DEBUG_TESTS};
  return sub { print $fd "[$$] $_[0]\n"; }
};
sub childL {
  open my $olderr, ">&STDERR";
  { no warnings 'redefine'; *L = mkL($olderr); }
}
*L = mkL(\*STDERR);

my $noit_pid = 0;
my $noit_log = undef;
my $stratcon_pid = 0;
my $stratcon_log = undef;


@EXPORT = qw($NOIT_TEST_DB $NOIT_TEST_DB_PORT
             $NOIT_API_PORT $NOIT_CLI_PORT
             $STRATCON_API_PORT $STRATCON_CLI_PORT
             $STRATCON_WEB_PORT
             pg make_noit_config start_noit stop_noit get_noit_log
             make_stratcon_config start_stratcon stop_stratcon get_stratcon_log
             $MODULES_DIR $LUA_DIR $all_noit_modules $all_stratcon_modules);

our $default_filterset = {
  allowall => [ { type => "allow" } ],
};
our $all_noit_modules = {
  'selfcheck' => { 'image' => 'selfcheck' },
  'ping_icmp' => { 'image' => 'ping_icmp' },
  'snmp' => { 'image' => 'snmp' },
  'ssh2' => { 'image' => 'ssh2' },
  'mysql' => { 'image' => 'mysql' },
  'postgres' => { 'image' => 'postgres' },
  'test_abort' => { 'image' => 'test_abort' },
  'varnish' => { 'loader' => 'lua', 'object' => 'noit.module.varnish' },
  'http' => { 'loader' => 'lua', 'object' => 'noit.module.http' },
  'resmon' => { 'loader' => 'lua', 'object' => 'noit.module.resmon' },
  'smtp' => { 'loader' => 'lua', 'object' => 'noit.module.smtp' },
  'tcp' => { 'loader' => 'lua', 'object' => 'noit.module.tcp' },
};

our $NOIT_TEST_DB = "/tmp/noit-test-db-$>";
our $NOIT_TEST_DB_PORT = 23816;
our $NOIT_API_PORT = 42364;
our $NOIT_CLI_PORT = 42365;
our $STRATCON_API_PORT = 42366;
our $STRATCON_CLI_PORT = 42367;
our $STRATCON_WEB_PORT = 42368;

our ($MODULES_DIR, $LUA_DIR);

sub pg {
  my $db = shift || 'postgres';
  my $user = shift || $ENV{USER};
  return DBI->connect(
    "dbi:Pg:host=localhost;port=$NOIT_TEST_DB_PORT;database=$db", $user, '',
    { 'PrintError' => 0 }
  );
}

sub make_eventer_config {
  my ($o, $opts) = @_;
  my $cwd = $opts->{cwd};
  $opts->{eventer_config}->{default_queue_threads} ||= 10;
  $opts->{eventer_config}->{default_ca_chain} ||= "$cwd/../test-ca.crt";
  print $o qq{
  <eventer>
    <config>
      <default_queue_threads>$opts->{eventer_config}->{default_queue_threads}</default_queue_threads>
      <default_ca_chain>$opts->{eventer_config}->{default_ca_chain}</default_ca_chain>
    </config>
  </eventer>
};
}
sub make_rest_acls {
  my ($o, $opts) = @_;
  my $acls = $opts->{rest_acls};
  print $o qq{  <rest>\n};
  foreach my $acl (@$acls) {
    print $o qq^    <acl^;
    print $o qq^ type="$acl->{type}"^ if exists($acl->{type});
    print $o qq^ cn="$acl->{cn}"^ if exists($acl->{cn});
    print $o qq^ url="$acl->{url}"^ if exists($acl->{url});
    print $o qq^>\n^;
    my $rules = $acl->{rules};
    foreach my $rule (@$rules) {
      print $o qq^      <rule^;
      print $o qq^ type="$rule->{type}"^ if exists($rule->{type});
      print $o qq^ cn="$rule->{cn}"^ if exists($rule->{cn});
      print $o qq^ url="$rule->{url}"^ if exists($rule->{url});
      print $o qq^/>\n^;
    }
    print $o qq^    </acl>\n^;
  }
  print $o qq{  </rest>\n};
}
sub make_log_section {
  my ($o, $type, $dis) = @_;
  print $o qq{      <$type>
        <outlet name="$type"/>
};
  while (my ($t, $d) = each %$dis) {
    next unless length($t);
    print $o qq{        <log name="$type/$t" disabled="$d"/>\n};
  }
  print $o qq{      </$type>\n};
}
sub make_logs_config {
  my ($o, $opts) = @_;
  my $cwd = $opts->{cwd};
  my @logtypes = qw/collectd dns eventer external lua mysql ping_icmp postgres
                    selfcheck snmp ssh2/;
  # These are disabled attrs, so they look backwards
  if(!exists($opts->{logs_error})) {
    $opts->{logs_error}->{''} ||= 'false';
  }
  if(!exists($opts->{logs_debug})) {
    $opts->{logs_debug}->{''} ||= 'true';
  }
  foreach(@logtypes) {
    $opts->{logs_error}->{$_} ||= 'false';
    $opts->{logs_debug}->{$_} ||= 'true';
  }
  
  print $o qq{
  <logs>
    <console_output>
      <outlet name="stderr"/>
      <log name="error" disabled="$opts->{logs_error}->{''}" timestamps="true"/>
      <log name="debug" disabled="$opts->{logs_debug}->{''}" timestamps="true"/>
    </console_output>
    <feeds>
      <log name="feed" type="jlog" path="$cwd/logs/$opts->{name}.feed(*)"/>
    </feeds>
    <components>
};
  make_log_section($o, 'error', $opts->{logs_error});
  make_log_section($o, 'debug', $opts->{logs_debug});
  print $o qq{
    </components>
    <feeds>
      <config><extended_id>on</extended_id></config>
      <outlet name="feed"/>
      <log name="bundle"/>
      <log name="check">
        <outlet name="error"/>
      </log>
      <log name="status"/>
      <log name="metrics"/>
      <log name="config"/>
    </feeds>
  </logs>
};
}
sub make_modules_config {
  my ($o, $opts) = @_;
  my $cwd = $opts->{cwd};
  print $o qq{
  <modules directory="$cwd/../../src/modules">
    <loader image="lua" name="lua">
      <config><directory>$cwd/../../src/modules-lua/?.lua</directory></config>
    </loader>
};
  foreach(keys %{$opts->{generics}}) {
    print $o qq{    <generic }; 
    print $o qq{ image="$opts->{generics}->{$_}->{image}"}
      if(exists($opts->{generics}->{$_}->{image}));
    print $o qq{ name="$_"/>\n};
  }
  foreach(keys %{$opts->{modules}}) {
    print $o qq{    <module }; 
    print $o qq{ image="$opts->{modules}->{$_}->{image}"}
      if(exists($opts->{modules}->{$_}->{image}));
    print $o qq{ loader="$opts->{modules}->{$_}->{loader}"}
      if(exists($opts->{modules}->{$_}->{loader}));
    print $o qq{ object="$opts->{modules}->{$_}->{object}"}
      if(exists($opts->{modules}->{$_}->{object}));
    print $o qq{ name="$_"/>\n};
  }
  print $o qq{</modules>\n};
}
sub make_noit_listeners_config {
  my ($o, $opts) = @_;
  my $cwd = $opts->{cwd};
  $opts->{noit_api_port} ||= $NOIT_API_PORT;
  $opts->{noit_cli_port} ||= $NOIT_CLI_PORT;
  print $o qq{
  <listeners>
    <sslconfig>
      <optional_no_ca>false</optional_no_ca>
      <certificate_file>$cwd/../test-noit.crt</certificate_file>
      <key_file>$cwd/../test-noit.key</key_file>
      <ca_chain>$cwd/../test-ca.crt</ca_chain>
      <crl>$cwd/../test-ca.crl</crl>
    </sslconfig>
    <consoles type="noit_console">
      <listener address="*" port="$opts->{noit_cli_port}">
        <config>
          <line_protocol>telnet</line_protocol>
        </config>
      </listener>
    </consoles>
    <listener type="control_dispatch" address="*" port="$opts->{noit_api_port}" ssl="on">
      <config>
        <log_transit_feed_name>feed</log_transit_feed_name>
      </config>
    </listener>
  </listeners>
};
}
sub do_check_print {
  my $o = shift;
  my $list = shift;
  return unless $list;
  foreach my $node (@$list) {
    print $o qq{<$node->[0]};
    while(my ($k, $v) = each %{$node->[1]}) {
      print $o qq{ $k="$v"};
    }
    if($node->[2]) {
      print $o qq{>\n};
      do_check_print($o, $node->[2]);
      print $o qq{</check>\n};
    }
    else {
      print $o qq{/>\n};
    }
  }
}
sub make_checks_config {
  my ($o, $opts) = @_;
  my $cwd = $opts->{cwd};
  print $o qq{  <checks max_initial_stutter="10" filterset="default">\n};
  do_check_print($o, $opts->{checks});
  print $o qq{  </checks>\n};
}
sub make_filtersets_config {
  my ($o, $opts) = @_;
  my $cwd = $opts->{cwd};
  print $o qq{<filtersets>\n};
  while (my ($name, $set) = each %{$opts->{filtersets}}) {
    print $o qq{  <filterset name="$name">\n};
    foreach my $rule (@$set) {
      print $o qq{    <rule };
      while(my ($k,$v) = each %$rule) {
        print $o qq{ $k="$v"};
      }
      print $o qq{/>\n};
    }
    print $o qq{  </filterset>\n};
  }
  print $o qq{</filtersets>\n};
}

sub make_noit_config {
  my $name = shift;
  my $options = shift;
  $options->{cwd} ||= getcwd();
  $options->{modules} = $all_noit_modules unless exists($options->{modules});
  $options->{filtersets} = $default_filterset unless exists($options->{filtersets});
  $options->{rest_acls} ||= [ { type => 'deny', rules => [ { type => 'allow' } ] } ];
  my $cwd = $options->{cwd};
  my $file = "$cwd/logs/${name}_noit.conf";
  open (my $o, ">$file") || BAIL_OUT("can't write config: $file");
  print $o qq{<?xml version="1.0" encoding="utf8" standalone="yes"?>\n};
  print $o qq{<noit>};
  make_eventer_config($o, $options);
  make_rest_acls($o, $options);
  make_logs_config($o, $options);
  make_modules_config($o, $options);
  make_noit_listeners_config($o, $options);
  make_checks_config($o, $options);
  make_filtersets_config($o, $options);
  print $o qq{</noit>\n};
  close($o);
  return $file;
}

sub make_stratcon_noits_config {
  my ($o, $opts) = @_;
  my $cwd = $opts->{cwd};
  $opts->{noit_api_port} ||= $NOIT_API_PORT;
  print $o qq{
  <noits>
    <sslconfig>
      <certificate_file>$cwd/../test-stratcon.crt</certificate_file>
      <key_file>$cwd/../test-stratcon.key</key_file>
      <ca_chain>$cwd/../test-ca.crt</ca_chain>
    </sslconfig>
    <config>
      <reconnect_initial_interval>1000</reconnect_initial_interval>
      <reconnect_maximum_interval>15000</reconnect_maximum_interval>
    </config>
};
  foreach my $n (@{$opts->{noits}}) {
    print $o qq{    <noit};
    while (my ($k,$v) = each %$n) {
      print $o qq{ $k=\"$v\"};
    }
    print $o qq{/>\n};
  }
  print $o qq{</noits>\n};
}

sub make_stratcon_listeners_config {
  my ($o, $opts) = @_;
  my $cwd = $opts->{cwd};
  $opts->{stratcon_api_port} ||= $STRATCON_API_PORT;
  $opts->{stratcon_web_port} ||= $STRATCON_WEB_PORT;
  print $o qq{
  <listeners>
    <sslconfig>
      <certificate_file>$cwd/../test-stratcon.crt</certificate_file>
      <key_file>$cwd/../test-stratcon.key</key_file>
      <ca_chain>$cwd/../test-ca.crt</ca_chain>
    </sslconfig>
    <realtime type="http_rest_api">
      <listener address="*" port="$opts->{stratcon_web_port}">
        <config>
          <hostname>stratcon.noit.example.com</hostname>
          <document_domain>noit.example.com</document_domain>
        </config>
      </listener>
    </realtime>
    <listener type="control_dispatch" address="*" port="$opts->{stratcon_api_port}" ssl="on" />
  </listeners>
};
}

sub make_iep_config {
  my ($o, $opts) = @_;
  my $cwd = $opts->{cwd};
  $opts->{iep}->{disabled} ||= 'false';
  mkdir("$cwd/logs/$opts->{name}_iep_root");
  open(my $run, "<$cwd/../../src/java/run-iep.sh") ||
    BAIL_OUT("cannot open source run-iep.sh");
  sysopen(my $newrun, "$cwd/logs/$opts->{name}_iep_root/run-iep.sh", O_WRONLY|O_CREAT, 0755) ||
    BAIL_OUT("cannot open target run-iep.sh");
  while(<$run>) {
    s%^DIRS="%DIRS="$cwd/../../src/java $cwd/../../src/java/lib %;
    print $newrun $_;
  }
  close($run);
  close($newrun);
  print $o qq{
  <iep disabled="$opts->{iep}->{disabled}">
    <start directory="$cwd/logs/$opts->{name}_iep_root"
           command="$cwd/logs/$opts->{name}_iep_root/run-iep.sh" />
};
  foreach my $mqt (keys %{$opts->{iep}->{mq}}) {
    print $o qq{    <mq type="$mqt">\n};
    while (my ($k,$v) = each %{$opts->{iep}->{mq}->{mqt}}) {
      print $o qq{      <$k>$v</$k>\n};
    }
    print $o qq{    </mq>\n};
  }
  foreach my $bt (keys %{$opts->{iep}->{broker}}) {
    print $o qq{    <broker adapter="$bt">\n};
    while (my ($k,$v) = each %{$opts->{iep}->{broker}->{bt}}) {
      print $o qq{      <$k>$v</$k>\n};
    }
    print $o qq{    </broker>\n};
  }
  print $o qq{    <queries master="iep">\n};
  foreach my $s (@{$opts->{iep}->{statements}}) {
    print $o qq{        <statement id="$s->{id}" provides="$s->{id}">\n};
    print $o qq{            <requires>$s->{requires}</requires>\n} if $s->{requires};
    print $o qq{            <epl><![CDATA[$s->{epl}]]></epl>\n};
    print $o qq{        </statement>\n};
  }
  foreach my $s (@{$opts->{iep}->{queries}}) {
    print $o qq{        <query id="$s->{id}" topic="$s->{topic}">\n};
    print $o qq{            <epl><![CDATA[$s->{epl}]]></epl>\n};
    print $o qq{        </query>\n};
  }
  print $o qq{    </queries>\n};
  print $o qq{</iep>\n};
}
sub make_database_config {
  my ($o, $opts) = @_;
  my $cwd = $opts->{cwd};
  print $o qq{
  <database>
    <journal>
      <path>$cwd/logs/$opts->{name}_stratcon.persist</path>
    </journal>
    <dbconfig>
      <host>localhost</host>
      <port>$NOIT_TEST_DB_PORT</port>
      <dbname>reconnoiter</dbname>
      <user>stratcon</user>
      <password>stratcon</password>
    </dbconfig>
    <statements>
      <allchecks><![CDATA[
        SELECT remote_address, id, target, module, name
          FROM check_currently
      ]]></allchecks>
      <findcheck><![CDATA[
        SELECT remote_address, id, target, module, name
          FROM check_currently
         WHERE sid = \$1
      ]]></findcheck>
      <allstoragenodes><![CDATA[
        SELECT storage_node_id, fqdn, dsn
          FROM stratcon.storage_node
      ]]></allstoragenodes>
      <findstoragenode><![CDATA[
        SELECT fqdn, dsn
          FROM stratcon.storage_node
         WHERE storage_node_id = \$1
      ]]></findstoragenode>
      <mapallchecks><![CDATA[
        SELECT id, sid, noit as remote_cn, storage_node_id, fqdn, dsn
          FROM stratcon.map_uuid_to_sid LEFT JOIN stratcon.storage_node USING (storage_node_id)
      ]]></mapallchecks>
      <mapchecktostoragenode><![CDATA[
        SELECT o_storage_node_id as storage_node_id, o_sid as sid,
               o_fqdn as fqdn, o_dsn as dsn
          FROM stratcon.map_uuid_to_sid(\$1,\$2)
      ]]></mapchecktostoragenode>
      <check><![CDATA[
        INSERT INTO check_archive_%Y%m%d
                    (remote_address, whence, sid, id, target, module, name)
             VALUES (\$1, 'epoch'::timestamptz + (\$2 || ' seconds')::interval,
                     \$3, \$4, \$5, \$6, \$7)
      ]]></check>
      <status><![CDATA[
        INSERT INTO check_status_archive_%Y%m%d
                    (whence, sid, state, availability, duration, status)
             VALUES ('epoch'::timestamptz + (\$1 || ' seconds')::interval,
                     \$2, \$3, \$4, \$5, \$6)
      ]]></status>
      <metric_numeric><![CDATA[
        INSERT INTO metric_numeric_archive_%Y%m%d
                    (whence, sid, name, value)
             VALUES ('epoch'::timestamptz + (\$1 || ' seconds')::interval,
                     \$2, \$3, \$4)
      ]]></metric_numeric>
      <metric_text><![CDATA[
        INSERT INTO metric_text_archive_%Y%m%d
                    ( whence, sid, name,value)
             VALUES ('epoch'::timestamptz + (\$1 || ' seconds')::interval,
                     \$2, \$3, \$4)
      ]]></metric_text>
      <config><![CDATA[
        SELECT stratcon.update_config
               (\$1, \$2, \$3,
                'epoch'::timestamptz + (\$4 || ' seconds')::interval,
                \$5)
      ]]></config>
      <findconfig><![CDATA[
        SELECT config FROM stratcon.current_node_config WHERE remote_cn = \$1
      ]]></findconfig>
    </statements>
  </database>
};
}

sub make_stratcon_config {
  my $name = shift;
  my $options = shift;
  L("make_stratcon_config");
  $options->{cwd} ||= getcwd();
  L("make_stratcon_config in $options->{cwd}");
  $options->{generics} ||= { 'stomp_driver' => { image => 'stomp_driver' },
                             'postgres_ingestor' => { image => 'postgres_ingestor' } };
  $options->{rest_acls} ||= [ { type => 'deny', rules => [ { type => 'allow' } ] } ];
  $options->{iep}->{mq} ||= { 'stomp' => {} };
  my $cwd = $options->{cwd};
  my $file = "$cwd/logs/${name}_stratcon.conf";
  L("make_stratcon_config -> open($file)");
  open (my $o, ">$file") || BAIL_OUT("can't write config: $file");
  print $o qq{<?xml version="1.0" encoding="utf8" standalone="yes"?>\n};
  print $o qq{<stratcon id="8325581c-1068-11e1-ac63-db8546d81c8b" metric_period="1000">};
  make_eventer_config($o, $options);
  make_rest_acls($o, $options);
  make_stratcon_noits_config($o, $options);
  make_logs_config($o, $options);
  make_modules_config($o, $options);
  make_stratcon_listeners_config($o, $options);
  make_database_config($o, $options);
  make_iep_config($o, $options);
  print $o qq{</stratcon>\n};
  L("make_stratcon_config -> close($file)");
  close($o);
  return $file;
}

$SIG{CHLD} = sub {
  my $pid = wait;
  $noit_pid = 0 if($pid == $noit_pid);
  $stratcon_pid = 0 if($pid == $stratcon_pid);
};

sub start_noit {
  my $name = shift;
  my $options = shift;
  $options->{name} = $name;
  return 0 if $noit_pid;
  L("start_noit -> config");
  my $conf = make_noit_config($name, $options);
  $noit_pid = fork();
  L("noit_pid -> $noit_pid") if ($noit_pid);
  mkdir "logs";
  $noit_log = "logs/${name}_noit.log";
  if($noit_pid == 0) {
    L("in child");
    childL;
    $noit_pid = $$;
    $noit_log = "logs/${name}_noit.log";
    L("in child -> closing stdin");
    close(STDIN);
    L("in child -> opening stdin");
    open(STDIN, "</dev/null");
    L("in child -> closing stdout");
    close(STDOUT);
    L("in child -> opening stdout");
    open(STDOUT, ">/dev/null");
    L("in child -> closing stderr");
    close(STDERR);
    L("in child -> opening err $noit_log");
    open(STDERR, ">$noit_log");
    my @args = ( 'noitd', '-D', '-c', $conf );
    L("in child -> exec");
    exec { '../../src/noitd' } @args;
    exit(-1);
  }
  L("in parent -> noitd($noit_pid)");
  return $noit_pid;
}
sub get_noit_log {
  return IO::File->new("<$noit_log");
}
sub stop_noit {
  return 0 unless ($noit_pid && kill 0, $noit_pid);
  kill 9, $noit_pid;
  $noit_pid = 0;
  return 1;
}

sub start_stratcon {
  my $name = shift;
  my $options = shift;
  $options->{name} = $name;
  return 0 if $stratcon_pid;
  L("start_stratcon -> config");
  my $conf = make_stratcon_config($name, $options);
  L("start_stratcon -> config($conf)");
  $stratcon_pid = fork();
  L("stratcon_pid -> $stratcon_pid") if($stratcon_pid);
  mkdir "logs";
  $stratcon_log = "logs/${name}_stratcon.log";
  if($stratcon_pid == 0) {
    L("in child");
    childL;
    $stratcon_pid = $$;
    $stratcon_log = "logs/${name}_stratcon.log";
    close(STDIN);
    open(STDIN, "</dev/null");
    close(STDOUT);
    open(STDOUT, ">/dev/null");
    close(STDERR);
    open(STDERR, ">$stratcon_log");
    my @args = ( 'stratcond', '-D', '-c', $conf );
    L("in child -> exec");
    exec { '../../src/stratcond' } @args;
    exit(-1);
  }
  return $stratcon_pid;
}
sub get_stratcon_log {
  return IO::File->new("<$stratcon_log");
}
sub stop_stratcon {
  return 0 unless ($stratcon_pid && kill 0, $stratcon_pid);
  kill 9, $stratcon_pid;
  $stratcon_pid = 0;
  return 1;
}

END {
  kill 9, $noit_pid if($noit_pid && kill 1, $noit_pid);
  kill 9, $stratcon_pid if($stratcon_pid && kill 1, $stratcon_pid);
}
1;
