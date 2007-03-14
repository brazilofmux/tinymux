/*! \file stringutil.h
 * \brief string utilities.
 *
 * $Id$
 *
 */

#ifndef STRINGUTIL_H
#define STRINGUTIL_H

extern const bool mux_isprint_ascii[256];
extern const bool mux_isprint_latin1[256];
extern const bool mux_isdigit[256];
extern const bool mux_isxdigit[256];
extern const bool mux_isazAZ[256];
extern const bool mux_isalpha[256];
extern const bool mux_isalnum[256];
extern const bool mux_islower_latin1[256];
extern const bool mux_isupper_latin1[256];
extern const bool mux_isspace[256];
extern bool mux_AttrNameInitialSet_latin1[256];
extern bool mux_AttrNameSet[256];
extern const bool mux_ObjectNameSet[256];
extern bool mux_PlayerNameSet[256];
extern const bool mux_issecure[256];
extern const bool mux_isescape[256];
extern const unsigned char mux_hex2dec[256];
extern const unsigned char mux_toupper_ascii[SCHAR_MAX+1];
extern const unsigned char mux_tolower_ascii[SCHAR_MAX+1];
extern const unsigned char mux_toupper_latin1[256];
extern const unsigned char mux_tolower_latin1[256];

#define UTF8_SIZE1     1
#define UTF8_SIZE2     2
#define UTF8_SIZE3     3
#define UTF8_SIZE4     4
#define UTF8_CONTINUE  5
#define UTF8_ILLEGAL   6
extern const unsigned char utf8_FirstByte[256];
extern const char *latin1_utf8[256];
#define latin1_utf8(x) ((const UTF8 *)latin1_utf8[(unsigned char)x])

#define mux_isprint_ascii(x) (mux_isprint_ascii[(unsigned char)(x)])
#define mux_isprint_latin1(x) (mux_isprint_latin1[(unsigned char)(x)])
#define mux_isdigit(x) (mux_isdigit[(unsigned char)(x)])
#define mux_isxdigit(x)(mux_isxdigit[(unsigned char)(x)])
#define mux_isazAZ(x)  (mux_isazAZ[(unsigned char)(x)])
#define mux_isalpha(x) (mux_isalpha[(unsigned char)(x)])
#define mux_isalnum(x) (mux_isalnum[(unsigned char)(x)])
#define mux_islower_latin1(x) (mux_islower_latin1[(unsigned char)(x)])
#define mux_isupper_latin1(x) (mux_isupper_latin1[(unsigned char)(x)])
#define mux_isspace(x) (mux_isspace[(unsigned char)(x)])
#define mux_hex2dec(x) (mux_hex2dec[(unsigned char)(x)])
#define mux_toupper(x) (mux_toupper_latin1[(unsigned char)(x)])
#define mux_tolower(x) (mux_tolower_latin1[(unsigned char)(x)])

#define mux_AttrNameInitialSet_latin1(x) (mux_AttrNameInitialSet_latin1[(unsigned char)(x)])
#define mux_ObjectNameSet(x)      (mux_ObjectNameSet[(unsigned char)(x)])
#define mux_PlayerNameSet(x)      (mux_PlayerNameSet[(unsigned char)(x)])
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
// 95007 included, 1019105 excluded, 0 errors.
// 164 states, 103 columns, 17148 bytes
//
#define CL_PRINT_START_STATE (0)
#define CL_PRINT_ACCEPTING_STATES_START (164)
extern const unsigned char cl_print_itt[256];
extern const unsigned char cl_print_stt[164][103];

inline bool mux_isprint(const unsigned char *p)
{
    int iState = CL_PRINT_START_STATE;
    do
    {
        unsigned char ch = *p++;
        iState = cl_print_stt[iState][cl_print_itt[(unsigned char)ch]];
    } while (iState < CL_PRINT_ACCEPTING_STATES_START);
    return ((iState - CL_PRINT_ACCEPTING_STATES_START) == 1) ? true : false;
}

// utf/cl_AttrNameInitial.txt
//
// 129 included, 1113983 excluded, 0 errors.
// 5 states, 10 columns, 306 bytes
//
#define CL_ATTRNAMEINITIAL_START_STATE (0)
#define CL_ATTRNAMEINITIAL_ACCEPTING_STATES_START (5)
extern const unsigned char cl_attrnameinitial_itt[256];
extern const unsigned char cl_attrnameinitial_stt[5][10];

