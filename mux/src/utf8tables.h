/*! \file utf8tables.h
 * \brief Static tables with UTF-8 state machines.
 *
 * $Id$
 *
 */

#ifndef UTF8TABLES_H
#define UTF8TABLES_H

typedef struct
{
    size_t n_bytes;
    size_t n_points;
    const UTF8 *p;
} string_desc;

// utf/cl_Printable.txt
//
// 106866 included, 1007246 excluded, 0 errors.
// 215 states, 95 columns, 3949 bytes
//
#define CL_PRINT_START_STATE (0)
#define CL_PRINT_ACCEPTING_STATES_START (215)
extern const unsigned char cl_print_itt[256];
extern const unsigned short cl_print_sot[215];
extern const unsigned char cl_print_sbt[3263];

// utf/cl_AttrNameInitial.txt
//
// 177 included, 1113935 excluded, 0 errors.
// 6 states, 14 columns, 312 bytes
//
#define CL_ATTRNAMEINITIAL_START_STATE (0)
#define CL_ATTRNAMEINITIAL_ACCEPTING_STATES_START (6)
extern const unsigned char cl_attrnameinitial_itt[256];
extern const unsigned char cl_attrnameinitial_sot[6];
extern const unsigned char cl_attrnameinitial_sbt[50];

// utf/cl_AttrName.txt
//
// 203 included, 1113909 excluded, 0 errors.
// 6 states, 14 columns, 312 bytes
//
#define CL_ATTRNAME_START_STATE (0)
#define CL_ATTRNAME_ACCEPTING_STATES_START (6)
extern const unsigned char cl_attrname_itt[256];
extern const unsigned char cl_attrname_sot[6];
extern const unsigned char cl_attrname_sbt[50];

// utf/cl_ObjectName.txt
//
// 87028 included, 1027084 excluded, 0 errors.
// 32 states, 63 columns, 639 bytes
//
#define CL_OBJECTNAME_START_STATE (0)
#define CL_OBJECTNAME_ACCEPTING_STATES_START (32)
extern const unsigned char cl_objectname_itt[256];
extern const unsigned short cl_objectname_sot[32];
extern const unsigned char cl_objectname_sbt[319];

// utf/cl_PlayerName.txt
//
// 86961 included, 1027151 excluded, 0 errors.
// 31 states, 57 columns, 622 bytes
//
#define CL_PLAYERNAME_START_STATE (0)
#define CL_PLAYERNAME_ACCEPTING_STATES_START (31)
extern const unsigned char cl_playername_itt[256];
extern const unsigned short cl_playername_sot[31];
extern const unsigned char cl_playername_sbt[304];

// utf/cl_8859_1.txt
//
// 191 included, 1113921 excluded, 0 errors.
// 3 states, 6 columns, 279 bytes
//
#define CL_8859_1_START_STATE (0)
#define CL_8859_1_ACCEPTING_STATES_START (3)
extern const unsigned char cl_8859_1_itt[256];
extern const unsigned char cl_8859_1_sot[3];
extern const unsigned char cl_8859_1_sbt[20];

// utf/cl_8859_2.txt
//
// 191 included, 1113921 excluded, 0 errors.
// 6 states, 21 columns, 335 bytes
//
#define CL_8859_2_START_STATE (0)
#define CL_8859_2_ACCEPTING_STATES_START (6)
extern const unsigned char cl_8859_2_itt[256];
extern const unsigned char cl_8859_2_sot[6];
extern const unsigned char cl_8859_2_sbt[73];

// utf/cl_hangul.txt
//
// 11172 included, 1102940 excluded, 0 errors.
// 6 states, 9 columns, 298 bytes
//
#define CL_HANGUL_START_STATE (0)
#define CL_HANGUL_ACCEPTING_STATES_START (6)
extern const unsigned char cl_hangul_itt[256];
extern const unsigned char cl_hangul_sot[6];
extern const unsigned char cl_hangul_sbt[36];

// utf/cl_hiragana.txt
//
// 94 included, 1114018 excluded, 0 errors.
// 8 states, 12 columns, 315 bytes
//
#define CL_HIRAGANA_START_STATE (0)
#define CL_HIRAGANA_ACCEPTING_STATES_START (8)
extern const unsigned char cl_hiragana_itt[256];
extern const unsigned char cl_hiragana_sot[8];
extern const unsigned char cl_hiragana_sbt[51];

// utf/cl_kanji.txt
//
// 75394 included, 1038718 excluded, 0 errors.
// 18 states, 29 columns, 413 bytes
//
#define CL_KANJI_START_STATE (0)
#define CL_KANJI_ACCEPTING_STATES_START (18)
extern const unsigned char cl_kanji_itt[256];
extern const unsigned char cl_kanji_sot[18];
extern const unsigned char cl_kanji_sbt[139];

// utf/cl_katakana.txt
//
// 115 included, 1113997 excluded, 0 errors.
// 8 states, 12 columns, 315 bytes
//
#define CL_KATAKANA_START_STATE (0)
#define CL_KATAKANA_ACCEPTING_STATES_START (8)
extern const unsigned char cl_katakana_itt[256];
extern const unsigned char cl_katakana_sot[8];
extern const unsigned char cl_katakana_sbt[51];

