#!/bin/nawk -f -
#
# Eliminate entries that are merely prefixes of one another, and entries
# that are just one character long.
#
BEGIN {
	lastnum = 0;
	laststr = "";
}

{
	currnum = $1;
	currstr = $0;
	gsub(/^[0-9]* '/,"",currstr);
	gsub(/'$/,"",currstr);

	# If this one's not an extension of the previous one, emit
	# the previous one.

	if(index(currstr,laststr) != 1) {
		if(length(laststr) > 1) {
			print (lastnum*length(laststr)) " \'" laststr "\'";
		}
		lastnum = currnum;
		laststr = currstr;
		next;
	}

	# This one IS an extension of the previous string, so check to see
	# if it occurs 'interestingly less often', defined here as 15 percent
	# less often. If so, emit the previous one, otherwise don't bother,
	# on the grounds that 'most of' the strings which match the previous
	# guy are really just this guy.

	if((currnum * 1.15) < lastnum && lastnum != 0) {
		if(length(laststr) > 1) {
			print (lastnum*length(laststr)) " \'" laststr "\'";
		}
		lastnum = currnum;
		laststr = currstr;
		next;
	}

	# Otherwise, the previous one is just a prefix of this one, so
	# axe it silently.

	lastnum = currnum;
	laststr = currstr;
}

END {
	if(length(laststr) > 1) {
		print (lastnum*length(laststr)) " \'" laststr "\'";
	}
}
	