inline bool mux_isattrnameinitial(const unsigned char *p)
{
    int iState = CL_ATTRNAMEINITIAL_START_STATE;
    do
    {
        unsigned char ch = *p++;
        iState = cl_attrnameinitial_stt[iState][cl_attrnameinitial_itt[(unsigned char)ch]];
    } while (iState < CL_ATTRNAMEINITIAL_ACCEPTING_STATES_START);
    return ((iState - CL_ATTRNAMEINITIAL_ACCEPTING_STATES_START) == 1) ? true : false;
}

// utf/cl_AttrName.txt
//
// 155 included, 1113957 excluded, 0 errors.
// 5 states, 10 columns, 306 bytes
//
#define CL_ATTRNAME_START_STATE (0)
#define CL_ATTRNAME_ACCEPTING_STATES_START (5)
extern const unsigned char cl_attrname_itt[256];
extern const unsigned char cl_attrname_stt[5][10];

inline bool mux_isattrname(const unsigned char *p)
{
    int iState = CL_ATTRNAME_START_STATE;
    do
    {
        unsigned char ch = *p++;
        iState = cl_attrname_stt[iState][cl_attrname_itt[(unsigned char)ch]];
    } while (iState < CL_ATTRNAME_ACCEPTING_STATES_START);
    return ((iState - CL_ATTRNAME_ACCEPTING_STATES_START) == 1) ? true : false;
}

// utf/cl_Upper.txt
//
// 56 included, 1114056 excluded, 0 errors.
// 2 states, 4 columns, 264 bytes
//
#define CL_UPPER_START_STATE (0)
#define CL_UPPER_ACCEPTING_STATES_START (2)
extern const unsigned char cl_upper_itt[256];
extern const unsigned char cl_upper_stt[2][4];

inline bool mux_isupper(const unsigned char *p)
{
    int iState = CL_UPPER_START_STATE;
    do
    {
        unsigned char ch = *p++;
        iState = cl_upper_stt[iState][cl_upper_itt[(unsigned char)ch]];
    } while (iState < CL_UPPER_ACCEPTING_STATES_START);
    return ((iState - CL_UPPER_ACCEPTING_STATES_START) == 1) ? true : false;
}

// utf/cl_Lower.txt
//
// 58 included, 1114054 excluded, 0 errors.
// 2 states, 4 columns, 264 bytes
//
#define CL_LOWER_START_STATE (0)
#define CL_LOWER_ACCEPTING_STATES_START (2)
extern const unsigned char cl_lower_itt[256];
extern const unsigned char cl_lower_stt[2][4];

inline bool mux_islower(const unsigned char *p)
{
    int iState = CL_LOWER_START_STATE;
    do
    {
        unsigned char ch = *p++;
        iState = cl_lower_stt[iState][cl_lower_itt[(unsigned char)ch]];
    } while (iState < CL_LOWER_ACCEPTING_STATES_START);
    return ((iState - CL_LOWER_ACCEPTING_STATES_START) == 1) ? true : false;
}

// utf/tr_utf8_latin1.txt
//
// 1503 code points.
// 71 states, 190 columns, 27236 bytes
//
#define TR_LATIN1_START_STATE (0)
#define TR_LATIN1_ACCEPTING_STATES_START (71)
extern const unsigned char tr_latin1_itt[256];
extern const unsigned short tr_latin1_stt[71][190];
const char *ConvertToLatin(const UTF8 *pString);

// utf/tr_utf8_ascii.txt
//
// 1446 code points.
// 67 states, 190 columns, 12986 bytes
//
#define TR_ASCII_START_STATE (0)
#define TR_ASCII_ACCEPTING_STATES_START (67)
extern const unsigned char tr_ascii_itt[256];
extern const unsigned char tr_ascii_stt[67][190];
const char *ConvertToAscii(const UTF8 *pString);

