/* Stub string.h for RV64 freestanding build. */
#ifndef _RV64_STRING_H
#define _RV64_STRING_H
#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n);
int   memcmp(const void *a, const void *b, size_t n);
void *memset(void *dst, int c, size_t n);
size_t strlen(const char *s);

#endif
