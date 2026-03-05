#!/usr/bin/perl
# Extract NFC_QC property from DerivedNormalizationProps.txt.
# Output format: "CP;VALUE" where VALUE is 1 (No) or 2 (Maybe).
# Default (Yes) is 0, handled by the DFA default state.
# Sorted by code point.

use strict;
use warnings;

my @entries;

open my $fh, '<', 'DerivedNormalizationProps.txt' or die $!;
while (<$fh>) {
    chomp;
    next if /^\s*#/ || /^\s*$/;
    next unless /NFC_QC/;

    my $val;
    if (/NFC_QC;\s*N/) {
        $val = 1;  # No
    } elsif (/NFC_QC;\s*M/) {
        $val = 2;  # Maybe
    } else {
        next;
    }

    # Parse code point or range.
    if (/^([0-9A-F]+)\.\.([0-9A-F]+)/i) {
        my ($start, $end) = (hex($1), hex($2));
        for (my $cp = $start; $cp <= $end; $cp++) {
            push @entries, [$cp, $val];
        }
    } elsif (/^([0-9A-F]+)/i) {
        push @entries, [hex($1), $val];
    }
}
close $fh;

# Sort by code point.
@entries = sort { $a->[0] <=> $b->[0] } @entries;

for my $e (@entries) {
    printf "%04X;%d\n", $e->[0], $e->[1];
}

printf STDERR "%d NFC_QC entries (No + Maybe).\n", scalar @entries;
