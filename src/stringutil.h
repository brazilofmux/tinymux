// stringutil.h -- string utilities
//
// $Id: stringutil.h,v 1.21 2001-08-24 18:21:17 sdennis Exp $
//
// MUX 2.1
// Portions are derived from MUX 1.6. Portions are original work.
//
// Copyright (C) 1998 through 2000 Solid Vertical Domains, Ltd. All
// rights not explicitly given are reserved. Permission is given to
// use this code for building and hosting text-based game servers.
// Permission is given to use this code for other non-commercial
// purposes. To use this code for commercial purposes other than
// building/hosting text-based game servers, contact the author at
// Stephen Dennis <sdennis@svdltd.com> for another license.
//
#ifndef STRINGUTIL_H
#define STRINGUTIL_H

extern const char Tiny_IsASCII[256];
extern const char Tiny_IsPrint[256];
extern const char Tiny_IsDigit[256];
extern char Tiny_IsAlpha[256];
extern const char Tiny_IsAlphaNumeric[256];
extern const char Tiny_IsUpper[256];
extern const char Tiny_IsLower[256];
extern const char Tiny_IsSpace[256];
extern char Tiny_IsAttributeNameCharacter[256];
extern const char Tiny_IsObjectNameCharacter[256];
extern char Tiny_IsPlayerNameCharacter[256];
extern const char Tiny_IsSecureCharacter[256];
extern const char Tiny_IsEscapeCharacter[256];
extern const unsigned char Tiny_ToUpper[256];
extern const unsigned char Tiny_ToLower[256];
int ANSI_lex(int nString, const char *pString, int *nLengthToken0, int *nLengthToken1);
#define TOKEN_TEXT_ANSI 0 // Text sequence + optional ANSI sequence.
#define TOKEN_ANSI      1 // ANSI sequence.

typedef struct
{
    char *pString;
    char aControl[256];
} TINY_STRTOK_STATE;

void Tiny_StrTokString(TINY_STRTOK_STATE *tts, char *pString);
void Tiny_StrTokControl(TINY_STRTOK_STATE *tts, char *pControl);
char *Tiny_StrTokParseLEN(TINY_STRTOK_STATE *tts, int *pnLen);
char *Tiny_StrTokParse(TINY_STRTOK_STATE *tts);
char *RemoveSetOfCharacters(char *pString, char *pSetToRemove);

int Tiny_ltoa(long val, char *buf);
char *Tiny_ltoa_t(long val);
void safe_ltoa(long val, char *buff, char **bufc, int size);
int Tiny_i64toa(INT64 val, char *buf);
char *Tiny_i64toa_t(INT64 val);
void safe_i64toa(INT64 val, char *buff, char **bufc, int size);
long Tiny_atol(const char *pString);
INT64 Tiny_atoi64(const char *pString);

extern BOOL is_integer(char *, int *);
extern BOOL is_number(char *);

#pragma pack(1)
typedef struct
{
    char bNormal;
    char bBlink;
    char bHighlite;
    char bInverse;
    char bUnder;

    char iForeground;
    char iBackground;
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

void ANSI_String_In_Init(struct ANSI_In_Context *pacIn, const char *szString, int iEndGoal);
void ANSI_String_Out_Init(struct ANSI_Out_Context *pacOut, char *pField, int nField, int vwMax, int iEndGoal);
void ANSI_String_Skip(struct ANSI_In_Context *pacIn, int maxVisualWidth, int *pnVisualWidth);
void ANSI_String_Copy(struct ANSI_Out_Context *pacOut, struct ANSI_In_Context *pacIn, int nField, int vwMax);
int ANSI_String_Finalize(struct ANSI_Out_Context *pacOut, int *pnVisualWidth);
int ANSI_TruncateToField(const char *szString, int nField, char *pField, int maxVisual, int *nVisualWidth, int iEndGoal);
extern char *strip_ansi(const char *szString, unsigned int *pnString = 0);
extern char *normal_to_white(const char *);
extern char *munge_space(char *);
extern char *trim_spaces(char *);
extern char *grabto(char **, char);
extern int  string_compare(const char *, const char *);
extern int  string_prefix(const char *, const char *);
extern const char * string_match(const char * ,const char *);
extern char *dollar_to_space(const char *);
extern char *replace_string(const char *, const char *, const char *);
extern char *replace_string_inplace(const char *,  const char *, char *);
extern int  FDECL(prefix_match, (const char *, const char *));
extern int  FDECL(minmatch, (char *, char *, int));
extern char *StringCloneLen(const char *str, unsigned int nStr);
extern char *StringClone(const char *str);
extern char *BufferCloneLen(const char *pBuffer, unsigned int nBuffer);
void safe_copy_str(const char *src, char *buff, char **bufp, int max);
int safe_copy_buf(const char *src, int nLen, char *buff, char **bufp, int nSizeOfBuffer);
extern int  FDECL(matches_exit_from_list, (char *, char *));
extern char *   FDECL(translate_string, (const char *, int));
#ifndef WIN32
extern int _stricmp(const char *a, const char *b);
extern int _strnicmp(const char *a, const char *b, int n);
extern void _strlwr(char *tp);
extern void _strupr(char *a);
#endif // WIN32

typedef struct tag_dtb
{
    int bFirst;
    char *buff;
    char **bufc;
    int nBufferAvailable;
} DTB;

void DbrefToBuffer_Init(DTB *p, char *arg_buff, char **arg_bufc);
int DbrefToBuffer_Add(DTB *pContext, int i);
void DbrefToBuffer_Final(DTB *pContext);
int DCL_CDECL Tiny_vsnprintf(char *buff, int count, const char *fmt, va_list va);
int GetLineTrunc(char *Buffer, size_t nBuffer, FILE *fp);

typedef struct
{
    int m_d[128];
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
    int m_bUseAn;
};

#endif // STRINGUTIL_H
