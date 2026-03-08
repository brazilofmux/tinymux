/*! \file core.h
 * \brief Core utility types and headers sufficient for files that don't
 *        depend on game state (mudconf, mudstate, db, etc.).
 *
 * Files that only need basic types (UTF8, UTF32), string utilities,
 * time utilities, hash/random, and buffer management can include this
 * instead of externs.h.
 *
 * externs.h includes core.h, so existing code is unaffected.
 */

#ifndef CORE_H
#define CORE_H

#include "_build.h"

#include "timeutil.h"
#include "svdrand.h"
#include "svdhash.h"

#include "libmux.h"

#include "alloc.h"

typedef struct
{
    size_t n_bytes;
    size_t n_points;
    const UTF8 *p;
} string_desc;

#include "ansi.h"
#include "utf8tables.h"
#include "stringutil.h"
#include "mathutil.h"

#ifndef UNIX_DIGEST
#include "sha1.h"
#endif

#endif // CORE_H
