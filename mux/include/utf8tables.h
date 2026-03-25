/*! \file utf8tables.h
 * \brief Static tables with UTF-8 state machines.
 *
 */

#ifndef UTF8TABLES_H
#define UTF8TABLES_H

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

// utf/cl_Printable.txt
//
// 155134 included, 958978 excluded, 0 errors.
// 314 states, 91 columns, 10078 bytes
//
#define CL_PRINT_START_STATE (0)
#define CL_PRINT_ACCEPTING_STATES_START (314)
extern LIBMUX_API const unsigned char cl_print_itt[256];
extern LIBMUX_API const unsigned short cl_print_sot[314];
extern LIBMUX_API const unsigned short cl_print_sbt[4597];

// utf/cl_Alpha.txt
//
// 141028 included, 973084 excluded, 0 errors.
// 298 states, 95 columns, 8842 bytes
//
#define CL_ALPHA_START_STATE (0)
#define CL_ALPHA_ACCEPTING_STATES_START (298)
extern LIBMUX_API const unsigned char cl_alpha_itt[256];
extern LIBMUX_API const unsigned short cl_alpha_sot[298];
extern LIBMUX_API const unsigned short cl_alpha_sbt[3995];

// utf/cl_Digit.txt
//
// 760 included, 1113352 excluded, 0 errors.
// 25 states, 65 columns, 624 bytes
//
#define CL_DIGIT_START_STATE (0)
#define CL_DIGIT_ACCEPTING_STATES_START (25)
extern LIBMUX_API const unsigned char cl_digit_itt[256];
extern LIBMUX_API const unsigned short cl_digit_sot[25];
extern LIBMUX_API const unsigned char cl_digit_sbt[318];

// utf/cl_Alnum.txt
//
// 141788 included, 972324 excluded, 0 errors.
// 314 states, 95 columns, 9440 bytes
//
#define CL_ALNUM_START_STATE (0)
#define CL_ALNUM_ACCEPTING_STATES_START (314)
extern LIBMUX_API const unsigned char cl_alnum_itt[256];
extern LIBMUX_API const unsigned short cl_alnum_sot[314];
extern LIBMUX_API const unsigned short cl_alnum_sbt[4278];

// utf/cl_AttrNameInitial.txt
//
// 177 included, 1113935 excluded, 0 errors.
// 6 states, 14 columns, 312 bytes
//
#define CL_ATTRNAMEINITIAL_START_STATE (0)
#define CL_ATTRNAMEINITIAL_ACCEPTING_STATES_START (6)
extern LIBMUX_API const unsigned char cl_attrnameinitial_itt[256];
extern LIBMUX_API const unsigned char cl_attrnameinitial_sot[6];
extern LIBMUX_API const unsigned char cl_attrnameinitial_sbt[50];

// utf/cl_AttrName.txt
//
// 203 included, 1113909 excluded, 0 errors.
// 6 states, 14 columns, 312 bytes
//
#define CL_ATTRNAME_START_STATE (0)
#define CL_ATTRNAME_ACCEPTING_STATES_START (6)
extern LIBMUX_API const unsigned char cl_attrname_itt[256];
extern LIBMUX_API const unsigned char cl_attrname_sot[6];
extern LIBMUX_API const unsigned char cl_attrname_sbt[50];

// utf/cl_ObjectName.txt
//
// 87250 included, 1026862 excluded, 0 errors.
// 32 states, 63 columns, 643 bytes
//
#define CL_OBJECTNAME_START_STATE (0)
#define CL_OBJECTNAME_ACCEPTING_STATES_START (32)
extern LIBMUX_API const unsigned char cl_objectname_itt[256];
extern LIBMUX_API const unsigned short cl_objectname_sot[32];
extern LIBMUX_API const unsigned char cl_objectname_sbt[323];

// utf/cl_PlayerName.txt
//
// 87183 included, 1026929 excluded, 0 errors.
// 31 states, 58 columns, 626 bytes
//
#define CL_PLAYERNAME_START_STATE (0)
#define CL_PLAYERNAME_ACCEPTING_STATES_START (31)
extern LIBMUX_API const unsigned char cl_playername_itt[256];
extern LIBMUX_API const unsigned short cl_playername_sot[31];
extern LIBMUX_API const unsigned char cl_playername_sbt[308];

// utf/cl_8859_1.txt
//
// 191 included, 1113921 excluded, 0 errors.
// 3 states, 6 columns, 279 bytes
//
#define CL_8859_1_START_STATE (0)
#define CL_8859_1_ACCEPTING_STATES_START (3)
extern LIBMUX_API const unsigned char cl_8859_1_itt[256];
extern LIBMUX_API const unsigned char cl_8859_1_sot[3];
extern LIBMUX_API const unsigned char cl_8859_1_sbt[20];

