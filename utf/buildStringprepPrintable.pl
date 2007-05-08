#!/usr/bin/perl

my $line;

my @prohibited = ();
$#prohibited = 1114109;

my $profiletable = $ARGV[0];
my $unicodetable = $ARGV[1];

open(STRINGPREP,"< $profiletable") || die("Eep, no stringprep.");

while ($line = <STRINGPREP>)
{
    if ($line =~ /^\s*([0-9A-F\-]+).*/) {
       my $codepage = $1;
       if ($codepage =~ /^([0-9A-F]+)\-([0-9A-F]+)$/) {
             my $codeval1 = hex $1;
             my $codeval2 = hex $2;
             for ($loop = $codeval1; $loop <= $codeval2; $loop++) {
                $prohibited[$loop] = 1;
             }
       }
       else {
             my $codeval = hex $codepage;
             $prohibited[$codeval] = 1;
       }
    }
}

close(STRINGPREP);

open (UNICODE,"< $unicodetable") || die("Eep, no unicode.");

while ($line = <UNICODE>)
{
    if ($line =~ /^([0-9A-F]+);.*/) {
       $unicodeval = hex $1;
       if (!defined $prohibited[$unicodeval]) {
          print $line;
       }
    }
}
