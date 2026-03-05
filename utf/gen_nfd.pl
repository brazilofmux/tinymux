#!/usr/bin/perl
# Filter Decompositions.txt to remove Hangul syllables (U+AC00..U+D7A3).
# Hangul decomposition is algorithmic and handled in C++ code.
# Output retains the same format for strings.exe.

use strict;
use warnings;

my $count = 0;
my $hangul = 0;
open my $fh, '<', 'Decompositions.txt' or die "Cannot open Decompositions.txt: $!";
while (<$fh>) {
    chomp;
    my @f = split /;/;
    my $cp = hex($f[0]);
    if ($cp >= 0xAC00 && $cp <= 0xD7A3) {
        $hangul++;
        next;
    }
    print "$_\n";
    $count++;
}
close $fh;
printf STDERR "%d decompositions written, %d Hangul excluded.\n", $count, $hangul;
