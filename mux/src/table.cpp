/*! \file table.cpp
 * \brief Table formatting and visual-width utilities.
 *
 * $Id$
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "interface.h"
#include "table.h"

mux_display_column::mux_display_column(const UTF8 *pHeader, LBUF_OFFSET nWidthMin, LBUF_OFFSET nWidthMax,
                                       LBUF_OFFSET nPadTrailing, UTF8 uchFill)
{
    m_pHeader       = pHeader;
    m_nWidthMin     = nWidthMin;
    m_nWidthMax     = nWidthMax;
    m_nPadTrailing  = nPadTrailing,
    m_uchFill       = uchFill;
}

mux_display_table::mux_display_table(dbref target, bool bRawNotify)
{
    // Table status.
    //
    m_bInHeader    = false;
    m_bInBody      = false;
    m_bHaveHeaders = false;

    // Column status.
    //
    m_nColumns    = 0;
    m_iColumn     = 0;

    // Row status.
    //
    m_puchRow     = alloc_lbuf("mux_table");
    m_fldRowPos(0, 0);

    // Cell status.
    m_fldCellPos(0, 0);

    // Output targets.
    //
    m_target      = target;
    m_bRawNotify  = bRawNotify;
}

mux_display_table::~mux_display_table(void)
{
    for (size_t i = 0; i < m_nColumns; i++)
    {
        delete m_aColumns[i];
    }
    free_lbuf(m_puchRow);
}

void mux_display_table::add_to_line(const UTF8 *pText, bool bAdvance)
{
    mux_display_column *pColumn = m_aColumns[m_iColumn];
    LBUF_OFFSET nWidthMax = pColumn->m_nWidthMax - m_fldCellPos.m_column;
    m_fldCellPos += StripTabsAndTruncate( pText,
                                          m_puchRow + m_fldRowPos.m_byte,
                                          (LBUF_SIZE-1) - m_fldRowPos.m_byte,
                                          nWidthMax);
    m_fldRowPos += m_fldCellPos;

    if (bAdvance)
    {
        LBUF_OFFSET nWidthNeeded;
        if (m_fldCellPos.m_column < pColumn->m_nWidthMin)
        {
            nWidthNeeded = (m_fldRowPos - m_fldCellPos).m_column + pColumn->m_nWidthMin + pColumn->m_nPadTrailing;
        }
        else
        {
            nWidthNeeded = m_fldRowPos.m_column + pColumn->m_nPadTrailing;
        }

        m_fldRowPos = PadField(m_puchRow, LBUF_SIZE-1,
                                nWidthNeeded, m_fldRowPos,
                                pColumn->m_uchFill);
    }

    m_puchRow[m_fldRowPos.m_byte] = '\0';
}

void mux_display_table::body_begin(void)
{
    m_bInBody = true;
}

void mux_display_table::body_end(void)
{
    m_bInBody = false;
}

void mux_display_table::cell_add(const UTF8 *pText, bool bAdvance)
{
    add_to_line(pText, bAdvance);
    if (bAdvance)
    {
        m_iColumn++;
        m_fldCellPos(0,0);
    }
}

void mux_display_table::cell_skip(void)
{
    add_to_line(Empty);
    m_iColumn++;
    m_fldCellPos(0,0);
}

void mux_display_table::column_add(const UTF8 *header, LBUF_OFFSET nWidthMin, LBUF_OFFSET nWidthMax,
                                   LBUF_OFFSET nPadTrailing, UTF8 uchFill)
{
    m_aColumns[m_nColumns] = new mux_display_column(header, nWidthMin, nWidthMax, nPadTrailing, uchFill);
    m_nColumns++;
}

void mux_display_table::header_begin(void)
{
    m_fldRowPos(0, 0);
    m_iColumn = 0;
    m_bInHeader = true;
}

void mux_display_table::header_end(void)
{
    m_fldRowPos(0, 0);
    for (m_iColumn = 0; m_iColumn < m_nColumns; m_iColumn++)
    {
        m_fldCellPos(0, 0);
        add_to_line(m_aColumns[m_iColumn]->m_pHeader);
    }
    output();
    m_bInHeader = false;
    m_bHaveHeaders = true;
}

void mux_display_table::output(void)
{
    if (m_bRawNotify)
    {
        raw_notify(m_target, m_puchRow);
    }
    else
    {
        notify(m_target, m_puchRow);
    }
}

void mux_display_table::row_begin(void)
{
    m_iColumn = 0;
    m_fldRowPos(0, 0);
    m_fldCellPos(0,0);
}

void mux_display_table::row_end(void)
{
    while (m_iColumn < m_nColumns)
    {
        cell_add(Empty);
    }
    output();
}
