#!/bin/nawk -f -
#
# Consume the output of 'analyse.sh', possible twiddled by
# hand, and generate a compresstab.h.
#
BEGIN {
	print "char *cmptab[] = {";
}

{
	if(NR >= 3965) {
		exit(0);
	}
	gsub(/^[0-9]* '/,"",$0);
	gsub(/'$/,"",$0);
	gsub(/\\/,"\\\\",$0);
	gsub(/'/,"\\\'",$0);
	gsub(/"/,"\\\"",$0);
	printf("\t\"%s\",\n", $0);
}

END {
	print "\tNULL";
	print "};"
}
