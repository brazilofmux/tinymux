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
extern const unsigned char mux_toupper[256];
extern const unsigned char mux_tolower[256];
extern const unsigned char mux_StripAccents[256];

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
#define mux_toupper(x) (mux_toupper[(unsigned char)(x)])
#define mux_tolower(x) (mux_tolower[(unsigned char)(x)])

#define mux_AttrNameInitialSet_latin1(x) (mux_AttrNameInitialSet_latin1[(unsigned char)(x)])
#define mux_ObjectNameSet(x)      (mux_ObjectNameSet[(unsigned char)(x)])
#define mux_PlayerNameSet(x)      (mux_PlayerNameSet[(unsigned char)(x)])
#define mux_issecure(x)           (mux_issecure[(unsigned char)(x)])
#define mux_isescape(x)           (mux_isescape[(unsigned char)(x)])
#define mux_StripAccents(x)       (mux_StripAccents[(unsigned char)(x)])

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

bool utf8_strlen(const UTF8 *pString, size_t &nString);

int ANSI_lex(size_t nString, const char *pString, size_t *nLengthToken0, size_t *nLengthToken1);
#define TOKEN_TEXT_ANSI 0 // Text sequence + optional ANSI sequence.
#define TOKEN_ANSI      1 // ANSI sequence.

typedef struct
{
    char *pString;
    char aControl[256];
} MUX_STRTOK_STATE;

void mux_strtok_src(MUX_STRTOK_STATE *tts, char *pString);
void mux_strtok_ctl(MUX_STRTOK_STATE *tts, char *pControl);
char *mux_strtok_parseLEN(MUX_STRTOK_STATE *tts, size_t *pnLen);
char *mux_strtok_parse(MUX_STRTOK_STATE *tts);
char *RemoveSetOfCharacters(char *pString, char *pSetToRemove);

size_t mux_ltoa(long val, char *buf);
char *mux_ltoa_t(long val);
void safe_ltoa(long val, char *buff, char **bufc);
size_t mux_i64toa(INT64 val, char *buf);
char *mux_i64toa_t(INT64 val);
void safe_i64toa(INT64 val, char *buff, char **bufc);
long mux_atol(const char *pString);
INT64 mux_atoi64(const char *pString);
double mux_atof(const char *szString, bool bStrict = true);
char *mux_ftoa(double r, bool bRounded, int frac);

bool is_integer(const char *str, int *pDigits = NULL);
bool is_rational(const char *str);
bool is_real(const char *str);

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
    const char     *m_p;
    size_t          m_n;
};

struct ANSI_Out_Context
{
    bool            m_bNoBleed;
    ANSI_ColorState m_cs;
    bool            m_bDone; // some constraint was met.
    char           *m_p;
    size_t          m_n;
    size_t          m_nMax;
    size_t          m_vw;
    size_t          m_vwMax;
};

