#!/usr/bin/perl -w
# Do regression tests.

use strict;
use Cwd;
use Getopt::Std;

our $opt_v;
getopts('v');

my $verbose=0;
if(defined($opt_v))
{ $verbose=1; }


# Grab our lists of tests to run
require "arr.pl";

my $stdir = getcwd();

# Top level is test programs we work with
foreach my $prog ( keys our %TESTS )
{
	print("-> $prog\n");
	if(!chdir("../$prog"))
	{
		print("***> cd failed!\n");
		next;
	}

	# For each program, keys are inputs
	my ($p,$f) = (0,0);
	foreach my $inp ( keys %{$TESTS{$prog}} )
	{
		if($verbose==1)
		{ print("   -> $inp\n"); }

		# Value is an array of attempts with that input
		foreach my $att_t ( @{$TESTS{$prog}->{$inp}} )
		{
			my %att = %$att_t;
			my $args = $att{'args'} || "";
			my $res = $att{'res'};

			# Run it
			my $ret = `../run.sh ./cidr_$prog -_ $args $inp`;
			chomp($ret);

			# See what we got
			$ret =~ /^'(.*)' -> '(.*)'$/;
			my $outp = $2;


			if($outp ne $res)
			{
				if($verbose==1)
				{
					print("      -> *FAILED* ($args) "
				    	. "(Got '$outp', wanted '$res')\n"),
				}
				else
				{
					print("   => FAIL $inp ($args) \n"
					    . "      Got '$outp', expecting '$res'.\n");
				}
				$f++;
			}
			else
			{
				if($verbose==1)
				{ print("      -> ok ($args) \n", ); }
				$p++;
			}
		}
	}

	print("<- $p pass, $f fail\n");
}
