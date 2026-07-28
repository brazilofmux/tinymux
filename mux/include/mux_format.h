/*! \file mux_format.h
 * \brief Module-safe declarations for the exported string formatters.
 *
 * The formatting and copying primitives live in stringutil.cpp and are
 * exported from libmux, but stringutil.h cannot be included from a loadable
 * module: its inline helpers reference the Ragel-generated tables in
 * utf8tables.h, which only the engine links.  So the correct primitives were
 * reachable at link time and undeclarable at compile time, and the path of
 * least resistance from a module pointed straight at the C library's
 * snprintf() -- 179 times, across comsys_mod and mail_mod, before anyone
 * counted.
 *
 * That matters because these are not interchangeable.  mux_vsnprintf (which
 * mux_sprintf wraps) measures in codepoints: it walks utf8_FirstByte when
 * applying precision, so a truncated result ends on a character boundary.
 * snprintf measures bytes, and truncation can slice a UTF-8 sequence in half
 * and emit invalid UTF-8 to a player.  Every one of these call sites
 * interpolates player-settable text into a fixed buffer, so that is the
 * ordinary case, not a corner.
 *
 * The second reason is tests/format/check_formats.py, which guards format
 * strings by wrapper name.  mux_sprintf is on its list; snprintf is not and
 * cannot be, since it is the C library's.  Calling the wrapper puts a site
 * inside a guard covering 1170 others; calling snprintf silently opts out.
 *
 * This header exists so a module has no reason to reach for the wrong one.
 * It mirrors mux_nls.h, which solves the same problem for mux_gettext.
 */

#ifndef MUX_FORMAT_H
#define MUX_FORMAT_H

// UTF8, LIBMUX_API, and DCL_CDECL come from config.h, included first.

// Bounded, codepoint-aware sprintf.  count includes the '\0'.  Prefer this to
// snprintf in every module; see tests/format/check_formats.py, which fails the
// build on new raw snprintf/sprintf/vsnprintf call sites.
//
LIBMUX_API void DCL_CDECL mux_sprintf(UTF8 *buff, size_t count,
    const UTF8 *fmt, ...);

// As mux_sprintf, but returns the length WRITTEN (0..count-1).  Note that
// is not snprintf's return, which is the length it WOULD have written --
// truncation shows here as a short result, not an over-long one.
//
LIBMUX_API size_t DCL_CDECL mux_snprintf(UTF8 *buff, size_t count,
    const UTF8 *fmt, ...);

// va_list form, for a module's own printf-like wrappers.  A wrapper added here
// should also be registered in check_formats.py's WRAPPERS so its call sites
// are checked.
//
LIBMUX_API size_t DCL_CDECL mux_vsnprintf(UTF8 *pBuffer, size_t nBuffer,
    const UTF8 *pFmt, va_list va);

// Bounded copy.  length_to_copy excludes the '\0', which is always written.
//
LIBMUX_API void mux_strncpy(UTF8 *dest, const UTF8 *src, size_t length_to_copy);

#endif // MUX_FORMAT_H
