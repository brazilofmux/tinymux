#!/usr/bin/perl
use strict;
use integer;

my @codepoints = ();
$#codepoints = 1114109;

my $unicodemaster = $ARGV[0];
open(UNICODE,"< $unicodemaster") || die("Eep, no UnicodeMaster.");

my $line;
while ($line = <UNICODE>)
{
    if ($line =~ /^([0-9A-F]+);([^;]*);([^;]*);([^;]*);([^;]*);([0-9A-F ]+);(.*)/)
    {
        my $unicodeval = hex $1;
        my $decomposition = $6;
        $decomposition =~ s/ / z/g;
        $codepoints[$unicodeval] = "z$decomposition";
    }
}

close (UNICODE);

my $loop;
my $changed = 1;
while ($changed)
{
    $changed = 0;
    for ($loop = 0; $loop < 1114110; $loop++)
    {
        if (defined $codepoints[$loop])
        {
            if ($codepoints[$loop] =~ /^([^z]*)z([0-9A-F]+)(.*)/)
            {
                my $probe = hex $2;
                if (defined $codepoints[$probe])
                {
                    $codepoints[$loop] = "$1$codepoints[$probe]$3";
                }
                else
                {
                    $codepoints[$loop] = "$1$2$3";
                }
                $changed = 1;
            }
        }
    }
}

open(UNICODE,"< $unicodemaster") || die("Eep, no UnicodeMaster.");

my $line;
while ($line = <UNICODE>)
{
    if ($line =~ /^([0-9A-F]+);([^;]*);([^;]*);([^;]*);([^;]*);([0-9A-F ]+);(.*)/)
    {
        my $unicodeval = hex $1;
        print "$1;$codepoints[$unicodeval];$2\n";
    }
}

close (UNICODE);

