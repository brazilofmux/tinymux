/* Stub stdlib.h for RV64 freestanding build. */
#ifndef _RV64_STDLIB_H
#define _RV64_STDLIB_H
#include <stddef.h>

void qsort(void *base, size_t nmemb, size_t size,
            int (*compar)(const void *, const void *));

long atol(const char *s);
double strtod(const char *s, char **endptr);
/* color_ops' parse_i64 uses strtoll (#1402).  The freestanding build had
 * no declaration, so regenerating softlib.rv64 has failed since that
 * change -- silently, because nothing in the normal build rebuilds the
 * blob.  See softlib.c for the implementation. */
long long strtoll(const char *s, char **endptr, int base);

void *malloc(size_t size);
void  free(void *ptr);

#ifndef NULL
#define NULL ((void *)0)
#endif

#endif
