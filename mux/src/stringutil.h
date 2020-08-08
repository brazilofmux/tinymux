/*! \file stringutil.h
 * \brief string utilities.
 *
 */

#ifndef STRINGUTIL_H
#define STRINGUTIL_H

#define mux_strlen(x)   strlen((const char *)x)

inline bool isEmpty(__in_opt const UTF8 *p)
{
    return ((nullptr == p) || ('\0' == p[0]));
}

extern const bool mux_isprint_ascii[256];
extern const bool mux_isprint_cp437[256];
extern const bool mux_isprint_latin1[256];
extern const bool mux_isprint_latin2[256];
extern const bool mux_isdigit[256];
extern const bool mux_isxdigit[256];
extern const bool mux_isazAZ[256];
extern const bool mux_isalpha[256];
extern const bool mux_isalnum[256];
extern const bool mux_islower_ascii[256];
extern const bool mux_isupper_ascii[256];
extern const bool mux_isspace[256];
extern const bool mux_issecure[256];
extern const bool mux_isescape[256];
extern const unsigned char mux_hex2dec[256];
extern const unsigned char mux_toupper_ascii[256];
extern const unsigned char mux_tolower_ascii[256];
extern const UTF8 TableATOI[16][10];

#define UTF8_SIZE1     1
#define UTF8_SIZE2     2
#define UTF8_SIZE3     3
#define UTF8_SIZE4     4
#define UTF8_CONTINUE  5
#define UTF8_ILLEGAL   6
extern const unsigned char utf8_FirstByte[256];
extern const UTF8 *cp437_utf8[256];
#define cp437_utf8(x) ((const UTF8 *)cp437_utf8[(unsigned char)x])
extern const UTF8 *latin1_utf8[256];
#define latin1_utf8(x) ((const UTF8 *)latin1_utf8[(unsigned char)x])
extern const UTF8 *latin2_utf8[256];
#define latin2_utf8(x) ((const UTF8 *)latin2_utf8[(unsigned char)x])

// This function trims the string back to the first valid UTF-8 sequence it
// finds, but it does not validate the entire string.
//
extern const int g_trimoffset[4][4];
inline size_t TrimPartialSequence(size_t n, __in_ecount(n) const UTF8 *p)
{
    for (size_t i = 0; i < n; i++)
    {
        int j = utf8_FirstByte[p[n-i-1]];
        if (j < UTF8_CONTINUE)
        {
            if (i < 4)
            {
                return n - g_trimoffset[i][j-1];
            }
            else
            {
                return n - i + j - 1;
            }
        }
    }
    return 0;
}

#define mux_isprint_ascii(x) (mux_isprint_ascii[(unsigned char)(x)])
#define mux_isprint_cp437(x) (mux_isprint_cp437[(unsigned char)(x)])
#define mux_isprint_latin1(x) (mux_isprint_latin1[(unsigned char)(x)])
#define mux_isprint_latin2(x) (mux_isprint_latin2[(unsigned char)(x)])
#define mux_isdigit(x) (mux_isdigit[(unsigned char)(x)])
#define mux_isxdigit(x)(mux_isxdigit[(unsigned char)(x)])
#define mux_isazAZ(x)  (mux_isazAZ[(unsigned char)(x)])
#define mux_isalpha(x) (mux_isazAZ[(unsigned char)(x)])
#define mux_isalnum(x) (mux_isalnum[(unsigned char)(x)])
#define mux_islower_ascii(x) (mux_islower_ascii[(unsigned char)(x)])
#define mux_isupper_ascii(x) (mux_isupper_ascii[(unsigned char)(x)])
#define mux_isspace(x) (mux_isspace[(unsigned char)(x)])
#define mux_hex2dec(x) (mux_hex2dec[(unsigned char)(x)])
#define mux_toupper_ascii(x) (mux_toupper_ascii[(unsigned char)(x)])
#define mux_tolower_ascii(x) (mux_tolower_ascii[(unsigned char)(x)])
#define TableATOI(x,y) (TableATOI[(unsigned char)(x)][(unsigned char)(y)])

#define mux_issecure(x)           (mux_issecure[(unsigned char)(x)])
#define mux_isescape(x)           (mux_isescape[(unsigned char)(x)])

#define UNI_EOF ((UTF32)-1)

#define UNI_REPLACEMENT_CHAR ((UTF32)0x0000FFFDUL)
#define UNI_MAX_BMP          ((UTF32)0x0000FFFFUL)
#define UNI_MAX_UTF16        ((UTF32)0x0010FFFFUL)
#define UNI_MAX_UTF32        ((UTF32)0x7FFFFFFFUL)
#define UNI_MAX_LEGAL_UTF32  ((UTF32)0x0010FFFFUL)
#define UNI_SUR_HIGH_START   ((UTF32)0x0000D800UL)
#define UNI_SUR_HIGH_END     ((UTF32)0x0000DBFFUL)
#define UNI_SUR_LOW_START    ((UTF32)0x0000DC00UL)
#define UNI_SUR_LOW_END      ((UTF32)0x0000DFFFUL)
#define UNI_PU1_START        ((UTF32)0x0000E000UL)
#define UNI_PU1_END          ((UTF32)0x0000F8FFUL)
#define UNI_PU2_START        ((UTF32)0x000F0000UL)
#define UNI_PU2_END          ((UTF32)0x000FFFFDUL)
#define UNI_PU3_START        ((UTF32)0x00100000UL)
#define UNI_PU3_END          ((UTF32)0x0010FFFDUL)

#define utf8_NextCodePoint(x)      (x + utf8_FirstByte[(unsigned char)*x])

// utf/cl_Printable.txt
//
inline bool mux_isprint(__in const unsigned char *p)
{
    unsigned short iState = CL_PRINT_START_STATE;
    do
    {
        unsigned char ch = *p++;
        unsigned char iColumn = cl_print_itt[(unsigned char)ch];
        unsigned short iOffset = cl_print_sot[iState];
        for (;;)
        {
            int y = cl_print_sbt[iOffset];
            if (y < 128)
            {
                // RUN phrase.
                //
                if (iColumn < y)
                {
                    iState = cl_print_sbt[iOffset+1];
                    break;
                }
                else
                {
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset += 2;
                }
            }
            else
            {
                // COPY phrase.
                //
                y = 256-y;
                if (iColumn < y)
                {
                    iState = cl_print_sbt[iOffset+iColumn+1];
                    break;
                }
                else
                {
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset = static_cast<unsigned short>(iOffset + y + 1);
                }
            }
        }
    } while (iState < CL_PRINT_ACCEPTING_STATES_START);
    return ((iState - CL_PRINT_ACCEPTING_STATES_START) == 1) ? true : false;
}

// utf/cl_AttrNameInitial.txt
//
inline bool mux_isattrnameinitial(__in const unsigned char *p)
{
    unsigned char iState = CL_ATTRNAMEINITIAL_START_STATE;
    do
    {
        unsigned char ch = *p++;
        unsigned char iColumn = cl_attrnameinitial_itt[(unsigned char)ch];
        unsigned char iOffset = cl_attrnameinitial_sot[iState];
        for (;;)
        {
            int y = cl_attrnameinitial_sbt[iOffset];
            if (y < 128)
            {
                // RUN phrase.
                //
                if (iColumn < y)
                {
                    iState = cl_attrnameinitial_sbt[iOffset+1];
                    break;
                }
                else
                {
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset += 2;
                }
            }
            else
            {
                // COPY phrase.
                //
                y = 256-y;
                if (iColumn < y)
                {
                    iState = cl_attrnameinitial_sbt[iOffset+iColumn+1];
                    break;
                }
                else
                {
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset = static_cast<unsigned char>(iOffset + y + 1);
                }
            }
        }
    } while (iState < CL_ATTRNAMEINITIAL_ACCEPTING_STATES_START);
    return ((iState - CL_ATTRNAMEINITIAL_ACCEPTING_STATES_START) == 1) ? true : false;
}

// utf/cl_AttrName.txt
//
inline bool mux_isattrname(__in const unsigned char *p)
{
    unsigned char iState = CL_ATTRNAME_START_STATE;
    do
    {
        unsigned char ch = *p++;
        unsigned char iColumn = cl_attrname_itt[(unsigned char)ch];
        unsigned char iOffset = cl_attrname_sot[iState];
        for (;;)
        {
            int y = cl_attrname_sbt[iOffset];
            if (y < 128)
            {
                // RUN phrase.
                //
                if (iColumn < y)
                {
                    iState = cl_attrname_sbt[iOffset+1];
                    break;
                }
                else
                {
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset += 2;
                }
            }
            else
            {
                // COPY phrase.
                //
                y = 256-y;
                if (iColumn < y)
                {
                    iState = cl_attrname_sbt[iOffset+iColumn+1];
                    break;
                }
                else
                {
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset = static_cast<unsigned char>(iOffset + y + 1);
                }
            }
        }
    } while (iState < CL_ATTRNAME_ACCEPTING_STATES_START);
    return ((iState - CL_ATTRNAME_ACCEPTING_STATES_START) == 1) ? true : false;
}

// utf/cl_Objectname.txt
//
inline bool mux_isobjectname(__in const unsigned char *p)
{
    unsigned char iState = CL_OBJECTNAME_START_STATE;
    do
    {
        unsigned char ch = *p++;
        unsigned char iColumn = cl_objectname_itt[(unsigned char)ch];
        unsigned short iOffset = cl_objectname_sot[iState];
        for (;;)
        {
            int y = cl_objectname_sbt[iOffset];
            if (y < 128)
            {
                // RUN phrase.
                //
                if (iColumn < y)
                {
                    iState = cl_objectname_sbt[iOffset+1];
                    break;
                }
                else
                {
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset += 2;
                }
            }
            else
            {
                // COPY phrase.
                //
                y = 256-y;
                if (iColumn < y)
                {
                    iState = cl_objectname_sbt[iOffset+iColumn+1];
                    break;
                }
                else
                {
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset = static_cast<unsigned char>(iOffset + y + 1);
                }
            }
        }
    } while (iState < CL_OBJECTNAME_ACCEPTING_STATES_START);
    return ((iState - CL_OBJECTNAME_ACCEPTING_STATES_START) == 1) ? true : false;
}

// utf/cl_PlayerName.txt
//
inline bool mux_isplayername(__in const unsigned char *p)
{
    unsigned char iState = CL_PLAYERNAME_START_STATE;
    do
    {
        unsigned char ch = *p++;
        unsigned char iColumn = cl_playername_itt[(unsigned char)ch];
        unsigned short iOffset = cl_playername_sot[iState];
        for (;;)
        {
            int y = cl_playername_sbt[iOffset];
            if (y < 128)
            {
                // RUN phrase.
                //
                if (iColumn < y)
                {
                    iState = cl_playername_sbt[iOffset+1];
                    break;
                }
                else
                {
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset += 2;
                }
            }
            else
            {
                // COPY phrase.
                //
                y = 256-y;
                if (iColumn < y)
                {
                    iState = cl_playername_sbt[iOffset+iColumn+1];
                    break;
                }
                else
                {
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset = static_cast<unsigned char>(iOffset + y + 1);
                }
            }
        }
    } while (iState < CL_PLAYERNAME_ACCEPTING_STATES_START);
    return ((iState - CL_PLAYERNAME_ACCEPTING_STATES_START) == 1) ? true : false;
}

// utf/cl_8859_1.txt
//
inline bool mux_is8859_1(__in const unsigned char *p)
{
    unsigned char iState = CL_8859_1_START_STATE;
    do
    {
        unsigned char ch = *p++;
        unsigned char iColumn = cl_8859_1_itt[(unsigned char)ch];
        unsigned char iOffset = cl_8859_1_sot[iState];
        for (;;)
        {
            int y = cl_8859_1_sbt[iOffset];
            if (y < 128)
            {
                // RUN phrase.
                //
                if (iColumn < y)
                {
                    iState = cl_8859_1_sbt[iOffset+1];
                    break;
                }
                else
                {
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset += 2;
                }
            }
            else
            {
                // COPY phrase.
                //
                y = 256-y;
                if (iColumn < y)
                {
                    iState = cl_8859_1_sbt[iOffset+iColumn+1];
                    break;
                }
                else
                {
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset = static_cast<unsigned char>(iOffset + y + 1);
                }
            }
        }
    } while (iState < CL_8859_1_ACCEPTING_STATES_START);
    return ((iState - CL_8859_1_ACCEPTING_STATES_START) == 1) ? true : false;
}