// utf/tr_tolower.txt
//
// 56 code points.
// 1 states, 2 columns, 258 bytes
//
#define TR_TOLOWER_START_STATE (0)
#define TR_TOLOWER_ACCEPTING_STATES_START (1)
extern const unsigned char tr_tolower_itt[256];
extern const unsigned char tr_tolower_stt[1][2];
extern const char *tr_tolower_ott[2];

inline const unsigned char *mux_lowerflip(const unsigned char *p)
{
    int iState = TR_TOLOWER_START_STATE;
    do
    {
        unsigned char ch = *p++;
        iState = tr_tolower_stt[iState][tr_tolower_itt[(unsigned char)ch]];
    } while (iState < TR_TOLOWER_ACCEPTING_STATES_START);
    return (const unsigned char *)tr_tolower_ott[iState - TR_TOLOWER_ACCEPTING_STATES_START];
}

// utf/tr_toupper.txt
//
// 57 code points.
// 1 states, 4 columns, 260 bytes
//
#define TR_TOUPPER_START_STATE (0)
#define TR_TOUPPER_ACCEPTING_STATES_START (1)
extern const unsigned char tr_toupper_itt[256];
extern const unsigned char tr_toupper_stt[1][4];
extern const char *tr_toupper_ott[3];

inline const unsigned char *mux_upperflip(const unsigned char *p)
{
    int iState = TR_TOUPPER_START_STATE;
    do
    {
        unsigned char ch = *p++;
        iState = tr_toupper_stt[iState][tr_toupper_itt[(unsigned char)ch]];
    } while (iState < TR_TOUPPER_ACCEPTING_STATES_START);
    return (const unsigned char *)tr_toupper_ott[iState - TR_TOUPPER_ACCEPTING_STATES_START];
}

// utf/tr_Color.txt
//
// 517 code points.
// 5 states, 11 columns, 311 bytes
//
#define TR_COLOR_START_STATE (0)
#define TR_COLOR_ACCEPTING_STATES_START (5)
extern const unsigned char tr_color_itt[256];
extern const unsigned char tr_color_stt[5][11];

inline int mux_color(const unsigned char *p)
{
    int iState = TR_COLOR_START_STATE;
    do
    {
        unsigned char ch = *p++;
        iState = tr_color_stt[iState][tr_color_itt[(unsigned char)ch]];
    } while (iState < TR_COLOR_ACCEPTING_STATES_START);
    return iState - TR_COLOR_ACCEPTING_STATES_START;
}

#define COLOR_UNDEFINED  0
#define COLOR_RESET      "\xEE\x80\x80"    // 1
#define COLOR_INTENSE    "\xEE\x80\x81"    // 2
#define COLOR_UNDERLINE  "\xEE\x80\x84"    // 3
#define COLOR_BLINK      "\xEE\x80\x85"    // 4
#define COLOR_INVERSE    "\xEE\x80\x87"    // 5
#define COLOR_FG_BLACK   "\xEE\x84\x80"    // 6
#define COLOR_FG_RED     "\xEE\x84\x81"    // 7
#define COLOR_FG_GREEN   "\xEE\x84\x82"    // 8
#define COLOR_FG_YELLOW  "\xEE\x84\x83"    // 9
#define COLOR_FG_BLUE    "\xEE\x84\x84"    // 10
#define COLOR_FG_MAGENTA "\xEE\x84\x85"    // 11
#define COLOR_FG_CYAN    "\xEE\x84\x86"    // 12
#define COLOR_FG_WHITE   "\xEE\x84\x87"    // 13
#define COLOR_BG_BLACK   "\xEE\x88\x80"    // 14
#define COLOR_BG_RED     "\xEE\x88\x81"    // 15
#define COLOR_BG_GREEN   "\xEE\x88\x82"    // 16
#define COLOR_BG_YELLOW  "\xEE\x88\x83"    // 17
#define COLOR_BG_BLUE    "\xEE\x88\x84"    // 18
#define COLOR_BG_MAGENTA "\xEE\x88\x85"    // 19
#define COLOR_BG_CYAN    "\xEE\x88\x86"    // 20
#define COLOR_BG_WHITE   "\xEE\x88\x87"    // 21
#define COLOR_LAST_CODE  21

bool utf8_strlen(const UTF8 *pString, size_t &nString);

