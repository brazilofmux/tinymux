#!/usr/bin/perl
use strict;

my $unicodemaster = $ARGV[0];
open(UNICODE,"< $unicodemaster") || die("Eep, no UnicodeMaster.");

my $line;
while ($line = <UNICODE>)
{
    if ($line =~ /^([0-9A-F]+);([^;]*);([^;]*);([^;]*);([^;]*);([0-9A-F ]+);([^;]*);([^;]*);([^;]*);([^;]*);([^;]*);([^;]*);([^;]*);([^;]*);(.*)/)
    {
        print "$1;$6;$2\n";
    }
}

close (UNICODE);