// utf/cl_8859_2.txt
//
inline bool mux_is8859_2(__in const unsigned char *p)
{
    unsigned char iState = CL_8859_2_START_STATE;
    do
    {
        unsigned char ch = *p++;
        unsigned char iColumn = cl_8859_2_itt[(unsigned char)ch];
        unsigned char iOffset = cl_8859_2_sot[iState];
        for (;;)
        {
            int y = cl_8859_2_sbt[iOffset];
            if (y < 128)
            {
                // RUN phrase.
                //
                if (iColumn < y)
                {
                    iState = cl_8859_2_sbt[iOffset+1];
                    break;
                }
                else
                {
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset += 2;
                }
            }
            else
            {
                // COPY phrase.
                //
                y = 256-y;
                if (iColumn < y)
                {
                    iState = cl_8859_2_sbt[iOffset+iColumn+1];
                    break;
                }
                else
                {
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset = static_cast<unsigned char>(iOffset + y + 1);
                }
            }
        }
    } while (iState < CL_8859_2_ACCEPTING_STATES_START);
    return ((iState - CL_8859_2_ACCEPTING_STATES_START) == 1) ? true : false;
}

// utf/cl_hangul.txt
//
inline bool mux_ishangul(__in const unsigned char *p)
{
    unsigned char iState = CL_HANGUL_START_STATE;
    do
    {
        unsigned char ch = *p++;
        unsigned char iColumn = cl_hangul_itt[(unsigned char)ch];
        unsigned char iOffset = cl_hangul_sot[iState];
        for (;;)
        {
            int y = cl_hangul_sbt[iOffset];
            if (y < 128)
            {
                // RUN phrase.
                //
                if (iColumn < y)
                {
                    iState = cl_hangul_sbt[iOffset+1];
                    break;
                }
                else
                {
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset += 2;
                }
            }
            else
            {
                // COPY phrase.
                //
                y = 256-y;
                if (iColumn < y)
                {
                    iState = cl_hangul_sbt[iOffset+iColumn+1];
                    break;
                }
                else
                {
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset = static_cast<unsigned char>(iOffset + y + 1);
                }
            }
        }
    } while (iState < CL_HANGUL_ACCEPTING_STATES_START);
    return ((iState - CL_HANGUL_ACCEPTING_STATES_START) == 1) ? true : false;
}

// utf/cl_hiragana.txt
//
inline bool mux_ishiragana(__in const unsigned char *p)
{
    unsigned char iState = CL_HIRAGANA_START_STATE;
    do
    {
        unsigned char ch = *p++;
        unsigned char iColumn = cl_hiragana_itt[(unsigned char)ch];
        unsigned char iOffset = cl_hiragana_sot[iState];
        for (;;)
        {
            int y = cl_hiragana_sbt[iOffset];
            if (y < 128)
            {
                // RUN phrase.
                //
                if (iColumn < y)
                {
                    iState = cl_hiragana_sbt[iOffset+1];
                    break;
                }
                else
                {
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset += 2;
                }
            }
            else
            {
                // COPY phrase.
                //
                y = 256-y;
                if (iColumn < y)
                {
                    iState = cl_hiragana_sbt[iOffset+iColumn+1];
                    break;
                }
                else
                {
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset = static_cast<unsigned char>(iOffset + y + 1);
                }
            }
        }
    } while (iState < CL_HIRAGANA_ACCEPTING_STATES_START);
    return ((iState - CL_HIRAGANA_ACCEPTING_STATES_START) == 1) ? true : false;
}

// utf/cl_kanji.txt
//
inline bool mux_iskanji(__in const unsigned char *p)
{
    unsigned char iState = CL_KANJI_START_STATE;
    do
    {
        unsigned char ch = *p++;
        unsigned char iColumn = cl_kanji_itt[(unsigned char)ch];
        unsigned char iOffset = cl_kanji_sot[iState];
        for (;;)
        {
            int y = cl_kanji_sbt[iOffset];
            if (y < 128)
            {
                // RUN phrase.
                //
                if (iColumn < y)
                {
                    iState = cl_kanji_sbt[iOffset+1];
                    break;
                }
                else
                {
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset += 2;
                }
            }
            else
            {
                // COPY phrase.
                //
                y = 256-y;
                if (iColumn < y)
                {
                    iState = cl_kanji_sbt[iOffset+iColumn+1];
                    break;
                }
                else
                {
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset = static_cast<unsigned char>(iOffset + y + 1);
                }
            }
        }
    } while (iState < CL_KANJI_ACCEPTING_STATES_START);
    return ((iState - CL_KANJI_ACCEPTING_STATES_START) == 1) ? true : false;
}

// utf/cl_katakana.txt
//
inline bool mux_iskatakana(__in const unsigned char *p)
{
    unsigned char iState = CL_KATAKANA_START_STATE;
    do
    {
        unsigned char ch = *p++;
        unsigned char iColumn = cl_katakana_itt[(unsigned char)ch];
        unsigned char iOffset = cl_katakana_sot[iState];
        for (;;)
        {
            int y = cl_katakana_sbt[iOffset];
            if (y < 128)
            {
                // RUN phrase.
                //
                if (iColumn < y)
                {
                    iState = cl_katakana_sbt[iOffset+1];
                    break;
                }
                else
                {
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset += 2;
                }
            }
            else
            {
                // COPY phrase.
                //
                y = 256-y;
                if (iColumn < y)
                {
                    iState = cl_katakana_sbt[iOffset+iColumn+1];
                    break;
                }
                else
                {
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset = static_cast<unsigned char>(iOffset + y + 1);
                }
            }
        }
    } while (iState < CL_KATAKANA_ACCEPTING_STATES_START);
    return ((iState - CL_KATAKANA_ACCEPTING_STATES_START) == 1) ? true : false;
}

// utf/tr_utf8_ascii.txt
//
const UTF8 *ConvertToAscii(__in const UTF8 *pString);

// utf/tr_utf8_cp437.txt
//
const UTF8 *ConvertToCp437(__in const UTF8 *pString);

// utf/tr_utf8_latin1.txt
//
const UTF8 *ConvertToLatin1(__in const UTF8 *pString);

// utf/tr_utf8_latin2.txt
//
const UTF8 *ConvertToLatin2(__in const UTF8 *pString);

// utf/tr_widths.txt
//
int ConsoleWidth(__in const UTF8 *pCodePoint);

// utf/tr_tolower.txt
//
inline const string_desc *mux_tolower(__in const unsigned char *p, bool &bXor)
{
    unsigned char iState = TR_TOLOWER_START_STATE;
    do
    {
        unsigned char ch = *p++;
        unsigned char iColumn = tr_tolower_itt[(unsigned char)ch];
        unsigned short iOffset = tr_tolower_sot[iState];
        for (;;)
        {
            int y = tr_tolower_sbt[iOffset];
            if (y < 128)
            {
                // RUN phrase.
                //
                if (iColumn < y)
                {
                    iState = tr_tolower_sbt[iOffset+1];
                    break;
                }
                else
                {
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset += 2;
                }
            }
            else
            {
                // COPY phrase.
                //
                y = 256-y;
                if (iColumn < y)
                {
                    iState = tr_tolower_sbt[iOffset+iColumn+1];
                    break;
                }
                else
                {
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset = static_cast<unsigned short>(iOffset + y + 1);
                }
            }
        }
    } while (iState < TR_TOLOWER_ACCEPTING_STATES_START);

    if (TR_TOLOWER_DEFAULT == iState - TR_TOLOWER_ACCEPTING_STATES_START)
    {
        bXor = false;
        return nullptr;
    }
    else
    {
        bXor = (TR_TOLOWER_XOR_START <= iState - TR_TOLOWER_ACCEPTING_STATES_START);
        return tr_tolower_ott + iState - TR_TOLOWER_ACCEPTING_STATES_START - 1;
    }
}

// utf/tr_toupper.txt
//
inline const string_desc *mux_toupper(__in const unsigned char *p, bool &bXor)
{
    unsigned char iState = TR_TOUPPER_START_STATE;
    do
    {
        unsigned char ch = *p++;
        unsigned char iColumn = tr_toupper_itt[(unsigned char)ch];
        unsigned short iOffset = tr_toupper_sot[iState];
        for (;;)
        {
            int y = tr_toupper_sbt[iOffset];
            if (y < 128)
            {
                // RUN phrase.
                //
                if (iColumn < y)
                {
                    iState = tr_toupper_sbt[iOffset+1];
                    break;
                }
                else
                {
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset += 2;
                }
            }
            else
            {
                // COPY phrase.
                //
                y = 256-y;
                if (iColumn < y)
                {
                    iState = tr_toupper_sbt[iOffset+iColumn+1];
                    break;
                }
                else
                {
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset = static_cast<unsigned short>(iOffset + y + 1);
                }
            }
        }
    } while (iState < TR_TOUPPER_ACCEPTING_STATES_START);

    if (TR_TOUPPER_DEFAULT == iState - TR_TOUPPER_ACCEPTING_STATES_START)
    {
        bXor = false;
        return nullptr;
    }
    else
    {
        bXor = (TR_TOUPPER_XOR_START <= iState - TR_TOUPPER_ACCEPTING_STATES_START);
        return tr_toupper_ott + iState - TR_TOUPPER_ACCEPTING_STATES_START - 1;
    }
}

// utf/tr_totitle.txt
//
inline const string_desc *mux_totitle(__in const unsigned char *p, bool &bXor)
{
    unsigned char iState = TR_TOTITLE_START_STATE;
    do
    {
        unsigned char ch = *p++;
        unsigned char iColumn = tr_totitle_itt[(unsigned char)ch];
        unsigned short iOffset = tr_totitle_sot[iState];
        for (;;)
        {
            int y = tr_totitle_sbt[iOffset];
            if (y < 128)
            {
                // RUN phrase.
                //
                if (iColumn < y)
                {
                    iState = tr_totitle_sbt[iOffset+1];
                    break;
                }
                else
                {
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset += 2;
                }
            }
            else
            {
                // COPY phrase.
                //
                y = 256-y;
                if (iColumn < y)
                {
                    iState = tr_totitle_sbt[iOffset+iColumn+1];
                    break;
                }
                else
                {
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset = static_cast<unsigned short>(iOffset + y + 1);
                }
            }
        }
    } while (iState < TR_TOTITLE_ACCEPTING_STATES_START);

    if (TR_TOTITLE_DEFAULT == iState - TR_TOTITLE_ACCEPTING_STATES_START)
    {
        bXor = false;
        return nullptr;
    }
    else
    {
        bXor = (TR_TOTITLE_XOR_START <= iState - TR_TOTITLE_ACCEPTING_STATES_START);
        return tr_totitle_ott + iState - TR_TOTITLE_ACCEPTING_STATES_START - 1;
    }
}

// utf/tr_foldpunc.txt
//
inline const string_desc *mux_foldmatch(__in const unsigned char *p, bool &bXor)
{
    int iState = TR_FOLDMATCH_START_STATE;
    do
    {
        unsigned char ch = *p++;
        unsigned char iColumn = tr_foldmatch_itt[(unsigned char)ch];
        unsigned short iOffset = tr_foldmatch_sot[iState];
        for (;;)
        {
            int y = tr_foldmatch_sbt[iOffset];
            if (y < 128)
            {
                // RUN phrase.
                //
                if (iColumn < y)
                {
                    iState = tr_foldmatch_sbt[iOffset+1];
                    break;
                }
                else
                {
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset += 2;
                }
            }
            else
            {
                // COPY phrase.
                //
                y = 256-y;
                if (iColumn < y)
                {
                    iState = tr_foldmatch_sbt[iOffset+iColumn+1];
                    break;
                }
                else
                {
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset = static_cast<unsigned short>(iOffset + y + 1);
                }
            }
        }
    } while (iState < TR_FOLDMATCH_ACCEPTING_STATES_START);

    if (TR_FOLDMATCH_DEFAULT == iState - TR_FOLDMATCH_ACCEPTING_STATES_START)
    {
        bXor = false;
        return nullptr;
    }
    else
    {
        bXor = (TR_FOLDMATCH_XOR_START <= iState - TR_FOLDMATCH_ACCEPTING_STATES_START);
        return tr_foldmatch_ott + iState - TR_FOLDMATCH_ACCEPTING_STATES_START - 1;
    }
}