int ANSI_lex(size_t nString, const UTF8 *pString, size_t *nLengthToken0, size_t *nLengthToken1);
#define TOKEN_TEXT_ANSI 0 // Text sequence + optional ANSI sequence.
#define TOKEN_ANSI      1 // ANSI sequence.

typedef struct
{
    UTF8 *pString;
    UTF8 aControl[256];
} MUX_STRTOK_STATE;

void mux_strtok_src(MUX_STRTOK_STATE *tts, UTF8 *pString);
void mux_strtok_ctl(MUX_STRTOK_STATE *tts, UTF8 *pControl);
UTF8 *mux_strtok_parseLEN(MUX_STRTOK_STATE *tts, size_t *pnLen);
UTF8 *mux_strtok_parse(MUX_STRTOK_STATE *tts);
UTF8 *RemoveSetOfCharacters(UTF8 *pString, UTF8 *pSetToRemove);

size_t mux_ltoa(long val, UTF8 *buf);
UTF8 *mux_ltoa_t(long val);
void safe_ltoa(long val, UTF8 *buff, UTF8 **bufc);
size_t mux_i64toa(INT64 val, UTF8 *buf);
UTF8 *mux_i64toa_t(INT64 val);
void safe_i64toa(INT64 val, UTF8 *buff, UTF8 **bufc);
long mux_atol(const UTF8 *pString);
INT64 mux_atoi64(const UTF8 *pString);
double mux_atof(const UTF8 *szString, bool bStrict = true);
UTF8 *mux_ftoa(double r, bool bRounded, int frac);

bool is_integer(const UTF8 *str, int *pDigits = NULL);
bool is_rational(const UTF8 *str);
bool is_real(const UTF8 *str);

#pragma pack(1)
typedef struct
{
    unsigned char bNormal:1;
    unsigned char bBlink:1;
    unsigned char bHighlite:1;
    unsigned char bInverse:1;
    unsigned char bUnder:1;

    unsigned char iForeground:4;
    unsigned char iBackground:4;
} ANSI_ColorState;
#pragma pack()

struct ANSI_In_Context
{
    ANSI_ColorState m_cs;
    const UTF8     *m_p;
    size_t          m_n;
};

struct ANSI_Out_Context
{
    bool            m_bNoBleed;
    ANSI_ColorState m_cs;
    bool            m_bDone; // some constraint was met.
    UTF8           *m_p;
    size_t          m_n;
    size_t          m_nMax;
    size_t          m_vw;
    size_t          m_vwMax;
};

void ANSI_String_In_Init(struct ANSI_In_Context *pacIn, const UTF8 *szString, bool bNoBleed = false);
void ANSI_String_Out_Init(struct ANSI_Out_Context *pacOut, UTF8 *pField, size_t nField, size_t vwMax, bool bNoBleed = false);
void ANSI_String_Copy(struct ANSI_Out_Context *pacOut, struct ANSI_In_Context *pacIn);
size_t ANSI_String_Finalize(struct ANSI_Out_Context *pacOut, size_t *pnVisualWidth);
UTF8 *ANSI_TruncateAndPad_sbuf(const UTF8 *pString, size_t nMaxVisualWidth, UTF8 fill = ' ');
size_t ANSI_TruncateToField(const UTF8 *szString, size_t nField, UTF8 *pField, size_t maxVisual, size_t *nVisualWidth, bool bNoBleed = false);
UTF8 *convert_color(const UTF8 *pString, bool bNoBleed);
UTF8 *strip_color(const UTF8 *pString, size_t *pnLength = 0, size_t *pnPoints = 0);
UTF8 *normal_to_white(const UTF8 *);
UTF8 *munge_space(const UTF8 *);
UTF8 *trim_spaces(UTF8 *);
UTF8 *grabto(UTF8 **, UTF8);
int  string_compare(const UTF8 *, const UTF8 *);
int  string_prefix(const UTF8 *, const UTF8 *);
const UTF8 * string_match(const UTF8 * ,const UTF8 *);
UTF8 *replace_string(const UTF8 *, const UTF8 *, const UTF8 *);
UTF8 *replace_tokens
(
    const UTF8 *s,
    const UTF8 *pBound,
    const UTF8 *pListPlace,
    const UTF8 *pSwitch
);
#if 0
int prefix_match(const UTF8 *, const UTF8 *);
char *BufferCloneLen(const UTF8 *pBuffer, unsigned int nBuffer);
#endif // 0
bool minmatch(UTF8 *str, UTF8 *target, int min);
UTF8 *StringCloneLen(const UTF8 *str, size_t nStr);
UTF8 *StringClone(const UTF8 *str);
void safe_copy_str(const UTF8 *src, UTF8 *buff, UTF8 **bufp, size_t nSizeOfBuffer);
void safe_copy_str_lbuf(const UTF8 *src, UTF8 *buff, UTF8 **bufp);
size_t safe_copy_buf(const UTF8 *src, size_t nLen, UTF8 *buff, UTF8 **bufp);
size_t safe_fill(UTF8 *buff, UTF8 **bufc, UTF8 chFile, size_t nSpaces);
void utf8_safe_chr(const UTF8 *src, UTF8 *buff, UTF8 **bufp);
UTF8 *ConvertToUTF8(UTF32 ch);
UTF8 *ConvertToUTF8(const char *p, size_t *pn);
UTF32 ConvertFromUTF8(const UTF8 *p);
void mux_strncpy(UTF8 *dest, const UTF8 *src, size_t nSizeOfBuffer);
bool matches_exit_from_list(UTF8 *, const UTF8 *);
UTF8 *translate_string(const UTF8 *, bool);
int mux_stricmp(const UTF8 *a, const UTF8 *b);
int mux_memicmp(const void *p1_arg, const void *p2_arg, size_t n);
void mux_strlwr(UTF8 *tp);
void mux_strupr(UTF8 *a);

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
bool ItemToList_AddString(ITL *pContext, UTF8 *pStr);
bool ItemToList_AddStringLEN(ITL *pContext, size_t nStr, UTF8 *pStr);
void ItemToList_Final(ITL *pContext);

