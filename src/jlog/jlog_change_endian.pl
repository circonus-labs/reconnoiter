#!/opt/msys/3rdParty/bin/perl
use strict;
use warnings;
use Fcntl;
use File::Path;

my $src = shift;

my $fromle = undef;
my $jlog_endian = shift;
if (defined $jlog_endian) {
  if ($jlog_endian =~ /^(tolittle|tole)$/) {
    $fromle = 0;
  } elsif ($jlog_endian =~ /^(tobig|tobe)$/) {
    $fromle = 1;
  }
}

my $dst = shift;

if (!defined $src or !defined $fromle or !defined $dst) {
  print "Usage: $0 <source jlog> <endianness (tobe or tole)> <destination jlog>\n";
  exit 1;
}

sub unpack_32 {
  if ($fromle) {
    return unpack('V1', $_[0]);
  } else {
    return unpack('N1', $_[0]);
  }
}
sub repack_32 {
  if ($fromle) {
    return pack('N1', $_[0]);
  } else {
    return pack('V1', $_[0]);
  }
}
# we have to use 2 numbers to represent a 64-bit value so this works on 32-bit
sub unpack_64 {
  if ($fromle) {
    return unpack('V1', substr($_[0], 4, 4)), unpack('V1', substr($_[0], 0, 4));
  } else {
    return unpack('N1', substr($_[0], 0, 4)), unpack('N1', substr($_[0], 4, 4));
  }
}
sub repack_64 {
  if ($fromle) {
    return pack('N2', $_[0], $_[1]);
  } else {
    return pack('V2', $_[1], $_[0]);
  }
}

mkdir($dst) or die "could not mkdir $dst: $!";
$SIG{__DIE__} = sub { rmtree $dst; };

opendir(DIR, $src) or die "could not opendir $src: $!";
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

if ((stat "$src/metastore")[7] != 12) {
  die "metastore has invalid size\n";
}
my ($current_segment, $unit_limit, $safety);
sysopen(META, "$src/metastore", O_RDONLY)
  or die "could not sysopen $src/metastore: $!";
my $data;
sysread(META, $data, 4) == 4 or die "metastore read error: $!";
$current_segment = unpack_32($data);
sysread(META, $data, 4) == 4 or die "metastore read error: $!";
$unit_limit = unpack_32($data);
sysread(META, $data, 4) == 4 or die "metastore read error: $!";
$safety = unpack_32($data);
close META;

sysopen(META, "$dst/metastore", O_WRONLY|O_CREAT)
  or die "could not sysopen $dst/metastore: $!";
$data = repack_32($current_segment);
syswrite(META, $data, 4) == 4 or die "metastore write error: $!";
$data = repack_32($unit_limit);
syswrite(META, $data, 4) == 4 or die "metastore write error: $!";
$data = repack_32($safety);
syswrite(META, $data, 4) == 4 or die "metastore write error: $!";
close META;

if (!scalar @$checkpoints) {
  die "no checkpoints\n";
}
my $oldest_cp_segment = 0xffffffff;
my $cpbyname = {};
foreach my $cp (@$checkpoints) {
  my $cpname = $cp;
  $cpname =~ s/^cp[.]//;
  $cpname = pack('H*', $cpname);
  if ((stat "$src/$cp")[7] != 8) {
    die "checkpoint $cpname has invalid size\n";
  }
  sysopen(CP, "$src/$cp", O_RDONLY) or die "could not sysopen $src/$cp: $!";
  sysread(CP, $data, 4) == 4 or die "checkpoint $cpname: read error: $!";
  my $segment = unpack_32($data);
  sysread(CP, $data, 4) == 4 or die "checkpoint $cpname: read error: $!";
  my $marker = unpack_32($data);
  close CP;
  if ($segment > $current_segment) {
    die "checkpoint $cpname points to segment newer than current segment\n";
  }
  sysopen(CP, "$dst/$cp", O_WRONLY|O_CREAT) or die "could not sysopen $dst/$cp: $!";
  $data = repack_32($segment);
  syswrite(CP, $data, 4) == 4 or die "checkpoint $cpname: write error: $!";
  $data = repack_32($marker);
  syswrite(CP, $data, 4) == 4 or die "checkpoint $cpname: write error: $!";
  close CP;
  $oldest_cp_segment = $segment if ($segment < $oldest_cp_segment);
  $segment = sprintf "%08x", $segment;
  $cpbyname->{$cpname} = {
    segment => $segment,
    marker => $marker,
  };
}

