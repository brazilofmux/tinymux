#!/usr/bin/perl
# Generate NFC canonical composition pairs from UnicodeData.txt.
# Output format: "CP1 CP2;RESULT" (all hex), sorted by CP1 then CP2.
#
# Excludes:
# - Hangul syllable composition (algorithmic, not table-driven)
# - Composition exclusions (singletons, non-starter decompositions)
#
# For now, we generate ALL canonical two-code-point decompositions
# (reversed into composition direction) and let CompositionExclusions
# be handled later.

use strict;
use warnings;

# Read CompositionExclusions.txt if available.
#
my %excluded;
if (open my $fh, '<', 'CompositionExclusions.txt') {
    while (<$fh>) {
        next if /^\s*#/ || /^\s*$/;
        if (/^([0-9A-F]+)/i) {
            $excluded{uc $1} = 1;
        }
    }
    close $fh;
    printf STDERR "%d composition exclusions loaded.\n", scalar keys %excluded;
}

# Also exclude non-starter decompositions (CCC != 0 for the composed character).
#
my %ccc;
my @pairs;

open my $fh, '<', 'UnicodeData.txt' or die "Cannot open UnicodeData.txt: $!";
while (<$fh>) {
    chomp;
    my @f = split /;/;
    my $cp = $f[0];
    my $combining_class = $f[3];
    $ccc{$cp} = $combining_class + 0;

    my $decomp = $f[5];
    next unless defined $decomp && $decomp ne '';

    # Skip compatibility decompositions (they start with <tag>).
    next if $decomp =~ /^</;

    my @parts = split / /, $decomp;
    next unless @parts == 2;

    my $cp1 = $parts[0];
    my $cp2 = $parts[1];

    # Skip Hangul syllable range (U+AC00..U+D7A3).
    my $cpval = hex($cp);
    next if $cpval >= 0xAC00 && $cpval <= 0xD7A3;

    push @pairs, [$cp1, $cp2, $cp];
}
close $fh;

# Apply exclusions:
# 1. Explicit CompositionExclusions.txt
# 2. Non-starter decompositions (the composed character has CCC != 0)
#    These are "singleton" exclusions per Unicode spec.
#
my @filtered;
for my $p (@pairs) {
    my ($cp1, $cp2, $result) = @$p;

    # Skip if explicitly excluded.
    next if $excluded{uc $result};

    # Skip if the composed character is a non-starter (CCC != 0).
    # These are "non-starter decompositions" excluded from composition.
    next if ($ccc{uc $result} || 0) != 0;

    push @filtered, $p;
}

# Sort by cp1 then cp2 (numerically by hex value).
@filtered = sort {
    hex($a->[0]) <=> hex($b->[0]) || hex($a->[1]) <=> hex($b->[1])
} @filtered;

for my $p (@filtered) {
    printf "%s %s;%s\n", $p->[0], $p->[1], $p->[2];
}

printf STDERR "%d canonical composition pairs written.\n", scalar @filtered;
