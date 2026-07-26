/*! \file mux_nls.h
 * \brief Optional GNU gettext wrappers for server messages (#1419).
 *
 * Softcode diagnostics (#-1 …) are a stable English ABI — use S_().
 * Player-facing notifies use T(), which becomes gettext when HAVE_NLS.
 *
 * Caveats (see also #1443, #1444):
 * - Never call gettext on "" — that msgid is the catalog header.
 * - T() is still applied tree-wide as a UTF-8 cast; flipping it to
 *   gettext is the Phase 1 spike. #1444 discusses opt-in prose macros.
 * - Static tables that use T("name") with HAVE_NLS become dynamic
 *   initialisers (gettext call before bindtextdomain); unbound gettext
 *   returns the msgid, so identifiers stay English until rebound.
 */

#ifndef MUX_NLS_H
#define MUX_NLS_H

// UTF8 is defined in config.h before this header is included.

#if defined(HAVE_NLS)
// Helper (not a ternary macro): empty msgids must not reach gettext —
// the empty key holds .mo metadata (#1443). Also evaluates the argument
// once, so expression call sites stay safe.
//
LIBMUX_API const UTF8 *mux_gettext(const UTF8 *msgid);

#define T(x)  (mux_gettext(reinterpret_cast<const UTF8 *>(x)))
// Softcode / machine tokens — never translate.
#define S_(x) (reinterpret_cast<const UTF8 *>(x))
// Mark-only for xgettext; stores the English msgid (no gettext call).
#define N_(x) (reinterpret_cast<const UTF8 *>(x))
#else
#define T(x)  (reinterpret_cast<const UTF8 *>(x))
#define S_(x) (reinterpret_cast<const UTF8 *>(x))
#define N_(x) (reinterpret_cast<const UTF8 *>(x))
#endif

// Bind domain "tinymux" under locale_dir (typically game/locale).
// No-op when HAVE_NLS is undefined. Safe to call with nullptr (skipped).
//
LIBMUX_API void mux_nls_init(const UTF8 *locale_dir);

// Re-resolve player-facing global message pointers through gettext after
// mux_nls_init. Implemented in engine/match.cpp (not libmux). No-op
// without HAVE_NLS. Softcode S_() tokens are never refreshed (English ABI).
//
void mux_nls_refresh_messages(void);

#endif // MUX_NLS_H
