package apiclient;

use WWW::Curl::Easy;

sub new {
    my $class = shift;
    my $host = shift;
    my $port = shift;
    my $curl = WWW::Curl::Easy->new();
    $curl->setopt(CURLOPT_SSL_VERIFYPEER, 0);
    $curl->setopt(CURLOPT_CAINFO, '../test-ca.crt');
    $curl->setopt(CURLOPT_SSLKEY, '../client.key');
    $curl->setopt(CURLOPT_SSLCERT, '../client.crt');
    $curl->setopt(CURLOPT_TIMEOUT, 35);
    return bless { curl => $curl, host => $host, port => $port }, $class;
}
sub do {
    my $self = shift;
    my $method = shift;
    my $uri = shift;
    if (@_) {
        my $payload = shift;
        $self->{curl}->setopt(CURLOPT_UPLOAD, 1);
        open (my $fh, "<", \$payload);
        $self->{curl}->setopt(CURLOPT_INFILE, $fh);
        $self->{curl}->setopt(CURLOPT_INFILESIZE, length($payload));
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
    die $self->{curl}->strerror($retcode);
}
sub get { shift->do('GET', @_); }
sub post { shift->do('POST', @_); }
sub put { shift->do('PUT', @_); }
sub delete { shift->do('DELETE', @_); }

1;
