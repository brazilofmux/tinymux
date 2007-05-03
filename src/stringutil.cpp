/*
 * stringutil.c -- string utilities 
 */
/*
 * $Id: stringutil.c,v 1.2 1997/04/16 06:01:56 dpassmor Exp $ 
 */

#include "copyright.h"
#include "autoconf.h"

#include "mudconf.h"
#include "config.h"
#include "externs.h"
#include "alloc.h"
#include "ansi.h"

#ifdef __linux__
char *___strtok;

#endif

/* Convert raw character sequences into MUX substitutions (type = 1)
 * or strips them (type = 0). */

char *translate_string(str, type)
const char *str;
int type;
{
	char old[LBUF_SIZE];
	static char new[LBUF_SIZE];
	char *j, *c, *bp;
	int i;

	bp = new;
	StringCopy(old, str);
		
	for (j = old; *j != '\0'; j++) {
		switch (*j) {
		case ESC_CHAR:
			c = strchr(j, 'm');
			if (c) {
				if (!type) {
					j = c;
					break;
				}
				
				*c = '\0';
				i = atoi(j + 2);
				switch (i) {
				case 0:
					safe_str("%cn", new, &bp);
					break;
				case 1:
					safe_str("%ch", new, &bp);
					break;
				case 5:
					safe_str("%cf", new, &bp);
					break;
				case 7:
					safe_str("%ci", new, &bp);
					break;
				case 30:
					safe_str("%cx", new, &bp);
					break;
				case 31:
					safe_str("%cr", new, &bp);
					break;
				case 32:
					safe_str("%cg", new, &bp);
					break;
				case 33:
					safe_str("%cy", new, &bp);
					break;
				case 34:
					safe_str("%cb", new, &bp);
					break;
				case 35:
					safe_str("%cm", new, &bp);
					break;
				case 36:
					safe_str("%cc", new, &bp);
					break;
				case 37:
					safe_str("%cw", new, &bp);
					break;
				case 40:
					safe_str("%cX", new, &bp);
					break;
				case 41:
					safe_str("%cR", new, &bp);
					break;
				case 42:
					safe_str("%cG", new, &bp);
					break;
				case 43:
					safe_str("%cY", new, &bp);
					break;
				case 44:
					safe_str("%cB", new, &bp);
					break;
				case 45:
					safe_str("%cM", new, &bp);
					break;
				case 46:
					safe_str("%cC", new, &bp);
					break;
				case 47:
					safe_str("%cW", new, &bp);
					break;
				}
				j = c;
			} else {
				safe_chr(*j, new, &bp);
			}
			break;
		case ' ':
			if ((*(j+1) == ' ') && type)
				safe_str("%b", new, &bp);
			else 
				safe_chr(' ', new, &bp);
			break;
		case '\\':
			if (type)
				safe_str("\\", new, &bp);
			else
				safe_chr('\\', new, &bp);
			break;
		case '%':
			if (type)
				safe_str("%%", new, &bp);
			else
				safe_chr('%', new, &bp);
			break;
		case '[':
			if (type)
				safe_str("%[", new, &bp);
			else
				safe_chr('[', new, &bp);
			break;
		case ']':
			if (type)
				safe_str("%]", new, &bp);
			else
				safe_chr(']', new, &bp);
			break;
		case '{':
			if (type)
				safe_str("%{", new, &bp);
			else
				safe_chr('{', new, &bp);
			break;
		case '}':
			if (type)
				safe_str("%}", new, &bp);
			else
				safe_chr('}', new, &bp);
			break;
		case '(':
			if (type)
				safe_str("%(", new, &bp);
			else
				safe_chr('(', new, &bp);
			break;
		case ')':
			if (type)
				safe_str("%)", new, &bp);
			else
				safe_chr(')', new, &bp);
			break;
		case '\r':
			break;
		case '\n':
			if (type)
				safe_str("%r", new, &bp);
			else
				safe_chr(' ', new, &bp);
			break;
		default:
			safe_chr(*j, new, &bp);
		}
	}
	*bp = '\0';
	return new;
}

/*
 * capitalizes an entire string
 */

char *upcasestr(s)
char *s;
{
	char *p;

	for (p = s; p && *p; p++)
		*p = ToUpper(*p);
	return s;
}

/*
 * returns a pointer to the non-space character in s, or a NULL if s == NULL
 * or *s == NULL or s has only spaces.
 */
char *skip_space(s)
const char *s;
{
	char *cp;

	cp = (char *)s;
	while (cp && *cp && isspace(*cp))
		cp++;
	return (cp);
}

