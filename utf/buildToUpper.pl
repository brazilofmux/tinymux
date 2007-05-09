#!/usr/bin/perl

my $unicodedata = $ARGV[0];
open(UNICODE,"< $unicodedata") || die("Eep, no UnicodeData.");

my $line;
while ($line = <UNICODE>)
{
    if ($line =~ /^([0-9A-F]+);([^;]*);([^;]*);([^;]*);([^;]*);([^;]*);([^;]*);([^;]*);([^;]*);([^;]*);([^;]*);([^;]*);([^;]*);([^;]*);(.*)/)
    {
        if ($13)
        {
            print "$1;$13;$2;$11\n"
        }
    }
}

close (UNICODE);
