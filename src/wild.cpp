/*
 * wild.c - wildcard routines
 * *
 * * Written by T. Alexander Popiel, 24 June 1993
 * * Last modified by T. Alexander Popiel, 19 August 1993
 * *
 * * Thanks go to Andrew Molitor for debugging
 * * Thanks also go to Rich $alz for code to benchmark against
 * *
 * * Copyright (c) 1993 by T. Alexander Popiel
 * * This code is hereby placed under GNU copyleft,
 * * see copyright.h for details.
 * *
 * * $Id: wild.c,v 1.2 1997/04/16 06:02:08 dpassmor Exp $
 */
#include "copyright.h"
#include "autoconf.h"

#include "config.h"
#include "db.h"
#include "mudconf.h"
#include "externs.h"
#include "alloc.h"

#define FIXCASE(a) (ToLower(a))
#define EQUAL(a,b) ((a == b) || (FIXCASE(a) == FIXCASE(b)))
#define NOTEQUAL(a,b) ((a != b) && (FIXCASE(a) != FIXCASE(b)))

static char **arglist;		/*

				 * argument return space 
				 */
static int numargs;		/*

				 * argument return size 
				 */

/*
 * ---------------------------------------------------------------------------
 * * quick_wild: do a wildcard match, without remembering the wild data.
 * *
 * * This routine will cause crashes if fed NULLs instead of strings.
 */
int quick_wild(tstr, dstr)
char *tstr, *dstr;
{
	while (*tstr != '*') {
		switch (*tstr) {
		case '?':
			/*
			 * Single character match.  Return false if at * end
			 * * * * of data. 
			 */
			if (!*dstr)
				return 0;
			break;
		case '\\':
			/*
			 * Escape character.  Move up, and force literal * *
			 * * * match of next character. 
			 */
			tstr++;
			/*
			 * FALL THROUGH 
			 */
		default:
			/*
			 * Literal character.  Check for a match. * If * * *
			 * matching end of data, return true. 
			 */
			if (NOTEQUAL(*dstr, *tstr))
				return 0;
			if (!*dstr)
				return 1;
		}
		tstr++;
		dstr++;
	}

	/*
	 * Skip over '*'. 
	 */

	tstr++;

	/*
	 * Return true on trailing '*'. 
	 */

	if (!*tstr)
		return 1;

	/*
	 * Skip over wildcards. 
	 */

	while ((*tstr == '?') || (*tstr == '*')) {
		if (*tstr == '?') {
			if (!*dstr)
				return 0;
			dstr++;
		}
		tstr++;
	}

	/*
	 * Skip over a backslash in the pattern string if it is there. 
	 */

	if (*tstr == '\\')
		tstr++;

	/*
	 * Return true on trailing '*'. 
	 */

	if (!*tstr)
		return 1;

	/*
	 * Scan for possible matches. 
	 */

	while (*dstr) {
		if (EQUAL(*dstr, *tstr) &&
		    quick_wild(tstr + 1, dstr + 1))
			return 1;
		dstr++;
	}
	return 0;
}

/*
 * ---------------------------------------------------------------------------
 * * wild1: INTERNAL: do a wildcard match, remembering the wild data.
 * *
 * * DO NOT CALL THIS FUNCTION DIRECTLY - DOING SO MAY RESULT IN
 * * SERVER CRASHES AND IMPROPER ARGUMENT RETURN.
 * *
 * * Side Effect: this routine modifies the 'arglist' static global
 * * variable.
 */
