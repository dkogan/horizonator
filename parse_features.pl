#!/usr/bin/perl
use strict;
use warnings;
use feature qw(say state);

# ask emacs to treat this file as plain text
say "// -*- text -*-";

my $key_line = <>;
chomp $key_line;

my @key = split '\|', $key_line;

my ($col_name) = grep { $key[$_] eq 'FEATURE_NAME'  } 0..$#key;
my ($col_lat ) = grep { $key[$_] eq 'PRIM_LAT_DEC'  } 0..$#key;
my ($col_lon ) = grep { $key[$_] eq 'PRIM_LONG_DEC' } 0..$#key;
my ($col_ele ) = grep { $key[$_] eq 'ELEV_IN_M'     } 0..$#key;
my ($col_class)= grep { $key[$_] eq 'FEATURE_CLASS' } 0..$#key;

while(<>)
{
  chomp;
  my @fields = split '\|';

  my ($name,$lat,$lon,$ele,$class) = @fields[$col_name, $col_lat, $col_lon, $col_ele, $col_class];

  next if $fields[$col_class] ne 'Summit';

  # convert to radians
  $lat *= 3.14159265 / 180.0;
  $lon *= 3.14159265 / 180.0;

  say "{ \"$name\", $lat, $lon, $ele },";
}
