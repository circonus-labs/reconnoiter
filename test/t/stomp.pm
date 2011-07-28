package stomp;
use strict;
use Net::Stomp;

sub new {
    my $class = shift;
    my $port = shift || 61613;
    my $stomp = Net::Stomp->new( { hostname => 'localhost', port => '61613'} );
    $stomp->connect( { login => 'guest', passcode => 'guest' } );
    return bless { stomp => $stomp }, $class;
}
sub subscribe {
    my $self = shift;
    my $topic = shift;
    $self->{stomp}->subscribe(
        {   destination             => $topic,
            'ack'                   => 'client',
            'activemq.prefetchSize' => 1
        }
    );
}
sub unsubscribe {
    my $self = shift;
    my $topic = shift;
    $self->{stomp}->unsubscribe( { destination => $topic });
}
sub get {
    my $self = shift;
    my $opts = shift || {};
    my $payload;
    alarm $opts->{timeout} if $opts->{timeout};
    eval {
        local $SIG{ALRM} = sub { die; };
        delete $opts->{timeout};
        my $frame = $self->{stomp}->receive_frame();
        if ($frame) {
            $payload = $frame->body;
            $self->{stomp}->ack( { frame => $frame } );
        }
        alarm 0;
    };
    return $payload;
}
sub disconnect {
    my $self = shift;
    eval { $self->{stomp}->disconnect(); } if $self->{stomp};
    delete $self->{stomp};
}
sub DESTROY {
    my $self = shift;
    $self->disconnect();
}
1;