size_t DCL_CDECL mux_vsnprintf(UTF8 *buff, size_t count, const char *fmt, va_list va);
void DCL_CDECL mux_sprintf(UTF8 *buff, size_t count, const char *fmt, ...);
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

typedef struct
{
    int         iLeadingSign;
    int         iString;
    const UTF8  *pDigitsA;
    size_t      nDigitsA;
    const UTF8  *pDigitsB;
    size_t      nDigitsB;
    int         iExponentSign;
    const UTF8  *pDigitsC;
    size_t      nDigitsC;
    const UTF8  *pMeat;
    size_t      nMeat;

} PARSE_FLOAT_RESULT;

extern bool ParseFloat(PARSE_FLOAT_RESULT *pfr, const UTF8 *str, bool bStrict = true);

class mux_string
{
private:
    // m_n, m_ach, m_ncs, and m_pcs work together as follows:
    //
    // m_n is always between 0 and LBUF_SIZE-1 inclusively.  The first m_n
    // characters of m_ach[] contain the non-ANSI portion of the string.
    // In addition to a length, m_ach[] is also terminated with '\0' at
    // m_ach[m_n].  This is intentionally redundant.
    //
    // m_ncs tracks the size of the array of color states pointed to
    // by m_pcs.  If m_ncs is 0, there is no color in the string and
    // m_pcs must not be dereferenced.  Otherwise each character in
    // m_ach[] has a corresponding color encoded in m_pcs[].  The
    // m_pcs[m_n] (which corresponds to '\0') is not guaranteed to
    // exist or be valid.
    //
    size_t          m_n;
    unsigned char   m_ach[LBUF_SIZE];
    size_t          m_ncs;
    ANSI_ColorState *m_pcs;

