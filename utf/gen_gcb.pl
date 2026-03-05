#!/usr/bin/perl
# Extract Grapheme_Cluster_Break property from GraphemeBreakProperty.txt.
# Output format: "CP;VALUE" where VALUE is the integer category.
# Default (Other) is 0, handled by the DFA default state.
# Sorted by code point.

use strict;
use warnings;

# GCB category → integer mapping.
# 0 = Other (default, not emitted).
my %gcb_val = (
    'CR'                 => 1,
    'LF'                 => 2,
    'Control'            => 3,
    'Extend'             => 4,
    'ZWJ'                => 5,
    'Regional_Indicator' => 6,
    'Prepend'            => 7,
    'SpacingMark'        => 8,
    'L'                  => 9,
    'V'                  => 10,
    'T'                  => 11,
    'LV'                 => 12,
    'LVT'                => 13,
);

my @entries;

open my $fh, '<', 'GraphemeBreakProperty.txt' or die $!;
while (<$fh>) {
    chomp;
    next if /^\s*#/ || /^\s*$/;

    # Format: CP_OR_RANGE ; PROPERTY # comment
    my ($range, $prop) = /^([0-9A-F.]+)\s*;\s*(\w+)/i or next;
    my $val = $gcb_val{$prop};
    unless (defined $val) {
        warn "Unknown GCB property: $prop\n";
        next;
    }

    if ($range =~ /^([0-9A-F]+)\.\.([0-9A-F]+)$/i) {
        my ($start, $end) = (hex($1), hex($2));
        for (my $cp = $start; $cp <= $end; $cp++) {
            push @entries, [$cp, $val];
        }
    } elsif ($range =~ /^([0-9A-F]+)$/i) {
        push @entries, [hex($1), $val];
    }
}
close $fh;

# Sort by code point.
@entries = sort { $a->[0] <=> $b->[0] } @entries;

for my $e (@entries) {
    printf "%04X;%d\n", $e->[0], $e->[1];
}

printf STDERR "%d GCB entries across %d categories.\n", scalar @entries, scalar keys %gcb_val;
