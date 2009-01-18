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
// 100312 included, 1013800 excluded, 0 errors.
// 198 states, 95 columns, 19066 bytes
//
#define CL_PRINT_START_STATE (0)
#define CL_PRINT_ACCEPTING_STATES_START (198)
extern const unsigned char cl_print_itt[256];
extern const unsigned char cl_print_stt[198][95];

// utf/cl_AttrNameInitial.txt
//
// 177 included, 1113935 excluded, 0 errors.
// 6 states, 14 columns, 340 bytes
//
#define CL_ATTRNAMEINITIAL_START_STATE (0)
#define CL_ATTRNAMEINITIAL_ACCEPTING_STATES_START (6)
extern const unsigned char cl_attrnameinitial_itt[256];
extern const unsigned char cl_attrnameinitial_stt[6][14];

// utf/cl_AttrName.txt
//
// 203 included, 1113909 excluded, 0 errors.
// 6 states, 14 columns, 340 bytes
//
#define CL_ATTRNAME_START_STATE (0)
#define CL_ATTRNAME_ACCEPTING_STATES_START (6)
extern const unsigned char cl_attrname_itt[256];
extern const unsigned char cl_attrname_stt[6][14];

// utf/cl_ObjectName.txt
//
// 257 included, 1113855 excluded, 0 errors.
// 8 states, 23 columns, 440 bytes
//
#define CL_OBJECTNAME_START_STATE (0)
#define CL_OBJECTNAME_ACCEPTING_STATES_START (8)
extern const unsigned char cl_objectname_itt[256];
extern const unsigned char cl_objectname_stt[8][23];

// utf/cl_PlayerName.txt
//
// 190 included, 1113922 excluded, 0 errors.
// 6 states, 14 columns, 340 bytes
//
#define CL_PLAYERNAME_START_STATE (0)
#define CL_PLAYERNAME_ACCEPTING_STATES_START (6)
extern const unsigned char cl_playername_itt[256];
extern const unsigned char cl_playername_stt[6][14];

// utf/cl_8859_1.txt
//
// 191 included, 1113921 excluded, 0 errors.
// 3 states, 6 columns, 274 bytes
//
#define CL_8859_1_START_STATE (0)
#define CL_8859_1_ACCEPTING_STATES_START (3)
extern const unsigned char cl_8859_1_itt[256];
extern const unsigned char cl_8859_1_stt[3][6];

// utf/cl_8859_2.txt
//
// 191 included, 1113921 excluded, 0 errors.
// 6 states, 21 columns, 382 bytes
//
#define CL_8859_2_START_STATE (0)
#define CL_8859_2_ACCEPTING_STATES_START (6)
extern const unsigned char cl_8859_2_itt[256];
extern const unsigned char cl_8859_2_stt[6][21];

// utf/tr_utf8_latin1.txt
//
// 2461 code points.
// 97 states, 193 columns, 37698 bytes
//
#define TR_LATIN1_START_STATE (0)
#define TR_LATIN1_ACCEPTING_STATES_START (97)
extern const unsigned char tr_latin1_itt[256];
extern const unsigned short tr_latin1_stt[97][193];

// utf/tr_utf8_ascii.txt
//
// 2424 code points.
// 95 states, 193 columns, 18591 bytes
//
#define TR_ASCII_START_STATE (0)
#define TR_ASCII_ACCEPTING_STATES_START (95)
extern const unsigned char tr_ascii_itt[256];
extern const unsigned char tr_ascii_stt[95][193];

// utf/tr_tolower.txt
//
// 1023 code points.
// 46 states, 86 columns, 4212 bytes
//
#define TR_TOLOWER_START_STATE (0)
#define TR_TOLOWER_ACCEPTING_STATES_START (46)
extern const unsigned char tr_tolower_itt[256];
extern const unsigned char tr_tolower_stt[46][86];

#define TR_TOLOWER_DEFAULT (0)
#define TR_TOLOWER_LITERAL_START (1)
#define TR_TOLOWER_XOR_START (13)
extern const string_desc tr_tolower_ott[97];

// utf/tr_toupper.txt
//
// 1030 code points.
// 48 states, 90 columns, 4576 bytes
//
#define TR_TOUPPER_START_STATE (0)
#define TR_TOUPPER_ACCEPTING_STATES_START (48)
extern const unsigned char tr_toupper_itt[256];
extern const unsigned char tr_toupper_stt[48][90];

#define TR_TOUPPER_DEFAULT (0)
#define TR_TOUPPER_LITERAL_START (1)
#define TR_TOUPPER_XOR_START (11)
extern const string_desc tr_toupper_ott[102];

// utf/tr_totitle.txt
//
// 1034 code points.
// 48 states, 90 columns, 4576 bytes
//
#define TR_TOTITLE_START_STATE (0)
#define TR_TOTITLE_ACCEPTING_STATES_START (48)
extern const unsigned char tr_totitle_itt[256];
extern const unsigned char tr_totitle_stt[48][90];

#define TR_TOTITLE_DEFAULT (0)
#define TR_TOTITLE_LITERAL_START (1)
#define TR_TOTITLE_XOR_START (11)
extern const string_desc tr_totitle_ott[100];

// utf/tr_Color.txt
//
// 517 code points.
// 5 states, 13 columns, 321 bytes
//
#define TR_COLOR_START_STATE (0)
#define TR_COLOR_ACCEPTING_STATES_START (5)
extern const unsigned char tr_color_itt[256];
extern const unsigned char tr_color_stt[5][13];

#endif // UTF8TABLES_H
