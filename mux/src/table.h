/*! \file table.h
 * \brief Table formatting and visual-width utilities.
 *
 * $Id$
 */

#ifndef TABLE_H
#define TABLE_H

//      Table->begin(...)
//
//      Table->header_begin(...)
//          Table->column_add(...)
//      Table->header_end()
//
//      Table->body_begin(...)
//          Table->row_begin(...)
//              Table->cell_add(...)
//          Table->row_end()
//      Table->body_end()
//
//      Table->end()

static const UTF8 *Empty = T("");

#define MAX_COLUMNS 255
#define FIXED_COLUMNS 10

class mux_display_column
{
public:
    const UTF8 *m_pHeader;
    LBUF_OFFSET m_nWidth;
    LBUF_OFFSET m_nPadTrailing;
    UTF8        m_uchFill;
    bool        m_bFill;

    mux_display_column(const UTF8 *header, LBUF_OFFSET nWidth, bool bFill = true, LBUF_OFFSET nPadTrailing = 1, UTF8 uchFill = (UTF8)' ');
};

class mux_display_table
{
private:
    bool                m_bInHeader;
    bool                m_bInBody;
    bool                m_bHaveHeaders;

    mux_display_column  *m_aColumns[FIXED_COLUMNS];
    UINT8                m_nColumns;
    UINT8                m_iColumn;

    UTF8                *m_puchRow;
    mux_field            m_fldRowPos;

    mux_field            m_fldCellPos;

    dbref                m_target;
    bool                 m_bRawNotify;

    void add_to_line(const UTF8 *pText, bool bAdvance = true);
    void output(void);

public:
    mux_display_table(dbref target, bool bRawNotify = true);
    ~mux_display_table(void);
    void body_begin(void);
    void body_end(void);
    void cell_add(const UTF8 *pText, bool bAdvance = true);
    void cell_skip(void);
    void column_add(const UTF8 *header, LBUF_OFFSET nWidth, bool bFill = true, LBUF_OFFSET nPadTrailing = 1, UTF8 uchFill = (UTF8)' ');
    void header_begin(void);
    void header_end(void);
    void row_begin(void);
    void row_end(void);
};
#endif // TABLE_H