// utf/cl_8859_2.txt
//
// 191 included, 1113921 excluded, 0 errors.
// 6 states, 21 columns, 335 bytes
//
#define CL_8859_2_START_STATE (0)
#define CL_8859_2_ACCEPTING_STATES_START (6)
extern LIBMUX_API const unsigned char cl_8859_2_itt[256];
extern LIBMUX_API const unsigned char cl_8859_2_sot[6];
extern LIBMUX_API const unsigned char cl_8859_2_sbt[73];

// utf/cl_hangul.txt
//
// 11172 included, 1102940 excluded, 0 errors.
// 6 states, 9 columns, 298 bytes
//
#define CL_HANGUL_START_STATE (0)
#define CL_HANGUL_ACCEPTING_STATES_START (6)
extern LIBMUX_API const unsigned char cl_hangul_itt[256];
extern LIBMUX_API const unsigned char cl_hangul_sot[6];
extern LIBMUX_API const unsigned char cl_hangul_sbt[36];

// utf/cl_hiragana.txt
//
// 94 included, 1114018 excluded, 0 errors.
// 8 states, 12 columns, 315 bytes
//
#define CL_HIRAGANA_START_STATE (0)
#define CL_HIRAGANA_ACCEPTING_STATES_START (8)
extern LIBMUX_API const unsigned char cl_hiragana_itt[256];
extern LIBMUX_API const unsigned char cl_hiragana_sot[8];
extern LIBMUX_API const unsigned char cl_hiragana_sbt[51];

// utf/cl_kanji.txt
//
// 75616 included, 1038496 excluded, 0 errors.
// 18 states, 30 columns, 417 bytes
//
#define CL_KANJI_START_STATE (0)
#define CL_KANJI_ACCEPTING_STATES_START (18)
extern LIBMUX_API const unsigned char cl_kanji_itt[256];
extern LIBMUX_API const unsigned char cl_kanji_sot[18];
extern LIBMUX_API const unsigned char cl_kanji_sbt[143];

// utf/cl_katakana.txt
//
// 115 included, 1113997 excluded, 0 errors.
// 8 states, 12 columns, 315 bytes
//
#define CL_KATAKANA_START_STATE (0)
#define CL_KATAKANA_ACCEPTING_STATES_START (8)
extern LIBMUX_API const unsigned char cl_katakana_itt[256];
extern LIBMUX_API const unsigned char cl_katakana_sot[8];
extern LIBMUX_API const unsigned char cl_katakana_sbt[51];

// utf/tr_utf8_ascii.txt
//
// 2538 code points.
// 99 states, 193 columns, 3885 bytes
//
#define TR_ASCII_START_STATE (0)
#define TR_ASCII_ACCEPTING_STATES_START (99)
extern LIBMUX_API const unsigned char tr_ascii_itt[256];
extern LIBMUX_API const unsigned short tr_ascii_sot[99];
extern LIBMUX_API const unsigned char tr_ascii_sbt[3431];

// utf/tr_utf8_cp437.txt
//
// 2813 code points.
// 114 states, 194 columns, 8614 bytes
//
#define TR_CP437_START_STATE (0)
#define TR_CP437_ACCEPTING_STATES_START (114)
extern LIBMUX_API const unsigned char tr_cp437_itt[256];
extern LIBMUX_API const unsigned short tr_cp437_sot[114];
extern LIBMUX_API const unsigned short tr_cp437_sbt[4065];

// utf/tr_utf8_latin1.txt
//
// 2579 code points.
// 101 states, 193 columns, 7434 bytes
//
#define TR_LATIN1_START_STATE (0)
#define TR_LATIN1_ACCEPTING_STATES_START (101)
extern LIBMUX_API const unsigned char tr_latin1_itt[256];
extern LIBMUX_API const unsigned short tr_latin1_sot[101];
extern LIBMUX_API const unsigned short tr_latin1_sbt[3488];

// utf/tr_utf8_latin2.txt
//
// 2543 code points.
// 99 states, 193 columns, 7374 bytes
//
#define TR_LATIN2_START_STATE (0)
#define TR_LATIN2_ACCEPTING_STATES_START (99)
extern LIBMUX_API const unsigned char tr_latin2_itt[256];
extern LIBMUX_API const unsigned short tr_latin2_sot[99];
extern LIBMUX_API const unsigned short tr_latin2_sbt[3460];

// utf/tr_widths.txt
//
// 355061 code points.
// 373 states, 94 columns, 12416 bytes
//
#define TR_WIDTHS_START_STATE (0)
#define TR_WIDTHS_ACCEPTING_STATES_START (373)
extern LIBMUX_API const unsigned char tr_widths_itt[256];
extern LIBMUX_API const unsigned short tr_widths_sot[373];
extern LIBMUX_API const unsigned short tr_widths_sbt[5707];