int wild1(tstr, dstr, arg)
char *tstr, *dstr;
int arg;
{
	char *datapos;
	int argpos, numextra;

	while (*tstr != '*') {
		switch (*tstr) {
		case '?':
			/*
			 * Single character match.  Return false if at * end
			 * * * * of data. 
			 */
			if (!*dstr)
				return 0;
			arglist[arg][0] = *dstr;
			arglist[arg][1] = '\0';
			arg++;

			/*
			 * Jump to the fast routine if we can. 
			 */

			if (arg >= numargs)
				return quick_wild(tstr + 1, dstr + 1);
			break;
		case '\\':
			/*
			 * Escape character.  Move up, and force literal * *
			 * * * match of next character. 
			 */
			tstr++;
			/*
			 * FALL THROUGH 
			 */
		default:
			/*
			 * Literal character.  Check for a match. * If * * *
			 * matching end of data, return true. 
			 */
			if (NOTEQUAL(*dstr, *tstr))
				return 0;
			if (!*dstr)
				return 1;
		}
		tstr++;
		dstr++;
	}

	/*
	 * If at end of pattern, slurp the rest, and leave. 
	 */

	if (!tstr[1]) {
		StringCopyTrunc(arglist[arg], dstr, LBUF_SIZE - 1);
		arglist[arg][LBUF_SIZE - 1] = '\0';
		return 1;
	}
	/*
	 * Remember current position for filling in the '*' return. 
	 */

	datapos = dstr;
	argpos = arg;

	/*
	 * Scan forward until we find a non-wildcard. 
	 */

	do {
		if (argpos < arg) {
			/*
			 * Fill in arguments if someone put another '*' * * * 
			 * 
			 * * before a fixed string. 
			 */
			arglist[argpos][0] = '\0';
			argpos++;

			/*
			 * Jump to the fast routine if we can. 
			 */

			if (argpos >= numargs)
				return quick_wild(tstr, dstr);

			/*
			 * Fill in any intervening '?'s 
			 */

			while (argpos < arg) {
				arglist[argpos][0] = *datapos;
				arglist[argpos][1] = '\0';
				datapos++;
				argpos++;

				/*
				 * Jump to the fast routine if we can. 
				 */

				if (argpos >= numargs)
					return quick_wild(tstr, dstr);
			}
		}
		/*
		 * Skip over the '*' for now... 
		 */

		tstr++;
		arg++;

		/*
		 * Skip over '?'s for now... 
		 */

		numextra = 0;
		while (*tstr == '?') {
			if (!*dstr)
				return 0;
			tstr++;
			dstr++;
			arg++;
			numextra++;
		}
	} while (*tstr == '*');

	/*
	 * Skip over a backslash in the pattern string if it is there. 
	 */

	if (*tstr == '\\')
		tstr++;

	/*
	 * Check for possible matches.  This loop terminates either at * end
	 * * * * of data (resulting in failure), or at a successful match. 
	 */
	while (1) {

		/*
		 * Scan forward until first character matches. 
		 */

		if (*tstr)
			while (NOTEQUAL(*dstr, *tstr)) {
				if (!*dstr)
					return 0;
				dstr++;
		} else
			while (*dstr)
				dstr++;

		/*
		 * The first character matches, now.  Check if the rest * * * 
		 * 
		 * * does, using the fastest method, as usual. 
		 */
		if (!*dstr ||
		    ((arg < numargs) ? wild1(tstr + 1, dstr + 1, arg)
		     : quick_wild(tstr + 1, dstr + 1))) {

			/*
			 * Found a match!  Fill in all remaining arguments. * 
			 * 
			 * *  * *  * * First do the '*'... 
			 */
			StringCopyTrunc(arglist[argpos], datapos,
					(dstr - datapos) - numextra);
			arglist[argpos][(dstr - datapos) - numextra] = '\0';
			datapos = dstr - numextra;
			argpos++;

			/*
			 * Fill in any trailing '?'s that are left. 
			 */

			while (numextra) {
				if (argpos >= numargs)
					return 1;
				arglist[argpos][0] = *datapos;
				arglist[argpos][1] = '\0';
				datapos++;
				argpos++;
				numextra--;
			}

			/*
			 * It's done! 
			 */

			return 1;
		} else {
			dstr++;
		}
	}
}

/*
 * ---------------------------------------------------------------------------
 * * wild: do a wildcard match, remembering the wild data.
 * *
 * * This routine will cause crashes if fed NULLs instead of strings.
 * *
 * * This function may crash if alloc_lbuf() fails.
 * *
 * * Side Effect: this routine modifies the 'arglist' and 'numargs'
 * * static global variables.
 */
int wild(tstr, dstr, args, nargs)
char *tstr, *dstr, *args[];
int nargs;
{
	int i, value;
	char *scan;

	/*
	 * Initialize the return array. 
	 */

	for (i = 0; i < nargs; i++)
		args[i] = NULL;

	/*
	 * Do fast match. 
	 */

	while ((*tstr != '*') && (*tstr != '?')) {
		if (*tstr == '\\')
			tstr++;
		if (NOTEQUAL(*dstr, *tstr))
			return 0;
		if (!*dstr)
			return 1;
		tstr++;
		dstr++;
	}

	/*
	 * Allocate space for the return args. 
	 */

	i = 0;
	scan = tstr;
	while (*scan && (i < nargs)) {
		switch (*scan) {
		case '?':
			args[i] = alloc_lbuf("wild.?");
			i++;
			break;
		case '*':
			args[i] = alloc_lbuf("wild.*");
			i++;
		}
		scan++;
	}

	/*
	 * Put stuff in globals for quick recursion. 
	 */

	arglist = args;
	numargs = nargs;

	/*
	 * Do the match. 
	 */

	value = nargs ? wild1(tstr, dstr, 0) : quick_wild(tstr, dstr);

	/*
	 * Clean out any fake match data left by wild1. 
	 */

	for (i = 0; i < nargs; i++)
		if ((args[i] != NULL) && (!*args[i] || !value)) {
			free_lbuf(args[i]);
			args[i] = NULL;
		}
	return value;
}

/*
 * ---------------------------------------------------------------------------
 * * wild_match: do either an order comparison or a wildcard match,
 * * remembering the wild data, if wildcard match is done.
 * *
 * * This routine will cause crashes if fed NULLs instead of strings.
 */
int wild_match(tstr, dstr)
char *tstr, *dstr;
{
	int i;

	switch (*tstr) {
	case '>':
		tstr++;
		if (isdigit(*tstr) || (*tstr == '-'))
			return (atoi(tstr) < atoi(dstr));
		else
			return (strcmp(tstr, dstr) < 0);
	case '<':
		tstr++;
		if (isdigit(*tstr) || (*tstr == '-'))
			return (atoi(tstr) > atoi(dstr));
		else
			return (strcmp(tstr, dstr) > 0);
	}

	return quick_wild(tstr, dstr);
}