    void realloc_m_pcs(size_t ncs);

public:
    mux_string(void);
    mux_string(const mux_string &sStr);
    mux_string(const UTF8 *pStr);
    ~mux_string(void);
    void append(const UTF8 cChar);
    void append(dbref num);
    void append(INT64 iInt);
    void append(long lLong);
    void append
    (
        const mux_string &sStr,
        size_t nStart = 0,
        size_t nLen = (LBUF_SIZE-1)
    );
    void append(const UTF8 *pStr);
    void append(const UTF8 *pStr, size_t nLen);
    void append_TextPlain(const UTF8 *pStr);
    void append_TextPlain(const UTF8 *pStr, size_t nLen);
    void compress(const UTF8 ch);
    void compress_Spaces(void);
    void delete_Chars(size_t nStart, size_t nLen);
    void edit(mux_string &sFrom, const mux_string &sTo);
    UTF8 export_Char(size_t n) const;
    LBUF_OFFSET export_Char_UTF8(size_t iFirst, UTF8 *pBuffer) const;
    ANSI_ColorState export_Color(size_t n) const;
    double export_Float(bool bStrict = true) const;
    INT64 export_I64(void) const;
    long export_Long(void) const;
    void export_TextAnsi
    (
        UTF8 *buff,
        UTF8 **bufc = NULL,
        size_t nStart = 0,
        size_t nLen = LBUF_SIZE,
        size_t nBuffer = (LBUF_SIZE-1),
        bool bNoBleed = false
    ) const;
    void export_TextPlain
    (
        UTF8 *buff,
        UTF8 **bufc = NULL,
        size_t nStart = 0,
        size_t nLen = LBUF_SIZE,
        size_t nBuffer = (LBUF_SIZE-1)
    ) const;
    void import(const UTF8 chIn);
    void import(dbref num);
    void import(INT64 iInt);
    void import(long lLong);
    void import(const mux_string &sStr, size_t nStart = 0);
    void import(const UTF8 *pStr);
    void import(const UTF8 *pStr, size_t nLen);
    size_t length(void) const;
    void prepend(const UTF8 cChar);
    void prepend(dbref num);
    void prepend(INT64 iInt);
    void prepend(long lLong);
    void prepend(const mux_string &sStr);
    void prepend(const UTF8 *pStr);
    void prepend(const UTF8 *pStr, size_t nLen);
    void replace_Chars(const mux_string &pTo, size_t nStart, size_t nLen);
    void reverse(void);
    bool search
    (
        const UTF8 *pPattern,
        size_t *nPos = NULL,
        size_t nStart = 0
    ) const;
    bool search
    (
        const mux_string &sPattern,
        size_t *nPos = NULL,
        size_t nStart = 0
    ) const;
    void set_Char(size_t n, const UTF8 cChar);
    void set_Color(size_t n, ANSI_ColorState csColor);
    void strip
    (
        const UTF8 *pStripSet,
        size_t nStart = 0,
        size_t nLen = (LBUF_SIZE-1)
    );
    void stripWithTable
    (
        const bool strip_table[UCHAR_MAX+1],
        size_t nStart = 0,
        size_t nLen = (LBUF_SIZE-1)
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
    void truncate(size_t nLen);

    static void * operator new(size_t size)
    {
        mux_assert(size == sizeof(mux_string));
        return alloc_string((UTF8 *)"new");
    }

    static void operator delete(void *p)
    {
        if (NULL != p)
        {
            free_string(p);
        }
    }

    UTF8 operator [](size_t i) const
    {
        if (m_n <= i)
        {
            return '\0';
        }
        return m_ach[i];
    }

    friend class mux_words;
};

// String buffers are LBUF_SIZE, so maximum string length is LBUF_SIZE-1.
// That means the longest possible list can consist of LBUF_SIZE-1 copies
// of the delimiter, making for LBUF_SIZE words in the list.
//
#define MAX_WORDS LBUF_SIZE

class mux_words
{
private:
    bool        m_aControl[UCHAR_MAX+1];
    LBUF_OFFSET m_nWords;
    LBUF_OFFSET m_aiWordBegins[MAX_WORDS];
    LBUF_OFFSET m_aiWordEnds[MAX_WORDS];
    const mux_string *m_s;

public:

    mux_words(const mux_string &sStr);
    void export_WordAnsi(LBUF_OFFSET n, UTF8 *buff, UTF8 **bufc = NULL);
    LBUF_OFFSET find_Words(void);
    LBUF_OFFSET find_Words(const UTF8 *pDelim);
    void ignore_Word(LBUF_OFFSET n);
    void set_Control(const UTF8 *pControlSet);
    void set_Control(const bool table[UCHAR_MAX+1]);
    LBUF_OFFSET wordBegin(LBUF_OFFSET n) const;
    LBUF_OFFSET wordEnd(LBUF_OFFSET n) const;
};

#endif // STRINGUTIL_H
