#!/usr/bin/perl
use strict;
use warnings;
use feature qw(say state);

my $key_line = <>;
chomp $key_line;

my @key = split '\|', $key_line;

my ($col_name) = grep { $key[$_] eq 'FEATURE_NAME'  } 0..$#key;
my ($col_lat ) = grep { $key[$_] eq 'PRIM_LAT_DEC'  } 0..$#key;
my ($col_lon ) = grep { $key[$_] eq 'PRIM_LONG_DEC' } 0..$#key;
my ($col_ele ) = grep { $key[$_] eq 'ELEV_IN_FT'    } 0..$#key;

while(<>)
{
  chomp;
  my @fields = split '\|';

  my ($name,$lat,$lon,$ele) = @fields[$col_name, $col_lat, $col_lon, $col_ele];

  say "{ \"$name\", $lat, $lon, $ele },";
}
