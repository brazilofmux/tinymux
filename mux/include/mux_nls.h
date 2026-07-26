/*! \file mux_nls.h
 * \brief Optional GNU gettext wrappers for server messages (#1419, #1444).
 *
 * Macro roles (keep these distinct — catalogs are maintenance cost):
 *
 *   T(x)   UTF-8 cast only. Command/function/attr names, formats, internals.
 *          Never runs gettext. Safe at static init.
 *   M_(x)  Player/staff prose. gettext when HAVE_NLS. Opt-in only.
 *   S_(x)  Softcode / machine ABI (#-1 …). Never translated.
 *   N_(x)  Mark-only for xgettext when the msgid is stored before runtime
 *          lookup (static globals re-bound via M_ after mux_nls_init).
 *
 * #1443: never call gettext on "" — that msgid is the .mo header.
 * #1444: opt-in M_() so the maintained set is the strings someone chose,
 *        not 6k tree-wide T() literals minus exclusions.
 */

#ifndef MUX_NLS_H
#define MUX_NLS_H

// UTF8 is defined in config.h before this header is included.

// UTF-8 cast — always, NLS or not. Identity for empty string / tables.
//
#define T(x)  (reinterpret_cast<const UTF8 *>(x))

// Softcode / machine tokens — never translate.
//
#define S_(x) (reinterpret_cast<const UTF8 *>(x))

// Mark-only for xgettext; no gettext call (static msgid storage).
//
#define N_(x) (reinterpret_cast<const UTF8 *>(x))

#if defined(HAVE_NLS)
// Runtime lookup for opted-in prose. Empty/nullptr msgids are returned
// unchanged so gettext("") cannot inject the catalog header (#1443).
//
LIBMUX_API const UTF8 *mux_gettext(const UTF8 *msgid);

#define M_(x) (mux_gettext(reinterpret_cast<const UTF8 *>(x)))
#else
#define M_(x) (reinterpret_cast<const UTF8 *>(x))
#endif

// Bind domain "tinymux" under locale_dir (typically game/locale).
// No-op when HAVE_NLS is undefined. Safe to call with nullptr (skipped).
//
LIBMUX_API void mux_nls_init(const UTF8 *locale_dir);

// Re-resolve player-facing global message pointers through M_() after
// mux_nls_init. Implemented in engine/match.cpp (not libmux). No-op
// without HAVE_NLS. Softcode S_() tokens are never refreshed.
//
void mux_nls_refresh_messages(void);

#endif // MUX_NLS_H
