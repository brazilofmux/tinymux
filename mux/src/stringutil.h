// stringutil.h -- string utilities.
//
// $Id: stringutil.h,v 1.25 2003-02-04 16:47:08 sdennis Exp $
//
// MUX 2.3
// Copyright (C) 1998 through 2003 Solid Vertical Domains, Ltd. All
// rights not explicitly given are reserved.  
//
#ifndef STRINGUTIL_H
#define STRINGUTIL_H

extern const char mux_isprint[256];
extern const char mux_isdigit[256];
extern const char mux_isalpha[256];
extern const char Tiny_IsAlphaNumeric[256];
extern const char Tiny_IsUpper[256];
extern const char Tiny_IsLower[256];
extern const char mux_isspace[256];
extern char Tiny_IsFirstAttributeNameCharacter[256];
extern char Tiny_IsAttributeNameCharacter[256];
extern const char mux_ObjectNameSet[256];
extern char mux_PlayerNameSet[256];
extern const char mux_issecure[256];
extern const char mux_isescape[256];
extern const unsigned char mux_toupper[256];
extern const unsigned char mux_tolower[256];
extern const unsigned char mux_StripAccents[256];
int ANSI_lex(int nString, const char *pString, int *nLengthToken0, int *nLengthToken1);
#define TOKEN_TEXT_ANSI 0 // Text sequence + optional ANSI sequence.
#define TOKEN_ANSI      1 // ANSI sequence.

typedef struct
{
    char *pString;
    char aControl[256];
} TINY_STRTOK_STATE;

void mux_strtok_src(TINY_STRTOK_STATE *tts, char *pString);
void mux_strtok_ctl(TINY_STRTOK_STATE *tts, char *pControl);
char *mux_strtok_parseLEN(TINY_STRTOK_STATE *tts, int *pnLen);
char *mux_strtok_parse(TINY_STRTOK_STATE *tts);
char *RemoveSetOfCharacters(char *pString, char *pSetToRemove);

int mux_ltoa(long val, char *buf);
char *mux_ltoa_t(long val);
void safe_ltoa(long val, char *buff, char **bufc);
int mux_i64toa(INT64 val, char *buf);
char *mux_i64toa_t(INT64 val);
void safe_i64toa(INT64 val, char *buff, char **bufc);
long mux_atol(const char *pString);
INT64 mux_atoi64(const char *pString);
double mux_atof(char *szString, BOOL bStrict = TRUE);
char *mux_ftoa(double r, BOOL bRounded, int frac);
BOOL bcd_valid(INT64 a);
INT64 bcd_add(INT64 a, INT64 b);
INT64 bcd_tencomp(INT64 a);
int   mux_bcdtoa(INT64 val, char *buf);
INT64 mux_atobcd(const char *pString);
void safe_bcdtoa(INT64 val, char *buff, char **bufc);

BOOL is_integer(char *, int *);
BOOL is_rational(char *);
BOOL is_real(char *);

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
    BOOL            m_bSawNormal;
};

struct ANSI_Out_Context
{
    int             m_iEndGoal;
    ANSI_ColorState m_acs;
    BOOL            m_bDone; // some constraint was met.
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
extern BOOL minmatch(char *str, char *target, int min);
extern char *StringCloneLen(const char *str, size_t nStr);
extern char *StringClone(const char *str);
void safe_copy_str(const char *src, char *buff, char **bufp, int max);
void safe_copy_str_lbuf(const char *src, char *buff, char **bufp);
int safe_copy_buf(const char *src, int nLen, char *buff, char **bufp);
int safe_fill(char *buff, char **bufc, char chFile, int nSpaces);
extern BOOL matches_exit_from_list(char *, const char *);
extern char *translate_string(const char *, BOOL);
extern int mux_stricmp(const char *a, const char *b);
extern int mux_memicmp(const void *p1_arg, const void *p2_arg, size_t n);
extern void mux_strlwr(char *tp);
extern void mux_strupr(char *a);

typedef struct tag_itl
{
    BOOL bFirst;
    char chPrefix;
    char chSep;
    char *buff;
    char **bufc;
    size_t nBufferAvailable;
} ITL;

void ItemToList_Init(ITL *pContext, char *arg_buff, char **arg_bufc,
    char arg_chPrefix = 0, char arg_chSep = ' ');
BOOL ItemToList_AddInteger(ITL *pContext, int i);
BOOL ItemToList_AddString(ITL *pContext, char *pStr);
BOOL ItemToList_AddStringLEN(ITL *pContext, size_t nStr, char *pStr);
void ItemToList_Final(ITL *pContext);

int DCL_CDECL mux_vsnprintf(char *buff, int count, const char *fmt, va_list va);
int GetLineTrunc(char *Buffer, size_t nBuffer, FILE *fp);

typedef struct
{
    int m_d[256];
    int m_skip2;
} BMH_State;
extern void BMH_Prepare(BMH_State *bmhs, int nPat, char *pPat);
extern int  BMH_Execute(BMH_State *bmhs, int nPat, char *pPat, int nSrc, char *pSrc);
extern int  BMH_StringSearch(int nPat, char *pPat, int nSrc, char *pSrc);
extern void BMH_PrepareI(BMH_State *bmhs, int nPat, char *pPat);
extern int  BMH_ExecuteI(BMH_State *bmhs, int nPat, char *pPat, int nSrc, char *pSrc);
extern int  BMH_StringSearchI(int nPat, char *pPat, int nSrc, char *pSrc);

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

extern BOOL ParseFloat(PARSE_FLOAT_RESULT *pfr, const char *str, BOOL bStrict = TRUE);

#endif // STRINGUTIL_H
