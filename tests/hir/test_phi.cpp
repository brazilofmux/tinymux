// test_phi.cpp — #2166 emit_phi single-input contract.
//
// A PHI with exactly one input IS its input.  Emitting it as a real
// node was a latent wrong-value bug: superblock formation merges the
// single-pred edge away and leaves a PHI with no incoming edges, whose
// slot no copy ever fills — empty output while jit_handled reports
// success.  emit_phi therefore returns the input value directly for
// nargs == 1, builds a real node for nargs >= 2, and must keep
// building an EMPTY node for nargs == 0 — SSA construction reserves
// zero-argument PHIs and fills them during renaming.

#include "hir.h"

#include <cstdio>

int main() {
    hir_program h;
    h.init();

    // Two values in the entry block, then a diamond-shaped merge.
    int a = h.emit_iconst(1);
    int b = h.emit_iconst(2);
    int blk_a = h.new_block();
    int blk_b = h.new_block();
    int merge = h.new_block();
    h.add_edge(0, blk_a);
    h.add_edge(0, blk_b);
    h.add_edge(blk_a, merge);
    h.add_edge(blk_b, merge);
    h.cur_block = merge;

    // nargs == 1: the value comes back, and NO node is emitted.
    int before = h.n_insns;
    int blocks1[1] = { blk_a };
    int vals1[1] = { a };
    int r1 = h.emit_phi(TY_INT, -1, blocks1, vals1, 1);
    if (r1 != a) {
        std::fprintf(stderr, "emit_phi(1 input) returned %d, expected the "
                     "input value %d\n", r1, a);
        return 1;
    }
    if (h.n_insns != before) {
        std::fprintf(stderr, "emit_phi(1 input) emitted %d node(s); a "
                     "single-input PHI must not exist\n",
                     h.n_insns - before);
        return 1;
    }

    // nargs == 2: a real PHI node with both arguments recorded.
    int blocks2[2] = { blk_a, blk_b };
    int vals2[2] = { a, b };
    int r2 = h.emit_phi(TY_INT, -1, blocks2, vals2, 2);
    if (r2 < 0 || h.kind[r2] != HIR_PHI || h.pnargs[r2] != 2
        || h.pval[h.pbase[r2]] != a || h.pval[h.pbase[r2] + 1] != b
        || h.pblk[h.pbase[r2]] != blk_a || h.pblk[h.pbase[r2] + 1] != blk_b) {
        std::fprintf(stderr, "emit_phi(2 inputs) did not build a proper "
                     "PHI node\n");
        return 1;
    }

    // nargs == 0: still a real (empty) node — the SSA-construction
    // reservation contract.  Returning a value here would break rename.
    int r0 = h.emit_phi(TY_INT, 3, nullptr, nullptr, 0);
    if (r0 < 0 || h.kind[r0] != HIR_PHI || h.pnargs[r0] != 0) {
        std::fprintf(stderr, "emit_phi(0 inputs) must reserve an empty "
                     "PHI node for SSA renaming\n");
        return 1;
    }

    if (h.overflowed) {
        std::fprintf(stderr, "program unexpectedly overflowed\n");
        return 1;
    }

    std::printf("ok - emit_phi: 1 input returns the value (no node), "
                "2 inputs builds a PHI, 0 inputs reserves for SSA\n");
    return 0;
}
