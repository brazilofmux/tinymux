/*! \file utf8tables.h
 * \brief Static tables with UTF-8 state machines.
 *
 */

#ifndef UTF8TABLES_H
#define UTF8TABLES_H

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

// utf/tr_tolower.txt
//
// 1460 code points.
// 62 states, 87 columns, 2080 bytes
//
#define TR_TOLOWER_START_STATE (0)
#define TR_TOLOWER_ACCEPTING_STATES_START (62)
extern LIBMUX_API const unsigned char tr_tolower_itt[256];
extern LIBMUX_API const unsigned short tr_tolower_sot[62];
extern LIBMUX_API const unsigned char tr_tolower_sbt[1700];

#define TR_TOLOWER_DEFAULT (0)
#define TR_TOLOWER_LITERAL_START (1)
#define TR_TOLOWER_XOR_START (28)
extern LIBMUX_API const string_desc tr_tolower_ott[144];

// utf/tr_toupper.txt
//
// 1477 code points.
// 67 states, 90 columns, 2210 bytes
//
#define TR_TOUPPER_START_STATE (0)
#define TR_TOUPPER_ACCEPTING_STATES_START (67)
extern LIBMUX_API const unsigned char tr_toupper_itt[256];
extern LIBMUX_API const unsigned short tr_toupper_sot[67];
extern LIBMUX_API const unsigned char tr_toupper_sbt[1820];

#define TR_TOUPPER_DEFAULT (0)
#define TR_TOUPPER_LITERAL_START (1)
#define TR_TOUPPER_XOR_START (33)
extern LIBMUX_API const string_desc tr_toupper_ott[158];

// utf/tr_totitle.txt
//
// 1481 code points.
// 67 states, 90 columns, 2211 bytes
//
#define TR_TOTITLE_START_STATE (0)
#define TR_TOTITLE_ACCEPTING_STATES_START (67)
extern LIBMUX_API const unsigned char tr_totitle_itt[256];
extern LIBMUX_API const unsigned short tr_totitle_sot[67];
extern LIBMUX_API const unsigned char tr_totitle_sbt[1821];

#define TR_TOTITLE_DEFAULT (0)
#define TR_TOTITLE_LITERAL_START (1)
#define TR_TOTITLE_XOR_START (33)
extern LIBMUX_API const string_desc tr_totitle_ott[156];

// utf/tr_foldmatch.txt
//
// 14 code points.
// 7 states, 11 columns, 310 bytes
//
#define TR_FOLDMATCH_START_STATE (0)
#define TR_FOLDMATCH_ACCEPTING_STATES_START (7)
extern LIBMUX_API const unsigned char tr_foldmatch_itt[256];
extern LIBMUX_API const unsigned char tr_foldmatch_sot[7];
extern LIBMUX_API const unsigned char tr_foldmatch_sbt[47];

#define TR_FOLDMATCH_DEFAULT (0)
#define TR_FOLDMATCH_LITERAL_START (1)
#define TR_FOLDMATCH_XOR_START (3)
extern LIBMUX_API const string_desc tr_foldmatch_ott[3];

// utf/tr_Color.txt
//
// 2053 code points.
// 37 states, 67 columns, 4820 bytes
//
#define TR_COLOR_START_STATE (0)
#define TR_COLOR_ACCEPTING_STATES_START (37)
extern LIBMUX_API const unsigned char tr_color_itt[256];
extern LIBMUX_API const unsigned short tr_color_sot[37];
extern LIBMUX_API const unsigned short tr_color_sbt[2245];

// utf/tr_ccc.txt
//
// 934 code points.
// 132 states, 83 columns, 3702 bytes
//
#define TR_CCC_START_STATE (0)
#define TR_CCC_ACCEPTING_STATES_START (132)
extern LIBMUX_API const unsigned char tr_ccc_itt[256];
extern LIBMUX_API const unsigned short tr_ccc_sot[132];
extern LIBMUX_API const unsigned short tr_ccc_sbt[1591];

// utf/tr_nfcqc.txt
//
// 1252 code points.
// 60 states, 75 columns, 1090 bytes
//
#define TR_NFCQC_START_STATE (0)
#define TR_NFCQC_ACCEPTING_STATES_START (60)
extern LIBMUX_API const unsigned char tr_nfcqc_itt[256];
extern LIBMUX_API const unsigned short tr_nfcqc_sot[60];
extern LIBMUX_API const unsigned char tr_nfcqc_sbt[714];

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
// 129 states, 112 columns, 7592 bytes
//
#define TR_NFC_COMPOSE_START_STATE (0)
#define TR_NFC_COMPOSE_ACCEPTING_STATES_START (129)
extern LIBMUX_API const unsigned char tr_nfc_compose_itt[256];
extern LIBMUX_API const unsigned short tr_nfc_compose_sot[129];
extern LIBMUX_API const unsigned short tr_nfc_compose_sbt[3539];
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
// 27 states, 64 columns, 3238 bytes
//
#define TR_DUCET_CONTRACT_START_STATE (0)
#define TR_DUCET_CONTRACT_ACCEPTING_STATES_START (27)
extern LIBMUX_API const unsigned char tr_ducet_contract_itt[256];
extern LIBMUX_API const unsigned short tr_ducet_contract_sot[27];
extern LIBMUX_API const unsigned short tr_ducet_contract_sbt[1464];
#define TR_DUCET_CONTRACT_NFC_COMPOSE_RESULTS (953)
extern LIBMUX_API const UTF32 tr_ducet_contract_nfc_compose_result[954];


#endif // UTF8TABLES_H
