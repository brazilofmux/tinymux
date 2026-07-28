/*! \file mux_nls.h
 * \brief Optional GNU gettext wrappers for server messages (#1419, #1444).
 *
 * Macro roles (keep these distinct — catalogs are maintenance cost):
 *
 *   T(x)   UTF-8 cast only. Command/function/attr names, internals.
 *          Never runs gettext. Safe at static init.
 *   M_(x)  Player/staff prose, including whole-sentence format templates
 *          used as tprintf/mux_sprintf/raw_broadcast formats. gettext when
 *          HAVE_NLS. Opt-in only. Catalogues must preserve conversion
 *          sequences (tests/nls/check_nls.py).
 *   MN_(s,p,n)  Plural-aware prose (#1622). Resolved against the
 *          catalogue's own Plural-Forms rule when HAVE_NLS (#1702); the
 *          English ternary otherwise. Use whenever a count decides the
 *          wording -- never compute the suffix at the call site and pass it
 *          in as %s, which can only ever express English.
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

// Plural-aware lookup (#1622).  The count is passed to the catalogue rather
// than resolved here, because "1 vs everything else" is an English rule:
// Korean and Japanese have one form, Russian and Polish three, Arabic six.
// A call site that picks the suffix itself can only ever be right for
// English, and leaves a translator with a %s slot nothing can fill.
//
LIBMUX_API const UTF8 *mux_ngettext(const UTF8 *msgid,
                                    const UTF8 *msgid_plural,
                                    unsigned long n);

#define MN_(s,p,n) (mux_ngettext(reinterpret_cast<const UTF8 *>(s), \
                                 reinterpret_cast<const UTF8 *>(p), \
                                 static_cast<unsigned long>(n)))
#else
#define M_(x) (reinterpret_cast<const UTF8 *>(x))
#define MN_(s,p,n) (reinterpret_cast<const UTF8 *>((1 == (n)) ? (s) : (p)))
#endif

// Bind domain "tinymux" under locale_dir (typically game/locale).
// No-op when HAVE_NLS is undefined. Safe to call with nullptr (skipped).
//
// `language` is the netmux.conf `language` directive (#1702): a catalogue
// name such as "ko", naming game/locale/<language>/LC_MESSAGES/tinymux.mo.
// nullptr or empty means follow the environment, which is what every
// configuration written before the directive existed does.
//
// It is a PARAMETER rather than an environment variable on purpose.  There is
// now one catalogue reader on every platform (#1702), and it opens the
// catalogue by path: selection does not go through the process locale, so it
// does not inherit gettext's rule that C/POSIX suppresses translation, and
// it cannot mean different things on different platforms.
//
// Passing a value also points at per-player locale later: resolving a
// catalogue becomes a function OF a language rather than of process state.
// That was not possible while libintl owned lookup, because its domain
// binding is process-global.
//
LIBMUX_API void mux_nls_init(const UTF8 *locale_dir, const UTF8 *language);

// Re-resolve player-facing global message pointers through M_() after
// mux_nls_init. Implemented in engine/match.cpp (not libmux). No-op
// without HAVE_NLS. Softcode S_() tokens are never refreshed.
//
void mux_nls_refresh_messages(void);

#endif // MUX_NLS_H