// utf/tr_Color.txt
//
inline int mux_color(__in const unsigned char *p)
{
    unsigned short iState = TR_COLOR_START_STATE;
    do
    {
        unsigned char ch = *p++;
        unsigned char iColumn = tr_color_itt[(unsigned char)ch];
        unsigned short iOffset = tr_color_sot[iState];
        for (;;)
        {
            int y = tr_color_sbt[iOffset];
            if (y < 128)
            {
                // RUN phrase.
                //
                if (iColumn < y)
                {
                    iState = tr_color_sbt[iOffset+1];
                    break;
                }
                else
                {
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset += 2;
                }
            }
            else
            {
                // COPY phrase.
                //
                y = 256-y;
                if (iColumn < y)
                {
                    iState = tr_color_sbt[iOffset+iColumn+1];
                    break;
                }
                else
                {
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset = static_cast<unsigned short>(iOffset + y + 1);
                }
            }
        }
    } while (iState < TR_COLOR_ACCEPTING_STATES_START);
    return iState - TR_COLOR_ACCEPTING_STATES_START;
}

#define mux_haswidth(x) mux_isprint(x)

bool utf8_strlen(__in const UTF8 *pString, __out size_t &nString);

typedef struct
{
    UTF8 *pString;
    UTF8 aControl[256];
} MUX_STRTOK_STATE;

void mux_strtok_src(__in MUX_STRTOK_STATE *tts, __in UTF8 *pString);
void mux_strtok_ctl(__in MUX_STRTOK_STATE *tts, __in const UTF8 *pControl);
UTF8 *mux_strtok_parseLEN(__in MUX_STRTOK_STATE *tts, __deref_out size_t *pnLen);
UTF8 *mux_strtok_parse(__deref_in MUX_STRTOK_STATE *tts);

// Color State
//
typedef UINT64 ColorState;

// Indexes into the aColors table:
//
// The first entry is not used. The second is reset to normal. The next four
// are the four character attributes. Finally, we see the 256 foreground and
// background colors.
//
#define NUM_OTHER               2
#define NUM_ATTR                4
#define NUM_FG                  256
#define NUM_BG                  256

#define COLOR_INDEX_ATTR        (NUM_OTHER)
#define COLOR_INDEX_FG          (COLOR_INDEX_ATTR + NUM_ATTR)
#define COLOR_INDEX_BG          (COLOR_INDEX_FG + NUM_FG)
#define COLOR_INDEX_FG_24       (COLOR_INDEX_BG + NUM_BG)
#define COLOR_INDEX_FG_24_RED   (COLOR_INDEX_FG_24)
#define COLOR_INDEX_FG_24_GREEN (COLOR_INDEX_FG_24_RED   + 256)
#define COLOR_INDEX_FG_24_BLUE  (COLOR_INDEX_FG_24_GREEN + 256)
#define COLOR_INDEX_BG_24       (COLOR_INDEX_FG_24_BLUE  + 256)
#define COLOR_INDEX_BG_24_RED   (COLOR_INDEX_BG_24)
#define COLOR_INDEX_BG_24_GREEN (COLOR_INDEX_BG_24_RED   + 256)
#define COLOR_INDEX_BG_24_BLUE  (COLOR_INDEX_BG_24_GREEN + 256)
#define COLOR_INDEX_LAST        (COLOR_INDEX_BG_24_BLUE  + 256 - 1)

#define COLOR_INDEX_RESET       1
#define COLOR_INDEX_INTENSE     (COLOR_INDEX_ATTR + 0)
#define COLOR_INDEX_UNDERLINE   (COLOR_INDEX_ATTR + 1)
#define COLOR_INDEX_BLINK       (COLOR_INDEX_ATTR + 2)
#define COLOR_INDEX_INVERSE     (COLOR_INDEX_ATTR + 3)

#define COLOR_INDEX_BLACK       0
#define COLOR_INDEX_RED         1
#define COLOR_INDEX_GREEN       2
#define COLOR_INDEX_YELLOW      3
#define COLOR_INDEX_BLUE        4
#define COLOR_INDEX_MAGENTA     5
#define COLOR_INDEX_CYAN        6
#define COLOR_INDEX_WHITE       7
#define COLOR_INDEX_DEFAULT     (NUM_FG)

#define COLOR_NOTCOLOR   0
#define COLOR_RESET      "\xEF\x94\x80"    // 1
#define COLOR_INTENSE    "\xEF\x94\x81"    // 2
#define COLOR_UNDERLINE  "\xEF\x94\x84"    // 3
#define COLOR_BLINK      "\xEF\x94\x85"    // 4
#define COLOR_INVERSE    "\xEF\x94\x87"    // 5

