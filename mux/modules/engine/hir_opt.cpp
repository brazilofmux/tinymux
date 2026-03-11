/*! \file hir_opt.cpp
 * \brief SSA optimization passes for the HIR.
 *
 * Implements:
 *   - Constant folding: arithmetic/comparison/logic on ICONST → ICONST
 *   - ATOI of SCONST → ICONST (compile-time string→int)
 *   - Copy propagation: replace uses of COPY with source
 *   - Dead code elimination: remove unused instructions
 *   - Algebraic simplification: x+0, x*1, x*0, etc.
 *
 * All passes operate on the HIR parallel arrays.  No guest memory
 * or rv_compiler access needed — folding produces ICONST only.
 * ITOA folding (ICONST→SCONST) is deferred to codegen since it
 * requires string pool allocation.
 *
 * For single-block programs (M1/M2), these passes clean up
 * redundant type conversions and constant arithmetic.  They become
 * essential in M4+ when control flow introduces PHI nodes.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "hir.h"

#include <cstring>
#include <cstdlib>

// ---------------------------------------------------------------
// Helper: parse string as int64 (mirrors mux_atol for consistency)
// ---------------------------------------------------------------

static bool parse_int(const std::string &s, int64_t &out) {
    if (s.empty()) { out = 0; return true; }
    const char *p = s.c_str();
    bool neg = false;
    if (*p == '-') { neg = true; p++; }
    if (*p == '\0') { out = 0; return true; }
    int64_t v = 0;
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (*p - '0');
        p++;
    }
    if (*p != '\0') { out = 0; return true; }  // non-numeric → 0 (MUX convention)
    out = neg ? -v : v;
    return true;
}

// ---------------------------------------------------------------
// Constant folding
//
// Single forward pass.  For each instruction, if all operands are
// compile-time constants, replace with the computed result.
// ---------------------------------------------------------------

void hir_const_fold(hir_program &h) {
    bool changed = true;
    while (changed) {
        changed = false;
        for (int i = 0; i < h.n_insns; i++) {
            int s1 = h.src1[i];
            int s2 = h.src2[i];

            switch (h.kind[i]) {

            // ATOI of SCONST → ICONST.
            case HIR_ATOI:
                if (s1 >= 0 && h.kind[s1] == HIR_SCONST) {
                    int64_t v;
                    parse_int(h.sval[s1], v);
                    h.kind[i] = HIR_ICONST;
                    h.ty[i] = TY_INT;
                    h.val[i] = v;
                    h.src1[i] = -1;
                    changed = true;
                }
                // ATOI of ICONST → identity (already int).
                else if (s1 >= 0 && h.kind[s1] == HIR_ICONST) {
                    h.kind[i] = HIR_ICONST;
                    h.ty[i] = TY_INT;
                    h.val[i] = h.val[s1];
                    h.src1[i] = -1;
                    changed = true;
                }
                break;

            // Binary arithmetic on two ICONSTs.
            case HIR_ADD:
                if (s1 >= 0 && s2 >= 0
                    && h.kind[s1] == HIR_ICONST && h.kind[s2] == HIR_ICONST) {
                    h.kind[i] = HIR_ICONST;
                    h.val[i] = h.val[s1] + h.val[s2];
                    h.src1[i] = h.src2[i] = -1;
                    changed = true;
                }
                // Algebraic: x + 0 = x.
                else if (s2 >= 0 && h.kind[s2] == HIR_ICONST && h.val[s2] == 0) {
                    h.kind[i] = HIR_COPY;
                    h.src2[i] = -1;
                    changed = true;
                }
                // Algebraic: 0 + x = x.
                else if (s1 >= 0 && h.kind[s1] == HIR_ICONST && h.val[s1] == 0) {
                    h.kind[i] = HIR_COPY;
                    h.src1[i] = s2;
                    h.src2[i] = -1;
                    changed = true;
                }
                break;

            case HIR_SUB:
                if (s1 >= 0 && s2 >= 0
                    && h.kind[s1] == HIR_ICONST && h.kind[s2] == HIR_ICONST) {
                    h.kind[i] = HIR_ICONST;
                    h.val[i] = h.val[s1] - h.val[s2];
                    h.src1[i] = h.src2[i] = -1;
                    changed = true;
                }
                // Algebraic: x - 0 = x.
                else if (s2 >= 0 && h.kind[s2] == HIR_ICONST && h.val[s2] == 0) {
                    h.kind[i] = HIR_COPY;
                    h.src2[i] = -1;
                    changed = true;
                }
                break;

            case HIR_MUL:
                if (s1 >= 0 && s2 >= 0
                    && h.kind[s1] == HIR_ICONST && h.kind[s2] == HIR_ICONST) {
                    h.kind[i] = HIR_ICONST;
                    h.val[i] = h.val[s1] * h.val[s2];
                    h.src1[i] = h.src2[i] = -1;
                    changed = true;
                }
                // Algebraic: x * 0 = 0.
                else if (s2 >= 0 && h.kind[s2] == HIR_ICONST && h.val[s2] == 0) {
                    h.kind[i] = HIR_ICONST;
                    h.val[i] = 0;
                    h.src1[i] = h.src2[i] = -1;
                    changed = true;
                }
                else if (s1 >= 0 && h.kind[s1] == HIR_ICONST && h.val[s1] == 0) {
                    h.kind[i] = HIR_ICONST;
                    h.val[i] = 0;
                    h.src1[i] = h.src2[i] = -1;
                    changed = true;
                }
                // Algebraic: x * 1 = x.
                else if (s2 >= 0 && h.kind[s2] == HIR_ICONST && h.val[s2] == 1) {
                    h.kind[i] = HIR_COPY;
                    h.src2[i] = -1;
                    changed = true;
                }
                else if (s1 >= 0 && h.kind[s1] == HIR_ICONST && h.val[s1] == 1) {
                    h.kind[i] = HIR_COPY;
                    h.src1[i] = s2;
                    h.src2[i] = -1;
                    changed = true;
                }
                break;

            case HIR_REM:
                if (s1 >= 0 && s2 >= 0
                    && h.kind[s1] == HIR_ICONST && h.kind[s2] == HIR_ICONST
                    && h.val[s2] != 0) {
                    h.kind[i] = HIR_ICONST;
                    h.val[i] = h.val[s1] % h.val[s2];
                    h.src1[i] = h.src2[i] = -1;
                    changed = true;
                }
                break;
            case HIR_DIV:
                if (s1 >= 0 && s2 >= 0
                    && h.kind[s1] == HIR_ICONST && h.kind[s2] == HIR_ICONST
                    && h.val[s2] != 0) {
                    h.kind[i] = HIR_ICONST;
                    h.val[i] = h.val[s1] / h.val[s2];
                    h.src1[i] = h.src2[i] = -1;
                    changed = true;
                }
                break;
            case HIR_ABS:
                if (s1 >= 0 && h.kind[s1] == HIR_ICONST) {
                    h.kind[i] = HIR_ICONST;
                    h.val[i] = (h.val[s1] < 0) ? -h.val[s1] : h.val[s1];
                    h.src1[i] = -1;
                    changed = true;
                }
                break;
            case HIR_SIGN:
                if (s1 >= 0 && h.kind[s1] == HIR_ICONST) {
                    h.kind[i] = HIR_ICONST;
                    h.val[i] = (h.val[s1] < 0) ? -1 : (h.val[s1] > 0) ? 1 : 0;
                    h.src1[i] = -1;
                    changed = true;
                }
                break;
            case HIR_MAX:
                if (s1 >= 0 && s2 >= 0
                    && h.kind[s1] == HIR_ICONST && h.kind[s2] == HIR_ICONST) {
                    h.kind[i] = HIR_ICONST;
                    h.val[i] = (h.val[s1] >= h.val[s2]) ? h.val[s1] : h.val[s2];
                    h.src1[i] = h.src2[i] = -1;
                    changed = true;
                }
                break;
            case HIR_MIN:
                if (s1 >= 0 && s2 >= 0
                    && h.kind[s1] == HIR_ICONST && h.kind[s2] == HIR_ICONST) {
                    h.kind[i] = HIR_ICONST;
                    h.val[i] = (h.val[s1] <= h.val[s2]) ? h.val[s1] : h.val[s2];
                    h.src1[i] = h.src2[i] = -1;
                    changed = true;
                }
                break;

            // Comparisons on two ICONSTs.
            case HIR_EQ:
                if (s1 >= 0 && s2 >= 0
                    && h.kind[s1] == HIR_ICONST && h.kind[s2] == HIR_ICONST) {
                    h.kind[i] = HIR_ICONST;
                    h.val[i] = (h.val[s1] == h.val[s2]) ? 1 : 0;
                    h.src1[i] = h.src2[i] = -1;
                    changed = true;
                }
                break;
            case HIR_NE:
                if (s1 >= 0 && s2 >= 0
                    && h.kind[s1] == HIR_ICONST && h.kind[s2] == HIR_ICONST) {
                    h.kind[i] = HIR_ICONST;
                    h.val[i] = (h.val[s1] != h.val[s2]) ? 1 : 0;
                    h.src1[i] = h.src2[i] = -1;
                    changed = true;
                }
                break;
            case HIR_LT:
                if (s1 >= 0 && s2 >= 0
                    && h.kind[s1] == HIR_ICONST && h.kind[s2] == HIR_ICONST) {
                    h.kind[i] = HIR_ICONST;
                    h.val[i] = (h.val[s1] < h.val[s2]) ? 1 : 0;
                    h.src1[i] = h.src2[i] = -1;
                    changed = true;
                }
                break;
            case HIR_LE:
                if (s1 >= 0 && s2 >= 0
                    && h.kind[s1] == HIR_ICONST && h.kind[s2] == HIR_ICONST) {
                    h.kind[i] = HIR_ICONST;
                    h.val[i] = (h.val[s1] <= h.val[s2]) ? 1 : 0;
                    h.src1[i] = h.src2[i] = -1;
                    changed = true;
                }
                break;
            case HIR_GT:
                if (s1 >= 0 && s2 >= 0
                    && h.kind[s1] == HIR_ICONST && h.kind[s2] == HIR_ICONST) {
                    h.kind[i] = HIR_ICONST;
                    h.val[i] = (h.val[s1] > h.val[s2]) ? 1 : 0;
                    h.src1[i] = h.src2[i] = -1;
                    changed = true;
                }
                break;
            case HIR_GE:
                if (s1 >= 0 && s2 >= 0
                    && h.kind[s1] == HIR_ICONST && h.kind[s2] == HIR_ICONST) {
                    h.kind[i] = HIR_ICONST;
                    h.val[i] = (h.val[s1] >= h.val[s2]) ? 1 : 0;
                    h.src1[i] = h.src2[i] = -1;
                    changed = true;
                }
                break;

            // NOT of ICONST.
            case HIR_NOT:
                if (s1 >= 0 && h.kind[s1] == HIR_ICONST) {
                    h.kind[i] = HIR_ICONST;
                    h.val[i] = (h.val[s1] == 0) ? 1 : 0;
                    h.src1[i] = -1;
                    changed = true;
                }
                break;
            // BOOL (t) of ICONST.
            case HIR_BOOL:
                if (s1 >= 0 && h.kind[s1] == HIR_ICONST) {
                    h.kind[i] = HIR_ICONST;
                    h.val[i] = (h.val[s1] != 0) ? 1 : 0;
                    h.src1[i] = -1;
                    changed = true;
                }
                break;

            // INC / DEC of ICONST.
            case HIR_INC:
                if (s1 >= 0 && h.kind[s1] == HIR_ICONST) {
                    h.kind[i] = HIR_ICONST;
                    h.val[i] = h.val[s1] + 1;
                    h.src1[i] = -1;
                    changed = true;
                }
                break;
            case HIR_DEC:
                if (s1 >= 0 && h.kind[s1] == HIR_ICONST) {
                    h.kind[i] = HIR_ICONST;
                    h.val[i] = h.val[s1] - 1;
                    h.src1[i] = -1;
                    changed = true;
                }
                break;

            // NEG of ICONST.
            case HIR_NEG:
                if (s1 >= 0 && h.kind[s1] == HIR_ICONST) {
                    h.kind[i] = HIR_ICONST;
                    h.val[i] = -h.val[s1];
                    h.src1[i] = -1;
                    changed = true;
                }
                break;

            default:
                break;
            }
        }
    }
}

// ---------------------------------------------------------------
// Copy propagation
//
// Replace all references to COPY instructions with their source.
// Iterates until no more changes (handles chains: COPY of COPY).
// ---------------------------------------------------------------

static int resolve_copy(hir_program &h, int i) {
    while (i >= 0 && h.kind[i] == HIR_COPY && h.src1[i] >= 0) {
        i = h.src1[i];
    }
    return i;
}

void hir_copy_prop(hir_program &h) {
    bool changed = true;
    while (changed) {
        changed = false;
        for (int i = 0; i < h.n_insns; i++) {
            // Propagate through src1.
            if (h.src1[i] >= 0) {
                int r = resolve_copy(h, h.src1[i]);
                if (r != h.src1[i]) {
                    h.src1[i] = r;
                    changed = true;
                }
            }
            // Propagate through src2 (skip BRC — src2 is a block number).
            if (h.src2[i] >= 0 && h.kind[i] != HIR_BRC) {
                int r = resolve_copy(h, h.src2[i]);
                if (r != h.src2[i]) {
                    h.src2[i] = r;
                    changed = true;
                }
            }
            // Propagate through call/strcat arguments.
            if (h.kind[i] == HIR_CALL || h.kind[i] == HIR_STRCAT) {
                int base = h.cbase[i];
                for (int j = 0; j < h.cnargs[i]; j++) {
                    int r = resolve_copy(h, h.carg[base + j]);
                    if (r != h.carg[base + j]) {
                        h.carg[base + j] = r;
                        changed = true;
                    }
                }
            }
            // Propagate through PHI arguments.
            if (h.kind[i] == HIR_PHI) {
                int base = h.pbase[i];
                for (int j = 0; j < h.pnargs[i]; j++) {
                    int r = resolve_copy(h, h.pval[base + j]);
                    if (r != h.pval[base + j]) {
                        h.pval[base + j] = r;
                        changed = true;
                    }
                }
            }
        }

        // Propagate through result.
        if (h.result >= 0) {
            int r = resolve_copy(h, h.result);
            if (r != h.result) {
                h.result = r;
                changed = true;
            }
        }
    }
}

// ---------------------------------------------------------------
// Dead code elimination
//
// Mark all instructions that contribute to the result or have
// side effects.  Replace unmarked instructions with NOP.
// ---------------------------------------------------------------

static bool has_side_effects(hir_kind k) {
    return k == HIR_CALL || k == HIR_STRCAT || k == HIR_STORE_Q
        || k == HIR_RET || k == HIR_BR || k == HIR_BRC;
}

void hir_dce(hir_program &h) {
    bool used[HIR_MAX_INSNS];
    memset(used, 0, sizeof(bool) * h.n_insns);

    // Mark side-effectful instructions.
    for (int i = 0; i < h.n_insns; i++) {
        if (has_side_effects(h.kind[i])) {
            used[i] = true;
        }
    }

    // Mark the result.
    if (h.result >= 0 && h.result < h.n_insns) {
        used[h.result] = true;
    }

    // Propagate: mark operands of used instructions.
    bool changed = true;
    while (changed) {
        changed = false;
        for (int i = 0; i < h.n_insns; i++) {
            if (!used[i]) continue;

            if (h.src1[i] >= 0 && !used[h.src1[i]]) {
                used[h.src1[i]] = true;
                changed = true;
            }
            // Skip BRC — src2 is a block number, not an insn ref.
            if (h.src2[i] >= 0 && h.kind[i] != HIR_BRC
                && !used[h.src2[i]]) {
                used[h.src2[i]] = true;
                changed = true;
            }

            // Call/strcat arguments.
            if (h.kind[i] == HIR_CALL || h.kind[i] == HIR_STRCAT) {
                int base = h.cbase[i];
                for (int j = 0; j < h.cnargs[i]; j++) {
                    int a = h.carg[base + j];
                    if (a >= 0 && !used[a]) {
                        used[a] = true;
                        changed = true;
                    }
                }
            }

            // PHI arguments.
            if (h.kind[i] == HIR_PHI) {
                int base = h.pbase[i];
                for (int j = 0; j < h.pnargs[i]; j++) {
                    int a = h.pval[base + j];
                    if (a >= 0 && !used[a]) {
                        used[a] = true;
                        changed = true;
                    }
                }
            }
        }
    }

    // Replace dead instructions with NOP.
    int eliminated = 0;
    for (int i = 0; i < h.n_insns; i++) {
        if (!used[i] && h.kind[i] != HIR_NOP) {
            h.kind[i] = HIR_NOP;
            h.src1[i] = h.src2[i] = -1;
            eliminated++;
        }
    }
}

// ---------------------------------------------------------------
// Top-level optimization entry point
// ---------------------------------------------------------------

void hir_optimize(hir_program &h) {
    // Run constant folding + copy prop + DCE.
    // Iterate: folding can create new COPYs, copy prop can expose
    // new constant operands, DCE can simplify the graph.
    for (int pass = 0; pass < 3; pass++) {
        int prev = h.n_insns;
        hir_const_fold(h);
        hir_copy_prop(h);
        hir_dce(h);

        // Count remaining live instructions to detect convergence.
        int live = 0;
        for (int i = 0; i < h.n_insns; i++) {
            if (h.kind[i] != HIR_NOP) live++;
        }
        if (live == prev) break;  // converged
        prev = live;
    }
}
