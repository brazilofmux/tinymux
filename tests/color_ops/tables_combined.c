/*
 * tables_combined.c — Merged Unicode DFA tables for standalone co_* testing.
 *
 * Sources:
 *   ragel/unicode_tables.c          — case mapping with real OTT data
 *   mux/rv64/src/unicode_tables.c   — tr_widths, tr_gcb, cl_extpict tables
 *   mux/rv64/src/softlib.c          — co_console_width() implementation
 *
 * Compile with: -I../../ragel -I../../mux/include
 */

/* --- Part 1: Case mapping tables (with real OTT) --- */
/* ragel/unicode_tables.c includes "unicode_tables.h" which is in ragel/ */
#include "../../ragel/unicode_tables.c"

/* --- Part 2: Width / GCB / ExtPict tables --- */
/* These are defined in mux/rv64/src/unicode_tables.c lines 19-720,
 * but that file also defines case mapping arrays that would conflict.
 * So we extract just the arrays we need via sed at build time into
 * tables_extra.c.  See Makefile. */
