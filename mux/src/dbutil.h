/*! \file dbutil.h
 * \brief Flatfile serialization primitives (libmux).
 *
 * Pure FILE*-based helpers for reading and writing integers and
 * escaped strings.  No engine state dependencies.
 */

#ifndef DBUTIL_H
#define DBUTIL_H

#include <cstdio>

void putref(FILE *f, int ref);
int  getref(FILE *f);
void putstring(FILE *f, const UTF8 *s);
void *getstring_noalloc(FILE *f, bool new_strings, size_t *pnBuffer);

#endif // DBUTIL_H