#define COLOR_FG_BLACK   "\xEF\x98\x80"    // 6
#define COLOR_FG_RED     "\xEF\x98\x81"    // 7
#define COLOR_FG_GREEN   "\xEF\x98\x82"    // 8
#define COLOR_FG_YELLOW  "\xEF\x98\x83"    // 9
#define COLOR_FG_BLUE    "\xEF\x98\x84"    // 10
#define COLOR_FG_MAGENTA "\xEF\x98\x85"    // 11
#define COLOR_FG_CYAN    "\xEF\x98\x86"    // 12
#define COLOR_FG_WHITE   "\xEF\x98\x87"    // 13
#define COLOR_FG_555555  "\xEF\x98\x88"    // 14
#define COLOR_FG_FF5555  "\xEF\x98\x89"    // 15
#define COLOR_FG_55FF55  "\xEF\x98\x8A"    // 16
#define COLOR_FG_FFFF55  "\xEF\x98\x8B"    // 17
#define COLOR_FG_5555FF  "\xEF\x98\x8C"    // 18
#define COLOR_FG_FF55FF  "\xEF\x98\x8D"    // 19
#define COLOR_FG_55FFFF  "\xEF\x98\x8E"    // 20
#define COLOR_FG_FFFFFF_1 "\xEF\x98\x8F"    // 21
#define COLOR_FG_000000  "\xEF\x98\x90"    // 22
#define COLOR_FG_00005F  "\xEF\x98\x91"    // 23
#define COLOR_FG_000087  "\xEF\x98\x92"    // 24
#define COLOR_FG_0000AF  "\xEF\x98\x93"    // 25
#define COLOR_FG_0000D7  "\xEF\x98\x94"    // 26
#define COLOR_FG_0000FF  "\xEF\x98\x95"    // 27
#define COLOR_FG_005F00  "\xEF\x98\x96"    // 28
#define COLOR_FG_005F5F  "\xEF\x98\x97"    // 29
#define COLOR_FG_005F87  "\xEF\x98\x98"    // 30
#define COLOR_FG_005FAF  "\xEF\x98\x99"    // 31
#define COLOR_FG_005FD7  "\xEF\x98\x9A"    // 32
#define COLOR_FG_005FFF  "\xEF\x98\x9B"    // 33
#define COLOR_FG_008700  "\xEF\x98\x9C"    // 34
#define COLOR_FG_00875F  "\xEF\x98\x9D"    // 35
#define COLOR_FG_008785  "\xEF\x98\x9E"    // 36
#define COLOR_FG_0087AF  "\xEF\x98\x9F"    // 37
#define COLOR_FG_0087D7  "\xEF\x98\xA0"    // 38
#define COLOR_FG_0087FF  "\xEF\x98\xA1"    // 39
#define COLOR_FG_00AF00  "\xEF\x98\xA2"    // 40
#define COLOR_FG_00AF5F  "\xEF\x98\xA3"    // 41
#define COLOR_FG_00AF87  "\xEF\x98\xA4"    // 42
#define COLOR_FG_00AFAF  "\xEF\x98\xA5"    // 43
#define COLOR_FG_00AFD7  "\xEF\x98\xA6"    // 44
#define COLOR_FG_00AFFF  "\xEF\x98\xA7"    // 45
#define COLOR_FG_00D700  "\xEF\x98\xA8"    // 46
#define COLOR_FG_00D75F  "\xEF\x98\xA9"    // 47
#define COLOR_FG_00D787  "\xEF\x98\xAA"    // 48
#define COLOR_FG_00D7AF  "\xEF\x98\xAB"    // 49
#define COLOR_FG_00D7D7  "\xEF\x98\xAC"    // 50
#define COLOR_FG_00D7FF  "\xEF\x98\xAD"    // 51
#define COLOR_FG_00FF00  "\xEF\x98\xAE"    // 52
#define COLOR_FG_00FF5A  "\xEF\x98\xAF"    // 53
#define COLOR_FG_00FF87  "\xEF\x98\xB0"    // 54
#define COLOR_FG_00FFAF  "\xEF\x98\xB1"    // 55
#define COLOR_FG_00FFD7  "\xEF\x98\xB2"    // 56
#define COLOR_FG_00FFFF  "\xEF\x98\xB3"    // 57
#define COLOR_FG_5F0000  "\xEF\x98\xB4"    // 58
#define COLOR_FG_5F005F  "\xEF\x98\xB5"    // 59
#define COLOR_FG_5F0087  "\xEF\x98\xB6"    // 60
#define COLOR_FG_5F00AF  "\xEF\x98\xB7"    // 61
#define COLOR_FG_5F00D7  "\xEF\x98\xB8"    // 62
#define COLOR_FG_5F00FF  "\xEF\x98\xB9"    // 63
#define COLOR_FG_5F5F00  "\xEF\x98\xBA"    // 64
#define COLOR_FG_5F5F5F  "\xEF\x98\xBB"    // 65
#define COLOR_FG_5F5F87  "\xEF\x98\xBC"    // 66
#define COLOR_FG_5F5FAF  "\xEF\x98\xBD"    // 67
#define COLOR_FG_5F5FD7  "\xEF\x98\xBE"    // 68
#define COLOR_FG_5F5FFF  "\xEF\x98\xBF"    // 69
#define COLOR_FG_5F8700  "\xEF\x99\x80"    // 70
#define COLOR_FG_5F875F  "\xEF\x99\x81"    // 71
#define COLOR_FG_5F8787  "\xEF\x99\x82"    // 72
#define COLOR_FG_5F87AF  "\xEF\x99\x83"    // 73
#define COLOR_FG_5F87D7  "\xEF\x99\x84"    // 74
#define COLOR_FG_5F87FF  "\xEF\x99\x85"    // 75
#define COLOR_FG_5FAF00  "\xEF\x99\x86"    // 76
#define COLOR_FG_5FAF5F  "\xEF\x99\x87"    // 77
#define COLOR_FG_5FAF87  "\xEF\x99\x88"    // 78
#define COLOR_FG_5FAFAF  "\xEF\x99\x89"    // 79
#define COLOR_FG_5FAFD7  "\xEF\x99\x8A"    // 80
#define COLOR_FG_5FAFFF  "\xEF\x99\x8B"    // 81
#define COLOR_FG_5FD700  "\xEF\x99\x8C"    // 82
#define COLOR_FG_5FD75F  "\xEF\x99\x8D"    // 83
#define COLOR_FG_5FD787  "\xEF\x99\x8E"    // 84
#define COLOR_FG_5FD7AF  "\xEF\x99\x8F"    // 85
#define COLOR_FG_5FD7D7  "\xEF\x99\x90"    // 86
#define COLOR_FG_5FD7FF  "\xEF\x99\x91"    // 87
#define COLOR_FG_5FFF00  "\xEF\x99\x92"    // 88
#define COLOR_FG_5FFF5F  "\xEF\x99\x93"    // 89
#define COLOR_FG_5FFF87  "\xEF\x99\x94"    // 90
#define COLOR_FG_5FFFAF  "\xEF\x99\x95"    // 91
#define COLOR_FG_5FFFD7  "\xEF\x99\x96"    // 92
#define COLOR_FG_5FFFFF  "\xEF\x99\x97"    // 93
#define COLOR_FG_870000  "\xEF\x99\x98"    // 94
#define COLOR_FG_87005F  "\xEF\x99\x99"    // 95
#define COLOR_FG_870087  "\xEF\x99\x9A"    // 96
#define COLOR_FG_8700AF  "\xEF\x99\x9B"    // 97
#define COLOR_FG_8700D7  "\xEF\x99\x9C"    // 98
#define COLOR_FG_8700FF  "\xEF\x99\x9D"    // 99
#define COLOR_FG_875F00  "\xEF\x99\x9E"    // 100
#define COLOR_FG_875F5F  "\xEF\x99\x9F"    // 101
#define COLOR_FG_875F87  "\xEF\x99\xA0"    // 102
#define COLOR_FG_875FAF  "\xEF\x99\xA1"    // 103
#define COLOR_FG_875FD7  "\xEF\x99\xA2"    // 104
#define COLOR_FG_875FFF  "\xEF\x99\xA3"    // 105
#define COLOR_FG_878700  "\xEF\x99\xA4"    // 106
#define COLOR_FG_87875F  "\xEF\x99\xA5"    // 107
#define COLOR_FG_878787  "\xEF\x99\xA6"    // 108
#define COLOR_FG_8787AF  "\xEF\x99\xA7"    // 109
#define COLOR_FG_8787D7  "\xEF\x99\xA8"    // 110
#define COLOR_FG_8787FF  "\xEF\x99\xA9"    // 111
#define COLOR_FG_87AF00  "\xEF\x99\xAA"    // 112
#define COLOR_FG_87AF5F  "\xEF\x99\xAB"    // 113
#define COLOR_FG_87AF87  "\xEF\x99\xAC"    // 114
#define COLOR_FG_87AFAF  "\xEF\x99\xAD"    // 115
#define COLOR_FG_87AFD7  "\xEF\x99\xAE"    // 116
#define COLOR_FG_87AFFF  "\xEF\x99\xAF"    // 117
#define COLOR_FG_87D700  "\xEF\x99\xB0"    // 118
#define COLOR_FG_87D75A  "\xEF\x99\xB1"    // 119
#define COLOR_FG_87D787  "\xEF\x99\xB2"    // 120
#define COLOR_FG_87D7AF  "\xEF\x99\xB3"    // 121
#define COLOR_FG_87D7D7  "\xEF\x99\xB4"    // 122
#define COLOR_FG_87D7FF  "\xEF\x99\xB5"    // 123
#define COLOR_FG_87FF00  "\xEF\x99\xB6"    // 124
#define COLOR_FG_87FF5F  "\xEF\x99\xB7"    // 125
#define COLOR_FG_87FF87  "\xEF\x99\xB8"    // 126
#define COLOR_FG_87FFAF  "\xEF\x99\xB9"    // 127
#define COLOR_FG_87FFD7  "\xEF\x99\xBA"    // 128
#define COLOR_FG_87FFFF  "\xEF\x99\xBB"    // 129
#define COLOR_FG_AF0000  "\xEF\x99\xBC"    // 130
#define COLOR_FG_AF005F  "\xEF\x99\xBD"    // 131
#define COLOR_FG_AF0087  "\xEF\x99\xBE"    // 132
#define COLOR_FG_AF00AF  "\xEF\x99\xBF"    // 133
#define COLOR_FG_AF00D7  "\xEF\x9A\x80"    // 134
#define COLOR_FG_AF00FF  "\xEF\x9A\x81"    // 135
#define COLOR_FG_AF5F00  "\xEF\x9A\x82"    // 136
#define COLOR_FG_AF5F5F  "\xEF\x9A\x83"    // 137
#define COLOR_FG_AF5F87  "\xEF\x9A\x84"    // 138
#define COLOR_FG_AF5FAF  "\xEF\x9A\x85"    // 139
#define COLOR_FG_AF5FD7  "\xEF\x9A\x86"    // 140
#define COLOR_FG_AF5FFF  "\xEF\x9A\x87"    // 141
#define COLOR_FG_AF8700  "\xEF\x9A\x88"    // 142
#define COLOR_FG_AF875F  "\xEF\x9A\x89"    // 143
#define COLOR_FG_AF8787  "\xEF\x9A\x8A"    // 144
#define COLOR_FG_AF87AF  "\xEF\x9A\x8B"    // 145
#define COLOR_FG_AF87D7  "\xEF\x9A\x8C"    // 146
#define COLOR_FG_AF87FF  "\xEF\x9A\x8D"    // 147
#define COLOR_FG_AFAF00  "\xEF\x9A\x8E"    // 148
#define COLOR_FG_AFAF5F  "\xEF\x9A\x8F"    // 149
#define COLOR_FG_AFAF87  "\xEF\x9A\x90"    // 150
#define COLOR_FG_AFAFAF  "\xEF\x9A\x91"    // 151
#define COLOR_FG_AFAFD7  "\xEF\x9A\x92"    // 152
#define COLOR_FG_AFAFFF  "\xEF\x9A\x93"    // 153
#define COLOR_FG_AFD700  "\xEF\x9A\x94"    // 154
#define COLOR_FG_AFD75F  "\xEF\x9A\x95"    // 155
#define COLOR_FG_AFD787  "\xEF\x9A\x96"    // 156
#define COLOR_FG_AFD7AF  "\xEF\x9A\x97"    // 157
#define COLOR_FG_AFD7D7  "\xEF\x9A\x98"    // 158
#define COLOR_FG_AFD7FF  "\xEF\x9A\x99"    // 159
#define COLOR_FG_AFFF00  "\xEF\x9A\x9A"    // 160
#define COLOR_FG_AFFF5F  "\xEF\x9A\x9B"    // 161
#define COLOR_FG_AFFF87  "\xEF\x9A\x9C"    // 162
#define COLOR_FG_AFFFAF  "\xEF\x9A\x9D"    // 163
#define COLOR_FG_AFFFD7  "\xEF\x9A\x9E"    // 164
#define COLOR_FG_AFFFFF  "\xEF\x9A\x9F"    // 165
#define COLOR_FG_D70000  "\xEF\x9A\xA0"    // 166
#define COLOR_FG_D7005F  "\xEF\x9A\xA1"    // 167
#define COLOR_FG_D70087  "\xEF\x9A\xA2"    // 168
#define COLOR_FG_D700AF  "\xEF\x9A\xA3"    // 169
#define COLOR_FG_D700D7  "\xEF\x9A\xA4"    // 170
#define COLOR_FG_D700FF  "\xEF\x9A\xA5"    // 171
#define COLOR_FG_D75F00  "\xEF\x9A\xA6"    // 172
#define COLOR_FG_D75F5F  "\xEF\x9A\xA7"    // 173
#define COLOR_FG_D75F87  "\xEF\x9A\xA8"    // 174
#define COLOR_FG_D75FAF  "\xEF\x9A\xA9"    // 175
#define COLOR_FG_D75FD7  "\xEF\x9A\xAA"    // 176
#define COLOR_FG_D75FFF  "\xEF\x9A\xAB"    // 177
#define COLOR_FG_D78700  "\xEF\x9A\xAC"    // 178
#define COLOR_FG_D7875A  "\xEF\x9A\xAD"    // 179
#define COLOR_FG_D78787  "\xEF\x9A\xAE"    // 180
#define COLOR_FG_D787AF  "\xEF\x9A\xAF"    // 181
#define COLOR_FG_D787D7  "\xEF\x9A\xB0"    // 182
#define COLOR_FG_D787FF  "\xEF\x9A\xB1"    // 183
#define COLOR_FG_D7AF00  "\xEF\x9A\xB2"    // 184
#define COLOR_FG_D7AF5A  "\xEF\x9A\xB3"    // 185
#define COLOR_FG_D7AF87  "\xEF\x9A\xB4"    // 186
#define COLOR_FG_D7AFAF  "\xEF\x9A\xB5"    // 187
#define COLOR_FG_D7AFD7  "\xEF\x9A\xB6"    // 188
#define COLOR_FG_D7AFFF  "\xEF\x9A\xB7"    // 189
#define COLOR_FG_D7D700  "\xEF\x9A\xB8"    // 190
#define COLOR_FG_D7D75F  "\xEF\x9A\xB9"    // 191
#define COLOR_FG_D7D787  "\xEF\x9A\xBA"    // 192
#define COLOR_FG_D7D7AF  "\xEF\x9A\xBB"    // 193
#define COLOR_FG_D7D7D7  "\xEF\x9A\xBC"    // 194
#define COLOR_FG_D7D7FF  "\xEF\x9A\xBD"    // 195
#define COLOR_FG_D7FF00  "\xEF\x9A\xBE"    // 196
#define COLOR_FG_D7FF5F  "\xEF\x9A\xBF"    // 197
#define COLOR_FG_D7FF87  "\xEF\x9B\x80"    // 198
#define COLOR_FG_D7FFAF  "\xEF\x9B\x81"    // 199
#define COLOR_FG_D7FFD7  "\xEF\x9B\x82"    // 200
#define COLOR_FG_D7FFFF  "\xEF\x9B\x83"    // 201
#define COLOR_FG_FF0000  "\xEF\x9B\x84"    // 202
#define COLOR_FG_FF005F  "\xEF\x9B\x85"    // 203
#define COLOR_FG_FF0087  "\xEF\x9B\x86"    // 204
#define COLOR_FG_FF00AF  "\xEF\x9B\x87"    // 205
#define COLOR_FG_FF00D7  "\xEF\x9B\x88"    // 206
#define COLOR_FG_FF00FF  "\xEF\x9B\x89"    // 207
#define COLOR_FG_FF5F00  "\xEF\x9B\x8A"    // 208
#define COLOR_FG_FF5F5F  "\xEF\x9B\x8B"    // 209
#define COLOR_FG_FF5F87  "\xEF\x9B\x8C"    // 210
#define COLOR_FG_FF5FAF  "\xEF\x9B\x8D"    // 211
#define COLOR_FG_FF5FD7  "\xEF\x9B\x8E"    // 212
#define COLOR_FG_FF5FFF  "\xEF\x9B\x8F"    // 213
#define COLOR_FG_FF8700  "\xEF\x9B\x90"    // 214
#define COLOR_FG_FF875F  "\xEF\x9B\x91"    // 215
#define COLOR_FG_FF8787  "\xEF\x9B\x92"    // 216
#define COLOR_FG_FF87AF  "\xEF\x9B\x93"    // 217
#define COLOR_FG_FF87D7  "\xEF\x9B\x94"    // 218
#define COLOR_FG_FF87FF  "\xEF\x9B\x95"    // 219
#define COLOR_FG_FFAF00  "\xEF\x9B\x96"    // 220
#define COLOR_FG_FFAF5F  "\xEF\x9B\x97"    // 221
#define COLOR_FG_FFAF87  "\xEF\x9B\x98"    // 222
#define COLOR_FG_FFAFAF  "\xEF\x9B\x99"    // 223
#define COLOR_FG_FFAFD7  "\xEF\x9B\x9A"    // 224
#define COLOR_FG_FFAFFF  "\xEF\x9B\x9B"    // 225
#define COLOR_FG_FFD700  "\xEF\x9B\x9C"    // 226
#define COLOR_FG_FFD75F  "\xEF\x9B\x9D"    // 227
#define COLOR_FG_FFD787  "\xEF\x9B\x9E"    // 228
#define COLOR_FG_FFD7AF  "\xEF\x9B\x9F"    // 229
#define COLOR_FG_FFD7D7  "\xEF\x9B\xA0"    // 230
#define COLOR_FG_FFD7FF  "\xEF\x9B\xA1"    // 231
#define COLOR_FG_FFFF00  "\xEF\x9B\xA2"    // 232
#define COLOR_FG_FFFF5F  "\xEF\x9B\xA3"    // 233
#define COLOR_FG_FFFF87  "\xEF\x9B\xA4"    // 234
#define COLOR_FG_FFFFAF  "\xEF\x9B\xA5"    // 235
#define COLOR_FG_FFFFD7  "\xEF\x9B\xA6"    // 236
#define COLOR_FG_FFFFFF_2 "\xEF\x9B\xA7"   // 237
#define COLOR_FG_080808  "\xEF\x9B\xA8"    // 238
#define COLOR_FG_121212  "\xEF\x9B\xA9"    // 239
#define COLOR_FG_1C1C1C  "\xEF\x9B\xAA"    // 240
#define COLOR_FG_262626  "\xEF\x9B\xAB"    // 241
#define COLOR_FG_303030  "\xEF\x9B\xAC"    // 242
#define COLOR_FG_3A3A3A  "\xEF\x9B\xAD"    // 243
#define COLOR_FG_444444  "\xEF\x9B\xAE"    // 244
#define COLOR_FG_4E4E4E  "\xEF\x9B\xAF"    // 245
#define COLOR_FG_585858  "\xEF\x9B\xB0"    // 246
#define COLOR_FG_626262  "\xEF\x9B\xB1"    // 247
#define COLOR_FG_6C6C6C  "\xEF\x9B\xB2"    // 248
#define COLOR_FG_767676  "\xEF\x9B\xB3"    // 249
#define COLOR_FG_808080  "\xEF\x9B\xB4"    // 250
#define COLOR_FG_8A8A8A  "\xEF\x9B\xB5"    // 251
#define COLOR_FG_949494  "\xEF\x9B\xB6"    // 252
#define COLOR_FG_9E9E9E  "\xEF\x9B\xB7"    // 253
#define COLOR_FG_A8A8A8  "\xEF\x9B\xB8"    // 254
#define COLOR_FG_B2B2B2  "\xEF\x9B\xB9"    // 255
#define COLOR_FG_BCBCBC  "\xEF\x9B\xBA"    // 256
#define COLOR_FG_C6C6C6  "\xEF\x9B\xBB"    // 257
#define COLOR_FG_D0D0D0  "\xEF\x9B\xBC"    // 258
#define COLOR_FG_DADADA  "\xEF\x9B\xBD"    // 259
#define COLOR_FG_E4E4E4  "\xEF\x9B\xBE"    // 260
#define COLOR_FG_EEEEEE  "\xEF\x9B\xBF"    // 261

