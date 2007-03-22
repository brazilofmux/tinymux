#!/usr/bin/perl

my $unicodedata = $ARGV[0];
open(UNICODE,"< $unicodedata") || die("Eep, no UnicodeData.");

my $line;
while ($line = <UNICODE>)
{
    if ($line =~ /^([0-9A-F]+);([^;]*);([^;]*);([^;]*);([^;]*);([^;]*);([^;]*);([^;]*);([^;]*);([^;]*);([^;]*);([^;]*);([^;]*);([^;]*);(.*)/)
    {
        if ($14)
        {
            print "$1;$14;$2;$11\n"
        }
    }
}

close (UNICODE);
