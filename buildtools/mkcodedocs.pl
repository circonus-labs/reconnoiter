#!/usr/bin/perl

use strict;
use File::Find;
my $inbase = shift || die;
my $outbase = shift || die;

sub xlate {
  my $in = shift;
  my $out = shift;
  open(F, "<$in") || die " <<< $in ";
  $/ = undef;
  my $a = <F>;
  close(F);
  unlink($out) if(-f $out);
  if($a =~ /\/\*\#\*\s*DOCBOOK(.+)\*\//sm) {
    my $b = $1;
    $b =~ s/^\s+(?:\#\s*)?\*//gm;
    print "Appending to $out\n";
    open(F, ">>$out");
    print F $b;
    close(F);
  }
}


finddepth({
  wanted => sub {
    (my $file = $File::Find::name) =~ s/^$inbase\///;
    return if($file !~ /\.(?:c|h|lua|pl|java)$/);
    (my $out = $file) =~ s/(\/|\.)/_/g;
    xlate("$inbase/$file", "$outbase/$out.xml");
  },
  no_chdir => 1
}, $inbase);
