/*! \file hir_ssa.cpp
 * \brief SSA construction for the HIR intermediate representation.
 *
 * Implements:
 *   - CFG construction (predecessors from successor edges)
 *   - Reverse post order (DFS-based)
 *   - Dominator tree (Cooper-Harvey-Kennedy iterative algorithm)
 *   - Dominance frontiers
 *   - PHI insertion (worklist algorithm)
 *   - SSA renaming (domtree walk with version stacks)
 *
 * For single-block programs (M1), hir_build_cfg() sets up trivial
 * CFG data and hir_ssa_construct() is not called.  The infrastructure
 * is exercised when M4 adds control flow (if/switch/iter).
 *
 * Reference: Cooper, Harvey, Kennedy — "A Simple, Fast Dominance
 * Algorithm" (Software Practice & Experience, 2001).
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "hir.h"

#include <cstring>
#include <vector>

// ---------------------------------------------------------------
// CFG construction
// ---------------------------------------------------------------

void hir_build_cfg(hir_program &h) {
    // Compute block instruction ranges from blk[] tags.
    for (int b = 0; b < h.n_blocks; b++) {
        h.block_first[b] = h.n_insns;  // sentinel
        h.block_last[b] = -1;
    }
    for (int i = 0; i < h.n_insns; i++) {
        int b = h.blk[i];
        if (b >= 0 && b < h.n_blocks) {
            if (i < h.block_first[b]) h.block_first[b] = i;
            if (i > h.block_last[b]) h.block_last[b] = i;
        }
    }

    // Build successor edges from terminator instructions (BR/BRC).
    // The lowering already called add_edge(), but rebuild from
    // instructions for correctness after optimization.
    for (int b = 0; b < h.n_blocks; b++) {
        h.block_nsucc[b] = 0;
        h.block_succ[b][0] = h.block_succ[b][1] = -1;
    }
    for (int i = 0; i < h.n_insns; i++) {
        int b = h.blk[i];
        if (h.kind[i] == HIR_BR) {
            int target = static_cast<int>(h.val[i]);
            if (h.block_nsucc[b] < 2) {
                h.block_succ[b][h.block_nsucc[b]++] = target;
            }
        } else if (h.kind[i] == HIR_BRC) {
            int true_blk = static_cast<int>(h.val[i]);
            int false_blk = h.src2[i];
            if (h.block_nsucc[b] < 2) {
                h.block_succ[b][h.block_nsucc[b]++] = true_blk;
            }
            if (h.block_nsucc[b] < 2) {
                h.block_succ[b][h.block_nsucc[b]++] = false_blk;
            }
        }
    }

    // For single-block programs, set up trivially and return.
    if (h.n_blocks == 1) {
        h.n_pred[0] = 0;
        h.pred_base[0] = 0;
        h.n_pred_total = 0;
        h.n_rpo = 1;
        h.rpo[0] = 0;
        h.rpo_pos[0] = 0;
        h.idom[0] = -1;
        return;
    }

    // Build predecessor lists from successor edges.
    for (int b = 0; b < h.n_blocks; b++) {
        h.n_pred[b] = 0;
    }
    for (int b = 0; b < h.n_blocks; b++) {
        for (int j = 0; j < h.block_nsucc[b]; j++) {
            int s = h.block_succ[b][j];
            if (s >= 0 && s < h.n_blocks) {
                h.n_pred[s]++;
            }
        }
    }

    h.n_pred_total = 0;
    for (int b = 0; b < h.n_blocks; b++) {
        h.pred_base[b] = h.n_pred_total;
        h.n_pred_total += h.n_pred[b];
    }

    int fill[HIR_MAX_BLOCKS];
    memset(fill, 0, sizeof(int) * h.n_blocks);
    for (int b = 0; b < h.n_blocks; b++) {
        for (int j = 0; j < h.block_nsucc[b]; j++) {
            int s = h.block_succ[b][j];
            if (s >= 0 && s < h.n_blocks) {
                h.pred_list[h.pred_base[s] + fill[s]] = b;
                fill[s]++;
            }
        }
    }
}

// ---------------------------------------------------------------
// Reverse post order via DFS
// ---------------------------------------------------------------

static void rpo_dfs(hir_program &h, int b, bool *visited, int &pos) {
    visited[b] = true;
    for (int j = 0; j < h.block_nsucc[b]; j++) {
        int s = h.block_succ[b][j];
        if (s >= 0 && s < h.n_blocks && !visited[s]) {
            rpo_dfs(h, s, visited, pos);
        }
    }
    h.rpo[pos] = b;
    h.rpo_pos[b] = pos;
    pos--;
}

static void hir_compute_rpo(hir_program &h) {
    bool visited[HIR_MAX_BLOCKS];
    memset(visited, 0, sizeof(bool) * h.n_blocks);
    int pos = h.n_blocks - 1;
    rpo_dfs(h, 0, visited, pos);
    h.n_rpo = h.n_blocks - (pos + 1);

    // Shift RPO array if some blocks are unreachable.
    if (pos >= 0) {
        int shift = pos + 1;
        for (int i = 0; i < h.n_rpo; i++) {
            h.rpo[i] = h.rpo[i + shift];
            h.rpo_pos[h.rpo[i]] = i;
        }
    }
}

// ---------------------------------------------------------------
// Dominator tree — Cooper-Harvey-Kennedy iterative algorithm
// ---------------------------------------------------------------

static int intersect(hir_program &h, int b1, int b2) {
    int f1 = h.rpo_pos[b1];
    int f2 = h.rpo_pos[b2];
    while (f1 != f2) {
        while (f1 > f2) {
            b1 = h.idom[b1];
            f1 = h.rpo_pos[b1];
        }
        while (f2 > f1) {
            b2 = h.idom[b2];
            f2 = h.rpo_pos[b2];
        }
    }
    return b1;
}

static void hir_compute_idom(hir_program &h) {
    // Initialize: entry dominates itself, rest undefined.
    for (int b = 0; b < h.n_blocks; b++) {
        h.idom[b] = -1;
    }
    h.idom[0] = 0;  // entry node

    bool changed = true;
    while (changed) {
        changed = false;
        // Process in RPO, skip entry (rpo[0] == 0).
        for (int ri = 1; ri < h.n_rpo; ri++) {
            int b = h.rpo[ri];
            int new_idom = -1;

            // Find first processed predecessor.
            for (int j = 0; j < h.n_pred[b]; j++) {
                int p = h.pred_list[h.pred_base[b] + j];
                if (h.idom[p] != -1) {
                    if (new_idom == -1) {
                        new_idom = p;
                    } else {
                        new_idom = intersect(h, new_idom, p);
                    }
                }
            }

            if (new_idom != h.idom[b]) {
                h.idom[b] = new_idom;
                changed = true;
            }
        }
    }
}

// ---------------------------------------------------------------
// Dominance frontiers
//
// Stored in a local structure since they're only needed during
// PHI insertion.
// ---------------------------------------------------------------

struct dom_frontiers {
    std::vector<int> df[HIR_MAX_BLOCKS];
};

static void hir_compute_df(hir_program &h, dom_frontiers &dfr) {
    for (int b = 0; b < h.n_blocks; b++) {
        dfr.df[b].clear();
    }

    for (int b = 0; b < h.n_blocks; b++) {
        if (h.n_pred[b] < 2) continue;  // no join point

        for (int j = 0; j < h.n_pred[b]; j++) {
            int runner = h.pred_list[h.pred_base[b] + j];
            while (runner != h.idom[b] && runner != -1) {
                dfr.df[runner].push_back(b);
                runner = h.idom[runner];
            }
        }
    }
}

// ---------------------------------------------------------------
// PHI insertion
//
// For each %q register that has STORE_Q instructions, insert PHI
// nodes at the dominance frontier of each defining block.
// Standard worklist algorithm.
// ---------------------------------------------------------------

static void hir_insert_phis(hir_program &h, dom_frontiers &dfr) {
    // Find which blocks define each %q register (contain STORE_Q).
    bool defs[HIR_NUM_QREGS][HIR_MAX_BLOCKS];
    memset(defs, 0, sizeof(defs));
    bool has_stores[HIR_NUM_QREGS];
    memset(has_stores, 0, sizeof(has_stores));

    for (int i = 0; i < h.n_insns; i++) {
        if (h.kind[i] == HIR_STORE_Q) {
            int qn = static_cast<int>(h.val[i]);
            if (qn >= 0 && qn < HIR_NUM_QREGS) {
                defs[qn][h.blk[i]] = true;
                has_stores[qn] = true;
            }
        }
    }

    // For each %q register with stores, insert PHI nodes.
    bool has_phi[HIR_NUM_QREGS][HIR_MAX_BLOCKS];
    memset(has_phi, 0, sizeof(has_phi));

    for (int qn = 0; qn < HIR_NUM_QREGS; qn++) {
        if (!has_stores[qn]) continue;

        // Worklist: blocks that define qn.
        std::vector<int> worklist;
        for (int b = 0; b < h.n_blocks; b++) {
            if (defs[qn][b]) worklist.push_back(b);
        }

        while (!worklist.empty()) {
            int b = worklist.back();
            worklist.pop_back();

            for (int df_b : dfr.df[b]) {
                if (!has_phi[qn][df_b]) {
                    has_phi[qn][df_b] = true;

                    // Insert PHI at the beginning of df_b.
                    // For now, emit with empty arguments — renaming
                    // fills in the actual values.
                    int phi = h.emit_phi(TY_STRING, qn, nullptr, nullptr, 0);
                    if (phi >= 0) {
                        h.blk[phi] = df_b;
                    }

                    // PHI is also a definition — propagate.
                    if (!defs[qn][df_b]) {
                        defs[qn][df_b] = true;
                        worklist.push_back(df_b);
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------
// SSA renaming — domtree walk with version stacks
//
// Replaces LOAD_Q with direct references to the most recent
// definition (STORE_Q value, PHI result, or initial undef).
// Fills in PHI arguments for each predecessor.
// ---------------------------------------------------------------

static void hir_rename_block(hir_program &h, int b,
                              std::vector<int> stacks[HIR_NUM_QREGS]) {
    // Save stack depths for rollback.
    int saved_depths[HIR_NUM_QREGS];
    for (int q = 0; q < HIR_NUM_QREGS; q++) {
        saved_depths[q] = static_cast<int>(stacks[q].size());
    }

    // Process instructions in block b.
    for (int i = h.block_first[b]; i <= h.block_last[b] && i < h.n_insns; i++) {
        if (h.blk[i] != b) continue;

        if (h.kind[i] == HIR_PHI) {
            int qn = static_cast<int>(h.val[i]);
            if (qn >= 0 && qn < HIR_NUM_QREGS) {
                stacks[qn].push_back(i);
            }
        } else if (h.kind[i] == HIR_LOAD_Q) {
            int qn = static_cast<int>(h.val[i]);
            if (qn >= 0 && qn < HIR_NUM_QREGS && !stacks[qn].empty()) {
                // Replace LOAD_Q with COPY of the current definition.
                int def = stacks[qn].back();
                h.kind[i] = HIR_COPY;
                h.src1[i] = def;
                h.ty[i] = h.ty[def];
            }
        } else if (h.kind[i] == HIR_STORE_Q) {
            int qn = static_cast<int>(h.val[i]);
            if (qn >= 0 && qn < HIR_NUM_QREGS) {
                stacks[qn].push_back(h.src1[i]);
            }
        }
    }

    // Fill PHI arguments in successor blocks.
    for (int j = 0; j < h.block_nsucc[b]; j++) {
        int succ = h.block_succ[b][j];
        if (succ < 0) continue;

        // Find PHI nodes in successor.
        for (int i = h.block_first[succ];
             i <= h.block_last[succ] && i < h.n_insns; i++) {
            if (h.blk[i] != succ) continue;
            if (h.kind[i] != HIR_PHI) continue;

            int qn = static_cast<int>(h.val[i]);
            if (qn >= 0 && qn < HIR_NUM_QREGS && !stacks[qn].empty()) {
                // Add (b, current_def) as a PHI argument.
                if (h.n_pargs < HIR_MAX_PARGS) {
                    // Extend this PHI's arguments.
                    // Since we build PHIs with 0 args initially,
                    // and pbase was set during emit_phi, we append.
                    int pi = h.pbase[i] + h.pnargs[i];
                    if (pi < HIR_MAX_PARGS) {
                        h.pblk[pi] = b;
                        h.pval[pi] = stacks[qn].back();
                        h.pnargs[i]++;
                        if (pi >= h.n_pargs) h.n_pargs = pi + 1;
                    }
                }
            }
        }
    }

    // Recurse into dominated children.
    for (int c = 0; c < h.n_blocks; c++) {
        if (h.idom[c] == b && c != b) {
            hir_rename_block(h, c, stacks);
        }
    }

    // Restore stacks.
    for (int q = 0; q < HIR_NUM_QREGS; q++) {
        stacks[q].resize(saved_depths[q]);
    }
}

static void hir_rename(hir_program &h) {
    std::vector<int> stacks[HIR_NUM_QREGS];
    hir_rename_block(h, 0, stacks);
}

// ---------------------------------------------------------------
// Top-level SSA construction
// ---------------------------------------------------------------

void hir_ssa_construct(hir_program &h) {
    // Step 1: Compute RPO.
    hir_compute_rpo(h);

    // Step 2: Compute dominator tree.
    hir_compute_idom(h);

    // Step 3: Compute dominance frontiers.
    dom_frontiers dfr;
    hir_compute_df(h, dfr);

    // Step 4: Insert PHI nodes.
    hir_insert_phis(h, dfr);

    // Step 5: Rename (replace LOAD_Q with direct refs, fill PHI args).
    hir_rename(h);
}
