#!/usr/bin/perl
use strict;
use warnings;
use Fcntl qw/SEEK_CUR SEEK_SET O_RDONLY O_WRONLY/;
sub systell { sysseek($_[0], 0, SEEK_CUR) }

my $le = 1;
my $jlog = shift;
my $jlog_endian = shift;
if (defined $jlog_endian) {
  if ($jlog_endian =~ /^(little|le)$/) {
    $le = 1;
  } elsif ($jlog_endian =~ /^(big|be)$/) {
    $le = 0;
  }
}

if (!defined $le or !defined $jlog) {
  print "Usage: $0 <path to jlog> <jlog endianness (be or le)>\n";
  exit 1;
}

sub unpack_32 {
  if ($le) {
    return unpack('V1', $_[0]);
  } else {
    return unpack('N1', $_[0]);
  }
}
# we have to use 2 numbers to represent a 64-bit value so this works on 32-bit
sub unpack_64 {
  if ($le) {
    return unpack('V1', substr($_[0], 4, 4)), unpack('V1', substr($_[0], 0, 4));
  } else {
    return unpack('N1', substr($_[0], 0, 4)), unpack('N1', substr($_[0], 4, 4));
  }
}

opendir(DIR, $jlog) or die "could not opendir $jlog: $!";
my $files = [ readdir(DIR) ];
closedir DIR;

my $metastore = grep /^metastore$/, @$files;
$files = [ grep !/^metastore$/, @$files ];
my $checkpoints = [ grep /^cp[.]/, @$files ];
$files = [ grep !/^cp[.]/, @$files ];
my $segments = [ sort { hex $a <=> hex $b } grep /^[0-9A-Fa-f]{8}$/, @$files ];
$files = [ grep !/^[0-9A-Fa-f]{8}$/, @$files ];
my $indexes = [ grep /^[0-9A-Fa-f]{8}.idx$/, @$files ];
$files = [ grep !/^[0-9A-Fa-f]{8}.idx$/, @$files ];

if (!$metastore) {
  die "no metastore found\n";
}

$files = [ grep !/^[.]{1,2}$/, @$files ];
foreach (@$files) {
  print "unexpected file found: $_ (skipping)\n";
}
undef $files;

if ((stat "$jlog/metastore")[7] != 16) {
  die "metastore has invalid size\n";
}
my ($current_segment, $unit_limit, $safety, $hdr_magic);
sysopen(META, "$jlog/metastore", O_RDONLY)
  or die "could not sysopen $jlog/metastore: $!";
my $data;
sysread(META, $data, 4) == 4 or die "metastore read error: $!";
$current_segment = unpack_32($data);
sysread(META, $data, 4) == 4 or die "metastore read error: $!";
$unit_limit = unpack_32($data);
sysread(META, $data, 4) == 4 or die "metastore read error: $!";
$safety = unpack_32($data);
sysread(META, $data, 4) == 4 or die "metastore read error: $!";
$hdr_magic = unpack_32($data);
close META;

my $oldest_cp_segment = 0xffffffff;
my $cpbyname = {};
foreach my $cp (@$checkpoints) {
  my $cpname = $cp;
  $cpname =~ s/^cp[.]//;
  $cpname = pack('H*', $cpname);
  if ((stat "$jlog/$cp")[7] != 8) {
    print "checkpoint $cpname has invalid size\n";
    next;
  }
  sysopen(CP, "$jlog/$cp", O_RDONLY) or die "could not sysopen $jlog/$cp: $!";
  sysread(CP, $data, 4) == 4 or die "checkpoint $cpname: read error: $!";
  my $segment = unpack_32($data);
  sysread(CP, $data, 4) == 4 or die "checkpoint $cpname: read error: $!";
  my $marker = unpack_32($data);
  close CP;
  if ($segment > $current_segment) {
    print "checkpoint $cpname points to segment newer than current segment\n";
    next;
  }
  $oldest_cp_segment = $segment if ($segment < $oldest_cp_segment);
  $segment = sprintf "%08x", $segment;
  $cpbyname->{$cpname} = {
    segment => $segment,
    marker => $marker,
  };
}
if (!scalar keys %$cpbyname) {
  warn "no valid checkpoints\n";
}

my $lastnum = $oldest_cp_segment;
foreach my $seg (@$segments) {
  my $num = hex $seg;
  if ($num < $oldest_cp_segment) {
    print "segment $seg is older than any checkpoint\n";
  }
  if ($num > $current_segment) {
    print "segment $seg is newer than the current segment\n";
  }
  if ($num > $lastnum + 1) {
    if ($num > $lastnum + 2) {
      printf "segments %08x though %08x missing\n", $lastnum + 1, $num - 1;
    } else {
      printf "segment %08x missing\n", $lastnum + 1;
    }
  }
  if ($num > $lastnum) {
    $lastnum = $num;
  }
  my ($idx) = grep /$seg[.]idx/i, @$indexes;
  $indexes = [ grep !/$seg[.]idx/i, @$indexes ];
  $seg = [ $seg, $idx ];
}
if ($current_segment > $lastnum + 1) {
  if ($current_segment > $lastnum + 2) {
    printf "segments %08x though %08x missing\n",
      $lastnum + 1, $current_segment - 1;
  } else {
    printf "segment %08x missing\n", $lastnum + 1;
  }
}

foreach my $idx (@$indexes) {
  print "index $idx doesn't correspond to any segment (skipping)\n";
}

