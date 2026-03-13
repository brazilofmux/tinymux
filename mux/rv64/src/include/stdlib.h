/* Stub stdlib.h for RV64 freestanding build. */
#ifndef _RV64_STDLIB_H
#define _RV64_STDLIB_H
#include <stddef.h>

void qsort(void *base, size_t nmemb, size_t size,
            int (*compar)(const void *, const void *));

long atol(const char *s);

#ifndef NULL
#define NULL ((void *)0)
#endif

#endif
