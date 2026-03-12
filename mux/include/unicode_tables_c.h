/*
 * unicode_tables.h — Unicode case mapping DFA tables for pure C.
 *
 * Provides the same DFA-driven case mapping as mux_toupper/mux_tolower/
 * mux_totitle from stringutil.h, but as standalone C functions using
 * tables extracted from utf8tables.cpp.
 *
 * DFA execution model:
 *   Feed bytes to the DFA one at a time.  When it reaches an accepting
 *   state, the state index encodes either "identity" (no change) or an
 *   index into the OTT (output text table).  OTT entries are either
 *   literal replacements or XOR masks.
 */

#ifndef UNICODE_TABLES_H
#define UNICODE_TABLES_H

#include <stddef.h>

/* Equivalent to MUX's string_desc — holds a UTF-8 replacement string. */
typedef struct {
    size_t n_bytes;
    size_t n_points;
    const unsigned char *p;
} co_string_desc;

/* UTF-8 lead byte → sequence length (1-4), 5=continuation, 6=illegal */
#define CO_UTF8_SIZE1     1
#define CO_UTF8_SIZE2     2
#define CO_UTF8_SIZE3     3
#define CO_UTF8_SIZE4     4
#define CO_UTF8_CONTINUE  5
#define CO_UTF8_ILLEGAL   6
extern const unsigned char utf8_FirstByte[256];

/* --- tr_tolower DFA tables --- */

#define TR_TOLOWER_START_STATE (0)
#define TR_TOLOWER_ACCEPTING_STATES_START (62)
extern const unsigned char tr_tolower_itt[256];
extern const unsigned short tr_tolower_sot[62];
extern const unsigned char tr_tolower_sbt[1700];

#define TR_TOLOWER_DEFAULT (0)
#define TR_TOLOWER_LITERAL_START (1)
#define TR_TOLOWER_XOR_START (28)
extern const co_string_desc tr_tolower_ott[144];

/* --- tr_toupper DFA tables --- */

#define TR_TOUPPER_START_STATE (0)
#define TR_TOUPPER_ACCEPTING_STATES_START (67)
extern const unsigned char tr_toupper_itt[256];
extern const unsigned short tr_toupper_sot[67];
extern const unsigned char tr_toupper_sbt[1820];

#define TR_TOUPPER_DEFAULT (0)
#define TR_TOUPPER_LITERAL_START (1)
#define TR_TOUPPER_XOR_START (33)
extern const co_string_desc tr_toupper_ott[158];

/* --- tr_totitle DFA tables --- */

#define TR_TOTITLE_START_STATE (0)
#define TR_TOTITLE_ACCEPTING_STATES_START (67)
extern const unsigned char tr_totitle_itt[256];
extern const unsigned short tr_totitle_sot[67];
extern const unsigned char tr_totitle_sbt[1821];

#define TR_TOTITLE_DEFAULT (0)
#define TR_TOTITLE_LITERAL_START (1)
#define TR_TOTITLE_XOR_START (33)
extern const co_string_desc tr_totitle_ott[156];

/*
 * co_dfa_toupper — Run the toupper DFA on a single code point.
 *
 * p points to the first byte of a UTF-8 code point.
 * On return, *bXor is set:
 *   - returns NULL:  identity (no change)
 *   - bXor == 1:     XOR each byte of input with result->p[]
 *   - bXor == 0:     literal replacement (may differ in byte count)
 */