// utf/tr_utf8_ascii.txt
//
// 2367 code points.
// 94 states, 192 columns, 3739 bytes
//
#define TR_ASCII_START_STATE (0)
#define TR_ASCII_ACCEPTING_STATES_START (94)
extern const unsigned char tr_ascii_itt[256];
extern const unsigned short tr_ascii_sot[94];
extern const unsigned char tr_ascii_sbt[3295];

// utf/tr_utf8_cp437.txt
//
// 2480 code points.
// 103 states, 193 columns, 7632 bytes
//
#define TR_CP437_START_STATE (0)
#define TR_CP437_ACCEPTING_STATES_START (103)
extern const unsigned char tr_cp437_itt[256];
extern const unsigned short tr_cp437_sot[103];
extern const unsigned short tr_cp437_sbt[3585];

// utf/tr_utf8_latin1.txt
//
// 2404 code points.
// 96 states, 192 columns, 7146 bytes
//
#define TR_LATIN1_START_STATE (0)
#define TR_LATIN1_ACCEPTING_STATES_START (96)
extern const unsigned char tr_latin1_itt[256];
extern const unsigned short tr_latin1_sot[96];
extern const unsigned short tr_latin1_sbt[3349];

// utf/tr_utf8_latin2.txt
//
// 2372 code points.
// 94 states, 192 columns, 7092 bytes
//
#define TR_LATIN2_START_STATE (0)
#define TR_LATIN2_ACCEPTING_STATES_START (94)
extern const unsigned char tr_latin2_itt[256];
extern const unsigned short tr_latin2_sot[94];
extern const unsigned short tr_latin2_sbt[3324];

// utf/tr_widths.txt
//
// 330647 code points.
// 206 states, 92 columns, 3703 bytes
//
#define TR_WIDTHS_START_STATE (0)
#define TR_WIDTHS_ACCEPTING_STATES_START (206)
extern const unsigned char tr_widths_itt[256];
extern const unsigned short tr_widths_sot[206];
extern const unsigned char tr_widths_sbt[3035];

// utf/tr_tolower.txt
//
// 1029 code points.
// 46 states, 86 columns, 1713 bytes
//
#define TR_TOLOWER_START_STATE (0)
#define TR_TOLOWER_ACCEPTING_STATES_START (46)
extern const unsigned char tr_tolower_itt[256];
extern const unsigned short tr_tolower_sot[46];
extern const unsigned char tr_tolower_sbt[1365];

#define TR_TOLOWER_DEFAULT (0)
#define TR_TOLOWER_LITERAL_START (1)
#define TR_TOLOWER_XOR_START (16)
extern const string_desc tr_tolower_ott[100];

// utf/tr_toupper.txt
//
// 1036 code points.
// 48 states, 90 columns, 1810 bytes
//
#define TR_TOUPPER_START_STATE (0)
#define TR_TOUPPER_ACCEPTING_STATES_START (48)
extern const unsigned char tr_toupper_itt[256];
extern const unsigned short tr_toupper_sot[48];
extern const unsigned char tr_toupper_sbt[1458];

#define TR_TOUPPER_DEFAULT (0)
#define TR_TOUPPER_LITERAL_START (1)
#define TR_TOUPPER_XOR_START (14)
extern const string_desc tr_toupper_ott[105];

// utf/tr_totitle.txt
//
// 1040 code points.
// 48 states, 90 columns, 1811 bytes
//
#define TR_TOTITLE_START_STATE (0)
#define TR_TOTITLE_ACCEPTING_STATES_START (48)
extern const unsigned char tr_totitle_itt[256];
extern const unsigned short tr_totitle_sot[48];
extern const unsigned char tr_totitle_sbt[1459];

#define TR_TOTITLE_DEFAULT (0)
#define TR_TOTITLE_LITERAL_START (1)
#define TR_TOTITLE_XOR_START (14)
extern const string_desc tr_totitle_ott[103];

// utf/tr_foldmatch.txt
//
// 14 code points.
// 7 states, 11 columns, 310 bytes
//
#define TR_FOLDMATCH_START_STATE (0)
#define TR_FOLDMATCH_ACCEPTING_STATES_START (7)
extern const unsigned char tr_foldmatch_itt[256];
extern const unsigned char tr_foldmatch_sot[7];
extern const unsigned char tr_foldmatch_sbt[47];

#define TR_FOLDMATCH_DEFAULT (0)
#define TR_FOLDMATCH_LITERAL_START (1)
#define TR_FOLDMATCH_XOR_START (3)
extern const string_desc tr_foldmatch_ott[3];

// utf/tr_Color.txt
//
// 2053 code points.
// 37 states, 67 columns, 4820 bytes
//
#define TR_COLOR_START_STATE (0)
#define TR_COLOR_ACCEPTING_STATES_START (37)
extern const unsigned char tr_color_itt[256];
extern const unsigned short tr_color_sot[37];
extern const unsigned short tr_color_sbt[2245];

#endif // UTF8TABLES_H
