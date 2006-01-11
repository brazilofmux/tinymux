// stringutil.h -- string utilities.
//
// $Id: stringutil.h,v 1.48 2006-01-11 07:16:38 sdennis Exp $
//
#ifndef STRINGUTIL_H
#define STRINGUTIL_H

extern const bool mux_isprint[256];
extern const bool mux_isdigit[256];
extern const bool mux_isxdigit[256];
extern const bool mux_isalpha[256];
extern const bool mux_isalnum[256];
extern const bool mux_islower[256];
extern const bool mux_isupper[256];
extern const bool mux_isspace[256];
extern bool mux_AttrNameInitialSet[256];
extern bool mux_AttrNameSet[256];
extern const bool mux_ObjectNameSet[256];
extern bool mux_PlayerNameSet[256];
extern const bool mux_issecure[256];
extern const bool mux_isescape[256];
extern const unsigned char mux_hex2dec[256];
extern const unsigned char mux_toupper[256];
extern const unsigned char mux_tolower[256];
extern const unsigned char mux_StripAccents[256];

#define mux_isprint(x) (mux_isprint[(unsigned char)(x)])
#define mux_isdigit(x) (mux_isdigit[(unsigned char)(x)])
#define mux_isxdigit(x)(mux_isxdigit[(unsigned char)(x)])
#define mux_isalpha(x) (mux_isalpha[(unsigned char)(x)])
#define mux_isalnum(x) (mux_isalnum[(unsigned char)(x)])
#define mux_islower(x) (mux_islower[(unsigned char)(x)])
#define mux_isupper(x) (mux_isupper[(unsigned char)(x)])
#define mux_isspace(x) (mux_isspace[(unsigned char)(x)])
#define mux_hex2dec(x) (mux_hex2dec[(unsigned char)(x)])
#define mux_toupper(x) (mux_toupper[(unsigned char)(x)])
#define mux_tolower(x) (mux_tolower[(unsigned char)(x)])

#define mux_AttrNameInitialSet(x) (mux_AttrNameInitialSet[(unsigned char)(x)])
#define mux_AttrNameSet(x)        (mux_AttrNameSet[(unsigned char)(x)])
#define mux_ObjectNameSet(x)      (mux_ObjectNameSet[(unsigned char)(x)])
#define mux_PlayerNameSet(x)      (mux_PlayerNameSet[(unsigned char)(x)])
#define mux_issecure(x)           (mux_issecure[(unsigned char)(x)])
#define mux_isescape(x)           (mux_isescape[(unsigned char)(x)])
#define mux_StripAccents(x)       (mux_StripAccents[(unsigned char)(x)])

int ANSI_lex(int nString, const char *pString, size_t *nLengthToken0, size_t *nLengthToken1);
#define TOKEN_TEXT_ANSI 0 // Text sequence + optional ANSI sequence.
#define TOKEN_ANSI      1 // ANSI sequence.

typedef struct
{
    char *pString;
    char aControl[256];
} MUX_STRTOK_STATE;

void mux_strtok_src(MUX_STRTOK_STATE *tts, char *pString);
void mux_strtok_ctl(MUX_STRTOK_STATE *tts, char *pControl);
char *mux_strtok_parseLEN(MUX_STRTOK_STATE *tts, int *pnLen);
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
double mux_atof(char *szString, bool bStrict = true);
char *mux_ftoa(double r, bool bRounded, int frac);

bool is_integer(char *, int *);
bool is_rational(char *);
bool is_real(char *);

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
    ANSI_ColorState m_acs;
    const char     *m_p;
    int             m_n;
    bool            m_bSawNormal;
};

struct ANSI_Out_Context
{
    int             m_iEndGoal;
    ANSI_ColorState m_acs;
    bool            m_bDone; // some constraint was met.
    char           *m_p;
    int             m_n;
    int             m_nMax;
    int             m_vw;
    int             m_vwMax;
};