/*
 * returns a pointer to the next character in s matching c, or a pointer to
 * the \0 at the end of s.  Yes, this is a lot like index, but not exactly.
 */
char *seek_char(s, c)
const char *s;
char c;
{
	char *cp;

	cp = (char *)s;
	while (cp && *cp && (*cp != c))
		cp++;
	return (cp);
}

/*
 * ---------------------------------------------------------------------------
 * * munge_space: Compress multiple spaces to one space, also remove leading and
 * * trailing spaces.
 */

char *munge_space(string)
char *string;
{
	char *buffer, *p, *q;

	buffer = alloc_lbuf("munge_space");
	p = string;
	q = buffer;
	while (p && *p && isspace(*p))
		p++;		/*
				 * remove inital spaces 
				 */
	while (p && *p) {
		while (*p && !isspace(*p))
			*q++ = *p++;
		while (*p && isspace(*++p)) ;
		if (*p)
			*q++ = ' ';
	}
	*q = '\0';		/*
				 * remove terminal spaces and terminate * * * 
				 * 
				 * * string 
				 */
	return (buffer);
}

/*
 * ---------------------------------------------------------------------------
 * * trim_spaces: Remove leading and trailing spaces.
 */

char *trim_spaces(string)
char *string;
{
	char *buffer, *p, *q;

	buffer = alloc_lbuf("trim_spaces");
	p = string;
	q = buffer;
	while (p && *p && isspace(*p))	/*
					 * remove inital spaces 
					 */
		p++;
	while (p && *p) {
		while (*p && !isspace(*p))	/*
						 * copy nonspace chars 
						 */
			*q++ = *p++;
		while (*p && isspace(*p))	/*
						 * compress spaces 
						 */
			p++;
		if (*p)
			*q++ = ' ';	/*
					 * leave one space 
					 */
	}
	*q = '\0';		/*
				 * terminate string 
				 */
	return (buffer);
}

/*
 * ---------------------------------------------------------------------------
 * * grabto: Return portion of a string up to the indicated character.  Also
 * * returns a modified pointer to the string ready for another call.
 */

char *grabto(str, targ)
char **str, targ;
{
	char *savec, *cp;

	if (!str || !*str || !**str)
		return NULL;

	savec = cp = *str;
	while (*cp && *cp != targ)
		cp++;
	if (*cp)
		*cp++ = '\0';
	*str = cp;
	return savec;
}

int string_compare(s1, s2)
const char *s1, *s2;
{
#ifndef STANDALONE
	if (!mudconf.space_compress) {
		while (*s1 && *s2 && ToLower(*s1) == ToLower(*s2))
			s1++, s2++;

		return (ToLower(*s1) - ToLower(*s2));
	} else {
#endif
		while (isspace(*s1))
			s1++;
		while (isspace(*s2))
			s2++;
		while (*s1 && *s2 && ((ToLower(*s1) == ToLower(*s2)) ||
				      (isspace(*s1) && isspace(*s2)))) {
			if (isspace(*s1) && isspace(*s2)) {	/*
								 * skip all * 
								 * 
								 * *  * *
								 * other * *
								 * * spaces 
								 */
				while (isspace(*s1))
					s1++;
				while (isspace(*s2))
					s2++;
			} else {
				s1++;
				s2++;
			}
		}
		if ((*s1) && (*s2))
			return (1);
		if (isspace(*s1)) {
			while (isspace(*s1))
				s1++;
			return (*s1);
		}
		if (isspace(*s2)) {
			while (isspace(*s2))
				s2++;
			return (*s2);
		}
		if ((*s1) || (*s2))
			return (1);
		return (0);
#ifndef STANDALONE
	}
#endif
}

int string_prefix(string, prefix)
const char *string, *prefix;
{
	int count = 0;

	while (*string && *prefix && ToLower(*string) == ToLower(*prefix))
		string++, prefix++, count++;
	if (*prefix == '\0')	/*
				 * Matched all of prefix 
				 */
		return (count);
	else
		return (0);
}

/*
 * accepts only nonempty matches starting at the beginning of a word 
 */

const char *string_match(src, sub)
const char *src, *sub;
{
	if ((*sub != '\0') && (src)) {
		while (*src) {
			if (string_prefix(src, sub))
				return src;
			/*
			 * else scan to beginning of next word 
			 */
			while (*src && isalnum(*src))
				src++;
			while (*src && !isalnum(*src))
				src++;
		}
	}
	return 0;
}

