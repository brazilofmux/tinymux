#!/usr/bin/perl

my $line;

my $unicodedata = $ARGV[0];

open(UNICODE,"< $unicodedata") || die("Eep, no UnicodeData.");


while ($line = <UNICODE>)
{
    if ($line =~ /^([0-9A-F]+);([^;]*);([^;]*);([^;]*);([^;]*);([^;]*);([^;]*);([^;]*);([^;]*);([^;]*);([^;]*);/)
    {
        print "$1;$7;$2;$11\n"
    }
}

close (UNICODE);
