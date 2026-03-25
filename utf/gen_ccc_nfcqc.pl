#!/usr/bin/perl
# Merge CCC and NFC_QC into a single combined data file.
# Output format: "CP;VALUE" where VALUE = ccc * 3 + nfcqc.
# Decode at runtime: ccc = val / 3, nfcqc = val % 3.
# Only outputs entries where the combined value != 0.
# Sorted by code point.

use strict;
use warnings;

my %ccc;    # cp (integer) -> ccc value
my %nfcqc;  # cp (integer) -> nfcqc value (1=No, 2=Maybe)

# Read CCC data.
open my $fh1, '<', 'tr_ccc.txt' or die "Cannot open tr_ccc.txt: $!";
while (<$fh1>) {
    chomp;
    next if /^\s*$/ || /^\s*#/;
    my ($cp_hex, $val) = split /;/;
    $ccc{hex($cp_hex)} = $val + 0;
}
close $fh1;

# Read NFC_QC data.
open my $fh2, '<', 'tr_nfcqc.txt' or die "Cannot open tr_nfcqc.txt: $!";
while (<$fh2>) {
    chomp;
    next if /^\s*$/ || /^\s*#/;
    my ($cp_hex, $val) = split /;/;
    $nfcqc{hex($cp_hex)} = $val + 0;
}
close $fh2;

# Merge: union of all code points from both sets.
my %all;
$all{$_} = 1 for keys %ccc;
$all{$_} = 1 for keys %nfcqc;

my @sorted = sort { $a <=> $b } keys %all;

my $count = 0;
for my $cp (@sorted) {
    my $c = $ccc{$cp}   || 0;
    my $q = $nfcqc{$cp} || 0;
    my $combined = $c * 3 + $q;
    next unless $combined != 0;
    printf "%04X;%d\n", $cp, $combined;
    $count++;
}

printf STDERR "%d combined CCC+NFCQC entries.\n", $count;
