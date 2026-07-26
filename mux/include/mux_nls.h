/*! \file mux_nls.h
 * \brief Optional GNU gettext wrappers for server messages (#1419).
 *
 * Softcode diagnostics (#-1 …) are a stable English ABI — use S_().
 * Player-facing notifies use T(), which becomes gettext when HAVE_NLS.
 */

#ifndef MUX_NLS_H
#define MUX_NLS_H

// UTF8 is defined in config.h before this header is included.

#if defined(HAVE_NLS)
#include <libintl.h>
// Player / staff prose (notify paths). Call only after mux_nls_init, or
// rebind globals via mux_nls_refresh_messages — never at static init.
//
// Cast msgid to const char* for gettext: call sites and macros (e.g.
// STARTLOG) sometimes pass UTF8* or nest T() when the old cast was
// idempotent.
//
#define T(x)  (reinterpret_cast<const UTF8 *>(gettext(reinterpret_cast<const char *>(x))))
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