// utf/tr_Color.txt
//
// 517 code points.
// 11 states, 66 columns, 1418 bytes
//
#define TR_COLOR_START_STATE (0)
#define TR_COLOR_ACCEPTING_STATES_START (11)
extern LIBMUX_API const unsigned char tr_color_itt[256];
extern LIBMUX_API const unsigned short tr_color_sot[11];
extern LIBMUX_API const unsigned short tr_color_sbt[570];

// utf/tr_ccc_nfcqc.txt
//
// 2143 code points.
// 164 states, 84 columns, 4846 bytes
//
#define TR_CCC_NFCQC_START_STATE (0)
#define TR_CCC_NFCQC_ACCEPTING_STATES_START (164)
extern LIBMUX_API const unsigned char tr_ccc_nfcqc_itt[256];
extern LIBMUX_API const unsigned short tr_ccc_nfcqc_sot[164];
extern LIBMUX_API const unsigned short tr_ccc_nfcqc_sbt[2131];

// utf/tr_nfd.txt
//
// 2081 code points.
// 97 states, 85 columns, 6542 bytes
//
#define TR_NFD_START_STATE (0)
#define TR_NFD_ACCEPTING_STATES_START (97)
extern LIBMUX_API const unsigned char tr_nfd_itt[256];
extern LIBMUX_API const unsigned short tr_nfd_sot[97];
extern LIBMUX_API const unsigned short tr_nfd_sbt[3046];

#define TR_NFD_DEFAULT (0)
#define TR_NFD_LITERAL_START (1)
#define TR_NFD_XOR_START (1332)
extern LIBMUX_API const string_desc tr_nfd_ott[2045];

// utf/tr_compose.txt
//
// 964 composition pairs.
// 1010 states, 141 columns, 19004 bytes
//
#define TR_NFC_COMPOSE_START_STATE (0)
#define TR_NFC_COMPOSE_ACCEPTING_STATES_START (1010)
extern LIBMUX_API const unsigned char tr_nfc_compose_itt[256];
extern LIBMUX_API const unsigned short tr_nfc_compose_sot[1010];
extern LIBMUX_API const unsigned short tr_nfc_compose_sbt[8364];
#define TR_NFC_COMPOSE_NFC_COMPOSE_RESULTS (964)
extern LIBMUX_API const UTF32 tr_nfc_compose_nfc_compose_result[965];

// utf/tr_gcb.txt
//
// 17602 code points.
// 208 states, 92 columns, 3594 bytes
//
#define TR_GCB_START_STATE (0)
#define TR_GCB_ACCEPTING_STATES_START (208)
extern LIBMUX_API const unsigned char tr_gcb_itt[256];
extern LIBMUX_API const unsigned short tr_gcb_sot[208];
extern LIBMUX_API const unsigned char tr_gcb_sbt[2922];

// utf/cl_ExtPict.txt
//
// 3537 included, 1110575 excluded, 0 errors.
// 44 states, 66 columns, 854 bytes
//
#define CL_EXTPICT_START_STATE (0)
#define CL_EXTPICT_ACCEPTING_STATES_START (44)
extern LIBMUX_API const unsigned char cl_extpict_itt[256];
extern LIBMUX_API const unsigned short cl_extpict_sot[44];
extern LIBMUX_API const unsigned char cl_extpict_sbt[510];

// utf/tr_ducet.txt
//
// 38443 code points.
// 702 states, 206 columns, 87942 bytes
//
#define TR_DUCET_START_STATE (0)
#define TR_DUCET_ACCEPTING_STATES_START (702)
extern LIBMUX_API const unsigned char tr_ducet_itt[256];
extern LIBMUX_API const unsigned short tr_ducet_sot[702];
extern LIBMUX_API const unsigned short tr_ducet_sbt[43141];

// utf/tr_ducet_contract.txt
//
// 956 composition pairs.
// 294 states, 77 columns, 6470 bytes
//
#define TR_DUCET_CONTRACT_START_STATE (0)
#define TR_DUCET_CONTRACT_ACCEPTING_STATES_START (294)
extern LIBMUX_API const unsigned char tr_ducet_contract_itt[256];
extern LIBMUX_API const unsigned short tr_ducet_contract_sot[294];
extern LIBMUX_API const unsigned short tr_ducet_contract_sbt[2813];
#define TR_DUCET_CONTRACT_NFC_COMPOSE_RESULTS (953)
extern LIBMUX_API const UTF32 tr_ducet_contract_nfc_compose_result[954];


#ifdef __cplusplus
}
#endif

#endif // UTF8TABLES_H