/*
 * ---------------------------------------------------------------------------
 * * replace_string: Returns an lbuf containing string STRING with all occurances
 * * of OLD replaced by NEW. OLD and NEW may be different lengths.
 * * (mitch 1 feb 91)
 */

char *replace_string(old, new, string)
const char *old, *new, *string;
{
	char *result, *r, *s;
	int olen;

	if (string == NULL)
		return NULL;
	s = (char *)string;
	olen = strlen(old);
	r = result = alloc_lbuf("replace_string");
	while (*s) {

		/*
		 * Copy up to the next occurrence of the first char of OLD 
		 */

		while (*s && *s != *old) {
			safe_chr(*s, result, &r);
			s++;
		}

		/*
		 * If we are really at an OLD, append NEW to the result and * 
		 * 
		 * *  * *  * * bump the input string past the occurrence of
		 * OLD. *  * * * Otherwise, copy the char and try again. 
		 */

		if (*s) {
			if (!strncmp(old, s, olen)) {
				safe_str((char *)new, result, &r);
				s += olen;
			} else {
				safe_chr(*s, result, &r);
				s++;
			}
		}
	}
	*r = '\0';
	return result;
}

/*
 * Returns string STRING with all occurances * of OLD replaced by NEW. OLD
 * and NEW may be different lengths. Modifies string, so: Note - STRING must
 * already be allocated large enough to handle the new size. (mitch 1 feb 91)
 */

char *replace_string_inplace(old, new, string)
const char *old, *new;
char *string;
{
	char *s;

	s = replace_string(old, new, string);
	StringCopy(string, s);
	free_lbuf(s);
	return string;
}

/*
 * Counts occurances of C in STR. - mnp 7 feb 91 
 */

int count_chars(str, c)
const char *str, c;
{
	register out = 0;
	register const char *p = str;

	if (p)
		while (*p != '\0')
			if (*p++ == c)
				out++;
	return out;
}

/*
 * returns the number of identical characters in the two strings 
 */
int prefix_match(s1, s2)
const char *s1, *s2;
{
	int count = 0;

	while (*s1 && *s2 && (ToLower(*s1) == ToLower(*s2)))
		s1++, s2++, count++;
	/*
	 * If the whole string matched, count the null.  (Yes really.) 
	 */
	if (!*s1 && !*s2)
		count++;
	return count;
}

int minmatch(str, target, min)
char *str, *target;
int min;
{
	while (*str && *target && (ToLower(*str) == ToLower(*target))) {
		str++;
		target++;
		min--;
	}
	if (*str)
		return 0;
	if (!*target)
		return 1;
	return ((min <= 0) ? 1 : 0);
}

char *strsave(s)
const char *s;
{
	char *p;
	p = (char *)XMALLOC(sizeof(char) * (strlen(s) + 1), "strsave");

	if (p)
		StringCopy(p, s);
	return p;
}

/*
 * ---------------------------------------------------------------------------
 * * safe_copy_str, safe_copy_chr - Copy buffers, watching for overflows.
 */

int safe_copy_str(src, buff, bufp, max)
char *src, *buff, **bufp;
int max;
{
	char *tp;

	tp = *bufp;
	if (src == NULL)
		return 0;
	while (*src && ((tp - buff) < max))
		*tp++ = *src++;
	*bufp = tp;
	return strlen(src);
}

int safe_copy_chr(src, buff, bufp, max)
char src, *buff, **bufp;
int max;
{
	char *tp;
	int retval;

	tp = *bufp;
	retval = 0;
	if ((tp - buff) < max) {
		*tp++ = src;
	} else {
		retval = 1;
	}
	*bufp = tp;
	return retval;
}

int matches_exit_from_list(str, pattern)
char *str, *pattern;
{
	char *s;

	while (*pattern) {
		for (s = str;	/*
				 * check out this one 
				 */
		     (*s && (ToLower(*s) == ToLower(*pattern)) &&
		      *pattern && (*pattern != EXIT_DELIMITER));
		     s++, pattern++) ;

		/*
		 * Did we match it all? 
		 */

		if (*s == '\0') {

			/*
			 * Make sure nothing afterwards 
			 */

			while (*pattern && isspace(*pattern))
				pattern++;

			/*
			 * Did we get it? 
			 */

			if (!*pattern || (*pattern == EXIT_DELIMITER))
				return 1;
		}
		/*
		 * We didn't get it, find next string to test 
		 */

		while (*pattern && *pattern++ != EXIT_DELIMITER) ;
		while (isspace(*pattern))
			pattern++;
	}
	return 0;
}
