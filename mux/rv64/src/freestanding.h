/*
 * freestanding.h — Minimal libc shims for RV64 cross-compilation.
 *
 * Provides just enough to compile color_ops.c in freestanding mode.
 * The actual implementations of memcpy/memcmp/memset/memswap are
 * either in softlib.c (as intrinsic targets) or provided by the DBT
 * as native x86-64 stubs.
 */

#ifndef FREESTANDING_H
#define FREESTANDING_H

/* Suppress real libc headers. */
#define _STDIO_H
#define _STDLIB_H
#define _STRING_H

/* stddef.h and stdint.h are provided by the compiler (freestanding). */
#include <stddef.h>
#include <stdint.h>

/* string.h replacements — declared here, defined in softlib.c. */
void *memcpy(void *dst, const void *src, size_t n);
int   memcmp(const void *a, const void *b, size_t n);
void *memset(void *dst, int c, size_t n);
void  memswap(void *a, void *b, size_t n);

/* strlen as intrinsic. */
static inline size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

/* snprintf — minimal stub for co_lpos (%zu only). */
int snprintf(char *buf, size_t size, const char *fmt, ...);

/* qsort — for sort/set operations. */
void qsort(void *base, size_t nmemb, size_t size,
            int (*compar)(const void *, const void *));

/* NULL */
#ifndef NULL
#define NULL ((void *)0)
#endif

#endif /* FREESTANDING_H */