#define COLOR_BG_BLACK   "\xEF\x9C\x80"    // 262
#define COLOR_BG_RED     "\xEF\x9C\x81"    // 263
#define COLOR_BG_GREEN   "\xEF\x9C\x82"    // 264
#define COLOR_BG_YELLOW  "\xEF\x9C\x83"    // 265
#define COLOR_BG_BLUE    "\xEF\x9C\x84"    // 266
#define COLOR_BG_MAGENTA "\xEF\x9C\x85"    // 267
#define COLOR_BG_CYAN    "\xEF\x9C\x86"    // 268
#define COLOR_BG_WHITE   "\xEF\x9C\x87"    // 269
#define COLOR_BG_555555  "\xEF\x9C\x88"    // 270
#define COLOR_BG_FF5555  "\xEF\x9C\x89"    // 271
#define COLOR_BG_55FF55  "\xEF\x9C\x8A"    // 272
#define COLOR_BG_FFFF55  "\xEF\x9C\x8B"    // 273
#define COLOR_BG_5555FF  "\xEF\x9C\x8C"    // 274
#define COLOR_BG_FF55FF  "\xEF\x9C\x8D"    // 275
#define COLOR_BG_55FFFF  "\xEF\x9C\x8E"    // 276
#define COLOR_BG_FFFFFF_1 "\xEF\x9C\x8F"   // 277
#define COLOR_BG_000000  "\xEF\x9C\x90"    // 278
#define COLOR_BG_00005F  "\xEF\x9C\x91"    // 279
#define COLOR_BG_000087  "\xEF\x9C\x92"    // 280
#define COLOR_BG_0000AF  "\xEF\x9C\x93"    // 281
#define COLOR_BG_0000D7  "\xEF\x9C\x94"    // 282
#define COLOR_BG_0000FF  "\xEF\x9C\x95"    // 283
#define COLOR_BG_005F00  "\xEF\x9C\x96"    // 284
#define COLOR_BG_005F5F  "\xEF\x9C\x97"    // 285
#define COLOR_BG_005F87  "\xEF\x9C\x98"    // 286
#define COLOR_BG_005FAF  "\xEF\x9C\x99"    // 287
#define COLOR_BG_005FD7  "\xEF\x9C\x9A"    // 288
#define COLOR_BG_005FFF  "\xEF\x9C\x9B"    // 289
#define COLOR_BG_008700  "\xEF\x9C\x9C"    // 290
#define COLOR_BG_00875F  "\xEF\x9C\x9D"    // 291
#define COLOR_BG_008785  "\xEF\x9C\x9E"    // 292
#define COLOR_BG_0087AF  "\xEF\x9C\x9F"    // 293
#define COLOR_BG_0087D7  "\xEF\x9C\xA0"    // 294
#define COLOR_BG_0087FF  "\xEF\x9C\xA1"    // 295
#define COLOR_BG_00AF00  "\xEF\x9C\xA2"    // 296
#define COLOR_BG_00AF5F  "\xEF\x9C\xA3"    // 297
#define COLOR_BG_00AF87  "\xEF\x9C\xA4"    // 298
#define COLOR_BG_00AFAF  "\xEF\x9C\xA5"    // 299
#define COLOR_BG_00AFD7  "\xEF\x9C\xA6"    // 300
#define COLOR_BG_00AFFF  "\xEF\x9C\xA7"    // 301
#define COLOR_BG_00D700  "\xEF\x9C\xA8"    // 302
#define COLOR_BG_00D75F  "\xEF\x9C\xA9"    // 303
#define COLOR_BG_00D787  "\xEF\x9C\xAA"    // 304
#define COLOR_BG_00D7AF  "\xEF\x9C\xAB"    // 305
#define COLOR_BG_00D7D7  "\xEF\x9C\xAC"    // 306
#define COLOR_BG_00D7FF  "\xEF\x9C\xAD"    // 307
#define COLOR_BG_00FF00  "\xEF\x9C\xAE"    // 308
#define COLOR_BG_00FF5A  "\xEF\x9C\xAF"    // 309
#define COLOR_BG_00FF87  "\xEF\x9C\xB0"    // 310
#define COLOR_BG_00FFAF  "\xEF\x9C\xB1"    // 311
#define COLOR_BG_00FFD7  "\xEF\x9C\xB2"    // 312
#define COLOR_BG_00FFFF  "\xEF\x9C\xB3"    // 313
#define COLOR_BG_5F0000  "\xEF\x9C\xB4"    // 314
#define COLOR_BG_5F005F  "\xEF\x9C\xB5"    // 315
#define COLOR_BG_5F0087  "\xEF\x9C\xB6"    // 316
#define COLOR_BG_5F00AF  "\xEF\x9C\xB7"    // 317
#define COLOR_BG_5F00D7  "\xEF\x9C\xB8"    // 318
#define COLOR_BG_5F00FF  "\xEF\x9C\xB9"    // 319
#define COLOR_BG_5F5F00  "\xEF\x9C\xBA"    // 320
#define COLOR_BG_5F5F5F  "\xEF\x9C\xBB"    // 321
#define COLOR_BG_5F5F87  "\xEF\x9C\xBC"    // 322
#define COLOR_BG_5F5FAF  "\xEF\x9C\xBD"    // 323
#define COLOR_BG_5F5FD7  "\xEF\x9C\xBE"    // 324
#define COLOR_BG_5F5FFF  "\xEF\x9C\xBF"    // 325
#define COLOR_BG_5F8700  "\xEF\x9D\x80"    // 326
#define COLOR_BG_5F875F  "\xEF\x9D\x81"    // 327
#define COLOR_BG_5F8787  "\xEF\x9D\x82"    // 328
#define COLOR_BG_5F87AF  "\xEF\x9D\x83"    // 329
#define COLOR_BG_5F87D7  "\xEF\x9D\x84"    // 330
#define COLOR_BG_5F87FF  "\xEF\x9D\x85"    // 331
#define COLOR_BG_5FAF00  "\xEF\x9D\x86"    // 332
#define COLOR_BG_5FAF5F  "\xEF\x9D\x87"    // 333
#define COLOR_BG_5FAF87  "\xEF\x9D\x88"    // 334
#define COLOR_BG_5FAFAF  "\xEF\x9D\x89"    // 335
#define COLOR_BG_5FAFD7  "\xEF\x9D\x8A"    // 336
#define COLOR_BG_5FAFFF  "\xEF\x9D\x8B"    // 337
#define COLOR_BG_5FD700  "\xEF\x9D\x8C"    // 338
#define COLOR_BG_5FD75F  "\xEF\x9D\x8D"    // 339
#define COLOR_BG_5FD787  "\xEF\x9D\x8E"    // 340
#define COLOR_BG_5FD7AF  "\xEF\x9D\x8F"    // 341
#define COLOR_BG_5FD7D7  "\xEF\x9D\x90"    // 342
#define COLOR_BG_5FD7FF  "\xEF\x9D\x91"    // 343
#define COLOR_BG_5FFF00  "\xEF\x9D\x92"    // 344
#define COLOR_BG_5FFF5F  "\xEF\x9D\x93"    // 345
#define COLOR_BG_5FFF87  "\xEF\x9D\x94"    // 346
#define COLOR_BG_5FFFAF  "\xEF\x9D\x95"    // 347
#define COLOR_BG_5FFFD7  "\xEF\x9D\x96"    // 348
#define COLOR_BG_5FFFFF  "\xEF\x9D\x97"    // 349
#define COLOR_BG_870000  "\xEF\x9D\x98"    // 350
#define COLOR_BG_87005F  "\xEF\x9D\x99"    // 351
#define COLOR_BG_870087  "\xEF\x9D\x9A"    // 352
#define COLOR_BG_8700AF  "\xEF\x9D\x9B"    // 353
#define COLOR_BG_8700D7  "\xEF\x9D\x9C"    // 354
#define COLOR_BG_8700FF  "\xEF\x9D\x9D"    // 355
#define COLOR_BG_875F00  "\xEF\x9D\x9E"    // 356
#define COLOR_BG_875F5F  "\xEF\x9D\x9F"    // 357
#define COLOR_BG_875F87  "\xEF\x9D\xA0"    // 358
#define COLOR_BG_875FAF  "\xEF\x9D\xA1"    // 359
#define COLOR_BG_875FD7  "\xEF\x9D\xA2"    // 360
#define COLOR_BG_875FFF  "\xEF\x9D\xA3"    // 361
#define COLOR_BG_878700  "\xEF\x9D\xA4"    // 362
#define COLOR_BG_87875F  "\xEF\x9D\xA5"    // 363
#define COLOR_BG_878787  "\xEF\x9D\xA6"    // 364
#define COLOR_BG_8787AF  "\xEF\x9D\xA7"    // 365
#define COLOR_BG_8787D7  "\xEF\x9D\xA8"    // 366
#define COLOR_BG_8787FF  "\xEF\x9D\xA9"    // 367
#define COLOR_BG_87AF00  "\xEF\x9D\xAA"    // 368
#define COLOR_BG_87AF5F  "\xEF\x9D\xAB"    // 369
#define COLOR_BG_87AF87  "\xEF\x9D\xAC"    // 370
#define COLOR_BG_87AFAF  "\xEF\x9D\xAD"    // 371
#define COLOR_BG_87AFD7  "\xEF\x9D\xAE"    // 372
#define COLOR_BG_87AFFF  "\xEF\x9D\xAF"    // 373
#define COLOR_BG_87D700  "\xEF\x9D\xB0"    // 374
#define COLOR_BG_87D75A  "\xEF\x9D\xB1"    // 375
#define COLOR_BG_87D787  "\xEF\x9D\xB2"    // 376
#define COLOR_BG_87D7AF  "\xEF\x9D\xB3"    // 377
#define COLOR_BG_87D7D7  "\xEF\x9D\xB4"    // 378
#define COLOR_BG_87D7FF  "\xEF\x9D\xB5"    // 379
#define COLOR_BG_87FF00  "\xEF\x9D\xB6"    // 380
#define COLOR_BG_87FF5F  "\xEF\x9D\xB7"    // 381
#define COLOR_BG_87FF87  "\xEF\x9D\xB8"    // 382
#define COLOR_BG_87FFAF  "\xEF\x9D\xB9"    // 383
#define COLOR_BG_87FFD7  "\xEF\x9D\xBA"    // 384
#define COLOR_BG_87FFFF  "\xEF\x9D\xBB"    // 385
#define COLOR_BG_AF0000  "\xEF\x9D\xBC"    // 386
#define COLOR_BG_AF005F  "\xEF\x9D\xBD"    // 387
#define COLOR_BG_AF0087  "\xEF\x9D\xBE"    // 388
#define COLOR_BG_AF00AF  "\xEF\x9D\xBF"    // 389
#define COLOR_BG_AF00D7  "\xEF\x9E\x80"    // 390
#define COLOR_BG_AF00FF  "\xEF\x9E\x81"    // 391
#define COLOR_BG_AF5F00  "\xEF\x9E\x82"    // 392
#define COLOR_BG_AF5F5F  "\xEF\x9E\x83"    // 393
#define COLOR_BG_AF5F87  "\xEF\x9E\x84"    // 394
#define COLOR_BG_AF5FAF  "\xEF\x9E\x85"    // 395
#define COLOR_BG_AF5FD7  "\xEF\x9E\x86"    // 396
#define COLOR_BG_AF5FFF  "\xEF\x9E\x87"    // 397
#define COLOR_BG_AF8700  "\xEF\x9E\x88"    // 398
#define COLOR_BG_AF875F  "\xEF\x9E\x89"    // 399
#define COLOR_BG_AF8787  "\xEF\x9E\x8A"    // 400
#define COLOR_BG_AF87AF  "\xEF\x9E\x8B"    // 401
#define COLOR_BG_AF87D7  "\xEF\x9E\x8C"    // 402
#define COLOR_BG_AF87FF  "\xEF\x9E\x8D"    // 403
#define COLOR_BG_AFAF00  "\xEF\x9E\x8E"    // 404
#define COLOR_BG_AFAF5F  "\xEF\x9E\x8F"    // 405
#define COLOR_BG_AFAF87  "\xEF\x9E\x90"    // 406
#define COLOR_BG_AFAFAF  "\xEF\x9E\x91"    // 407
#define COLOR_BG_AFAFD7  "\xEF\x9E\x92"    // 408
#define COLOR_BG_AFAFFF  "\xEF\x9E\x93"    // 409
#define COLOR_BG_AFD700  "\xEF\x9E\x94"    // 410
#define COLOR_BG_AFD75F  "\xEF\x9E\x95"    // 411
#define COLOR_BG_AFD787  "\xEF\x9E\x96"    // 412
#define COLOR_BG_AFD7AF  "\xEF\x9E\x97"    // 413
#define COLOR_BG_AFD7D7  "\xEF\x9E\x98"    // 414
#define COLOR_BG_AFD7FF  "\xEF\x9E\x99"    // 415
#define COLOR_BG_AFFF00  "\xEF\x9E\x9A"    // 416
#define COLOR_BG_AFFF5F  "\xEF\x9E\x9B"    // 417
#define COLOR_BG_AFFF87  "\xEF\x9E\x9C"    // 418
#define COLOR_BG_AFFFAF  "\xEF\x9E\x9D"    // 419
#define COLOR_BG_AFFFD7  "\xEF\x9E\x9E"    // 420
#define COLOR_BG_AFFFFF  "\xEF\x9E\x9F"    // 421
#define COLOR_BG_D70000  "\xEF\x9E\xA0"    // 422
#define COLOR_BG_D7005F  "\xEF\x9E\xA1"    // 423
#define COLOR_BG_D70087  "\xEF\x9E\xA2"    // 424
#define COLOR_BG_D700AF  "\xEF\x9E\xA3"    // 425
#define COLOR_BG_D700D7  "\xEF\x9E\xA4"    // 426
#define COLOR_BG_D700FF  "\xEF\x9E\xA5"    // 427
#define COLOR_BG_D75F00  "\xEF\x9E\xA6"    // 428
#define COLOR_BG_D75F5F  "\xEF\x9E\xA7"    // 429
#define COLOR_BG_D75F87  "\xEF\x9E\xA8"    // 430
#define COLOR_BG_D75FAF  "\xEF\x9E\xA9"    // 431
#define COLOR_BG_D75FD7  "\xEF\x9E\xAA"    // 432
#define COLOR_BG_D75FFF  "\xEF\x9E\xAB"    // 433
#define COLOR_BG_D78700  "\xEF\x9E\xAC"    // 434
#define COLOR_BG_D7875A  "\xEF\x9E\xAD"    // 435
#define COLOR_BG_D78787  "\xEF\x9E\xAE"    // 436
#define COLOR_BG_D787AF  "\xEF\x9E\xAF"    // 437
#define COLOR_BG_D787D7  "\xEF\x9E\xB0"    // 438
#define COLOR_BG_D787FF  "\xEF\x9E\xB1"    // 439
#define COLOR_BG_D7AF00  "\xEF\x9E\xB2"    // 440
#define COLOR_BG_D7AF5A  "\xEF\x9E\xB3"    // 441
#define COLOR_BG_D7AF87  "\xEF\x9E\xB4"    // 442
#define COLOR_BG_D7AFAF  "\xEF\x9E\xB5"    // 443
#define COLOR_BG_D7AFD7  "\xEF\x9E\xB6"    // 444
#define COLOR_BG_D7AFFF  "\xEF\x9E\xB7"    // 445
#define COLOR_BG_D7D700  "\xEF\x9E\xB8"    // 446
#define COLOR_BG_D7D75F  "\xEF\x9E\xB9"    // 447
#define COLOR_BG_D7D787  "\xEF\x9E\xBA"    // 448
#define COLOR_BG_D7D7AF  "\xEF\x9E\xBB"    // 449
#define COLOR_BG_D7D7D7  "\xEF\x9E\xBC"    // 450
#define COLOR_BG_D7D7FF  "\xEF\x9E\xBD"    // 451
#define COLOR_BG_D7FF00  "\xEF\x9E\xBE"    // 452
#define COLOR_BG_D7FF5F  "\xEF\x9E\xBF"    // 453
#define COLOR_BG_D7FF87  "\xEF\x9F\x80"    // 454
#define COLOR_BG_D7FFAF  "\xEF\x9F\x81"    // 455
#define COLOR_BG_D7FFD7  "\xEF\x9F\x82"    // 456
#define COLOR_BG_D7FFFF  "\xEF\x9F\x83"    // 457
#define COLOR_BG_FF0000  "\xEF\x9F\x84"    // 458
#define COLOR_BG_FF005F  "\xEF\x9F\x85"    // 459
#define COLOR_BG_FF0087  "\xEF\x9F\x86"    // 460
#define COLOR_BG_FF00AF  "\xEF\x9F\x87"    // 461
#define COLOR_BG_FF00D7  "\xEF\x9F\x88"    // 462
#define COLOR_BG_FF00FF  "\xEF\x9F\x89"    // 463
#define COLOR_BG_FF5F00  "\xEF\x9F\x8A"    // 464
#define COLOR_BG_FF5F5F  "\xEF\x9F\x8B"    // 465
#define COLOR_BG_FF5F87  "\xEF\x9F\x8C"    // 466
#define COLOR_BG_FF5FAF  "\xEF\x9F\x8D"    // 467
#define COLOR_BG_FF5FD7  "\xEF\x9F\x8E"    // 468
#define COLOR_BG_FF5FFF  "\xEF\x9F\x8F"    // 469
#define COLOR_BG_FF8700  "\xEF\x9F\x90"    // 470
#define COLOR_BG_FF875F  "\xEF\x9F\x91"    // 471
#define COLOR_BG_FF8787  "\xEF\x9F\x92"    // 472
#define COLOR_BG_FF87AF  "\xEF\x9F\x93"    // 473
#define COLOR_BG_FF87D7  "\xEF\x9F\x94"    // 474
#define COLOR_BG_FF87FF  "\xEF\x9F\x95"    // 475
#define COLOR_BG_FFAF00  "\xEF\x9F\x96"    // 476
#define COLOR_BG_FFAF5F  "\xEF\x9F\x97"    // 477
#define COLOR_BG_FFAF87  "\xEF\x9F\x98"    // 478
#define COLOR_BG_FFAFAF  "\xEF\x9F\x99"    // 479
#define COLOR_BG_FFAFD7  "\xEF\x9F\x9A"    // 480
#define COLOR_BG_FFAFFF  "\xEF\x9F\x9B"    // 481
#define COLOR_BG_FFD700  "\xEF\x9F\x9C"    // 482
#define COLOR_BG_FFD75F  "\xEF\x9F\x9D"    // 483
#define COLOR_BG_FFD787  "\xEF\x9F\x9E"    // 484
#define COLOR_BG_FFD7AF  "\xEF\x9F\x9F"    // 485
#define COLOR_BG_FFD7D7  "\xEF\x9F\xA0"    // 486
#define COLOR_BG_FFD7FF  "\xEF\x9F\xA1"    // 487
#define COLOR_BG_FFFF00  "\xEF\x9F\xA2"    // 488
#define COLOR_BG_FFFF5F  "\xEF\x9F\xA3"    // 489
#define COLOR_BG_FFFF87  "\xEF\x9F\xA4"    // 490
#define COLOR_BG_FFFFAF  "\xEF\x9F\xA5"    // 491
#define COLOR_BG_FFFFD7  "\xEF\x9F\xA6"    // 492
#define COLOR_BG_FFFFFF_2 "\xEF\x9F\xA7"   // 493
#define COLOR_BG_080808  "\xEF\x9F\xA8"    // 494
#define COLOR_BG_121212  "\xEF\x9F\xA9"    // 495
#define COLOR_BG_1C1C1C  "\xEF\x9F\xAA"    // 496
#define COLOR_BG_262626  "\xEF\x9F\xAB"    // 497
#define COLOR_BG_303030  "\xEF\x9F\xAC"    // 498
#define COLOR_BG_3A3A3A  "\xEF\x9F\xAD"    // 499
#define COLOR_BG_444444  "\xEF\x9F\xAE"    // 500
#define COLOR_BG_4E4E4E  "\xEF\x9F\xAF"    // 501
#define COLOR_BG_585858  "\xEF\x9F\xB0"    // 502
#define COLOR_BG_626262  "\xEF\x9F\xB1"    // 503
#define COLOR_BG_6C6C6C  "\xEF\x9F\xB2"    // 504
#define COLOR_BG_767676  "\xEF\x9F\xB3"    // 505
#define COLOR_BG_808080  "\xEF\x9F\xB4"    // 506
#define COLOR_BG_8A8A8A  "\xEF\x9F\xB5"    // 507
#define COLOR_BG_949494  "\xEF\x9F\xB6"    // 508
#define COLOR_BG_9E9E9E  "\xEF\x9F\xB7"    // 509
#define COLOR_BG_A8A8A8  "\xEF\x9F\xB8"    // 510
#define COLOR_BG_B2B2B2  "\xEF\x9F\xB9"    // 511
#define COLOR_BG_BCBCBC  "\xEF\x9F\xBA"    // 512
#define COLOR_BG_C6C6C6  "\xEF\x9F\xBB"    // 513
#define COLOR_BG_D0D0D0  "\xEF\x9F\xBC"    // 514
#define COLOR_BG_DADADA  "\xEF\x9F\xBD"    // 515
#define COLOR_BG_E4E4E4  "\xEF\x9F\xBE"    // 516
#define COLOR_BG_EEEEEE  "\xEF\x9F\xBF"    // 517

