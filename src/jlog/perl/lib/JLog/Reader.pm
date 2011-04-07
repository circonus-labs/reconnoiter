package JLog::Reader;

require DynaLoader;

use JLog;
use strict;

use vars qw/@ISA/;

@ISA = qw(Exporter DynaLoader JLog);

1;
__END__

=head1 NAME

JLog::Reader - Perl extension for reading to a jlog journal.

=head1 SUMMARY

  use JLog::Reader;
  # create a new reader off the log directory
  $r = JLog::Reader->new($log);
  # open the log as the indicated subscriber
  $r->open($subscriber);
  while(my $line = $r->read) {
    # work with $line
  }
  # mark the seen records as read
  $r->checkpoint;

or

  use JLog::Reader;
  $r = JLog::Reader->new($log);
  $r->open($subscriber);
  # mark lines read as they are pulled off the queue
  $r->auto_checkpoint(1);
  while(my $line = $r->read) {
    # work with $line
  }


=head1 DESCRIPTION

JLog::Reader allows you to access a jlog queue for reader.

=head1 INTERFACE

=head2 Constructor

=head3 new

  $w = JLog::Reader->new( $path_to_jlog, [ $flags [, $size ] ] );

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

=head2 Reading From The Queue

=head3 open

  $w->open( $subscriber_name );

Opens the JLog for reading.

=over 4

=item $subscriber_name

The name we want to subscribe under.  This must previously have been
registered as a log subscriber via add_subscriber().

=back

=head3 read

  $message = $w->read;

Read the next message from the JLog queue.

=head3 checkpoint

  $r->checkpoint;

Checkpoint your read.  This will notify the JLog that you have successfully
read logs up to this point.  If all registered subscribers have read to
a certain point, the JLog system can remove the underlying data for the
read messages.

=head2 auto_checkpoint( [ $val ] )

Returns (and optionally sets) the auto_checkpoint property.  With 
auto-checkpointing enabled, JLog::Reader will automatically
checkpoint whenever you call read().

=over 4

=item $val

The value you wish to set auto_checkpointing to.

=back

=head2 Internals

=head3 alter_journal_size

  $r->alter_journal_size( $size );

Set the size of the individual journal files.

=over 4

=item $size

The desired size in bytes.

=back

=head3 raw_size

  $size = $r->raw_size;

The size of the existing journal (including checkpointed but unpurged messages
in the current journal file), in bytes.

=head rewind

  $r->rewind;

Rewind the jlog to the previous transaction id (when in an uncommitted state).  
This is useful for implementing a 'peek' style action.

=back


=head1 SEE ALSO

JLog
JLog::Writer

=head1 COPYRIGHT

Copyright (C) 2006-2008 by Message Systems, Inc.

=cut
