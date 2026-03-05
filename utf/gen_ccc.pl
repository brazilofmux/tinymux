#!/usr/bin/perl
# Extract Canonical Combining Class from UnicodeData.txt.
# Output format: "CP;CCC" (hex code point, decimal CCC value).
# Only outputs entries with CCC != 0 (the default).
# Sorted by code point.

use strict;
use warnings;

my $count = 0;
open my $fh, '<', 'UnicodeData.txt' or die "Cannot open UnicodeData.txt: $!";
while (<$fh>) {
    chomp;
    my @f = split /;/;
    next unless @f >= 4;
    my $cp = $f[0];
    my $ccc = $f[3] + 0;
    next unless $ccc != 0;
    printf "%s;%d\n", $cp, $ccc;
    $count++;
}
close $fh;
printf STDERR "%d code points with non-zero CCC.\n", $count;