#define COLOR_INDEX_FG_WHITE    (COLOR_INDEX_FG + COLOR_INDEX_WHITE)

typedef struct
{
    ColorState  cs;
    ColorState  csMask;
    const char  pAnsi[12];
    size_t      nAnsi;
    const UTF8 *pUTF;
    size_t      nUTF;
    const UTF8 *pEscape;
    size_t      nEscape;
} MUX_COLOR_SET;

extern const MUX_COLOR_SET aColors[];

typedef struct
{
    int r;
    int g;
    int b;
} RGB;

typedef struct
{
    int y;
    int u;
    int v;
    int y2;
} YUV;

typedef struct
{
    RGB  rgb;
    YUV  yuv;
    int  child[2];
    int  color8;
    int  color16;
} PALETTE_ENTRY;
extern PALETTE_ENTRY palette[];

int FindNearestPaletteEntry(RGB &rgb, bool fColor256);
UTF8 *LettersToBinary(UTF8 *pLetters);

UTF8 *convert_to_html(const UTF8 *pString);
UTF8 *convert_color(__in const UTF8 *pString, bool fNoBleed, bool fColor256);
UTF8 *strip_color(__in const UTF8 *pString, __out_opt size_t *pnLength = 0, __out_opt size_t *pnPoints = 0);
UTF8 *munge_space(__in const UTF8 *);
UTF8 *trim_spaces(__in const UTF8 *);
UTF8 *grabto(__deref_inout UTF8 **, UTF8);
int  string_compare(__in const UTF8 *, __in const UTF8 *);
int  string_prefix(__in const UTF8 *, __in const UTF8 *);
const UTF8 *string_match(__in const UTF8 *, __in const UTF8 *);
UTF8 *replace_string(__in const UTF8 *, __in const UTF8 *, __in const UTF8 *);
UTF8 *replace_tokens
(
    __in const UTF8 *s,
    __in const UTF8 *pBound,
    __in const UTF8 *pListPlace,
    __in const UTF8 *pSwitch
);
#if 0
char *BufferCloneLen(const UTF8 *pBuffer, unsigned int nBuffer);
#endif // 0
bool minmatch(__in const UTF8 *str, __in const UTF8 *target, int min);
UTF8 *StringCloneLen(__in_ecount(nStr) const UTF8 *str, size_t nStr);
UTF8 *StringClone(__in const UTF8 *str);
void safe_copy_str(__in const UTF8 *src, __inout_ecount_full(nSizeOfBuffer) UTF8 *buff, __deref_inout UTF8 **bufp, size_t nSizeOfBuffer);
void safe_copy_str_lbuf(__in const UTF8 *src, __inout UTF8 *buff, __deref_inout UTF8 **bufp);
size_t safe_copy_buf(__in_ecount(nLen) const UTF8 *src, size_t nLen, __in UTF8 *buff, __deref_inout UTF8 **bufp);
size_t safe_fill(__inout UTF8 *buff, __deref_inout UTF8 **bufc, UTF8 chFile, size_t nSpaces);
void safe_chr_utf8(__in const UTF8 *src, __inout UTF8 *buff, __deref_inout UTF8 **bufp);
#define utf8_safe_chr safe_chr_utf8
UTF8 *ConvertToUTF8(UTF32 ch);
UTF8 *ConvertToUTF8(__in const char *p, size_t *pn);
UTF16 *ConvertToUTF16(UTF32 ch);
UTF32 ConvertFromUTF8(__in const UTF8 *p);
size_t ConvertFromUTF16(__out UTF16 *pString, UTF32 &ch);
UTF16 *ConvertFromUTF8ToUTF16(__in const UTF8 *pString, __deref_out size_t *pnString);
UTF8  *ConvertFromUTF16ToUTF8(__in const UTF16 *pSTring);
void mux_strncpy(__out_ecount(nSizeOfBuffer-1) UTF8 *dest, __in const UTF8 *src, size_t nSizeOfBuffer);
bool matches_exit_from_list(__in const UTF8 *, __in const UTF8 *);
UTF8 *translate_string(__in const UTF8 *, bool);
bool IsDecompFriendly(const UTF8 *pString);
int mux_stricmp(__in const UTF8 *a, __in const UTF8 *b);
int mux_memicmp(__in const void *p1_arg, __in const void *p2_arg, size_t n);
UTF8 *mux_strlwr(__in const UTF8 *a, size_t &n);
UTF8 *mux_strupr(__in const UTF8 *a, size_t &n);
UTF8 *mux_foldmatch(__in const UTF8 *a, size_t &n, bool &fChanged);

