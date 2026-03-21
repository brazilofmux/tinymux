/*! \file hir_opt.cpp
 * \brief SSA optimization passes for the HIR.
 *
 * Implements:
 *   - Constant folding: arithmetic/comparison/logic on ICONST → ICONST
 *   - ATOI of SCONST → ICONST (compile-time string→int)
 *   - Copy propagation: replace uses of COPY with source
 *   - Global Value Numbering (GVN): dominator-tree walk with scoped
 *     hash table — subsumes CSE, sees through COPYs
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
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------
// Value Numbering Key for CSE
// ---------------------------------------------------------------

struct ValueKey {
    hir_kind kind;
    hir_type ty;
    int      src1;
    int      src2;
    int64_t  val;

    bool operator==(const ValueKey &other) const {
        return kind == other.kind && ty == other.ty && src1 == other.src1
            && src2 == other.src2 && val == other.val;
    }
};

struct ValueKeyHash {
    size_t operator()(const ValueKey &k) const {
        size_t h = std::hash<int>{}(static_cast<int>(k.kind));
        h ^= std::hash<int>{}(static_cast<int>(k.ty)) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(k.src1) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(k.src2) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int64_t>{}(k.val) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

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
        || k == HIR_SETQ_SYNC
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
// Common Subexpression Elimination (CSE)
//
// Walk instructions in block order.  For each pure instruction,
// check if an identical (kind, ty, src1, src2, val) exists earlier
// in a dominating block.  If so, replace the duplicate with a COPY
// referencing the original.  ECALLs and side-effecting instructions
// are never CSE candidates (they may produce different results on
// repeated calls, e.g., rand()).
// ---------------------------------------------------------------

static bool is_pure_op(hir_kind k) {
    switch (k) {
        case HIR_ADD: case HIR_SUB: case HIR_MUL: case HIR_DIV:
        case HIR_REM: case HIR_NEG: case HIR_ABS: case HIR_SIGN:
        case HIR_MAX: case HIR_MIN:
        case HIR_EQ: case HIR_NE: case HIR_LT: case HIR_LE:
        case HIR_GT: case HIR_GE:
        case HIR_NOT: case HIR_BOOL:
        case HIR_INC: case HIR_DEC:
        case HIR_ATOI: case HIR_ITOA: case HIR_STRCMP:
        case HIR_LUA_GETI: case HIR_LUA_SETI:
        case HIR_FADD: case HIR_FSUB: case HIR_FMUL: case HIR_FDIV:
        case HIR_FNEG:
        case HIR_FEQ: case HIR_FLT: case HIR_FLE:
        case HIR_ITOF: case HIR_FTOI:
            return true;
        default:
            return false;
    }
}

// Keep old name as alias for LICM and any other consumer.
static bool is_cse_candidate(hir_kind k) { return is_pure_op(k); }

// ---------------------------------------------------------------
// Global Value Numbering (GVN)
//
// Dominator-tree walk with a scoped hash table.  Strictly more
// powerful than the old CSE: uses value numbers of operands
// (not raw instruction indices) so it sees through COPYs, and
// the dominator-tree walk guarantees that only dominating values
// are visible — no per-candidate dominance check needed.
//
// Subsumes the old hir_cse().
// ---------------------------------------------------------------

// Resolve an instruction to its value number: chase COPY chains.
//
static int vn_resolve(const hir_program &h, const int *vn, int i) {
    if (i < 0) return i;
    // Chase COPYs to their source's value number.
    int limit = 64;
    while (i >= 0 && i < h.n_insns && h.kind[i] == HIR_COPY && --limit > 0) {
        i = h.src1[i];
    }
    if (i >= 0 && i < h.n_insns) return vn[i];
    return i;
}

// Scoped value table: entries added in a dominator subtree are
// removed when leaving.  Each scope records what to undo.
//
struct GVNScope {
    // Keys inserted in this scope that need removal on exit.
    std::vector<ValueKey> inserted;
    // Keys that were overwritten — restore on exit.
    std::vector<std::pair<ValueKey, int>> overwritten;
};

static void gvn_process_block(
    hir_program &h,
    int b,
    int *vn,
    std::unordered_map<ValueKey, int, ValueKeyHash> &table)
{
    GVNScope scope;

    // Process all instructions in this block.
    for (int i = 0; i < h.n_insns; i++) {
        if (h.blk[i] != b) continue;
        if (h.kind[i] == HIR_NOP) continue;

        // Every instruction's value number defaults to itself.
        vn[i] = i;

        // COPYs just inherit their source's value number.
        if (h.kind[i] == HIR_COPY) {
            vn[i] = vn_resolve(h, vn, h.src1[i]);
            continue;
        }

        // PHIs: value number is self (can't look through them here).
        if (h.kind[i] == HIR_PHI) continue;

        // Only pure operations participate in numbering.
        if (!is_pure_op(h.kind[i])) continue;

        // Build key using value-numbered operands.
        int vn_src1 = vn_resolve(h, vn, h.src1[i]);
        int vn_src2 = vn_resolve(h, vn, h.src2[i]);
        ValueKey key = { h.kind[i], h.ty[i], vn_src1, vn_src2, h.val[i] };

        // Normalize commutative operations.
        if (h.kind[i] == HIR_ADD || h.kind[i] == HIR_MUL ||
            h.kind[i] == HIR_EQ  || h.kind[i] == HIR_NE ||
            h.kind[i] == HIR_FADD || h.kind[i] == HIR_FMUL) {
            if (key.src1 > key.src2) {
                std::swap(key.src1, key.src2);
            }
        }

        auto it = table.find(key);
        if (it != table.end()) {
            // Value already computed by a dominating instruction.
            // Replace this instruction with a COPY.
            vn[i] = it->second;
            h.kind[i] = HIR_COPY;
            h.src1[i] = it->second;
            h.src2[i] = -1;
            h.val[i] = 0;
        } else {
            // New value — add to table.
            vn[i] = i;
            table[key] = i;
            scope.inserted.push_back(key);
        }
    }

    // Recurse into dominated children.
    for (int c = 0; c < h.n_blocks; c++) {
        if (h.idom[c] == b && c != b) {
            gvn_process_block(h, c, vn, table);
        }
    }

    // Undo this scope: remove entries added in this block.
    for (auto &key : scope.inserted) {
        table.erase(key);
    }
}

void hir_gvn(hir_program &h) {
    int vn[HIR_MAX_INSNS];
    for (int i = 0; i < h.n_insns; i++) {
        vn[i] = i;
    }

    std::unordered_map<ValueKey, int, ValueKeyHash> table;
    gvn_process_block(h, 0, vn, table);
}

// ---------------------------------------------------------------
// Loop-Invariant Code Motion (LICM)
//
// For each loop (identified by back-edges), move pure instructions
// whose operands are all defined outside the loop to the preheader.
// "Moving" an instruction means changing its block assignment.
// The preheader is the unique predecessor of the header that is NOT
// the latch (i.e., the entry block for our structured iter loops).
// ---------------------------------------------------------------

void hir_licm(hir_program &h) {
    if (h.n_rpo == 0) return;

    // Find back-edges and process each loop.
    for (int latch = 0; latch < h.n_blocks; latch++) {
        for (int s = 0; s < h.block_nsucc[latch]; s++) {
            int header = h.block_succ[latch][s];
            if (header < 0) continue;
            if (h.rpo_pos[header] > h.rpo_pos[latch]) continue;

            // Back-edge: latch → header.
            // Find preheader: predecessor of header that is NOT in the loop.
            int preheader = -1;
            for (int p = 0; p < h.n_pred[header]; p++) {
                int pred = h.pred_list[h.pred_base[header] + p];
                if (h.rpo_pos[pred] < h.rpo_pos[header]) {
                    preheader = pred;
                    break;
                }
            }
            if (preheader < 0) continue;

            // Mark which blocks are in this loop (RPO between header
            // and latch inclusive).
            bool in_loop[HIR_MAX_BLOCKS];
            memset(in_loop, 0, sizeof(in_loop));
            for (int b = 0; b < h.n_blocks; b++) {
                if (h.rpo_pos[b] >= h.rpo_pos[header] &&
                    h.rpo_pos[b] <= h.rpo_pos[latch]) {
                    in_loop[b] = true;
                }
            }

            // Iteratively hoist loop-invariant instructions.
            bool moved = true;
            while (moved) {
                moved = false;
                for (int i = 0; i < h.n_insns; i++) {
                    if (h.kind[i] == HIR_NOP) continue;
                    if (!in_loop[h.blk[i]]) continue;
                    if (!is_cse_candidate(h.kind[i])) continue;

                    // Check if all operands are loop-invariant
                    // (defined outside the loop or already hoisted).
                    auto is_invariant = [&](int v) -> bool {
                        if (v < 0) return true;
                        if (v >= h.n_insns) return true;
                        if (h.kind[v] == HIR_ICONST || h.kind[v] == HIR_SCONST)
                            return true;
                        return !in_loop[h.blk[v]];
                    };

                    if (!is_invariant(h.src1[i])) continue;
                    if (h.kind[i] != HIR_BRC && !is_invariant(h.src2[i]))
                        continue;

                    // Hoist: move to preheader.
                    h.blk[i] = preheader;
                    moved = true;
                }
            }
        }
    }
}

// ---------------------------------------------------------------
// Top-level optimization entry point
// ---------------------------------------------------------------

void hir_optimize(hir_program &h) {
    // Run constant folding + copy prop + CSE + DCE.
    // Iterate: folding can create new COPYs, copy prop can expose
    // new constant operands, CSE replaces duplicates with COPYs,
    // DCE can simplify the graph.
    for (int pass = 0; pass < 3; pass++) {
        int prev = h.n_insns;
        hir_const_fold(h);
        hir_copy_prop(h);
        hir_gvn(h);
        hir_dce(h);

        // Count remaining live instructions to detect convergence.
        int live = 0;
        for (int i = 0; i < h.n_insns; i++) {
            if (h.kind[i] != HIR_NOP) live++;
        }
        if (live == prev) break;  // converged
        prev = live;
    }

    // LICM runs once after the main optimization loop.
    // It benefits from cleaned-up IR (constants folded, copies
    // propagated, dead code eliminated).
    if (h.n_blocks > 1) {
        hir_licm(h);
    }
}