void ANSI_String_In_Init(struct ANSI_In_Context *pacIn, const char *szString, bool bNoBleed = false);
void ANSI_String_Out_Init(struct ANSI_Out_Context *pacOut, char *pField, size_t nField, size_t vwMax, bool bNoBleed = false);
void ANSI_String_Copy(struct ANSI_Out_Context *pacOut, struct ANSI_In_Context *pacIn);
size_t ANSI_String_Finalize(struct ANSI_Out_Context *pacOut, size_t *pnVisualWidth);
char *ANSI_TruncateAndPad_sbuf(const char *pString, size_t nMaxVisualWidth, char fill = ' ');
size_t ANSI_TruncateToField(const char *szString, size_t nField, char *pField, size_t maxVisual, size_t *nVisualWidth, bool bNoBleed = false);
char *strip_ansi(const char *szString, size_t *pnString = 0);
char *strip_accents(const char *szString, size_t *pnString = 0);
char *normal_to_white(const char *);
char *munge_space(const char *);
char *trim_spaces(char *);
char *grabto(char **, char);
int  string_compare(const char *, const char *);
int  string_prefix(const char *, const char *);
const char * string_match(const char * ,const char *);
char *replace_string(const char *, const char *, const char *);
char *replace_tokens
(
    const char *s,
    const char *pBound,
    const char *pListPlace,
    const char *pSwitch
);
#if 0
int prefix_match(const char *, const char *);
char *BufferCloneLen(const char *pBuffer, unsigned int nBuffer);
#endif // 0
bool minmatch(char *str, char *target, int min);
char *StringCloneLen(const char *str, size_t nStr);
char *StringClone(const char *str);
void safe_copy_str(const char *src, char *buff, char **bufp, size_t nSizeOfBuffer);
void safe_copy_str_lbuf(const char *src, char *buff, char **bufp);
size_t safe_copy_buf(const char *src, size_t nLen, char *buff, char **bufp);
size_t safe_fill(char *buff, char **bufc, char chFile, size_t nSpaces);
void utf8_safe_chr(const UTF8 *src, UTF8 *buff, UTF8 **bufp);
UTF8 *ConvertToUTF8(UTF32 ch);
UTF8 *ConvertToUTF8(const char *p);
UTF32 ConvertFromUTF8(const UTF8 *p);
void mux_strncpy(char *dest, const char *src, size_t nSizeOfBuffer);
bool matches_exit_from_list(char *, const char *);
char *translate_string(const char *, bool);
int mux_stricmp(const char *a, const char *b);
int mux_memicmp(const void *p1_arg, const void *p2_arg, size_t n);
void mux_strlwr(char *tp);
void mux_strupr(char *a);

typedef struct tag_itl
{
    bool bFirst;
    char chPrefix;
    char chSep;
    char *buff;
    char **bufc;
    size_t nBufferAvailable;
} ITL;

void ItemToList_Init(ITL *pContext, char *arg_buff, char **arg_bufc,
    char arg_chPrefix = 0, char arg_chSep = ' ');
bool ItemToList_AddInteger(ITL *pContext, int i);
bool ItemToList_AddInteger64(ITL *pContext, INT64 i);
bool ItemToList_AddString(ITL *pContext, char *pStr);
bool ItemToList_AddStringLEN(ITL *pContext, size_t nStr, char *pStr);
void ItemToList_Final(ITL *pContext);

size_t DCL_CDECL mux_vsnprintf(char *buff, size_t count, const char *fmt, va_list va);
void DCL_CDECL mux_sprintf(char *buff, size_t count, const char *fmt, ...);
size_t GetLineTrunc(char *Buffer, size_t nBuffer, FILE *fp);

typedef struct
{
    size_t m_d[256];
    size_t m_skip2;
} BMH_State;

extern void BMH_Prepare(BMH_State *bmhs, size_t nPat, const char *pPat);
extern bool BMH_Execute(BMH_State *bmhs, size_t *pnMatched, size_t nPat, const char *pPat, size_t nSrc, const char *pSrc);
extern bool BMH_StringSearch(size_t *pnMatched, size_t nPat, const char *pPat, size_t nSrc, const char *pSrc);
extern void BMH_PrepareI(BMH_State *bmhs, size_t nPat, const char *pPat);
extern bool BMH_ExecuteI(BMH_State *bmhs, size_t *pnMatched, size_t nPat, const char *pPat, size_t nSrc, const char *pSrc);
extern bool BMH_StringSearchI(size_t *pnMatched, size_t nPat, const char *pPat, size_t nSrc, const char *pSrc);

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
    const char  *pDigitsA;
    size_t      nDigitsA;
    const char  *pDigitsB;
    size_t      nDigitsB;
    int         iExponentSign;
    const char  *pDigitsC;
    size_t      nDigitsC;
    const char  *pMeat;
    size_t      nMeat;

} PARSE_FLOAT_RESULT;

