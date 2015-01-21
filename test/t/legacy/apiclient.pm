package apiclient;

use WWW::Curl::Easy;

sub new {
    my $class = shift;
    my $host = shift;
    my $port = shift;
    my $cn = shift;
    my $options = {
        'cainfo' => '../test-ca.crt',
        'key' => '../client.key',
        'cert' => '../client.crt',
    };
    my $ext = shift || {};
    while(my ($k,$v) = each %$ext) {
      $options->{$k} = $v;
    }
    my $curl = WWW::Curl::Easy->new();
    my $version_str = $curl->version();
    my $version = 100000;
    if($version_str =~ /libcurl\/(\d+)\.(\d+)\.(\d+)/) {
      $version = 1000*$1 + $2 + $3/1000;
    }
    $curl->setopt(CURLOPT_SSL_VERIFYPEER, 0);
    if($cn && $WWW::Curl::Easy::VERSION > 4.15 && $version > 7032) {
      no strict 'subs'; # for older WWW::Curl::Easy
      my $map = $cn.":$port:$host";
      $host = $cn;
      $curl->setopt(CURLOPT_RESOLVE, [$map]);
      $curl->setopt(CURLOPT_SSL_VERIFYHOST, 2);
    }
    elsif($version > 7032) {
      $curl->setopt(CURLOPT_SSL_VERIFYHOST, 0);
    }
    else {
      $curl->setopt(CURLOPT_SSL_VERIFYHOST, 1);
    }
    $curl->setopt(CURLOPT_CAINFO, $options->{cainfo});
    $curl->setopt(CURLOPT_SSLKEY, $options->{key});
    $curl->setopt(CURLOPT_SSLCERT, $options->{cert});
    $curl->setopt(CURLOPT_TIMEOUT, 35);
    return bless { curl => $curl, host => $host, port => $port }, $class;
}
sub do {
    my $self = shift;
    my $method = shift;
    my $uri = shift || '/';
    if (@_) {
        my $payload = shift;
        $self->{curl}->setopt(CURLOPT_UPLOAD, 1);
        open (my $fh, "<", \$payload);
        $self->{curl}->setopt(CURLOPT_INFILE, $fh);
        $self->{curl}->setopt(CURLOPT_INFILESIZE, length($payload));
    }
    else {
        $self->{curl}->setopt(CURLOPT_UPLOAD, 0);
    }
    $self->{curl}->setopt(CURLOPT_CUSTOMREQUEST, $method);
    $self->{curl}->setopt(CURLOPT_URL, "https://$self->{host}:$self->{port}$uri");
    my $response_body;

    open (my $fileb, ">", \$response_body);
    $self->{curl}->setopt(CURLOPT_WRITEDATA, $fileb);

    my $retcode = $self->{curl}->perform();
    my $response_code = $self->{curl}->getinfo(CURLINFO_HTTP_CODE);
    if ($retcode == 0) {
        return ($response_code, $response_body);
    }
    return ($response_code, $self->{curl}->strerror($retcode));
}
sub capabilities { shift->do('CAPA', @_); }
sub get { shift->do('GET', @_); }
sub post { shift->do('POST', @_); }
sub put { shift->do('PUT', @_); }
sub delete { shift->do('DELETE', @_); }

1;
