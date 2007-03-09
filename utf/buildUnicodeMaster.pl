#!/usr/bin/perl

my $line;

my @codepages = ();
$#codepages = 1114109;

my $unicodedata = $ARGV[0];
my $secondary = $ARGV[1];

open(UNICODE,"< $unicodedata") || die("Eep, no UnicodeData.");


while ($line = <UNICODE>)
{
    if ($line =~ /^([0-9A-F]+);(.*)/) {
       $unicodeval = hex $1;
       $codepages[$unicodeval] = $2;
    }
}

close (UNICODE);

open (CJKRANGE, "< $secondary") || die ("Eep, no secondary data!");

while ($line = <CJKRANGE>)
{
	if ($line =~ /^\s*([0-9A-F\-]+);(.*)/) {
	   my $codepage = $1;
	   my $codename = $2;
	   if ($codepage =~ /^([0-9A-F]+)\-([0-9A-F]+)$/) {
	   	  my $codeval1 = hex $1;
	   	  my $codeval2 = hex $2;
	   	  for ($loop = $codeval1; $loop <= $codeval2; $loop++) {
	   	  	 my $temphex = sprintf("%04X", $loop);
	   	     $codepages[$loop] = "$codename CHARACTER $temphex;So;0;ON;;;;N;;;;;";
	   	  }
	   }
	   else {
	   	  my $codeval = hex $codepage;
	   	  $codepages[$codeval] = "$codename CHARACTER $codepage;So;0;ON;;;;N;;;;;";
	   }
	}
}

for ($loop = 0; $loop < 1114110; $loop++) {
	if (defined $codepages[$loop]) {
   	   my $temphex = sprintf("%04X", $loop);
	   print "$temphex;$codepages[$loop]\n";
	}
}
