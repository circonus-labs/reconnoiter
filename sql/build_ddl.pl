#!/usr/bin/perl

sub sql {
  open(my $f, "<".shift);
  while(<$f>) { /\\i (\S+)/ ? sql($1) : print; }
  close($f);
}

sql(shift);