#define ANSI_ENDGOAL_NORMAL  0
#define ANSI_ENDGOAL_NOBLEED 1
#define ANSI_ENDGOAL_LEAK    2

extern void ANSI_String_In_Init(struct ANSI_In_Context *pacIn, const char *szString, int iEndGoal);
extern void ANSI_String_Out_Init(struct ANSI_Out_Context *pacOut, char *pField, int nField, int vwMax, int iEndGoal);
extern void ANSI_String_Skip(struct ANSI_In_Context *pacIn, int maxVisualWidth, int *pnVisualWidth);
extern void ANSI_String_Copy(struct ANSI_Out_Context *pacOut, struct ANSI_In_Context *pacIn, int vwMax);
extern int ANSI_String_Finalize(struct ANSI_Out_Context *pacOut, int *pnVisualWidth);
extern char *ANSI_TruncateAndPad_sbuf(const char *pString, int nMaxVisualWidth, char fill = ' ');
extern int ANSI_TruncateToField(const char *szString, int nField, char *pField, int maxVisual, int *nVisualWidth, int iEndGoal);
extern char *strip_ansi(const char *szString, size_t *pnString = 0);
extern char *strip_accents(const char *szString, size_t *pnString = 0);
extern char *normal_to_white(const char *);
extern char *munge_space(const char *);
extern char *trim_spaces(char *);
extern char *grabto(char **, char);
extern int  string_compare(const char *, const char *);
extern int  string_prefix(const char *, const char *);
extern const char * string_match(const char * ,const char *);
extern char *replace_string(const char *, const char *, const char *);
extern char *replace_tokens
(
    const char *s,
    const char *pBound,
    const char *pListPlace,
    const char *pSwitch
);
#if 0
extern int prefix_match(const char *, const char *);
extern char *BufferCloneLen(const char *pBuffer, unsigned int nBuffer);
#endif // 0
extern bool minmatch(char *str, char *target, int min);
extern char *StringCloneLen(const char *str, size_t nStr);
extern char *StringClone(const char *str);
void safe_copy_str(const char *src, char *buff, char **bufp, int max);
void safe_copy_str_lbuf(const char *src, char *buff, char **bufp);
size_t safe_copy_buf(const char *src, size_t nLen, char *buff, char **bufp);
size_t safe_fill(char *buff, char **bufc, char chFile, size_t nSpaces);
void mux_strncpy(char *dest, const char *src, size_t nSizeOfBuffer);
extern bool matches_exit_from_list(char *, const char *);
extern char *translate_string(const char *, bool);
extern int mux_stricmp(const char *a, const char *b);
extern int mux_memicmp(const void *p1_arg, const void *p2_arg, size_t n);
extern void mux_strlwr(char *tp);
extern void mux_strupr(char *a);

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
bool ItemToList_AddString(ITL *pContext, char *pStr);
bool ItemToList_AddStringLEN(ITL *pContext, size_t nStr, char *pStr);
void ItemToList_Final(ITL *pContext);

int DCL_CDECL mux_vsnprintf(char *buff, size_t count, const char *fmt, va_list va);
void DCL_CDECL mux_sprintf(char *buff, size_t count, const char *fmt, ...);
int GetLineTrunc(char *Buffer, size_t nBuffer, FILE *fp);

typedef struct
{
    int m_d[256];
    int m_skip2;
} BMH_State;

extern void BMH_Prepare(BMH_State *bmhs, int nPat, const char *pPat);
extern int  BMH_Execute(BMH_State *bmhs, int nPat, const char *pPat, int nSrc, const char *pSrc);
extern int  BMH_StringSearch(int nPat, const char *pPat, int nSrc, const char *pSrc);
extern void BMH_PrepareI(BMH_State *bmhs, int nPat, const char *pPat);
extern int  BMH_ExecuteI(BMH_State *bmhs, int nPat, const char *pPat, int nSrc, const char *pSrc);
extern int  BMH_StringSearchI(int nPat, const char *pPat, int nSrc, const char *pSrc);

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

#endif // STRINGUTIL_H
