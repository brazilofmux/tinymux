/*! \file table.h
 * \brief Table formatting and visual-width utilities.
 *
 * $Id: $
 */

#ifndef TABLE_H
#define TABLE_H

static const UTF8 *Empty = T("");
#define MAX_CHAR_LENGTH 25 // Long story.
#define MAX_LINE_WIDTH 255
#define MAX_LINE_LENGTH (MAX_LINE_WIDTH+1 * MAX_CHAR_LENGTH)
#define MAX_COLUMNS 10

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
    mux_display_column  *m_aColumns[MAX_COLUMNS];
    UINT8                m_nColumns;
    UINT8                m_iColumn;
    UTF8                *m_puchRow;
    mux_field            m_fldRowPos;
    bool                 m_bInitial;
    dbref                m_target;

    void add_to_line(const UTF8 *pText);
    void output(void);
    void output_headers(void);

public:
    mux_display_table(dbref target);
    ~mux_display_table(void);
    void add_column(const UTF8 *header, LBUF_OFFSET nWidth, bool bFill = true, LBUF_OFFSET nPadTrailing = 1, UTF8 uchFill = (UTF8)' ');
    void cell_fill(const UTF8 *pText);
    void cell_skip(void);
    void row_begin(void);
    void row_end(void);
};
#endif // TABLE_H