my $lastnum = $oldest_cp_segment;
foreach my $seg (@$segments) {
  my $num = hex $seg;
  if ($num < $oldest_cp_segment) {
    die "segment $seg is older than any checkpoint\n";
  }
  if ($num > $current_segment) {
    die "segment $seg is newer than the current segment\n";
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
  my $warned_toobig = 0;
  my $data_off = 0;
  my $idx_off = 0;
  sysopen(SEG, "$src/$seg", O_RDONLY) or die "could not sysopen $src/$seg: $!";
  my $data_len = (stat SEG)[7];
  sysopen(WSEG, "$dst/$seg", O_WRONLY|O_CREAT) or die "could not sysopen $dst/$seg: $!";
  my $idx_len;
  if ($idx) {
    sysopen(IDX, "$src/$idx", O_RDONLY) or die "could not sysopen $src/$idx: $!";
    $idx_len = (stat IDX)[7];
    sysopen(WIDX, "$dst/$idx", O_WRONLY|O_CREAT) or die "could not sysopen $dst/$idx: $!";
  }

  while ($data_off < $data_len) {
    if (!$warned_toobig and ($data_off > $unit_limit)) {
      print "segment $seg has message offset larger than unit limit\n";
      $warned_toobig = 1;
    }
    if ($data_off + 16 > $data_len) {
      die "segment $seg offset $data_off: not enough room for message header\n";
    }
    sysread(SEG, $data, 16) == 16
      or die "segment $seg offset $data_off: read error: $!";
    my $reserved = unpack_32(substr $data, 0, 4);
    my $tv_sec = unpack_32(substr $data, 4, 4);
    my $tv_usec = unpack_32(substr $data, 8, 4);
    my $mlen = unpack_32(substr $data, 12, 4);
    if ($reserved) {
      die "segment $seg offset $data_off: reserved field not 0\n";
    }
    if ($data_off + $mlen > $data_len) {
      die "segment $seg offset $data_off: not enough room for message body\n";
    }
    $data = repack_32($reserved) . repack_32($tv_sec) .
            repack_32($tv_usec) . repack_32($mlen);
    syswrite(WSEG, $data, 16) == 16
      or die "segment $seg offset $data_off: write error: $!";
    sysread(SEG, $data, $mlen) == $mlen
      or die "segment $seg offset $data_off + 16: read error: $!";
    syswrite(WSEG, $data, $mlen) == $mlen
      or die "segment $seg offset $data_off + 16: write error: $!";
    $last_marker++;

    if ($idx) {
      if ($idx_off == $idx_len) {
        if ($current_segment > hex $seg) {
          print "index $idx is incomplete (not an error)\n";
        }
        close IDX;
        close WIDX;
        undef $idx;
      } elsif ($idx_off + 8 > $idx_len) {
        die "index $idx offset $idx_off: no room for next offset\n";
      } else {
        sysread(IDX, $data, 8) == 8
          or die "index $idx offset $idx_off: read error: $!";
        my ($offh, $offl) = unpack_64($data);
        if ($offh != 0 or $offl != $data_off) {
          die "index $idx offset $idx_off: index points to wrong offset\n";
        } else {
          $data = repack_64($offh, $offl);
          syswrite(WIDX, $data, 8) == 8
            or die "index $idx offset $idx_off: write error: $!";
          $idx_off += 8;
        }
      }
    }

    $data_off += 16 + $mlen;
  }
  $segdata->[2] = $last_marker;

  close SEG;
  close WSEG;
  if ($idx) {
    if ($idx_off == $idx_len) {
      if (hex $seg < $current_segment) {
        print "index $idx not current or closed\n";
      }
    } elsif ($idx_off + 8 > $idx_len) {
      die "index $idx offset $idx_off: no room for closing index\n";
    } elsif ($idx_off + 8 < $idx_len) {
      die "index $idx offset $idx_off: index too long\n";
    } else {
      sysread(IDX, $data, 8) == 8
        or die "index $idx offset $idx_off: read error: $!";
      my ($offh, $offl) = unpack_64($data);
      if ($offh != 0 or $offl != 0) {
        die "index $idx offset $idx_off: closing offset not 0\n";
      }
      $data = repack_64($offh, $offl);
      syswrite(WIDX, $data, 8) == 8
        or die "index $idx offset $idx_off: write error: $!";
    }
    close IDX;
    close WIDX;
  }
}

foreach my $cp (keys %$cpbyname) {
  if ($cpbyname->{$cp}{segment} ne '00000000' or
      $cpbyname->{$cp}{marker} != 0)
  {
    my ($segdata) = grep { $_->[0] eq $cpbyname->{$cp}{segment} } @$segments;
    if (!defined $segdata) {
      if (hex $cpbyname->{$cp}{segment} != $current_segment) {
        die "checkpoint $cp points to nonexistent segment\n";
      }
    } elsif ($cpbyname->{$cp}{marker} > $segdata->[2]) {
      die "checkpoint $cp points past the end of segment\n";
    }
  }
}
