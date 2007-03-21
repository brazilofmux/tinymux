/*! \file table.cpp
 * \brief Table formatting and visual-width utilities.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "interface.h"
#include "table.h"

mux_display_column::mux_display_column(const UTF8 *pHeader, LBUF_OFFSET nWidth, bool bFill, LBUF_OFFSET nPadTrailing, UTF8 uchFill)
{
    m_pHeader       = pHeader;
    m_nWidth        = nWidth;
    m_nPadTrailing  = nPadTrailing,
    m_uchFill       = uchFill;
    m_bFill         = bFill;
}

mux_display_table::mux_display_table(dbref target)
{
    m_target      = target;
    m_nColumns    = 0;
    m_iColumn     = 0;
    m_fldRowPos(0, 0);
    m_bInitial    = true;
    m_puchRow     = alloc_lbuf("mux_table");
}

mux_display_table::~mux_display_table(void)
{
    for (size_t i = 0; i < m_nColumns; i++)
    {
        delete m_aColumns[i];
    }
    free_lbuf(m_puchRow);
}

void mux_display_table::add_column(const UTF8 *header, LBUF_OFFSET nWidth, bool bFill, LBUF_OFFSET nPadTrailing, UTF8 uchFill)
{
    m_aColumns[m_nColumns] = new mux_display_column(header, nWidth, bFill, nPadTrailing, uchFill);
    m_nColumns++;
}

void mux_display_table::add_to_line(const UTF8 *pText)
{
    m_fldRowPos += StripTabsAndTruncate( pText,
                                       m_puchRow + m_fldRowPos.m_byte,
                                       (LBUF_SIZE-1) - m_fldRowPos.m_byte,
                                       m_aColumns[m_iColumn]->m_nWidth,
                                       m_aColumns[m_iColumn]->m_bFill);

    const mux_field fldAscii(1, 1);
    for (LBUF_OFFSET i = 0; i < m_aColumns[m_iColumn]->m_nPadTrailing; i++)
    {
        m_puchRow[m_fldRowPos.m_byte] = m_aColumns[m_iColumn]->m_uchFill;
        m_fldRowPos += fldAscii;
    }
    m_puchRow[m_fldRowPos.m_byte] = '\0';
}

void mux_display_table::cell_fill(const UTF8 *pText)
{
    add_to_line(pText);
    m_iColumn++;
}

void mux_display_table::cell_skip(void)
{
    add_to_line(Empty);
    m_iColumn++;
}

void mux_display_table::output(void)
{
    raw_notify(m_target, m_puchRow);
}

void mux_display_table::output_headers(void)
{
    m_fldRowPos(0, 0);
    for (m_iColumn = 0; m_iColumn < m_nColumns; m_iColumn++)
    {
        add_to_line(m_aColumns[m_iColumn]->m_pHeader);
    }
    raw_notify(m_target, m_puchRow);
}

void mux_display_table::row_begin(void)
{
    if (m_bInitial)
    {
        m_bInitial = false;
        output_headers();
    }
    m_iColumn = 0;
    m_fldRowPos(0, 0);
}

void mux_display_table::row_end(void)
{
    while (m_iColumn < m_nColumns)
    {
        cell_fill(Empty);
    }
    output();
}
