/*! \file mux_table.h
 * \brief Module-safe multi-column notify layout on co_copy_field (#1667 Phase 3).
 *
 * Headers and data cells for player-facing tables must share one construction
 * path (docs/design-tabular-notifies.md).  Modules cannot include stringutil.h;
 * co_copy_field is exported from libmux via color_ops.h, but the
 * pad-to-width / append helpers lived as copy-pasted statics in comsys_mod and
 * mail_mod.  This header is that helper surface — same shape as mux_format.h.
 *
 * Layout does not call gettext.  Callers pass already-resolved labels
 * (M_() / T_() outside this layer).
 *
 * Self-contained for freestanding C compiles (like color_ops.h).  When
 * included after config.h, UTF8 is already typedef'd and this matches.
 */

#ifndef MUX_TABLE_H
#define MUX_TABLE_H

#include <stddef.h>

/* Portable LIBMUX_API — same pattern as color_ops.h / mux_format.h. */
#ifndef LIBMUX_API
#if defined(_WIN32) || defined(WIN32)
#ifdef BUILDING_LIBMUX
#define LIBMUX_API __declspec(dllexport)
#else
#define LIBMUX_API __declspec(dllimport)
#endif
#else
#define LIBMUX_API __attribute__((visibility("default")))
#endif
#endif

#ifndef UTF8
typedef unsigned char UTF8;
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Field appenders — write into buf at pos, return new pos.  nBuf is capacity
// including the trailing NUL.  All keep the buffer NUL-terminated when nBuf > 0.
// ---------------------------------------------------------------------------

// Left-justify to nCols display columns: co_copy_field then pad with spaces.
//
LIBMUX_API size_t mux_table_append_ljust(
    UTF8 *buf, size_t nBuf, size_t pos,
    const UTF8 *text, size_t nCols);

// Truncate to at most nCols display columns; no padding.
//
LIBMUX_API size_t mux_table_append_trunc(
    UTF8 *buf, size_t nBuf, size_t pos,
    const UTF8 *text, size_t nCols);

// Append a C string literal (separators, fixed chrome).  NULL lit is a no-op.
//
LIBMUX_API size_t mux_table_append_bytes(
    UTF8 *buf, size_t nBuf, size_t pos, const char *lit);

// After writing a NUL-terminated fragment at buf[pos] (e.g. via mux_sprintf),
// advance pos to the terminator.
//
LIBMUX_API size_t mux_table_pos_after(
    const UTF8 *buf, size_t nBuf, size_t pos);

// ---------------------------------------------------------------------------
// Column descriptor + multi-field emit (header and data rows share this).
// ---------------------------------------------------------------------------

// One column: display width only.  Alignment is left-justified for Phase 3;
// chrome between columns is the caller's sep (typically a single space).
//
typedef struct mux_table_col
{
    size_t width;
} mux_table_col;

// Emit nCol fields into buf: for each i, append_ljust(cells[i], cols[i].width),
// then sep (except after the last).  cells[i] may be NULL (treated as empty).
// Returns the write position (NUL-terminated).  Empty nCol or null cols → pos 0.
//
// Header: pass translated labels as cells.
// Data row: pass cell values.  Same widths → alignment by construction.
//
LIBMUX_API size_t mux_table_emit_fields(
    UTF8 *buf, size_t nBuf,
    const mux_table_col *cols, size_t nCol,
    const UTF8 *const *cells,
    const char *sep);

#ifdef __cplusplus
}
#endif

#endif // MUX_TABLE_H