typedef struct tag_itl
{
    bool bFirst;
    char chPrefix;
    char chSep;
    UTF8 *buff;
    UTF8 **bufc;
    size_t nBufferAvailable;
} ITL;

void ItemToList_Init(ITL *pContext, UTF8 *arg_buff, UTF8 **arg_bufc,
    UTF8 arg_chPrefix = 0, UTF8 arg_chSep = ' ');
bool ItemToList_AddInteger(ITL *pContext, int i);
bool ItemToList_AddInteger64(ITL *pContext, INT64 i);
bool ItemToList_AddString(ITL *pContext, const UTF8 *pStr);
bool ItemToList_AddStringLEN(ITL *pContext, size_t nStr, const UTF8 *pStr);
void ItemToList_Final(ITL *pContext);

size_t DCL_CDECL mux_vsnprintf(__in_ecount(nBuffer) UTF8 *pBuffer, __in size_t nBuffer, __in_z const UTF8 *pFmt, va_list va);
void DCL_CDECL mux_sprintf(__in_ecount(count) UTF8 *buff, __in size_t count, __in_z const UTF8 *fmt, ...);
void DCL_CDECL mux_fprintf(FILE *fp, __in_z const UTF8 *fmt, ...);
size_t GetLineTrunc(UTF8 *Buffer, size_t nBuffer, FILE *fp);

typedef struct
{
    size_t m_d[256];
    size_t m_skip2;
} BMH_State;

extern void BMH_Prepare(BMH_State *bmhs, size_t nPat, const UTF8 *pPat);
extern bool BMH_Execute(BMH_State *bmhs, size_t *pnMatched, size_t nPat, const UTF8 *pPat, size_t nSrc, const UTF8 *pSrc);
extern bool BMH_StringSearch(size_t *pnMatched, size_t nPat, const UTF8 *pPat, size_t nSrc, const UTF8 *pSrc);
extern void BMH_PrepareI(BMH_State *bmhs, size_t nPat, const UTF8 *pPat);
extern bool BMH_ExecuteI(BMH_State *bmhs, size_t *pnMatched, size_t nPat, const UTF8 *pPat, size_t nSrc, const UTF8 *pSrc);
extern bool BMH_StringSearchI(size_t *pnMatched, size_t nPat, const UTF8 *pPat, size_t nSrc, const UTF8 *pSrc);

struct ArtRuleset
{
    ArtRuleset* m_pNextRule;

    void* m_pRegexp;
    void *m_pRegexpStudy;
    int m_bUseAn;
};

class mux_field
{
public:
    LBUF_OFFSET m_byte;
    LBUF_OFFSET m_column;

    inline mux_field(size_t byte = 0, size_t column = 0)
    {
        m_byte = static_cast<LBUF_OFFSET>(byte);
        m_column = static_cast<LBUF_OFFSET>(column);
    };

    inline void operator =(const mux_field &c)
    {
        m_byte = c.m_byte;
        m_column = c.m_column;
    };

    inline void operator ()(size_t byte, size_t column)
    {
        m_byte = static_cast<LBUF_OFFSET>(byte);
        m_column = static_cast<LBUF_OFFSET>(column);
    };

    inline bool operator <(const mux_field &a) const
    {
        return (  m_byte < a.m_byte
               && m_column < a.m_column);
    };

    inline bool operator <=(const mux_field &a) const
    {
        return (  m_byte <= a.m_byte
               && m_column <= a.m_column);
    };

    inline bool operator ==(const mux_field &a) const
    {
        return (m_byte == a.m_byte) && (m_column == a.m_column);
    };

    inline bool operator !=(const mux_field &a) const
    {
        return (m_byte != a.m_byte) || (m_column != a.m_column);
    };

    inline mux_field operator -(const mux_field &a) const
    {
        mux_field b;
        if (  a.m_byte  < m_byte
           && a.m_column < m_column)
        {
            b.m_byte  = m_byte  - a.m_byte;
            b.m_column = m_column - a.m_column;
        }
        else
        {
            b.m_byte  = 0;
            b.m_column = 0;
        }
        return b;
    };

    inline mux_field operator +(const mux_field &a) const
    {
        mux_field b;
        b.m_byte  = m_byte  + a.m_byte;
        b.m_column = m_column + a.m_column;
        return b;
    };

    inline void operator +=(const mux_field &a)
    {
        m_byte  = m_byte + a.m_byte;
        m_column = m_column + a.m_column;
    };

    inline void operator -=(const mux_field &a)
    {
        if (  a.m_byte  < m_byte
           && a.m_column < m_column)
        {
            m_byte  = m_byte - a.m_byte;
            m_column = m_column - a.m_column;
        }
        else
        {
            m_byte  = 0;
            m_column = 0;
        }
    };
};

// mux_string cursors are used for iterators internally.  They should not be
// used externally, as the external view is always that an index refers to a
// code point.
//
class mux_cursor
{
public:
    LBUF_OFFSET m_byte;
    LBUF_OFFSET m_point;

    inline mux_cursor(size_t byte = 0, size_t point = 0)
    {
        m_byte = static_cast<LBUF_OFFSET>(byte);
        m_point = static_cast<LBUF_OFFSET>(point);
    };

    inline void operator =(const mux_cursor &c)
    {
        m_byte = c.m_byte;
        m_point = c.m_point;
    };

    inline void operator ()(size_t byte, size_t point)
    {
        m_byte = static_cast<LBUF_OFFSET>(byte);
        m_point = static_cast<LBUF_OFFSET>(point);
    };

    inline bool operator <(const mux_cursor &a) const
    {
        return (  m_byte < a.m_byte
               && m_point < a.m_point);
    };

    inline bool operator <=(const mux_cursor &a) const
    {
        return (  m_byte <= a.m_byte
               && m_point <= a.m_point);
    };

    inline bool operator ==(const mux_cursor &a) const
    {
        return (m_byte == a.m_byte) && (m_point == a.m_point);
    };

    inline bool operator !=(const mux_cursor &a) const
    {
        return (m_byte != a.m_byte) || (m_point != a.m_point);
    };

    inline mux_cursor operator -(const mux_cursor &a) const
    {
        mux_cursor b;
        if (  a.m_byte  < m_byte
           && a.m_point < m_point)
        {
            b.m_byte  = m_byte  - a.m_byte;
            b.m_point = m_point - a.m_point;
        }
        else
        {
            b.m_byte  = 0;
            b.m_point = 0;
        }
        return b;
    };

    inline mux_cursor operator +(const mux_cursor &a) const
    {
        mux_cursor b;
        b.m_byte  = m_byte  + a.m_byte;
        b.m_point = m_point + a.m_point;
        return b;
    };

    inline void operator +=(const mux_cursor &a)
    {
        m_byte  = m_byte + a.m_byte;
        m_point = m_point + a.m_point;
    };

    inline void operator -=(const mux_cursor &a)
    {
        if (  a.m_byte  < m_byte
           && a.m_point < m_point)
        {
            m_byte  = m_byte - a.m_byte;
            m_point = m_point - a.m_point;
        }
        else
        {
            m_byte  = 0;
            m_point = 0;
        }
    };
};

static const mux_field fldAscii(1, 1);
static const mux_field fldMin(0, 0);

bool utf8_strlen(const UTF8 *pString, mux_cursor &nString);
mux_field StripTabsAndTruncate(const UTF8 *pString, UTF8 *pBuffer,
    size_t nLength, size_t nWidth);
mux_field PadField(UTF8 *pBuffer, size_t nMaxBytes, LBUF_OFFSET nMinWidth,
                   mux_field fldOutput = fldMin);

size_t TruncateToBuffer(const UTF8 *pString, UTF8 *pBuffer, size_t nBuffer);

static const mux_cursor CursorMin(0,0);
static const mux_cursor CursorMax(LBUF_SIZE - 1, LBUF_SIZE - 1);

static const mux_cursor curAscii(1, 1);