extern bool ParseFloat(PARSE_FLOAT_RESULT *pfr, const char *str, bool bStrict = true);

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
    mux_string(const char *pStr);
    ~mux_string(void);
    void append(const char cChar);
    void append(dbref num);
    void append(INT64 iInt);
    void append(long lLong);
    void append
    (
        const mux_string &sStr,
        size_t nStart = 0,
        size_t nLen = (LBUF_SIZE-1)
    );
    void append(const char *pStr);
    void append(const char *pStr, size_t nLen);
    void append_TextPlain(const char *pStr);
    void append_TextPlain(const char *pStr, size_t nLen);
    void compress(const char ch);
    void compress_Spaces(void);
    void delete_Chars(size_t nStart, size_t nLen);
    void edit(mux_string &sFrom, const mux_string &sTo);
    char export_Char(size_t n) const;
    ANSI_ColorState export_Color(size_t n) const;
    double export_Float(bool bStrict = true) const;
    INT64 export_I64(void) const;
    long export_Long(void) const;
    void export_TextAnsi
    (
        char *buff,
        char **bufc = NULL,
        size_t nStart = 0,
        size_t nLen = LBUF_SIZE,
        size_t nBuffer = (LBUF_SIZE-1),
        bool bNoBleed = false
    ) const;
    void export_TextPlain
    (
        char *buff,
        char **bufc = NULL,
        size_t nStart = 0,
        size_t nLen = LBUF_SIZE,
        size_t nBuffer = (LBUF_SIZE-1)
    ) const;
    void import(const char chIn);
    void import(dbref num);
    void import(INT64 iInt);
    void import(long lLong);
    void import(const mux_string &sStr, size_t nStart = 0);
    void import(const char *pStr);
    void import(const char *pStr, size_t nLen);
    size_t length(void) const;
    void prepend(const char cChar);
    void prepend(dbref num);
    void prepend(INT64 iInt);
    void prepend(long lLong);
    void prepend(const mux_string &sStr);
    void prepend(const char *pStr);
    void prepend(const char *pStr, size_t nLen);
    void replace_Chars(const mux_string &pTo, size_t nStart, size_t nLen);
    void reverse(void);
    bool search
    (
        const char *pPattern,
        size_t *nPos = NULL,
        size_t nStart = 0
    ) const;
    bool search
    (
        const mux_string &sPattern,
        size_t *nPos = NULL,
        size_t nStart = 0
    ) const;
    void set_Char(size_t n, const char cChar);
    void set_Color(size_t n, ANSI_ColorState csColor);
    void strip
    (
        const char *pStripSet,
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
    void transformWithTable
    (
        const unsigned char xfrmTable[256],
        size_t nStart = 0,
        size_t nLen = (LBUF_SIZE-1)
    );
    void trim(const char ch = ' ', bool bLeft = true, bool bRight = true);
    void trim(const char *p, bool bLeft = true, bool bRight = true);
    void trim(const char *p, size_t n, bool bLeft = true, bool bRight = true);
    void truncate(size_t nLen);

    static void * operator new(size_t size)
    {
        mux_assert(size == sizeof(mux_string));
        return alloc_string("new");
    }

    static void operator delete(void *p)
    {
        if (NULL != p)
        {
            free_string(p);
        }
    }

    char operator [](size_t i) const
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
    void export_WordAnsi(LBUF_OFFSET n, char *buff, char **bufc = NULL);
    LBUF_OFFSET find_Words(void);
    LBUF_OFFSET find_Words(const char *pDelim);
    void ignore_Word(LBUF_OFFSET n);
    void set_Control(const char *pControlSet);
    void set_Control(const bool table[UCHAR_MAX+1]);
    LBUF_OFFSET wordBegin(LBUF_OFFSET n) const;
    LBUF_OFFSET wordEnd(LBUF_OFFSET n) const;
};

#endif // STRINGUTIL_H
