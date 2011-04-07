package JLog::Writer;

require DynaLoader;

use JLog;
use strict;

use vars qw/@ISA/;

@ISA = qw(Exporter DynaLoader JLog);

1;
__END__

=head1 NAME

JLog::Writer - Perl extension for writing to a jlog journal.

=head1 SUMMARY

  use JLog::Writer;
  use Fcntl qw/:DEFAULT/;
  
  my $sub = "testsubscriber";
  my $log = "foo.jlog";
  
  # open a log - this respects stander Fcntl flags
  my $w = JLog::Writer->new($log, O_CREAT);
  
  # add a subscriber - without this there is danger that
  # a log may be expired without the unnamed subscriber
  # stating its intention to read.
  $w->add_subscriber($sub);
  
  # open for writing
  $w->open;
  
  foreach (1 ... 3) {
    # write to the queue
    $w->write("foo $_");
  }
  # close the queue
  $w->close;

=head1 DESCRIPTION

JLog::Writer allows you to access a jlog queue for writing.

=head1 INTERFACE

=head2 Constructor

=head3 new

  $w = JLog::Writer->new( $path_to_jlog, [ $flags [, $size ] ] );

Instantiates a JLog writer object associated with the JLog directory.

=over 4 

=item $path_to_jlog

The directory for the JLog queue.

=item $flags

Optional flags, from 'Fcntl'.  The default is O_CREAT.

=item $size

Optional size of the individual journal files.

=back

=head2 Subscriber Management

These functions are inherited from JLog

=head3 add_subscriber

  $w->add_subscriber( $name, [ $flag ] );

Add a subscriber to the JLog queue. 

=over 4

=item $name

The name of the subscriber.

=item $flag

An optional flag dictating where the subscriber should be marked interested
from.  The default is JLog::JLOG_BEGIN.  The other available option is
JLog::JLOG_END.

=back

=head3 remove_subscriber

  $w->remove_subscriber ( $name );

Remove a subscriber to the JLog queue.

=over 4

=item $name

The name of the subscriber.

=back

=head2 Writing to the Queue

=head3 open

  $w->open();

Opens the JLog for writing.

=head3 write

  $w->write( $message [, $ts ] );

Write a message to the JLog.

=over 4

=item $message

The message to write.

=item $ts 

The timestamp (in epoch seconds) to set the record timestamp to.  The default is time().

=back

=head2 Internals

=head3 alter_journal_size

  $w->alter_journal_size( $size );

Set the size of the individual journal files.

=over 4

=item $size

The desired size in bytes.

=back

=head3 raw_size

  $size = $w->raw_size;

The size of the existing journal (including checkpointed but unpurged messages
in the current journal file), in bytes.

=head1 SEE ALSO

JLog
JLog::Reader

=head1 COPYRIGHT AND LICENSE

Copyright (C) 2006-2008 by Message Systems, Inc.

=cut

