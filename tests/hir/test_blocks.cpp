// test_blocks.cpp — #1863 HIR CFG edge bounds after block exhaustion.
//
// hir_program::new_block() returns -1 when HIR_MAX_BLOCKS is full and
// sets overflowed.  add_edge() used to index block_nsucc[src] with that
// -1 before any caller checked overflowed.  This program fills the
// block table, then passes invalid IDs to add_edge; under ASan/UBSan it
// must exit 0 with no report.

#include "hir.h"

#include <cstdio>
#include <cstdlib>

int main() {
    hir_program h;
    h.init();

    // Exhaust the block table (block 0 already exists from init).
    //
    int allocated = 0;
    for (;;) {
        int b = h.new_block();
        if (b < 0) {
            break;
        }
        allocated++;
        if (allocated > HIR_MAX_BLOCKS + 8) {
            std::fprintf(stderr, "new_block never returned -1\n");
            return 1;
        }
    }
    if (!h.overflowed) {
        std::fprintf(stderr, "new_block returned -1 without overflowed\n");
        return 1;
    }
    if (h.n_blocks != HIR_MAX_BLOCKS) {
        std::fprintf(stderr, "n_blocks=%d expected %d\n",
                     h.n_blocks, HIR_MAX_BLOCKS);
        return 1;
    }

    // These would OOB-write before #1863.
    //
    h.add_edge(-1, 0);
    h.add_edge(0, -1);
    h.add_edge(-1, -1);
    h.add_edge(h.n_blocks, 0);
    h.add_edge(0, h.n_blocks);
    h.add_edge(99999, 99999);

    if (!h.overflowed) {
        std::fprintf(stderr, "add_edge cleared overflowed unexpectedly\n");
        return 1;
    }

    // Valid edge still accepted (block 0 -> 0 is a self-loop; nsucc cap 2).
    //
    h.add_edge(0, 0);
    if (h.block_nsucc[0] < 1 || h.block_succ[0][0] != 0) {
        std::fprintf(stderr, "valid add_edge(0,0) failed\n");
        return 1;
    }

    std::printf("ok - HIR block exhaustion: add_edge rejects -1 without OOB "
                "(%d blocks allocated past entry)\n",
                allocated);
    return 0;
}
