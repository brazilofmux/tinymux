/*! \file dbt_x64_div_gen.cpp
 * \brief Dump the x86-64 DBT div/rem guard sequences to raw byte blobs.
 *
 * Part of the x86-64 idiv regression check (dbt_x64_div_qemu_test.sh).
 * Compiles natively on any host — it only *emits* x86-64 bytes via the
 * project's real emitter (dbt_emit_x64.h), it does not execute them.
 * The companion harness loads these blobs and runs them under qemu so the
 * AArch64 dev box can verify the x86-64 idiv guards (#811).
 *
 * Usage: dbt_x64_div_gen <out_dir>   (writes blob_<form>.bin, + RET)
 */
#include "dbt_emit_x64.h"
#include <cstdio>
#include <string>

static void dump(const std::string &dir, const char *name, void (*fn)(emit_t *)) {
    uint8_t buf[256];
    emit_t e; e.buf = buf; e.offset = 0; e.capacity = sizeof(buf);
    fn(&e);
    emit_byte(&e, 0xC3);  // RET — the harness strips this and adds its own epilogue
    std::string path = dir + "/blob_" + name + ".bin";
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) { perror(path.c_str()); return; }
    fwrite(buf, 1, e.offset, f);
    fclose(f);
    printf("  %-8s %u bytes\n", name, e.offset);
}

int main(int argc, char **argv) {
    std::string dir = (argc > 1) ? argv[1] : ".";
    dump(dir, "div64",  emit_rv_div64);
    dump(dir, "divu64", emit_rv_divu64);
    dump(dir, "rem64",  emit_rv_rem64);
    dump(dir, "remu64", emit_rv_remu64);
    dump(dir, "divw",   emit_rv_divw);
    dump(dir, "divuw",  emit_rv_divuw);
    dump(dir, "remw",   emit_rv_remw);
    dump(dir, "remuw",  emit_rv_remuw);
    return 0;
}
