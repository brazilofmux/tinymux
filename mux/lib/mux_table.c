/*! \file mux_table.c
 * \brief Multi-column notify layout helpers (#1667 Phase 3).
 *
 * Compiled as freestanding C into libmux (same model as color_ops.c): no
 * config.h / C++ headers.
 */

#include "color_ops.h"
#include "mux_table.h"

#include <string.h>

size_t mux_table_append_ljust(
    UTF8 *buf, size_t nBuf, size_t pos,
    const UTF8 *text, size_t nCols)
{
    if (NULL == buf || 0 == nBuf)
    {
        return 0;
    }
    if (pos + 1 >= nBuf)
    {
        buf[nBuf - 1] = '\0';
        return pos;
    }

    const unsigned char *src = (NULL != text)
        ? (const unsigned char *)text
        : (const unsigned char *)"";

    co_field fld = co_copy_field(
        (unsigned char *)(buf + pos),
        nBuf - pos, src, NULL, nCols);

    pos += fld.bytes;
    {
        size_t cols = fld.columns;
        while (cols < nCols && pos + 1 < nBuf)
        {
            buf[pos++] = ' ';
            cols++;
        }
    }
    if (pos < nBuf)
    {
        buf[pos] = '\0';
    }
    return pos;
}

size_t mux_table_append_trunc(
    UTF8 *buf, size_t nBuf, size_t pos,
    const UTF8 *text, size_t nCols)
{
    if (NULL == buf || 0 == nBuf)
    {
        return 0;
    }
    if (pos + 1 >= nBuf)
    {
        buf[nBuf - 1] = '\0';
        return pos;
    }

    const unsigned char *src = (NULL != text)
        ? (const unsigned char *)text
        : (const unsigned char *)"";

    co_field fld = co_copy_field(
        (unsigned char *)(buf + pos),
        nBuf - pos, src, NULL, nCols);

    pos += fld.bytes;
    if (pos < nBuf)
    {
        buf[pos] = '\0';
    }
    return pos;
}

size_t mux_table_append_bytes(
    UTF8 *buf, size_t nBuf, size_t pos, const char *lit)
{
    if (NULL == buf || 0 == nBuf)
    {
        return 0;
    }
    if (NULL == lit)
    {
        if (pos < nBuf)
        {
            buf[pos] = '\0';
        }
        return pos;
    }
    while ('\0' != *lit && pos + 1 < nBuf)
    {
        buf[pos++] = (UTF8)*lit++;
    }
    if (pos < nBuf)
    {
        buf[pos] = '\0';
    }
    return pos;
}

size_t mux_table_pos_after(
    const UTF8 *buf, size_t nBuf, size_t pos)
{
    if (NULL == buf || 0 == nBuf)
    {
        return 0;
    }
    while (pos < nBuf && '\0' != buf[pos])
    {
        pos++;
    }
    return pos;
}

size_t mux_table_emit_fields(
    UTF8 *buf, size_t nBuf,
    const mux_table_col *cols, size_t nCol,
    const UTF8 *const *cells,
    const char *sep)
{
    if (NULL == buf || 0 == nBuf)
    {
        return 0;
    }
    buf[0] = '\0';
    if (NULL == cols || 0 == nCol || NULL == cells)
    {
        return 0;
    }

    size_t pos = 0;
    size_t i;
    for (i = 0; i < nCol; i++)
    {
        const UTF8 *cell = cells[i];
        pos = mux_table_append_ljust(buf, nBuf, pos, cell, cols[i].width);
        if (i + 1 < nCol && NULL != sep)
        {
            pos = mux_table_append_bytes(buf, nBuf, pos, sep);
        }
    }
    return pos;
}
