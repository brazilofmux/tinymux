#!/usr/bin/perl

my $line;

my @codepoints = ();
$#codepoints = 1114109;

my $unicodetable = $ARGV[0];

open (UNICODE,"< $unicodetable") || die("Eep, no unicode.");

while ($line = <UNICODE>)
{
    if ($line =~ /^\s*([0-9A-F\-]+);(.*)/)
    {
        my $codepage = $1;
        my $codename = $2;
        if ($codepage =~ /^([0-9A-F]+)\-([0-9A-F]+)$/)
        {
            my $codeval1 = hex $1;
            my $codeval2 = hex $2;
            for ($loop = $codeval1; $loop <= $codeval2; $loop++)
            {
                my $temphex = sprintf("%04X", $loop);
                $codepoints[$loop] = "$codename CHARACTER $temphex;So;0;ON;;;;N;;;;;";
            }
        }
        else
        {
            my $codeval = hex $codepage;
            $codepoints[$codeval] = "$codename CHARACTER $codepage;So;0;ON;;;;N;;;;;";
        }
    }
}

close(UNICODE);

for ($loop = 0; $loop < 1114110; $loop++)
{
    if (defined $codepoints[$loop])
    {
        my $temphex = sprintf("%04X", $loop);
        print "$temphex;$codepoints[$loop]\n";
    }
}