static inline const co_string_desc *co_dfa_toupper(const unsigned char *p, int *bXor)
{
    unsigned char iState = TR_TOUPPER_START_STATE;
    do
    {
        unsigned char ch = *p++;
        unsigned char iColumn = tr_toupper_itt[ch];
        unsigned short iOffset = tr_toupper_sot[iState];
        for (;;)
        {
            int y = tr_toupper_sbt[iOffset];
            if (y < 128)
            {
                /* RUN phrase. */
                if (iColumn < y)
                {
                    iState = tr_toupper_sbt[iOffset+1];
                    break;
                }
                else
                {
                    iColumn = (unsigned char)(iColumn - y);
                    iOffset += 2;
                }
            }
            else
            {
                /* COPY phrase. */
                y = 256 - y;
                if (iColumn < y)
                {
                    iState = tr_toupper_sbt[iOffset+iColumn+1];
                    break;
                }
                else
                {
                    iColumn = (unsigned char)(iColumn - y);
                    iOffset = (unsigned short)(iOffset + y + 1);
                }
            }
        }
    } while (iState < TR_TOUPPER_ACCEPTING_STATES_START);

    if (TR_TOUPPER_DEFAULT == iState - TR_TOUPPER_ACCEPTING_STATES_START)
    {
        *bXor = 0;
        return (const co_string_desc *)0;
    }
    else
    {
        *bXor = (TR_TOUPPER_XOR_START <= iState - TR_TOUPPER_ACCEPTING_STATES_START);
        return tr_toupper_ott + iState - TR_TOUPPER_ACCEPTING_STATES_START - 1;
    }
}

static inline const co_string_desc *co_dfa_tolower(const unsigned char *p, int *bXor)
{
    unsigned char iState = TR_TOLOWER_START_STATE;
    do
    {
        unsigned char ch = *p++;
        unsigned char iColumn = tr_tolower_itt[ch];
        unsigned short iOffset = tr_tolower_sot[iState];
        for (;;)
        {
            int y = tr_tolower_sbt[iOffset];
            if (y < 128)
            {
                if (iColumn < y)
                {
                    iState = tr_tolower_sbt[iOffset+1];
                    break;
                }
                else
                {
                    iColumn = (unsigned char)(iColumn - y);
                    iOffset += 2;
                }
            }
            else
            {
                y = 256 - y;
                if (iColumn < y)
                {
                    iState = tr_tolower_sbt[iOffset+iColumn+1];
                    break;
                }
                else
                {
                    iColumn = (unsigned char)(iColumn - y);
                    iOffset = (unsigned short)(iOffset + y + 1);
                }
            }
        }
    } while (iState < TR_TOLOWER_ACCEPTING_STATES_START);

    if (TR_TOLOWER_DEFAULT == iState - TR_TOLOWER_ACCEPTING_STATES_START)
    {
        *bXor = 0;
        return (const co_string_desc *)0;
    }
    else
    {
        *bXor = (TR_TOLOWER_XOR_START <= iState - TR_TOLOWER_ACCEPTING_STATES_START);
        return tr_tolower_ott + iState - TR_TOLOWER_ACCEPTING_STATES_START - 1;
    }
}

static inline const co_string_desc *co_dfa_totitle(const unsigned char *p, int *bXor)
{
    unsigned char iState = TR_TOTITLE_START_STATE;
    do
    {
        unsigned char ch = *p++;
        unsigned char iColumn = tr_totitle_itt[ch];
        unsigned short iOffset = tr_totitle_sot[iState];
        for (;;)
        {
            int y = tr_totitle_sbt[iOffset];
            if (y < 128)
            {
                if (iColumn < y)
                {
                    iState = tr_totitle_sbt[iOffset+1];
                    break;
                }
                else
                {
                    iColumn = (unsigned char)(iColumn - y);
                    iOffset += 2;
                }
            }
            else
            {
                y = 256 - y;
                if (iColumn < y)
                {
                    iState = tr_totitle_sbt[iOffset+iColumn+1];
                    break;
                }
                else
                {
                    iColumn = (unsigned char)(iColumn - y);
                    iOffset = (unsigned short)(iOffset + y + 1);
                }
            }
        }
    } while (iState < TR_TOTITLE_ACCEPTING_STATES_START);

    if (TR_TOTITLE_DEFAULT == iState - TR_TOTITLE_ACCEPTING_STATES_START)
    {
        *bXor = 0;
        return (const co_string_desc *)0;
    }
    else
    {
        *bXor = (TR_TOTITLE_XOR_START <= iState - TR_TOTITLE_ACCEPTING_STATES_START);
        return tr_totitle_ott + iState - TR_TOTITLE_ACCEPTING_STATES_START - 1;
    }
}

#endif /* UNICODE_TABLES_H */
