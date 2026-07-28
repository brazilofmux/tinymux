// test_mux_table.cpp — unit tests for mux_table_* (#1667 Phase 3).
//
// Layout helpers sit on co_copy_field: display columns, pad, CJK width, and
// multi-field emit (header and data share the same construction).
//
// Build (Windows, after netmux.sln Release):
//   cl /nologo /EHsc /O2 /std:c++17 /utf-8 /DWIN32 /DNDEBUG /DUNICODE
//      /I..\..\mux\include test_mux_table.cpp /Fe:test_mux_table.exe
//      ..\..\mux\bin_release\libmux.lib
//   copy libmux.dll next to the exe before running.

#include "autoconf.h"
#include "config.h"
#include "mux_format.h"
#include "mux_table.h"
#include "color_ops.h"

#include <cstdio>
#include <cstring>

static int g_pass = 0;
static int g_fail = 0;

static void expect_str(const char *name, const UTF8 *got, const char *want)
{
    if (0 == strcmp(reinterpret_cast<const char *>(got), want))
    {
        g_pass++;
        return;
    }
    g_fail++;
    printf("not ok - %s\n  got  [%s]\n  want [%s]\n",
           name, reinterpret_cast<const char *>(got), want);
}

static void expect_size(const char *name, size_t got, size_t want)
{
    if (got == want)
    {
        g_pass++;
        return;
    }
    g_fail++;
    printf("not ok - %s: got %zu want %zu\n", name, got, want);
}

static void test_ljust_ascii(void)
{
    UTF8 buf[64];
    size_t pos = mux_table_append_ljust(buf, sizeof(buf), 0,
        reinterpret_cast<const UTF8 *>("ab"), 5);
    expect_size("ljust ascii pos", pos, 5);
    expect_str("ljust ascii", buf, "ab   ");
}

static void test_trunc_ascii(void)
{
    UTF8 buf[64];
    size_t pos = mux_table_append_trunc(buf, sizeof(buf), 0,
        reinterpret_cast<const UTF8 *>("abcdef"), 3);
    expect_size("trunc ascii pos", pos, 3);
    expect_str("trunc ascii", buf, "abc");
}

static void test_cjk_width(void)
{
    // "日本" is two fullwidth ideographs → 4 display columns when both fit.
    const UTF8 *jp = reinterpret_cast<const UTF8 *>("日本");
    UTF8 buf[64];
    size_t pos = mux_table_append_ljust(buf, sizeof(buf), 0, jp, 4);
    expect_size("cjk full pos", pos, strlen(reinterpret_cast<const char *>(jp)));
    expect_str("cjk full", buf, "日本");

    // One display column is not enough for a width-2 ideograph → empty field,
    // then pad to width 1 with a space (same as co_copy_field empty + pad).
    pos = mux_table_append_ljust(buf, sizeof(buf), 0, jp, 1);
    expect_size("cjk into 1 col pos", pos, 1);
    expect_str("cjk into 1 col", buf, " ");
}

static void test_emit_fields_header_and_row(void)
{
    mux_table_col cols[] = { { 6 }, { 4 } };
    const UTF8 *header[] = {
        reinterpret_cast<const UTF8 *>("Name"),
        reinterpret_cast<const UTF8 *>("On")
    };
    const UTF8 *row[] = {
        reinterpret_cast<const UTF8 *>("Wizard"),
        reinterpret_cast<const UTF8 *>("yes")
    };

    UTF8 hbuf[64];
    UTF8 rbuf[64];
    size_t hp = mux_table_emit_fields(hbuf, sizeof(hbuf), cols, 2, header, " ");
    size_t rp = mux_table_emit_fields(rbuf, sizeof(rbuf), cols, 2, row, " ");
    expect_size("header width", hp, 6 + 1 + 4);
    expect_size("row width", rp, 6 + 1 + 4);
    expect_str("header line", hbuf, "Name   On  ");
    expect_str("row line", rbuf, "Wizard yes ");
}

static void test_bytes_and_pos_after(void)
{
    UTF8 buf[64];
    size_t pos = 0;
    pos = mux_table_append_bytes(buf, sizeof(buf), pos, "[");
    pos = mux_table_append_bytes(buf, sizeof(buf), pos, "N");
    pos = mux_table_append_bytes(buf, sizeof(buf), pos, "] ");
    expect_str("bytes chrome", buf, "[N] ");
    expect_size("bytes pos", pos, 4);

    mux_sprintf(buf + pos, sizeof(buf) - pos,
        reinterpret_cast<const UTF8 *>("%d"), 12);
    pos = mux_table_pos_after(buf, sizeof(buf), pos);
    expect_str("after sprintf", buf, "[N] 12");
    expect_size("pos after", pos, 6);
}

int main(void)
{
    test_ljust_ascii();
    test_trunc_ascii();
    test_cjk_width();
    test_emit_fields_header_and_row();
    test_bytes_and_pos_after();

    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
