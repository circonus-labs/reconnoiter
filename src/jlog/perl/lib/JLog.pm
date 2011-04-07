package JLog;

use 5.008005;
use strict;
use warnings;

require Exporter;

our @ISA = qw(Exporter);

# Items to export into callers namespace by default. Note: do not export
# names by default without a very good reason. Use EXPORT_OK instead.
# Do not simply export all your public functions/methods/constants.

# This allows declaration	use JLog ':all';
# If you do not need this, moving things directly into @EXPORT or @EXPORT_OK
# will save memory.
our %EXPORT_TAGS = ( 'all' => [ qw(
	
) ] );

our @EXPORT_OK = ( @{ $EXPORT_TAGS{'all'} } );

our @EXPORT = qw(
	
);

our $VERSION = '1.0';

require XSLoader;
XSLoader::load('JLog', $VERSION);

# Preloaded methods go here.

1;
__END__

=head1 NAME

JLog - Perl extension for the jlog journaled queueing system

=head1 DESCRIPTION

Parent class for JLog::Reader and JLog::Writer.  You probably want to
be looking at those instead.  JLog is a durable, reliable, 
publish-and-subscribe queueing system. 

=head1 INTERFACE

=head2 Subscriber Management

A JLog must have subscribers to be functional.  Without a subscriber,
a queue may be purged, as there are no interested readers.  For this
reason it is highly recommended that you add a subscriber before
writing to a log.

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

=head3 list_subscribers

  @subscribers = $w->list_subscribers;

Return a list of all the subscribers to a JLog queue.

=head2 Internals

=head3 alter_journal_size

This function is a stub provided for backwards compatibility. It will always
return false.

=over 4

=item $size

The desired size in bytes.

=back

=head3 raw_size

  $size = $w->raw_size;

The size of the existing journal (including checkpointed but unpurged messages
in the current journal file), in bytes.

=head1 SEE ALSO

JLog::Reader
JLog::Writer

=head1 COPYRIGHT AND LICENSE

Copyright (C) 2006-2008 by Message Systems, Inc.

=cut