foreach my $segdata (@$segments) {
  my ($seg, $idx) = @$segdata;
  my $last_marker = 0;
  my $last_tv_sec = 0;
  my $last_tv_usec = 0;
  my $warned_timewarp = 0;
  my $warned_toobig = 0;
  my $data_off = 0;
  my $idx_off = 0;
  sysopen(SEG, "$jlog/$seg", O_RDONLY) or die "could not sysopen $jlog/$seg: $!";
  my $data_len = (stat SEG)[7];
  my $idx_len;
  if ($idx) {
    sysopen(IDX, "$jlog/$idx", O_RDONLY) or die "could not sysopen $jlog/$idx: $!";
    $idx_len = (stat IDX)[7];
  }

  my $nrec = 0;
  my @fixers = ();
  while ($data_off < $data_len) {
    if (!$warned_toobig and ($data_off > $unit_limit)) {
      print "segment $seg has message offset larger than unit limit\n";
      $warned_toobig = 1;
    }
    if ($data_off + 16 > $data_len) {
      print "segment $seg offset $data_off: not enough room for message header\n";
      last;
    }
    my $offset = systell(*SEG);
    sysread(SEG, $data, 16) == 16
      or die "segment $seg offset $data_off: read error: $!";
    my $reserved = unpack_32(substr $data, 0, 4);
    my $tv_sec = unpack_32(substr $data, 4, 4);
    my $tv_usec = unpack_32(substr $data, 8, 4);
    my $mlen = unpack_32(substr $data, 12, 4);
    if ($reserved != $hdr_magic) {
      printf "segment $seg offset $data_off: reserved field (%08x != %08x)\n",
             $reserved, $hdr_magic;
      push @fixers, $offset if ($reserved == 0);
    }
    if (!$warned_timewarp) {
      if ($tv_sec < $last_tv_sec or
          ($tv_sec == $last_tv_sec and $tv_usec < $last_tv_usec))
      {
        print "segment $seg offset $data_off: time goes backwards\n";
        $warned_timewarp = 1;
      } else {
        $last_tv_sec = $tv_sec;
        $last_tv_usec = $tv_usec;
      }
    }
    if ($data_off + $mlen > $data_len) {
      print "segment $seg offset $data_off: not enough room for message body\n";
      last;
    }
    sysread(SEG, $data, $mlen) == $mlen
      or die "segment $seg offset $data_off + 16: read error: $!";
    $last_marker++;
    $nrec++;
    if ($idx) {
      if ($idx_off == $idx_len) {
        if ($current_segment > hex $seg) {
          print "index $idx is incomplete (not an error)\n";
        }
        close IDX;
        undef $idx;
      } elsif ($idx_off + 8 > $idx_len) {
        print "index $idx offset $idx_off: no room for next offset\n";
        close IDX;
        undef $idx;
      } else {
        sysread(IDX, $data, 8) == 8
          or die "index $idx offset $idx_off: read error: $!";
        my ($offh, $offl) = unpack_64($data);
        if ($offh != 0 or $offl != $data_off) {
          print "index $idx offset $idx_off: index points to wrong offset\n";
          close IDX;
          undef $idx;
        } else {
          $idx_off += 8;
        }
      }
    }

    $data_off += 16 + $mlen;
  }
  if ($data_off == $data_len) {
    $segdata->[2] = $last_marker;
    foreach my $offset (@fixers) {
      printf "writing new hdr_magic at off: %d\n", $offset;
      sysopen(FIX, "$jlog/$seg", O_WRONLY) or die "could not sysopen $jlog/$seg: $!";
      sysseek(FIX, $offset, SEEK_SET);
      my $hdr_blob = pack('V1', $hdr_magic);
      die unless length($hdr_blob) == 4;
      syswrite(FIX, pack('V1', $hdr_magic), 4);
      close FIX;
    }
  } else {
    close IDX;
    undef $idx;
  }

  close SEG;
  if ($idx) {
    if ($idx_off == $idx_len) {
      if (hex $seg < $current_segment) {
        print "index $idx not current or closed\n";
      }
    } elsif ($idx_off + 8 > $idx_len) {
      print "index $idx offset $idx_off: no room for closing index\n";
    } elsif ($idx_off + 8 < $idx_len) {
      print "index $idx offset $idx_off: index too long\n";
    } else {
      sysread(IDX, $data, 8) == 8
        or die "index $idx offset $idx_off: read error: $!";
      my ($offh, $offl) = unpack_64($data);
      if ($offh != 0 or $offl != 0) {
        print "index $idx offset $idx_off: closing offset not 0\n";
      }
    }
    close IDX;
  }
  printf "segment $seg has %d records\n", $nrec;
}

foreach my $cp (keys %$cpbyname) {
  if ($cpbyname->{$cp}{segment} ne '00000000' or
      $cpbyname->{$cp}{marker} != 0)
  {
    my ($segdata) = grep { $_->[0] eq $cpbyname->{$cp}{segment} } @$segments;
    if (!defined $segdata) {
      if (hex $cpbyname->{$cp}{segment} != $current_segment) {
        printf "checkpoint $cp points to nonexistent segment\n";
      }
    } elsif (!defined $segdata->[2]) {
      print "checkpoint $cp points to a damaged segment\n";
    } elsif ($cpbyname->{$cp}{marker} > $segdata->[2]) {
      print "checkpoint $cp points past the end of segment\n";
    }
  }
}
