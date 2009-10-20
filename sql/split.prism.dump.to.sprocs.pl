#!/usr/bin/perl -w
use strict;
use Carp;
use autodie;

=head1 Running

To run this program simply call it like this:

    pg_dump -s -n prism -U .... -h ... -p ... database_name | ./split.prism.dump.to.sprocs.pl

And that's all. If there will be problems - it will print them, and stop processing output.

=head1 Potential problems

If you'll get error like:

    Can't locate autodie.pm in @INC (@INC contains: /etc/perl /usr/local/lib/perl/5.8.8 /usr/local/share/perl/5.8.8 /usr/lib/perl5 /usr/share/perl5 /usr/lib/perl/5.8 /usr/share/perl/5.8 /usr/local/lib/site_perl .) at ./split.prism.dump.to.sprocs.pl line 4.
    BEGIN failed--compilation aborted at ./split.prism.dump.to.sprocs.pl line 4.

Change 4th line of this script from:

    use autodie;

to:

    use Fatal qw( open );

or install autodie Perl module.

=cut

my %buffers = ();

my $content = do { local $/; <STDIN> };

for my $function ( $content =~ m{ ^ ( CREATE \s+ FUNCTION \s+ .*? \s+ LANGUAGE \s+ [^;]* ; [^\S\n]* \n ) }xmsg ) {

    croak "Can't find function name in:\n$function\n" unless
        $function =~ s/\ACREATE FUNCTION ([^\s\(]+)/CREATE OR REPLACE FUNCTION prism.$1/;

    my $function_name = $1;
    push @{ $buffers{ $function_name } }, $function;
}

for my $alter_statement ( $content =~ m{ ^ ( ALTER \s+ FUNCTION \s+ prism [^\n]* \n ) }xmsg ) {
    croak "Can't find function name in:\n$alter_statement\n" unless
        $alter_statement =~ m{^ALTER \s+ FUNCTION \s+ prism \. ( [^\s\(]+ )}xms;

    my $function_name = $1;
    push @{ $buffers{ $function_name } }, $alter_statement;
}

for my $privileges_statement ( $content =~ m{ ^ ( (?: GRANT | REVOKE ) \s+ [^\n]+ \s+ ON \s+ FUNCTION \s+ [^\n]* \n ) }xmsg ) {
    croak "Can't find function name in:\n$privileges_statement\n" unless
        $privileges_statement =~ m{^(?: GRANT | REVOKE ) \s+ [^\n]+ \s+ ON \s+ FUNCTION \s+ ( [^\s\(]+ )}xms;

    my $function_name = $1;
    push @{ $buffers{ $function_name } }, $privileges_statement;
}

while ( my ( $function_name, $buffer ) = each %buffers ) {

    my $fullname = "sprocs/prism.$function_name.sql";
    $fullname =~ s/"//g;    # remove quoting of function names

    open my $fh, '>', $fullname;
    print $fh join( "\n\n", @{ $buffer } );
    close $fh;
}

exit;
