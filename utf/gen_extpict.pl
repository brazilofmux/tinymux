#!/usr/bin/perl
# Extract Extended_Pictographic property from emoji-data.txt.
# Output format: UnicodeData.txt style lines for classify.exe.
# Only code points with Extended_Pictographic=Yes are emitted.

use strict;
use warnings;

my @entries;

open my $fh, '<', 'emoji-data.txt' or die $!;
while (<$fh>) {
    chomp;
    next if /^\s*#/ || /^\s*$/;
    next unless /Extended_Pictographic/;

    if (/^([0-9A-F]+)\.\.([0-9A-F]+)/i) {
        my ($start, $end) = (hex($1), hex($2));
        for (my $cp = $start; $cp <= $end; $cp++) {
            push @entries, $cp;
        }
    } elsif (/^([0-9A-F]+)/i) {
        push @entries, hex($1);
    }
}
close $fh;

@entries = sort { $a <=> $b } @entries;

for my $cp (@entries) {
    printf "%04X;EXTENDED_PICTOGRAPHIC;So;0;ON;;;;;N;;;;;\n", $cp;
}

printf STDERR "%d Extended_Pictographic entries.\n", scalar @entries;
