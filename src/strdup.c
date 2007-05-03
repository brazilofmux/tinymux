
/*
 * strdup.c - For systems like Ultrix that don't have it. 
 */

#include "copyright.h"
#include "autoconf.h"

char *strdup(const char *s)
{
	char *result;

	result = (char *)malloc(strlen(s) + 1);
	strcpy(result, s);
	return result;
}
