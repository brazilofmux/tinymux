#!/usr/bin/perl

my $unicodedata = $ARGV[0];
open(UNICODE,"< $unicodedata") || die("Eep, no UnicodeData.");

my $line;
while ($line = <UNICODE>)
{
    if ($line =~ /^([0-9A-F]+);([^;]*);([^;]*);([^;]*);([^;]*);([^;]*);([^;]*);([^;]*);([^;]*);([^;]*);([^;]*);([^;]*);([^;]*);([^;]*);(.*)/)
    {
        if ($15)
        {
            print "$1;$15;$2;$11\n"
        }
    }
}

close (UNICODE);