class mux_string
{
    // m_nutf, m_ncs, m_autf, m_ncs, and m_pcs work together as follows:
    //
    // m_nutf is always between 0 and LBUF_SIZE-1 inclusively.  The first
    // m_nutf bytes of m_atuf[] contain the non-color UTF-8-encoded code
    // points.  A terminating '\0' at m_autf[m_nutf] is not included in this
    // size even though '\0' is a UTF-8 code point.  In this way, m_nutf
    // corresponds to strlen() in units of bytes.  The use of both a length
    // and a terminating '\0' is intentionally redundant.
    //
    // m_ncp is between 0 and LBUF_SIZE-1 inclusively and represents the
    // number of non-color UTF-8-encoded code points stored in m_autf[].
    // The terminating '\0' is not included in this size.  In this way, m_ncp
    // corresponds to strlen() in units of code points.
    //
    // If color is associated with the above code points, m_pcs will point
    // to an array of ColorStates, otherwise, it is nullptr.  When m_pcs is
    // nullptr, it is equivalent to every code point having CS_NORMAL color.
    // Each color state corresponds with a UTF-8 code point in m_autf[].
    // There is no guaranteed association between a position in m_autf[] and
    // a position in m_pcs because UTF-8 code points are variable length.
    //
    // Not all ColorStates in m_pcs may be used. m_ncs is between 0 and
    // LBUF_SIZE-1 inclusively and represents how many ColorStates are
    // allocated and available for use.  m_ncp is always less than or equal to
    // m_ncs.
    //
    // To recap, m_nutf has units of bytes while m_ncp and m_ncs are in units
    // of code points.
    //
private:
    mux_cursor  m_iLast;
    UTF8        m_autf[LBUF_SIZE];
    size_t      m_ncs;
    ColorState *m_pcs;
    void realloc_m_pcs(size_t ncs);

public:
    mux_string(void);
    mux_string(const mux_string &sStr);
    mux_string(const UTF8 *pStr);
    ~mux_string(void);

    void Validate(void) const;

    inline bool isAscii(void)
    {
        // If every byte corresponds to a point, then all the bytes must be ASCII.
        //
        return (m_iLast.m_byte == m_iLast.m_point);
    }

    void append(dbref num);
    void append(INT64 iInt);
    void append(long lLong);
    void append
    (
        const mux_string &sStr,
        mux_cursor nStart = CursorMin,
        mux_cursor iEnd   = CursorMax
    );
    void append(const UTF8 *pStr);
    void append(const UTF8 *pStr, size_t nLen);
    void append_TextPlain(const UTF8 *pStr);
    void append_TextPlain(const UTF8 *pStr, size_t nLen);
    void compress(const UTF8 *ch);
    void compress_Spaces(void);
    void delete_Chars(mux_cursor iStart, mux_cursor iEnd);
    void edit(mux_string &sFrom, const mux_string &sTo);
    void encode_Html(void);
    UTF8 export_Char(size_t n) const; // Deprecated.
    LBUF_OFFSET export_Char_UTF8(size_t iFirst, UTF8 *pBuffer) const;
    ColorState export_Color(size_t n) const;
    double export_Float(bool bStrict = true) const;
    INT64 export_I64(void) const;
    long export_Long(void) const;
    LBUF_OFFSET export_TextColor
    (
        UTF8 *pBuffer,
        mux_cursor iStart = CursorMin,
        mux_cursor iEnd   = CursorMax,
        size_t nBytesMax = (LBUF_SIZE-1)
    ) const;
    UTF8 *export_TextConverted
    (
        bool fColor,
        bool fNoBleed,
        bool fColor256,
        bool fHtml
    ) const;
    void export_TextHtml(size_t nBuffer, UTF8 *aBuffer) const;
    LBUF_OFFSET export_TextPlain
    (
        UTF8 *pBuffer,
        mux_cursor iStart = CursorMin,
        mux_cursor iEnd   = CursorMax,
        size_t nBytesMax = (LBUF_SIZE-1)
    ) const;
    void import(dbref num);
    void import(INT64 iInt);
    void import(long lLong);
    void import(const mux_string &sStr, mux_cursor iStart = CursorMin);
    void import(const UTF8 *pStr);
    void import(const UTF8 *pStr, size_t nLen);

    inline mux_cursor length_cursor(void) const
    {
        return m_iLast;
    }

    inline size_t length_byte(void) const
    {
        return m_iLast.m_byte;
    }

    inline size_t length_point(void) const
    {
        return m_iLast.m_point;
    }

    void prepend(dbref num);
    void prepend(INT64 iInt);
    void prepend(long lLong);
    void prepend(const mux_string &sStr);
    void prepend(const UTF8 *pStr);
    void prepend(const UTF8 *pStr, size_t nLen);
    void replace_Chars(const mux_string &pTo, mux_cursor iStart, mux_cursor nLen);
    bool replace_Point(const UTF8 *p, const mux_cursor &i);
    void replace_Char(const mux_cursor &i, const mux_string &sStr, const mux_cursor &j);
    void reverse(void);
    bool search
    (
        const UTF8 *pPattern,
        mux_cursor *iPos = nullptr,
        mux_cursor iStart = CursorMin,
        mux_cursor iEnd = CursorMax
    ) const;
    bool search
    (
        const mux_string &sPattern,
        mux_cursor *iPos = nullptr,
        mux_cursor iStart = CursorMin,
        mux_cursor iEnd = CursorMax
    ) const;
    void set_Char(size_t n, const UTF8 cChar); // Deprecated.
    void set_Color(size_t n, ColorState csColor);
    bool compare_Char(const mux_cursor &i, const mux_string &sStr) const;
    void strip
    (
        const UTF8 *pStripSet,
        mux_cursor iStart = CursorMin,
        mux_cursor iEnd = CursorMax
    );
    void stripWithTable
    (
        const bool strip_table[UCHAR_MAX+1],
        mux_cursor iStart = CursorMin,
        mux_cursor iEnd = CursorMax
    );
    void transform
    (
        mux_string &sFromSet,
        mux_string &sToSet,
        size_t nStart = 0,
        size_t nLen = (LBUF_SIZE-1)
    );
    void transform_Ascii
    (
        const UTF8 asciiTable[SCHAR_MAX+1],
        size_t nStart = 0,
        size_t nLen = (LBUF_SIZE-1)
    );
    void trim(const UTF8 ch = ' ', bool bLeft = true, bool bRight = true);
    void trim(const UTF8 *p, bool bLeft = true, bool bRight = true);
    void trim(const UTF8 *p, size_t n, bool bLeft = true, bool bRight = true);
    void truncate(mux_cursor iEnd);

    static void * operator new(size_t size)
    {
        mux_assert(size == sizeof(mux_string));
        return alloc_string("new");
    }

    static void operator delete(void *p)
    {
        if (nullptr != p)
        {
            free_string(p);
        }
    }

    void UpperCase(void);
    void LowerCase(void);
    void UpperCaseFirst(void);
    void FoldForMatching(void);

    // mux_string_cursor c;
    // cursor_start(c);
    // while (cursor_next(c))
    // {
    // }
    //
    inline bool cursor_start(mux_cursor &c) const
    {
        c.m_byte  = 0;
        c.m_point = 0;
        return (0 != m_iLast.m_point);
    }

    inline bool cursor_next(mux_cursor &c) const
    {
        if ('\0' != m_autf[c.m_byte])
        {
#ifdef NEW_MUX_STRING_PARANOID
            size_t n = utf8_FirstByte[m_autf[c.m_byte]];
            mux_assert(n < UTF8_CONTINUE);
            while (n--)
            {
                c.m_byte++;
                mux_assert(UTF8_CONTINUE == utf8_FirstByte[m_autf[c.m_byte]]);
            }
            mux_assert(0 <= c.m_point && c.m_point < m_ncp);
#else
            c.m_byte = (LBUF_OFFSET)(c.m_byte + utf8_FirstByte[m_autf[c.m_byte]]);
#endif // NEW_MUX_STRING_PARANOID
            c.m_point++;
            return true;
        }
        return false;
    };

    // mux_cursor c;
    // cursor_end(c);
    // while (cursor_prev(c))
    // {
    // }
    //
    inline void cursor_end(mux_cursor &c) const
    {
        c = m_iLast;
    }

    inline bool cursor_prev(mux_cursor &c) const
    {
        if (0 < c.m_byte)
        {
#ifdef NEW_MUX_STRING_PARANOID
            size_t n = 1;
            while (UTF8_CONTINUE == utf8_FirstByte[m_autf[c.m_byte - n]])
            {
                n++;
                mux_assert(0 < c.m_byte - n);
            }
            mux_assert(utf8_FirstByte[m_autf[c.m_byte - n]] < UTF8_CONTINUE);
            c.m_byte -= n;
            mux_assert(0 < c.m_byte && c.m_byte <= m_ncp);
#else
            c.m_byte--;
            while (  0 < c.m_byte
                  && UTF8_CONTINUE == utf8_FirstByte[m_autf[c.m_byte]])
            {
                c.m_byte--;
            }
#endif // NEW_MUX_STRING_PARANOID
            c.m_point--;
            return true;
        }
        return false;
    };

    inline bool cursor_from_point(mux_cursor &c, LBUF_OFFSET iPoint) const
    {
        if (iPoint <= m_iLast.m_point)
        {
            if (m_iLast.m_point == m_iLast.m_byte)
            {
                // Special case of ASCII.
                //
                c.m_byte  = iPoint;
                c.m_point = iPoint;
            }
            else if (iPoint < m_iLast.m_point/2)
            {
                // Start from the beginning.
                //
                cursor_start(c);
                while (  c.m_point < iPoint
                      && cursor_next(c))
                {
                    ; // Nothing.
                }
            }
            else
            {
                // Start from the end.
                //
                cursor_end(c);
                while (  iPoint < c.m_point
                      && cursor_prev(c))
                {
                    ; // Nothing.
                }
            }
            return true;
        }
        cursor_end(c);
        return false;
    }

    inline bool cursor_from_byte(mux_cursor &c, LBUF_OFFSET iByte) const
    {
        if (iByte <= m_iLast.m_byte)
        {
            if (m_iLast.m_point == m_iLast.m_byte)
            {
                // Special case of ASCII.
                //
                c.m_byte  = iByte;
                c.m_point = iByte;
            }
            else if (iByte < m_iLast.m_byte/2)
            {
                // Start from the beginning.
                //
                cursor_start(c);
                while (  c.m_byte < iByte
                      && cursor_next(c))
                {
                    ; // Nothing.
                }
            }
            else
            {
                // Start from the end.
                //
                cursor_end(c);
                while (  iByte < c.m_byte
                      && cursor_prev(c))
                {
                    ; // Nothing.
                }
            }
            return true;
        }
        return false;
    }

    inline bool IsEscape(mux_cursor &c)
    {
        return mux_isescape(m_autf[c.m_byte]);
    }

    friend class mux_words;
};

inline bool isEmpty(const mux_string *p)
{
    return ((nullptr == p) || (0 == p->length_byte()));
}

// String buffers are LBUF_SIZE, so maximum string length is LBUF_SIZE-1.
// That means the longest possible list can consist of LBUF_SIZE-1 copies
// of the delimiter, making for LBUF_SIZE words in the list.
//
#define MAX_WORDS LBUF_SIZE

class mux_words
{
private:
    LBUF_OFFSET m_nWords;
    mux_cursor m_aiWordBegins[MAX_WORDS];
    mux_cursor m_aiWordEnds[MAX_WORDS];
    const mux_string *m_s;

public:

    mux_words(const mux_string &sStr);
    void export_WordColor(LBUF_OFFSET n, UTF8 *buff, UTF8 **bufc = nullptr);
    LBUF_OFFSET find_Words(const UTF8 *pDelim, bool bFavorEmptyList = false);
    void ignore_Word(LBUF_OFFSET n);
    mux_cursor wordBegin(LBUF_OFFSET n) const;
    mux_cursor wordEnd(LBUF_OFFSET n) const;
    LBUF_OFFSET Count(void) { return m_nWords; }
};

#endif // STRINGUTIL_H
