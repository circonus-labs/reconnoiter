package testconfig;
use DBI;
use Cwd;
use Exporter 'import';
use Data::Dumper;

my $noit_pid = 0;
my $stratcon_pid = 0;


@EXPORT = qw($NOIT_TEST_DB $NOIT_TEST_DB_PORT
             $NOIT_API_PORT $NOIT_CLI_PORT
             pg make_noit_config start_noit stop_noit
             $MODULES_DIR $LUA_DIR $all_noit_modules);

our $all_noit_modules = {
  'selfcheck' => { 'image' => 'selfcheck' },
  'ping_icmp' => { 'image' => 'ping_icmp' },
  'snmp' => { 'image' => 'snmp' },
  'ssh2' => { 'image' => 'ssh2' },
  'mysql' => { 'image' => 'mysql' },
  'postgres' => { 'image' => 'postgres' },
  'varnish' => { 'loader' => 'lua', 'object' => 'noit.module.varnish' },
  'http' => { 'loader' => 'lua', 'object' => 'noit.module.http' },
  'resmon' => { 'loader' => 'lua', 'object' => 'noit.module.resmon' },
  'smtp' => { 'loader' => 'lua', 'object' => 'noit.module.smtp' },
  'tcp' => { 'loader' => 'lua', 'object' => 'noit.module.tcp' },
};

our $NOIT_TEST_DB = "/tmp/noit-test-db";
our $NOIT_TEST_DB_PORT = 23816;
our $NOIT_API_PORT = 42364;
our $NOIT_CLI_PORT = 42365;

our ($MODULES_DIR, $LUA_DIR);

sub pg {
  my $db = shift || 'postgres';
  my $user = shift || $ENV{USER};
  return DBI->connect(
    "dbi:Pg:host=localhost;port=$NOIT_TEST_DB_PORT;database=$db", $user, ''
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
    $opts->{logs_debug}->{$_} ||= 'false';
  }
  
  print $o qq{
  <logs>
    <console_output>
      <outlet name="stderr"/>
      <log name="error" disabled="$opts->{logs_error}->{''}"/>
      <log name="debug" disabled="$opts->{logs_debug}->{''}"/>
    </console_output>
    <feeds>
      <log name="feed" type="jlog" path="$cwd/logs/$opts->{name}.feed(stratcon)"/>
    </feeds>
    <components>
};
  make_log_section($o, 'error', $opts->{logs_error});
  make_log_section($o, 'debug', $opts->{logs_debug});
  print $o qq{
    </components>
    <feeds>
      <outlet name="feed"/>
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
sub make_listeners_config {
  my ($o, $opts) = @_;
  my $cwd = $opts->{cwd};
  $options->{noit_api_port} ||= $NOIT_API_PORT;
  $options->{noit_cli_port} ||= $NOIT_CLI_PORT;
  print $o qq{
  <listeners>
    <sslconfig>
      <optional_no_ca>false</optional_no_ca>
      <certificate_file>$cwd/../test-noit.crt</certificate_file>
      <key_file>$cwd/../test-noit.key</key_file>
      <ca_chain>$cwd/../test-ca.crt</ca_chain>
    </sslconfig>
    <consoles type="noit_console">
      <listener address="*" port="$options->{noit_cli_port}">
        <config>
          <line_protocol>telnet</line_protocol>
        </config>
      </listener>
    </consoles>
    <listener type="control_dispatch" address="*" port="$options->{noit_api_port}" ssl="on">
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
  print $o qq{  <checks max_initial_stutter="1000" filterset="default">\n};
  do_check_print($o, $options->{checks});
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
  $options->{cwd} ||= cwd();
  my $cwd = $options->{cwd};
  my $file = "$cwd/logs/${name}_noit.conf";
  open (my $o, ">$file");
  print $o qq{<?xml version="1.0" encoding="utf8" standalone="yes"?>\n};
  print $o qq{<noit>};
  make_eventer_config($o, $options);
  make_logs_config($o, $options);
  make_modules_config($o, $options);
  make_listeners_config($o, $options);
  make_checks_config($o, $options);
  make_filtersets_config($o, $options);
  print $o qq{</noit>\n};
  close($o);
  return $file;
}

sub start_noit {
  my $name = shift;
  my $options = shift;
  $options->{name} = $name;
  return 0 if $noit_pid;
  my $conf = make_noit_config($name, $options);
  $noit_pid = fork();
  mkdir "logs";
  if($noit_pid == 0) {
    close(STDIN);
    open(STDIN, "</dev/null");
    close(STDOUT);
    open(STDOUT, ">/dev/null");
    close(STDERR);
    open(STDERR, ">logs/${name}_noit.log");
    my @args = ( 'noitd', '-D', '-c', $conf );
    exec { '../../src/noitd' } @args;
    exit(-1);
  }
  return $noit_pid;
}
sub stop_noit {
  kill 9, $noit_pid if($noit_pid && kill 1, $noit_pid);
  $noit_pid = 0;
  return 1;
}

END {
  kill 9, $noit_pid if($noit_pid && kill 1, $noit_pid);
  kill 9, $stratcon_pid if($stratcon_pid && kill 1, $stratcon_pid);
}
1;
