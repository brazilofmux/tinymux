/*! \file hir_lower.cpp
 * \brief HIR lowering: AST to HIR translation.
 *
 * Constant folding, type tracking, and the hir_lower_* family
 * that translates AST nodes into linear HIR instructions.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"
#include "ast.h"

#include "dbt_compile.h"
#include "engine_api.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <vector>
#include <string>

// Constant folding: evaluate known functions at compile time.
//
// Uses the same libmux functions (is_integer, mux_atof, fval, etc.)
// that the real engine functions use, so results are identical.
// ---------------------------------------------------------------

// Format a double result the same way fval() does: use a temporary
// buffer and call fval, then extract the string.
//
static std::string format_double(double val) {
    LBuf buf = LBuf_Src("format_double");
    UTF8 *bufc = buf;
    fval(buf, &bufc, val);
    *bufc = '\0';
    return std::string(reinterpret_cast<const char *>(buf.get()));
}

static std::string format_long(long val) {
    UTF8 buf[64];
    UTF8 *bufc = buf;
    safe_ltoa(val, buf, &bufc);
    *bufc = '\0';
    return std::string(reinterpret_cast<const char *>(buf));
}

// Maximum digit table for add() overflow detection — same as funmath.cpp.
//
static const long nMaximums[10] = {
    0, 9, 99, 999, 9999, 99999, 999999, 9999999, 99999999, 999999999
};

// Try to constant-fold a function call.
// Returns true and sets result if successful.
//
// Uses the same libmux functions that the engine uses at runtime,
// so results are bit-identical.
//


// Helper: two-arg integer fast path (same guard as funmath.cpp).
//
static inline bool two_int9(const std::string &a, const std::string &b,
                            long &va, long &vb) {
    int nDigits;
    if (is_integer(u8(a), &nDigits) && nDigits <= 9
        && is_integer(u8(b), &nDigits) && nDigits <= 9) {
        va = mux_atol(u8(a));
        vb = mux_atol(u8(b));
        return true;
    }
    return false;
}

// Helper: fold a two-arg comparison (int fast path, float fallback).
//
template<typename IntCmp, typename DblCmp>
static bool fold_cmp2(const std::vector<std::string> &args,
                      std::string &result,
                      IntCmp icmp, DblCmp dcmp) {
    long va, vb;
    if (two_int9(args[0], args[1], va, vb)) {
        result = icmp(va, vb) ? "1" : "0";
    } else {
        double da = mux_atof(u8(args[0]));
        double db = mux_atof(u8(args[1]));
        result = dcmp(da, db) ? "1" : "0";
    }
    return true;
}

// Helper: xlate() equivalent for constant strings.
// Matches the real xlate() logic: #-xxx=false, #xxx=true,
// number=nonzero, empty=false, other=true.
//
static bool const_xlate(const std::string &s) {
    if (s.empty()) return false;
    if (s[0] == '#') {
        return !(s.size() > 1 && s[1] == '-');
    }
    // Try as number: zero is false, nonzero is true.
    int nDigits;
    if (is_integer(u8(s), &nDigits)) {
        return mux_atol(u8(s)) != 0;
    }
    double d = mux_atof(u8(s));
    if (d != 0.0) return true;
    // If it parsed as a float zero, it's false.  If it didn't
    // parse as a number at all, it's true (non-empty string).
    // The real xlate uses ParseFloat — we approximate: if
    // mux_atof returns 0 and string isn't "0"-like, it's true.
    if (s == "0" || s == "0.0" || s == "+0" || s == "-0") return false;
    // Non-numeric non-empty string.
    return true;
}

static bool try_fold(const std::string &func_name,
                     const std::vector<std::string> &args,
                     std::string &result) {

    // Uppercase for comparison.
    std::string upper = func_name;
    for (auto &c : upper) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));

    int nargs = static_cast<int>(args.size());

    // =============================================================
    // Arithmetic
    // =============================================================

    // --- ADD(a, b, ...) ---
    if (upper == "ADD" && nargs >= 2) {
        bool all_int = true;
        long nMaxValue = 0;
        for (int i = 0; i < nargs; i++) {
            int nDigits;
            if (!is_integer(u8(args[i]), &nDigits)
                || nDigits > 9
                || (nMaxValue += nMaximums[nDigits]) > 999999999L) {
                all_int = false;
                break;
            }
        }
        if (all_int) {
            long sum = 0;
            for (int i = 0; i < nargs; i++) sum += mux_atol(u8(args[i]));
            result = format_long(sum);
        } else {
            double sum = 0.0;
            for (int i = 0; i < nargs; i++) sum += mux_atof(u8(args[i]));
            result = format_double(sum);
        }
        return true;
    }

    // --- SUB(a, b) ---
    if (upper == "SUB" && nargs == 2) {
        long va, vb;
        if (two_int9(args[0], args[1], va, vb)) {
            result = format_long(va - vb);
        } else {
            result = format_double(mux_atof(u8(args[0])) - mux_atof(u8(args[1])));
        }
        return true;
    }

    // --- MUL(a, b, ...) ---
    if (upper == "MUL" && nargs >= 2) {
        double prod = 1.0;
        for (int i = 0; i < nargs; i++) prod *= mux_atof(u8(args[i]));
        result = format_double(NearestPretty(prod));
        return true;
    }

    // --- FDIV(a, b) ---
    if (upper == "FDIV" && nargs == 2) {
        double top = mux_atof(u8(args[0]));
        double bot = mux_atof(u8(args[1]));
        if (bot == 0.0) {
            if (top > 0.0) result = "+Inf";
            else if (top < 0.0) result = "-Inf";
            else result = "Ind";
        } else {
            result = format_double(top / bot);
        }
        return true;
    }

    // --- IDIV(a, b) ---
    if (upper == "IDIV" && nargs == 2) {
        int64_t top = mux_atoi64(u8(args[0]));
        int64_t bot = mux_atoi64(u8(args[1]));
        if (bot == 0) {
            result = "#-1 DIVIDE BY ZERO";
        } else {
            UTF8 buf[64]; UTF8 *bufc = buf;
            safe_i64toa(i64Division(top, bot), buf, &bufc);
            *bufc = '\0';
            result = reinterpret_cast<const char *>(buf);
        }
        return true;
    }

    // --- MOD(a, b) ---
    if (upper == "MOD" && nargs == 2) {
        int64_t top = mux_atoi64(u8(args[0]));
        int64_t bot = mux_atoi64(u8(args[1]));
        if (bot == 0) bot = 1;
        UTF8 buf[64];
        UTF8 *bufc = buf;
        safe_i64toa(i64Mod(top, bot), buf, &bufc);
        *bufc = '\0';
        result = reinterpret_cast<const char *>(buf);
        return true;
    }

    // --- INC(a) / DEC(a) ---
    if (upper == "INC") {
        int64_t v = (nargs >= 1) ? mux_atoi64(u8(args[0])) : 0;
        UTF8 buf[64]; UTF8 *bufc = buf;
        safe_i64toa(v + 1, buf, &bufc);
        *bufc = '\0';
        result = reinterpret_cast<const char *>(buf);
        return true;
    }
    if (upper == "DEC") {
        int64_t v = (nargs >= 1) ? mux_atoi64(u8(args[0])) : 0;
        UTF8 buf[64]; UTF8 *bufc = buf;
        safe_i64toa(v - 1, buf, &bufc);
        *bufc = '\0';
        result = reinterpret_cast<const char *>(buf);
        return true;
    }

    // --- ABS(a) ---
    if (upper == "ABS" && nargs == 1) {
        double d = mux_atof(u8(args[0]));
        result = format_double(fabs(d));
        return true;
    }

    // --- SIGN(a) ---
    if (upper == "SIGN" && nargs == 1) {
        double d = mux_atof(u8(args[0]));
        if (d > 0.0) result = "1";
        else if (d < 0.0) result = "-1";
        else result = "0";
        return true;
    }

    // --- FLOOR / CEIL / TRUNC / ROUND ---
    if (upper == "FLOOR" && nargs == 1) {
        result = format_double(floor(mux_atof(u8(args[0]))));
        return true;
    }
    if (upper == "CEIL" && nargs == 1) {
        result = format_double(ceil(mux_atof(u8(args[0]))));
        return true;
    }
    if (upper == "TRUNC" && nargs == 1) {
        double d = mux_atof(u8(args[0]));
        double ip;
        modf(d, &ip);
        result = format_double(ip);
        return true;
    }
    if (upper == "ROUND" && nargs == 1) {
        result = format_double(round(mux_atof(u8(args[0]))));
        return true;
    }

    // --- MAX(a, b, ...) / MIN(a, b, ...) ---
    if (upper == "MAX" && nargs >= 1) {
        double m = mux_atof(u8(args[0]));
        for (int i = 1; i < nargs; i++) {
            double d = mux_atof(u8(args[i]));
            if (d > m) m = d;
        }
        result = format_double(m);
        return true;
    }
    if (upper == "MIN" && nargs >= 1) {
        double m = mux_atof(u8(args[0]));
        for (int i = 1; i < nargs; i++) {
            double d = mux_atof(u8(args[i]));
            if (d < m) m = d;
        }
        result = format_double(m);
        return true;
    }
    if (upper == "BOUND" && nargs == 3) {
        long x = mux_atol(u8(args[0]));
        long lo = mux_atol(u8(args[1]));
        long hi = mux_atol(u8(args[2]));
        if (x < lo) x = lo;
        if (x > hi) x = hi;
        result = format_long(x);
        return true;
    }

    // =============================================================
    // Comparisons (return "0" or "1")
    // =============================================================

    if (upper == "EQ" && nargs == 2) {
        // Matches fun_eq: int fast path, then string, then float.
        long va, vb;
        if (two_int9(args[0], args[1], va, vb)) {
            result = (va == vb) ? "1" : "0";
        } else if (args[0] == args[1]) {
            result = "1";
        } else {
            double da = mux_atof(u8(args[0]));
            double db = mux_atof(u8(args[1]));
            result = (da == db) ? "1" : "0";
        }
        return true;
    }
    if (upper == "NEQ" && nargs == 2) {
        long va, vb;
        if (two_int9(args[0], args[1], va, vb)) {
            result = (va != vb) ? "1" : "0";
        } else if (args[0] == args[1]) {
            result = "0";
        } else {
            double da = mux_atof(u8(args[0]));
            double db = mux_atof(u8(args[1]));
            result = (da != db) ? "1" : "0";
        }
        return true;
    }
    if (upper == "GT" && nargs == 2)
        return fold_cmp2(args, result,
            [](long a, long b) { return a > b; },
            [](double a, double b) { return a > b; });
    if (upper == "GTE" && nargs == 2)
        return fold_cmp2(args, result,
            [](long a, long b) { return a >= b; },
            [](double a, double b) { return a >= b; });
    if (upper == "LT" && nargs == 2)
        return fold_cmp2(args, result,
            [](long a, long b) { return a < b; },
            [](double a, double b) { return a < b; });
    if (upper == "LTE" && nargs == 2)
        return fold_cmp2(args, result,
            [](long a, long b) { return a <= b; },
            [](double a, double b) { return a <= b; });

    // --- COMP(a, b) — string comparison, returns -1/0/1 ---
    if (upper == "COMP" && nargs >= 2) {
        // Default: ASCII comparison (simplified — real impl has Unicode collation).
        int cmp = strcmp(args[0].c_str(), args[1].c_str());
        if (cmp < 0) result = "-1";
        else if (cmp > 0) result = "1";
        else result = "0";
        return true;
    }

    // =============================================================
    // Boolean
    // =============================================================

    if (upper == "NOT" && nargs == 1) {
        result = const_xlate(args[0]) ? "0" : "1";
        return true;
    }
    if (upper == "T") {
        if (nargs == 0) { result = "0"; return true; }
        result = const_xlate(args[0]) ? "1" : "0";
        return true;
    }

    // =============================================================
    // String functions
    // =============================================================

    if (upper == "STRLEN" && nargs == 1) {
        result = format_long(static_cast<long>(args[0].size()));
        return true;
    }

    if (upper == "CAT") {
        std::string merged;
        for (int i = 0; i < nargs; i++) {
            if (i > 0) merged += ' ';
            merged += args[i];
        }
        result = merged;
        return true;
    }

    if (upper == "STRCAT") {
        std::string merged;
        for (int i = 0; i < nargs; i++) merged += args[i];
        result = merged;
        return true;
    }

    if (upper == "LCSTR" && nargs == 1) {
        LBuf out = LBuf_Src("hir_lcstr");
        size_t n = co_tolower(reinterpret_cast<unsigned char *>(out.get()),
            reinterpret_cast<const unsigned char *>(args[0].data()),
            args[0].size());
        result.assign(reinterpret_cast<const char *>(out.get()), n);
        return true;
    }

    if (upper == "UCSTR" && nargs == 1) {
        LBuf out = LBuf_Src("hir_ucstr");
        size_t n = co_toupper(reinterpret_cast<unsigned char *>(out.get()),
            reinterpret_cast<const unsigned char *>(args[0].data()),
            args[0].size());
        result.assign(reinterpret_cast<const char *>(out.get()), n);
        return true;
    }

    if (upper == "CAPSTR" && nargs == 1) {
        LBuf out = LBuf_Src("hir_capstr");
        size_t n = co_totitle(reinterpret_cast<unsigned char *>(out.get()),
            reinterpret_cast<const unsigned char *>(args[0].data()),
            args[0].size());
        result.assign(reinterpret_cast<const char *>(out.get()), n);
        return true;
    }

    if (upper == "REVERSE" && nargs == 1) {
        unsigned char out[LBUF_SIZE];
        size_t n = co_reverse(out,
            reinterpret_cast<const unsigned char *>(args[0].data()),
            args[0].size());
        result.assign(reinterpret_cast<const char *>(out), n);
        return true;
    }

    if (upper == "ESCAPE" && nargs == 1) {
        unsigned char out[LBUF_SIZE];
        size_t n = co_escape(out,
            reinterpret_cast<const unsigned char *>(args[0].data()),
            args[0].size());
        result.assign(reinterpret_cast<const char *>(out), n);
        return true;
    }

    // String functions (MID, FIRST, REST, WORDS, POS, STRMATCH) are
    // NOT constant-folded — their edge-case behavior (negative indices,
    // custom delimiters, Unicode graphemes, wildcard matching) is too
    // complex to replicate here.  Let them go through ECALL.

    return false;
}

// ---------------------------------------------------------------
// Type tracking for native integer arithmetic
// ---------------------------------------------------------------

// Functions known to always return integer strings.
//
bool returns_int(const std::string &upper) {
    return upper == "RAND" || upper == "STRLEN" || upper == "WORDS"
        || upper == "POS" || upper == "EQ" || upper == "NEQ"
        || upper == "GT" || upper == "GTE" || upper == "LT" || upper == "LTE"
        || upper == "NOT" || upper == "T" || upper == "COMP"
        || upper == "INC" || upper == "DEC" || upper == "SIGN"
        || upper == "MOD" || upper == "ABS" || upper == "MAX"
        || upper == "MIN" || upper == "BOUND" || upper == "IDIV"
        || upper == "STRMATCH" || upper == "MEMBER";
}

// Functions known to always return floating-point strings.
//
static bool returns_float(const std::string &upper) {
    return upper == "SIN" || upper == "COS" || upper == "TAN"
        || upper == "ASIN" || upper == "ACOS" || upper == "ATAN"
        || upper == "ATAN2" || upper == "EXP" || upper == "LOG"
        || upper == "LOG10" || upper == "LN" || upper == "SQRT"
        || upper == "POWER" || upper == "FDIV" || upper == "FMOD"
        || upper == "CEIL" || upper == "FLOOR"
        || upper == "ROUND" || upper == "TRUNC"
        || upper == "PI" || upper == "E"
        || upper == "DIST2D" || upper == "DIST3D"
        || upper == "CTU"
        || upper == "MEAN" || upper == "MEDIAN" || upper == "STDDEV";
}

// Unary FP math functions: map MUX name → blob symbol name.
// These are the raw libm stubs in the Tier 2 blob that the DBT
// intercepts and executes natively via registered intrinsics.
//
struct fp_math_entry {
    const char *mux_name;
    const char *blob_sym;
    int         fmath;      // fmath_id for compile-time folding
};

static const fp_math_entry s_fp_unary[] = {
    { "SIN",   "sin",   FMATH_SIN   },
    { "COS",   "cos",   FMATH_COS   },
    { "TAN",   "tan",   FMATH_TAN   },
    { "ASIN",  "asin",  FMATH_ASIN  },
    { "ACOS",  "acos",  FMATH_ACOS  },
    { "ATAN",  "atan",  FMATH_ATAN  },
    { "EXP",   "exp",   FMATH_EXP   },
    { "LOG",   "log",   FMATH_LOG   },
    { "LOG10", "log10", FMATH_LOG10 },
    { "SQRT",  "sqrt",  FMATH_SQRT  },
    { "CEIL",  "ceil",  FMATH_CEIL  },
    { "FLOOR", "floor", FMATH_FLOOR },
    { "ABS",   "fabs",  FMATH_FABS  },
    { nullptr, nullptr, 0 }
};

static const fp_math_entry s_fp_binary[] = {
    { "POWER", "pow",   FMATH_POW   },
    { "ATAN2", "atan2", FMATH_ATAN2 },
    { "FMOD",  "fmod",  FMATH_FMOD  },
    { nullptr, nullptr, 0 }
};

// Look up the blob address for a direct FP intrinsic call.
// Returns 0 if the blob is not loaded or the symbol is missing.
//
static uint64_t fp_intrinsic_addr(const char *blob_sym) {
    return tier2_sym_addr(blob_sym);
}

// (Old compile_node chain removed — replaced by HIR pipeline below.)
// ===============================================================
// HIR LOWERING: AST → HIR
//
// Produces a linear sequence of HIR instructions from the AST.
// Constant folding and native arithmetic decisions happen here.
//
// %q register tracking (M2):
//   For single-block programs, setq/setr/r are handled at compile
//   time via the qreg[] array.  Each entry tracks the HIR instruction
//   index currently holding that register's value.
//   For multi-block programs (M4+), STORE_Q/LOAD_Q instructions
//   are emitted and SSA construction promotes them.
// ===============================================================

// Compile-time %q register tracking.
static int qreg[HIR_NUM_QREGS];
static bool qreg_used;  // true if any setq/setr/r was seen

void qreg_init() {
    for (int i = 0; i < HIR_NUM_QREGS; i++) qreg[i] = -1;
    qreg_used = false;
}

// Compile-time eval flag tracking.
//
// s_compile_eval: the EV_* flags in effect for the current compilation.
// s_fcheck_available: true if the next AST_FUNCCALL at sequence top-level
//   should receive the EV_FCHECK function check.  Consumed after the
//   first non-space child of a SEQUENCE (mirroring ast_eval_node).
//
int  s_compile_eval;
bool s_fcheck_available;

// Tier 3 compile-time state: deps collector and inline depth.
// Set by compile_expression() before calling hir_lower_node().
//
std::vector<compiled_program::inline_dep> *s_compile_deps = nullptr;
int s_inline_depth = 0;
static constexpr int MAX_INLINE_DEPTH = 3;

static bool hir_is_malformed_qsubst(const ASTNode *node) {
    if (!node || node->type != AST_SUBST) return false;
    const std::string &txt = node->text;
    return txt.size() >= 3
        && txt[0] == '%'
        && (txt[1] == 'q' || txt[1] == 'Q')
        && txt[2] == '<'
        && txt.find('>', 3) == std::string::npos;
}

// Check whether a function name is known to the engine (builtin or ufunc).
// Returns true if the function exists and would be dispatched as a call.
//
static bool is_known_function(const char *upper_name) {
    // Check engine API table (indexed dispatch).
    if (engine_api_lookup(upper_name) > 0) return true;

    // Check builtin_functions map (string dispatch).
    size_t nLen = strlen(upper_name);
    std::vector<UTF8> key(reinterpret_cast<const UTF8 *>(upper_name),
                          reinterpret_cast<const UTF8 *>(upper_name) + nLen);
    if (mudstate.builtin_functions.find(key) != mudstate.builtin_functions.end())
        return true;

    // Check user-defined functions.
    if (mudstate.ufunc_htab.find(key) != mudstate.ufunc_htab.end())
        return true;

    return false;
}

// Internal Q register indices for iter() loop state.
// These are promoted to PHI nodes by SSA construction.
//
static constexpr int QREG_ITER_INUM   = 10;  // iteration counter (0-based, TY_INT)
static constexpr int QREG_ITER_ACC    = 11;  // accumulated result string
static constexpr int QREG_ITER_CURSOR = 12;  // byte offset into list (TY_STRING)

// Iter context: set during body lowering so AST_SUBST nodes (## / #@)
// resolve to the current element and 1-based index.
//
static int iter_itext_val = -1;  // HIR value: current element (TY_STRING)
static int iter_inum1_val = -1;  // HIR value: 1-based index (TY_INT)


// Lower a sequence node (string concatenation).
//
static int hir_lower_sequence(hir_program &h, rv_compiler &rc,
                               const ASTNode *node) {
    if (node->children.size() == 1) {
        return hir_lower_node(h, rc, node->children[0].get());
    }

    size_t first = 0;
    size_t last = node->children.size();
    if (mudconf.space_compress && !(s_compile_eval & EV_NO_COMPRESS)) {
        while (last > first && hir_is_malformed_qsubst(node->children[last - 1].get())) {
            last--;
        }
        while (last > first && node->children[last - 1]->type == AST_SPACE) {
            last--;
        }
    }
    if (first == last) {
        uint64_t addr = rc.pool_str("");
        return h.emit_sconst(addr, "");
    }
    if (last - first == 1) {
        return hir_lower_node(h, rc, node->children[first].get());
    }

    // EV_FCHECK without EV_FMAND: only the first effective (non-space)
    // child gets the function check.  After lowering that child, clear
    // s_fcheck_available so subsequent AST_FUNCCALL nodes in this
    // sequence emit as literal text.
    //
    // In the classic parser, FCHECK is scoped to each mux_exec call.
    // Function arguments are evaluated by recursive mux_exec calls,
    // each with fresh EV_FCHECK.  A sequence maps to one such scope:
    // FCHECK is consumed within it (siblings see it consumed), but
    // the parent scope is restored when the sequence returns.
    //
    bool saved_fcheck = s_fcheck_available;
    bool strip_fcheck = s_fcheck_available
                     && (s_compile_eval & EV_FCHECK)
                     && !(s_compile_eval & EV_FMAND);

    // Lower each child.
    std::vector<int> children;
    for (size_t idx = first; idx < last; idx++) {
        auto &child = node->children[idx];
        children.push_back(hir_lower_node(h, rc, child.get()));

        // After the first non-space child, consume FCHECK for siblings.
        if (strip_fcheck && child->type != AST_SPACE) {
            s_fcheck_available = false;
            strip_fcheck = false;
        }
    }

    // Restore parent scope — FCHECK consumption is local to this sequence.
    s_fcheck_available = saved_fcheck;

    // Check if all constant.
    bool all_const = true;
    for (int ci : children) {
        if (!h.is_const(ci)) { all_const = false; break; }
    }

    if (all_const) {
        std::string merged;
        for (int ci : children) merged += h.const_str(ci);
        uint64_t addr = rc.pool_str(merged);
        return h.emit_sconst(addr, merged);
    }

    // Mixed: emit STRCAT.  Convert any ints/floats to strings first.
    for (auto &ci : children) {
        if (h.ty[ci] == TY_INT) {
            ci = h.emit(HIR_ITOA, TY_STRING, ci);
        } else if (h.ty[ci] == TY_FLOAT) {
            ci = h.emit(HIR_FTOA, TY_STRING, ci);
        }
    }

    int strcat_idx = engine_api_lookup("STRCAT");
    int i = h.emit_strcat(children.data(),
                           static_cast<int>(children.size()));
    if (i >= 0) h.func_idx[i] = strcat_idx;
    h.ecalls++;
    h.needs_jit = true;
    return i;
}


// Lower a NOEVAL child, stripping leading/trailing spaces and braces.
//
// MUX NOEVAL functions (switch/case/if/iter) evaluate their arguments
// with EV_STRIP_CURLY, which strips outer braces and leading/trailing
// whitespace.  In the AST, the space after a comma becomes an AST_SPACE
// node at the start of the argument's sequence.  This helper handles
// both brace unwrapping and space trimming.
//
static int hir_lower_trimmed(hir_program &h, rv_compiler &rc,
                              const ASTNode *child) {
    // EV_STRIP_CURLY: unwrap outer brace group.
    const ASTNode *inner = child;
    if (inner->type == AST_BRACEGROUP && !inner->children.empty()) {
        inner = inner->children[0].get();
    }

    if (inner->type == AST_SEQUENCE && !inner->children.empty()) {
        size_t first = 0, last = inner->children.size();
        while (first < last && inner->children[first]->type == AST_SPACE) first++;
        while (last > first && inner->children[last-1]->type == AST_SPACE) last--;
        if (first == last) {
            uint64_t addr = rc.pool_str("");
            return h.emit_sconst(addr, "");
        }

        // After trimming, if a single brace group remains, unwrap it.
        // This handles: if(1, {text}) where leading space is trimmed.
        if (last - first == 1
            && inner->children[first]->type == AST_BRACEGROUP
            && !inner->children[first]->children.empty()) {
            return hir_lower_node(h, rc,
                inner->children[first]->children[0].get());
        }

        if (first == 0 && last == inner->children.size()) {
            return hir_lower_node(h, rc, inner);
        }
        // Lower only the trimmed children.
        std::vector<int> parts;
        for (size_t i = first; i < last; i++) {
            parts.push_back(hir_lower_node(h, rc, inner->children[i].get()));
        }
        if (parts.size() == 1) return parts[0];
        // Concatenate: ensure all parts are strings.
        for (auto &p : parts) {
            if (h.ty[p] == TY_INT) {
                p = h.emit(HIR_ITOA, TY_STRING, p);
            } else if (h.ty[p] == TY_FLOAT) {
                p = h.emit(HIR_FTOA, TY_STRING, p);
            }
        }
        int strcat_idx = engine_api_lookup("STRCAT");
        int r = h.emit_strcat(parts.data(), static_cast<int>(parts.size()));
        if (r >= 0) h.func_idx[r] = strcat_idx;
        h.ecalls++;
        h.needs_jit = true;
        return r;
    }
    return hir_lower_node(h, rc, inner);
}

// Lower a normal function argument, trimming only top-level surrounding
// spaces to match parse_arglist()/parse_to() comma argument handling.
static int hir_lower_argument(hir_program &h, rv_compiler &rc,
                              const ASTNode *child) {
    if (  child->type == AST_SEQUENCE
       && !child->children.empty()
       && mudconf.space_compress
       && !(s_compile_eval & EV_NO_COMPRESS)) {
        size_t first = 0, last = child->children.size();
        while (first < last && child->children[first]->type == AST_SPACE) first++;
        while (last > first && child->children[last-1]->type == AST_SPACE) last--;
        if (first == last) {
            uint64_t addr = rc.pool_str("");
            return h.emit_sconst(addr, "");
        }
        if (first == 0 && last == child->children.size()) {
            return hir_lower_node(h, rc, child);
        }
        std::vector<int> parts;
        for (size_t i = first; i < last; i++) {
            parts.push_back(hir_lower_node(h, rc, child->children[i].get()));
        }
        if (parts.size() == 1) return parts[0];
        for (auto &p : parts) {
            if (h.ty[p] == TY_INT) {
                p = h.emit(HIR_ITOA, TY_STRING, p);
            } else if (h.ty[p] == TY_FLOAT) {
                p = h.emit(HIR_FTOA, TY_STRING, p);
            }
        }
        int strcat_idx = engine_api_lookup("STRCAT");
        int r = h.emit_strcat(parts.data(), static_cast<int>(parts.size()));
        if (r >= 0) h.func_idx[r] = strcat_idx;
        h.ecalls++;
        h.needs_jit = true;
        return r;
    }
    return hir_lower_node(h, rc, child);
}

// Lower a function call: try fold, try native arith, else ECALL.
//
static int hir_lower_funccall(hir_program &h, rv_compiler &rc,
                               const ASTNode *node) {
    // ---------------------------------------------------------------
    // EV_FCHECK gate: if FCHECK has been consumed (by hir_lower_sequence
    // after the first child) and FMAND is not set, this function call
    // is not at the start of the expression — emit as literal text.
    // This matches the classic parser where only the first '(' is
    // checked as a potential function call.
    // ---------------------------------------------------------------

    if (!s_fcheck_available && !(s_compile_eval & EV_FMAND)) {
        // Reconstruct as literal: name(arg1,arg2,...).
        // Arguments are still lowered (%-substitutions resolved).
        std::vector<int> args;
        for (auto &child : node->children) {
            args.push_back(hir_lower_node(h, rc, child.get()));
        }
        int nargs = static_cast<int>(args.size());

        // Try all-constant path first.
        bool all_const = true;
        for (int ai : args) {
            if (!h.is_const(ai)) { all_const = false; break; }
        }
        if (all_const) {
            std::string lit = node->text;
            lit += '(';
            for (int ai = 0; ai < nargs; ai++) {
                if (ai > 0) lit += ',';
                lit += h.const_str(args[ai]);
            }
            if (node->has_close_paren) lit += ')';
            uint64_t addr = rc.pool_str(lit);
            return h.emit_sconst(addr, lit);
        }

        // Runtime path: strcat pieces.
        std::vector<int> parts;
        std::string prefix = node->text;
        prefix += '(';
        uint64_t paddr = rc.pool_str(prefix);
        parts.push_back(h.emit_sconst(paddr, prefix));
        for (int ai = 0; ai < nargs; ai++) {
            if (ai > 0) {
                uint64_t caddr = rc.pool_str(",");
                parts.push_back(h.emit_sconst(caddr, ","));
            }
            int arg = args[ai];
            if (h.ty[arg] == TY_INT) {
                arg = h.emit(HIR_ITOA, TY_STRING, arg);
            } else if (h.ty[arg] == TY_FLOAT) {
                arg = h.emit(HIR_FTOA, TY_STRING, arg);
            }
            parts.push_back(arg);
        }
        if (node->has_close_paren) {
            uint64_t raddr = rc.pool_str(")");
            parts.push_back(h.emit_sconst(raddr, ")"));
        }
        int strcat_idx = engine_api_lookup("STRCAT");
        int r = h.emit_strcat(parts.data(),
                               static_cast<int>(parts.size()));
        if (r >= 0) h.func_idx[r] = strcat_idx;
        h.ecalls++;
        h.needs_jit = true;
        return r;
    }

    // ---------------------------------------------------------------
    // %q register operations (compile-time tracking for single block).
    // ---------------------------------------------------------------

    // Uppercase name for comparison.
    std::string fname = node->text;
    for (auto &c : fname)
        c = static_cast<char>(toupper(static_cast<unsigned char>(c)));

    // r(n) — read %q register.
    if (fname == "R" && node->children.size() == 1) {
        // Register number must be a literal constant 0-9.
        const ASTNode *arg0 = node->children[0].get();
        if (arg0->type == AST_LITERAL && !arg0->text.empty()) {
            int rn = arg0->text[0] - '0';
            if (rn >= 0 && rn < HIR_NUM_QREGS && qreg[rn] >= 0) {
                qreg_used = true;
                return qreg[rn];
            }
        }
        // Fall through to ECALL if register number unknown or not set.
    }

    // setq(n, value) — set %q register, return empty string.
    if (fname == "SETQ" && node->children.size() == 2) {
        const ASTNode *arg0 = node->children[0].get();
        if (arg0->type == AST_LITERAL && !arg0->text.empty()) {
            int rn = arg0->text[0] - '0';
            if (rn >= 0 && rn < HIR_NUM_QREGS) {
                int val = hir_lower_argument(h, rc, node->children[1].get());
                qreg[rn] = val;
                qreg_used = true;

                // Emit write-through: sync to SUBST slot + mudstate.
                int sval = val;
                if (h.ty[sval] == TY_INT) {
                    sval = h.emit(HIR_ITOA, TY_STRING, sval);
                } else if (h.ty[sval] == TY_FLOAT) {
                    sval = h.emit(HIR_FTOA, TY_STRING, sval);
                }
                h.emit(HIR_SETQ_SYNC, TY_VOID, sval, -1, rn);
                h.needs_jit = true;

                // setq() returns empty string.
                uint64_t addr = rc.pool_str("");
                return h.emit_sconst(addr, "");
            }
        }
    }

    // setr(n, value) — set %q register, return value.
    if (fname == "SETR" && node->children.size() == 2) {
        const ASTNode *arg0 = node->children[0].get();
        if (arg0->type == AST_LITERAL && !arg0->text.empty()) {
            int rn = arg0->text[0] - '0';
            if (rn >= 0 && rn < HIR_NUM_QREGS) {
                int val = hir_lower_argument(h, rc, node->children[1].get());
                qreg[rn] = val;
                qreg_used = true;

                // Emit write-through: sync to SUBST slot + mudstate.
                int sval = val;
                if (h.ty[sval] == TY_INT) {
                    sval = h.emit(HIR_ITOA, TY_STRING, sval);
                } else if (h.ty[sval] == TY_FLOAT) {
                    sval = h.emit(HIR_FTOA, TY_STRING, sval);
                }
                h.emit(HIR_SETQ_SYNC, TY_VOID, sval, -1, rn);
                h.needs_jit = true;

                return val;
            }
        }
    }

    // ---------------------------------------------------------------
    // Control flow: if(cond, true) / ifelse(cond, true, false)
    //
    // Short-circuit: only the selected branch is evaluated.
    // Constant condition: fold at compile time (no blocks needed).
    // Runtime condition: emit BRC + blocks + PHI.
    // ---------------------------------------------------------------

    if ((fname == "IFELSE" && node->children.size() == 3)
        || (fname == "IF" && node->children.size() == 2)) {
        bool has_else = (fname == "IFELSE");

        // Lower the condition (always evaluated).
        int cond = hir_lower_node(h, rc, node->children[0].get());

        // Ensure condition is integer.
        if (h.ty[cond] != TY_INT) {
            if (h.kind[cond] == HIR_SCONST) {
                int64_t v = static_cast<int64_t>(
                    mux_atol(u8(h.sval[cond])));
                cond = h.emit_iconst(v);
            } else if (h.ty[cond] == TY_FLOAT) {
                // Float condition: truncate to int (nonzero = true).
                cond = h.emit(HIR_FTOI, TY_INT, cond);
            } else if (h.known_int[cond]) {
                cond = h.emit(HIR_ATOI, TY_INT, cond);
            } else {
                // Non-integer condition (string truth = non-empty).
                // Fall through to ECALL for safety.
                goto general_lowering;
            }
        }

        // Constant condition: fold — only lower the selected branch.
        // Use hir_lower_trimmed to strip braces (EV_STRIP_CURLY).
        if (h.kind[cond] == HIR_ICONST) {
            if (h.val[cond] != 0) {
                return hir_lower_trimmed(h, rc, node->children[1].get());
            } else if (has_else) {
                return hir_lower_trimmed(h, rc, node->children[2].get());
            } else {
                uint64_t addr = rc.pool_str("");
                return h.emit_sconst(addr, "");
            }
        }

        // Runtime condition: multi-block code.
        int entry_block = h.cur_block;
        int true_block = h.new_block();
        int false_block = h.new_block();
        int merge_block = h.new_block();

        // BRC in entry block: src1=cond, val=true_block, src2=false_block.
        h.emit(HIR_BRC, TY_VOID, cond, false_block, true_block);
        h.add_edge(entry_block, true_block);
        h.add_edge(entry_block, false_block);

        // Lower true branch (strip braces via hir_lower_trimmed).
        h.cur_block = true_block;
        int true_val = hir_lower_trimmed(h, rc, node->children[1].get());
        int true_exit = h.cur_block;  // might change with nested ifelse
        h.emit(HIR_BR, TY_VOID, -1, -1, merge_block);
        h.add_edge(true_exit, merge_block);

        // Lower false branch (strip braces via hir_lower_trimmed).
        h.cur_block = false_block;
        int false_val;
        if (has_else) {
            false_val = hir_lower_trimmed(h, rc, node->children[2].get());
        } else {
            // if() with no else: false branch returns empty string.
            uint64_t addr = rc.pool_str("");
            false_val = h.emit_sconst(addr, "");
        }
        int false_exit = h.cur_block;
        h.emit(HIR_BR, TY_VOID, -1, -1, merge_block);
        h.add_edge(false_exit, merge_block);

        // Merge block with PHI.
        h.cur_block = merge_block;
        hir_type rty = (h.ty[true_val] == TY_INT && h.ty[false_val] == TY_INT)
                     ? TY_INT : TY_STRING;
        int blocks[2] = { true_exit, false_exit };
        int vals[2] = { true_val, false_val };
        int phi = h.emit_phi(rty, -1, blocks, vals, 2);

        h.needs_jit = true;
        return phi;
    }

    // ---------------------------------------------------------------
    // Short-circuit logic: cand/candbool/cor/corbool
    //
    // cand(a,b,c): eval a, if false → 0; eval b, if false → 0;
    //   eval c, if false → 0; result = 1.
    // cor(a,b,c):  eval a, if true → 1; eval b, if true → 1;
    //   eval c, if true → 1; result = 0.
    //
    // Structure: chain of test blocks, each with BRC to either the
    // next test or the short-circuit result.  Final merge via PHI.
    // ---------------------------------------------------------------

    if ((fname == "CAND" || fname == "CANDBOOL"
         || fname == "COR" || fname == "CORBOOL")
        && node->children.size() >= 1) {
        bool is_and = (fname == "CAND" || fname == "CANDBOOL");
        int nfargs = static_cast<int>(node->children.size());

        // Lower args one at a time (preserving short-circuit semantics).
        // Chain blocks are allocated during the loop; result blocks are
        // allocated AFTER the loop so they get higher block numbers
        // (ensuring all branches in the generated code go forward).
        //
        // BRC instructions emitted during the loop use a placeholder (-1)
        // for the short-circuit target (false_blk or true_blk).  After
        // the result blocks are allocated, we patch those BRC instructions.
        bool multi_block = false;
        bool last_was_brc = false;
        std::vector<int> brc_patch_insns;    // BRC insn indices to patch
        std::vector<int> br_shortcircuit;    // BR insns → short-circuit target
        int br_allpassed = -1;               // BR insn → "all passed" target

        for (int ai = 0; ai < nfargs; ai++) {
            int cond = hir_lower_node(h, rc, node->children[ai].get());

            // Ensure condition is integer.
            if (h.ty[cond] != TY_INT) {
                if (h.kind[cond] == HIR_SCONST) {
                    int64_t v = static_cast<int64_t>(
                        mux_atol(u8(h.sval[cond])));
                    cond = h.emit_iconst(v);
                } else if (h.ty[cond] == TY_FLOAT) {
                    cond = h.emit(HIR_FTOI, TY_INT, cond);
                } else if (h.known_int[cond]) {
                    cond = h.emit(HIR_ATOI, TY_INT, cond);
                } else {
                    goto general_lowering;
                }
            }

            // Constant: fold at compile time.
            if (h.kind[cond] == HIR_ICONST) {
                bool truthy = (h.val[cond] != 0);
                if (is_and && !truthy) {
                    if (!multi_block) {
                        uint64_t addr = rc.pool_str("0");
                        return h.emit_sconst(addr, "0");
                    }
                    int br = h.emit(HIR_BR, TY_VOID, -1, -1, -1);
                    br_shortcircuit.push_back(br);
                    goto cand_cor_done;
                }
                if (!is_and && truthy) {
                    if (!multi_block) {
                        uint64_t addr = rc.pool_str("1");
                        return h.emit_sconst(addr, "1");
                    }
                    int br = h.emit(HIR_BR, TY_VOID, -1, -1, -1);
                    br_shortcircuit.push_back(br);
                    goto cand_cor_done;
                }
                last_was_brc = false;
                continue;
            }

            multi_block = true;
            last_was_brc = true;

            // Allocate next test block for non-last args.
            if (ai < nfargs - 1) {
                int next_blk = h.new_block();
                if (is_and) {
                    // cand: true → next, false → false_blk (placeholder -1).
                    int brc = h.emit(HIR_BRC, TY_VOID, cond, -1, next_blk);
                    brc_patch_insns.push_back(brc);
                    h.add_edge(h.cur_block, next_blk);
                } else {
                    // cor: true → true_blk (placeholder -1), false → next.
                    int brc = h.emit(HIR_BRC, TY_VOID, cond, next_blk, -1);
                    brc_patch_insns.push_back(brc);
                    h.add_edge(h.cur_block, next_blk);
                }
                h.cur_block = next_blk;
            } else {
                // Last arg: both paths go to result blocks (placeholders).
                int brc = h.emit(HIR_BRC, TY_VOID, cond, -1, -1);
                brc_patch_insns.push_back(brc);
            }
        }

        // Fell through all args (no constant short-circuit).
        if (!multi_block) {
            uint64_t addr = rc.pool_str(is_and ? "1" : "0");
            return h.emit_sconst(addr, is_and ? "1" : "0");
        }

        // If the last arg was a constant (no BRC terminated the block),
        // we need a BR as terminator → "all passed" result.
        if (!last_was_brc) {
            br_allpassed = h.emit(HIR_BR, TY_VOID, -1, -1, -1);
        }

    cand_cor_done:
        {
            // Allocate result blocks (after all chain blocks).
            int true_blk = h.new_block();
            int false_blk = h.new_block();
            int merge_blk = h.new_block();

            // Patch BRC instructions.
            for (int pi : brc_patch_insns) {
                if (is_and) {
                    h.src2[pi] = false_blk;
                    h.add_edge(h.blk[pi], false_blk);
                    if (h.val[pi] == -1) {
                        // Last arg's BRC: true path also needs patching.
                        h.val[pi] = true_blk;
                        h.add_edge(h.blk[pi], true_blk);
                    }
                } else {
                    if (h.val[pi] == -1) {
                        h.val[pi] = true_blk;
                    }
                    h.add_edge(h.blk[pi], true_blk);
                    if (h.src2[pi] == -1) {
                        // Last arg's BRC: false path also needs patching.
                        h.src2[pi] = false_blk;
                        h.add_edge(h.blk[pi], false_blk);
                    }
                }
            }

            // Patch short-circuit BRs (constant fold → early exit).
            for (int bi : br_shortcircuit) {
                int target = is_and ? false_blk : true_blk;
                h.val[bi] = target;
                h.add_edge(h.blk[bi], target);
            }

            // Patch "all passed" BR (last arg was constant true/false pass-through).
            if (br_allpassed >= 0) {
                int target = is_and ? true_blk : false_blk;
                h.val[br_allpassed] = target;
                h.add_edge(h.blk[br_allpassed], target);
            }

            // True result block.
            h.cur_block = true_blk;
            int val_t = h.emit_iconst(1);
            int true_exit = h.cur_block;
            h.emit(HIR_BR, TY_VOID, -1, -1, merge_blk);
            h.add_edge(true_exit, merge_blk);

            // False result block.
            h.cur_block = false_blk;
            int val_f = h.emit_iconst(0);
            int false_exit = h.cur_block;
            h.emit(HIR_BR, TY_VOID, -1, -1, merge_blk);
            h.add_edge(false_exit, merge_blk);

            // Merge with PHI.
            h.cur_block = merge_blk;
            int blocks[2] = { true_exit, false_exit };
            int vals[2] = { val_t, val_f };
            int phi = h.emit_phi(TY_INT, -1, blocks, vals, 2);

            h.needs_jit = true;
            return phi;
        }
    }

    // ---------------------------------------------------------------
    // switch(expr, pat1, res1, pat2, res2, ..., default)
    // case(expr, pat1, res1, pat2, res2, ..., default)
    //
    // Evaluate expr once.  Then test each pattern in order:
    //   case(): exact string comparison (strcmp == 0)
    //   switch(): wildcard match via ECALL STRMATCH
    // Only the matching result branch is evaluated (NOEVAL).
    // If no pattern matches, evaluate the default (if present).
    //
    // Structure: chain of test blocks, each with BRC to either
    // the result block or the next test.  Results and default
    // branch to a merge block with a PHI.
    // ---------------------------------------------------------------

    if ((fname == "SWITCH" || fname == "CASE"
         || fname == "SWITCHALL" || fname == "CASEALL")
        && node->children.size() >= 3) {
        bool bWild = (fname == "SWITCH" || fname == "SWITCHALL");
        bool bAll = (fname == "SWITCHALL" || fname == "CASEALL");
        int nfargs = static_cast<int>(node->children.size());

        // switchall/caseall: fall through to ECALL for now.
        // These evaluate ALL matching branches, not just the first.
        if (bAll) goto general_lowering;

        // Evaluate the target expression (child[0]), trimmed.
        int target = hir_lower_trimmed(h, rc,node->children[0].get());
        // Ensure target is a string for comparison.
        if (h.ty[target] == TY_INT) {
            target = h.emit(HIR_ITOA, TY_STRING, target);
        } else if (h.ty[target] == TY_FLOAT) {
            target = h.emit(HIR_FTOA, TY_STRING, target);
        }

        // Count pattern/result pairs and whether there's a default.
        int npairs = (nfargs - 1) / 2;  // number of pat/res pairs
        bool has_default = ((nfargs - 1) % 2) == 1;

        // We need: npairs test blocks, npairs result blocks,
        // optionally a default block, and a merge block.
        // Allocate blocks as we go (like cand/cor).

        // Pre-resolve ECALL indices we'll need.
        int strmatch_idx = bWild ? engine_api_lookup("STRMATCH") : 0;
        int comp_idx = bWild ? 0 : engine_api_lookup("COMP");

        // Track result values and their exit blocks for the final PHI.
        std::vector<int> result_vals;
        std::vector<int> result_exits;

        int merge_blk = -1;  // allocated after all test/result blocks

        for (int pi = 0; pi < npairs; pi++) {
            // We're in the current test block.
            int test_block = h.cur_block;

            // Lower the pattern (always evaluated), trimmed.
            int pat = hir_lower_trimmed(h, rc,
                node->children[1 + pi * 2].get());
            if (h.ty[pat] == TY_INT) {
                pat = h.emit(HIR_ITOA, TY_STRING, pat);
            } else if (h.ty[pat] == TY_FLOAT) {
                pat = h.emit(HIR_FTOA, TY_STRING, pat);
            }

            // Compare target against pattern.
            int cond;
            if (bWild) {
                // ECALL STRMATCH(target, pattern) → "0" or "1"
                int cargs[2] = { target, pat };
                int sm = h.emit_call(TY_STRING, strmatch_idx, cargs, 2);
                h.known_int[sm] = true;
                h.ecalls++;
                // Convert to int: strmatch returns "1" for match.
                cond = h.emit(HIR_ATOI, TY_INT, sm);
            } else {
                // ECALL COMP(target, pattern) → "-1"/"0"/"1"
                int cargs[2] = { target, pat };
                int cm = h.emit_call(TY_STRING, comp_idx, cargs, 2);
                h.known_int[cm] = true;
                h.ecalls++;
                // comp==0 means match; convert: eq(comp,0) → 1 if match.
                int cm_int = h.emit(HIR_ATOI, TY_INT, cm);
                int zero = h.emit_iconst(0);
                cond = h.emit(HIR_EQ, TY_INT, cm_int, zero);
                h.native_ops++;
            }

            // Allocate result block and next-test block.
            int result_blk = h.new_block();
            int next_blk;
            if (pi < npairs - 1) {
                next_blk = h.new_block();
            } else if (has_default) {
                next_blk = h.new_block();  // default block
            } else {
                next_blk = h.new_block();  // "no match" block
            }

            // BRC: cond true → result_blk, false → next_blk.
            h.emit(HIR_BRC, TY_VOID, cond, next_blk, result_blk);
            h.add_edge(test_block, result_blk);
            h.add_edge(test_block, next_blk);

            // Lower result branch (NOEVAL — only evaluated on match), trimmed.
            h.cur_block = result_blk;
            int rval = hir_lower_trimmed(h, rc,
                node->children[2 + pi * 2].get());
            result_vals.push_back(rval);
            result_exits.push_back(h.cur_block);
            // BR to merge (patched later).
            h.emit(HIR_BR, TY_VOID, -1, -1, -1);  // target = merge, patched below

            // Move to next test block.
            h.cur_block = next_blk;
        }

        // Handle default or "no match" (empty string).
        int default_val;
        if (has_default) {
            default_val = hir_lower_trimmed(h, rc,
                node->children[nfargs - 1].get());
        } else {
            uint64_t addr = rc.pool_str("");
            default_val = h.emit_sconst(addr, "");
        }
        result_vals.push_back(default_val);
        result_exits.push_back(h.cur_block);
        // BR to merge (patched below).
        h.emit(HIR_BR, TY_VOID, -1, -1, -1);

        // Allocate merge block.
        merge_blk = h.new_block();
        h.cur_block = merge_blk;

        // Patch all BR instructions to point to merge_blk and add edges.
        // The BR instructions are the last insn in each result/default block.
        for (int ri = 0; ri < static_cast<int>(result_exits.size()); ri++) {
            int blk = result_exits[ri];
            // Find the BR instruction (last in block — scan backwards).
            for (int ii = h.n_insns - 1; ii >= 0; ii--) {
                if (h.blk[ii] == blk && h.kind[ii] == HIR_BR && h.val[ii] == -1) {
                    h.val[ii] = merge_blk;
                    h.add_edge(blk, merge_blk);
                    break;
                }
            }
        }

        // Build PHI node at merge.
        hir_type rty = TY_STRING;
        for (int rv : result_vals) {
            if (h.ty[rv] != TY_INT) { rty = TY_STRING; break; }
            rty = TY_INT;
        }
        int phi = h.emit_phi(rty, -1,
            result_exits.data(), result_vals.data(),
            static_cast<int>(result_vals.size()));

        h.needs_jit = true;
        return phi;
    }

    // ---------------------------------------------------------------
    // iter(list, body, delim, osep)
    //
    // Compile iter() as a counted loop:
    //   entry: evaluate list, count words, init inum=0 acc=""
    //   header: PHI(inum, acc), check inum < nwords, BRC → body/exit
    //   body: extract element, set ## and #@, lower body,
    //         accumulate: first iteration → body_val,
    //         subsequent → strcat(acc, osep, body_val)
    //   latch: inum++, BR → header (back-edge)
    //   exit: result = acc
    //
    // STORE_Q/LOAD_Q + SSA construction handles the loop PHIs.
    // ---------------------------------------------------------------

    if (fname == "ITER" && node->children.size() >= 2) {
        int nfargs = static_cast<int>(node->children.size());

        // Evaluate the list (child[0]) — always evaluated.
        int list_val = hir_lower_trimmed(h, rc, node->children[0].get());
        if (h.ty[list_val] == TY_INT) {
            list_val = h.emit(HIR_ITOA, TY_STRING, list_val);
        } else if (h.ty[list_val] == TY_FLOAT) {
            list_val = h.emit(HIR_FTOA, TY_STRING, list_val);
        }

        // Evaluate delimiters (child[2] = input, child[3] = output).
        int delim_val;
        if (nfargs >= 3) {
            delim_val = hir_lower_trimmed(h, rc, node->children[2].get());
            if (h.ty[delim_val] == TY_INT) {
                delim_val = h.emit(HIR_ITOA, TY_STRING, delim_val);
            } else if (h.ty[delim_val] == TY_FLOAT) {
                delim_val = h.emit(HIR_FTOA, TY_STRING, delim_val);
            }
        } else {
            uint64_t addr = rc.pool_str(" ");
            delim_val = h.emit_sconst(addr, " ");
        }

        int osep_val;
        if (nfargs >= 4) {
            osep_val = hir_lower_trimmed(h, rc, node->children[3].get());
            if (h.ty[osep_val] == TY_INT) {
                osep_val = h.emit(HIR_ITOA, TY_STRING, osep_val);
            } else if (h.ty[osep_val] == TY_FLOAT) {
                osep_val = h.emit(HIR_FTOA, TY_STRING, osep_val);
            }
        } else {
            uint64_t addr = rc.pool_str(" ");
            osep_val = h.emit_sconst(addr, " ");
        }

        // Count elements: nwords = WORDS(list, delim) — Tier 2 if available.
        int words_idx = engine_api_lookup("WORDS");
        int wargs[2] = { list_val, delim_val };
        int nwords_str = h.emit_call(TY_STRING, words_idx, wargs, 2);
        h.known_int[nwords_str] = true;
        uint64_t t2words = tier2_lookup("WORDS");
        if (t2words) {
            h.tier2_addr[nwords_str] = t2words;
            h.tier2_calls++;
        } else {
            h.ecalls++;
        }
        int nwords_int = h.emit(HIR_ATOI, TY_INT, nwords_str);

        // Allocate a fixed cursor buffer in the output region.
        // Cleared to 0x00 before each run; satoi("") returns 0
        // which is the correct initial offset.
        uint64_t cursor_addr = rc.alloc_output();

        // Initialize loop state.
        int inum_init = h.emit_iconst(0);
        h.emit(HIR_STORE_Q, TY_VOID, inum_init, -1, QREG_ITER_INUM);

        uint64_t empty_addr = rc.pool_str("");
        int acc_init = h.emit_sconst(empty_addr, "");
        h.emit(HIR_STORE_Q, TY_VOID, acc_init, -1, QREG_ITER_ACC);

        // entry → header.
        int entry_block = h.cur_block;
        int header_block = h.new_block();
        h.emit(HIR_BR, TY_VOID, -1, -1, header_block);
        h.add_edge(entry_block, header_block);

        // Header: load inum, check < nwords, branch.
        h.cur_block = header_block;
        int inum = h.emit(HIR_LOAD_Q, TY_INT, -1, -1, QREG_ITER_INUM);
        int acc = h.emit(HIR_LOAD_Q, TY_STRING, -1, -1, QREG_ITER_ACC);
        int cond = h.emit(HIR_LT, TY_INT, inum, nwords_int);
        h.native_ops++;

        int body_block = h.new_block();
        int exit_block = h.new_block();
        h.emit(HIR_BRC, TY_VOID, cond, exit_block, body_block);
        h.add_edge(header_block, body_block);
        h.add_edge(header_block, exit_block);

        // Body: split_token or extract element, set iter context, lower body.
        h.cur_block = body_block;

        // inum_1based for #@ resolution in body.
        int one_int = h.emit_iconst(1);
        int inum_1based = h.emit(HIR_ADD, TY_INT, inum, one_int);
        h.native_ops++;

        // Element extraction: use SPLIT_TOKEN (O(n) cursor) if available,
        // else fall back to EXTRACT (O(n²) re-scan).
        uint64_t t2split = tier2_lookup("SPLIT_TOKEN");
        int elem;
        if (t2split) {
            // SPLIT_TOKEN(list, cursor, delim, cursor) — reads cursor,
            // writes new cursor back to same buffer.  cursor_addr is a
            // fixed output slot cleared to 0x00 before each run.
            int cursor_val = h.emit_sconst(cursor_addr, "");
            int stargs[4] = { list_val, cursor_val, delim_val, cursor_val };
            elem = h.emit_call(TY_STRING, 0, stargs, 4);
            h.tier2_addr[elem] = t2split;
            h.tier2_calls++;
        } else {
            // Fallback: EXTRACT(list, inum+1, 1, delim).
            int inum_1str = h.emit(HIR_ITOA, TY_STRING, inum_1based);
            int extract_idx = engine_api_lookup("EXTRACT");
            uint64_t one_addr = rc.pool_str("1");
            int one_str = h.emit_sconst(one_addr, "1");
            int eargs[4] = { list_val, inum_1str, one_str, delim_val };
            elem = h.emit_call(TY_STRING, extract_idx, eargs, 4);
            uint64_t t2ext = tier2_lookup("EXTRACT");
            if (t2ext) {
                h.tier2_addr[elem] = t2ext;
                h.tier2_calls++;
            } else {
                h.ecalls++;
            }
        }

        // Set iter context for ## and #@ resolution in body.
        int saved_itext = iter_itext_val;
        int saved_inum1 = iter_inum1_val;
        iter_itext_val = elem;
        iter_inum1_val = inum_1based;

        // Lower the body (child[1], NOEVAL — trimmed).
        int body_val = hir_lower_trimmed(h, rc, node->children[1].get());
        if (h.ty[body_val] == TY_INT) {
            body_val = h.emit(HIR_ITOA, TY_STRING, body_val);
        } else if (h.ty[body_val] == TY_FLOAT) {
            body_val = h.emit(HIR_FTOA, TY_STRING, body_val);
        }

        // Restore iter context.
        iter_itext_val = saved_itext;
        iter_inum1_val = saved_inum1;

        // Accumulate: first iteration → body_val,
        //             otherwise → strcat(acc, osep, body_val).
        int zero = h.emit_iconst(0);
        int is_first = h.emit(HIR_EQ, TY_INT, inum, zero);
        h.native_ops++;

        int first_block = h.new_block();
        int cat_block = h.new_block();
        h.emit(HIR_BRC, TY_VOID, is_first, cat_block, first_block);
        h.add_edge(h.cur_block, first_block);
        h.add_edge(h.cur_block, cat_block);

        // First iteration: acc = body_val.
        h.cur_block = first_block;
        h.emit(HIR_STORE_Q, TY_VOID, body_val, -1, QREG_ITER_ACC);
        int latch_block = h.new_block();
        h.emit(HIR_BR, TY_VOID, -1, -1, latch_block);
        h.add_edge(first_block, latch_block);

        // Subsequent: acc = strcat(acc, osep, body_val).
        h.cur_block = cat_block;
        int strcat_idx = engine_api_lookup("STRCAT");
        int cargs[3] = { acc, osep_val, body_val };
        int new_acc = h.emit_strcat(cargs, 3);
        if (new_acc >= 0) h.func_idx[new_acc] = strcat_idx;
        if (tier2_lookup("STRCAT")) {
            h.tier2_calls++;
        } else {
            h.ecalls++;
        }
        h.emit(HIR_STORE_Q, TY_VOID, new_acc, -1, QREG_ITER_ACC);
        h.emit(HIR_BR, TY_VOID, -1, -1, latch_block);
        h.add_edge(cat_block, latch_block);

        // Latch: increment inum, branch back to header.
        h.cur_block = latch_block;
        int inum_next = h.emit(HIR_ADD, TY_INT, inum, one_int);
        h.native_ops++;
        h.emit(HIR_STORE_Q, TY_VOID, inum_next, -1, QREG_ITER_INUM);
        h.emit(HIR_BR, TY_VOID, -1, -1, header_block);
        h.add_edge(latch_block, header_block);

        // Exit → continuation block.
        // The continuation block is allocated AFTER all loop-interior
        // blocks, so it has a higher block number and sits after them
        // in layout order.  This prevents fall-through into loop
        // blocks.  We use BR (not RET) so iter can be a subexpression.
        h.cur_block = exit_block;
        int cont_block = h.new_block();
        h.emit(HIR_BR, TY_VOID, -1, -1, cont_block);
        h.add_edge(exit_block, cont_block);

        h.cur_block = cont_block;
        int result = h.emit(HIR_LOAD_Q, TY_STRING, -1, -1, QREG_ITER_ACC);

        h.needs_jit = true;
        return result;
    }

    // ---------------------------------------------------------------
    // @@(expr) — null function.  Discard argument, return empty.
    // ---------------------------------------------------------------

    if (fname == "@@" && node->children.size() == 1) {
        uint64_t addr = rc.pool_str("");
        return h.emit_sconst(addr, "");
    }

    // ---------------------------------------------------------------
    // lit(expr) — return argument text unevaluated.
    // The AST node's child is a literal text node; just emit it as-is.
    // ---------------------------------------------------------------

    if (fname == "LIT" && node->children.size() == 1) {
        auto &child = node->children[0];
        if (child->type == AST_LITERAL) {
            std::string text(reinterpret_cast<const char *>(child->text.data()),
                             child->text.size());
            uint64_t addr = rc.pool_str(text);
            return h.emit_sconst(addr, text);
        }
        // Non-literal child (e.g., nested function call) — emit its
        // raw text representation.  For now, fall through to ECALL.
    }

    // ---------------------------------------------------------------
    // Tier 3: u()/ulocal() compile-time inlining.
    //
    // When the first argument is a constant obj/attr reference,
    // resolve the attr at compile time and inline the body.  All
    // correctness requirements handled via registered helpers:
    //   - Permission guard: _CHECK_U_PERM → BRC fallback
    //   - CARGS save/restore: _SAVE_CARGS / _RESTORE_CARGS
    //   - CARGS writing: _WRITE_CARG + _SET_NCARGS
    //   - ULOCAL qregs: _SAVE_QREGS / _RESTORE_QREGS
    //   - Cache staleness: per-attr mod_count deps
    // ---------------------------------------------------------------

    if ((fname == "U" || fname == "ULOCAL")
        && node->children.size() >= 1
        && s_compile_deps != nullptr
        && s_inline_depth < MAX_INLINE_DEPTH)
    {
        const ASTNode *arg0 = node->children[0].get();
        std::string arg0_str;
        bool arg0_const = false;

        if (arg0->type == AST_LITERAL) {
            arg0_str = arg0->text;
            arg0_const = true;
        } else if (arg0->type == AST_SEQUENCE
                   && arg0->children.size() == 1
                   && arg0->children[0]->type == AST_LITERAL) {
            arg0_str = arg0->children[0]->text;
            arg0_const = true;
        }

        // Only inline literal #dbref/attr references (e.g., "#21/bbtime").
        // Name-based ("me/foo", "here/foo", "SomeName/foo") or relative
        // references would resolve against GOD at compile time instead of
        // the runtime executor, producing wrong results.
        //
        size_t slash_pos = arg0_str.find('/');
        bool is_dbref_literal = arg0_const
            && slash_pos != std::string::npos
            && slash_pos > 1
            && arg0_str[0] == '#'
            && mux_isdigit(arg0_str[1]);

        // Don't inline if too many extra args (>10) — the CARGS
        // helper layer only supports 10 slots.
        int nExtra_check = static_cast<int>(node->children.size()) - 1;

        if (is_dbref_literal && nExtra_check <= 10)
        {
            dbref thing;
            ATTR *pattr = nullptr;

            if (!parse_attrib(GOD,
                    reinterpret_cast<const UTF8 *>(arg0_str.c_str()),
                    &thing, &pattr)
                || !pattr || !Good_obj(thing))
            {
                // Can't resolve — fall through to general lowering.
            }
            else
            {
                dbref aowner;
                int aflags;
                size_t nBodyLen = 0;
                UTF8 *body = atr_pget_LEN(thing, pattr->number,
                                           &aowner, &aflags, &nBodyLen);

                if (body && nBodyLen > 0)
                {
                    auto body_ast = ast_parse_string(body, nBodyLen);
                    free_lbuf(body);

                    if (body_ast)
                    {
                        bool is_local = (fname == "ULOCAL");
                        int nExtra = static_cast<int>(
                            node->children.size()) - 1;

                        // Record dependency for cache staleness.
                        uint32_t mc = attr_mod_count_get(thing,
                            pattr->number);
                        s_compile_deps->push_back({
                            static_cast<int32_t>(thing),
                            static_cast<int32_t>(pattr->number),
                            mc
                        });

                        // Lower all u() arguments (including arg0).
                        std::vector<int> u_args;
                        for (auto &child : node->children) {
                            u_args.push_back(
                                hir_lower_argument(h, rc, child.get()));
                        }

                        // --- Permission check ---
                        int perm_idx = engine_api_lookup("_CHECK_U_PERM");
                        std::string thing_str = std::to_string(thing);
                        std::string attr_str = std::to_string(pattr->number);
                        uint64_t ta = rc.pool_str(thing_str);
                        uint64_t aa = rc.pool_str(attr_str);
                        int thing_c = h.emit_sconst(ta, thing_str);
                        int attr_c = h.emit_sconst(aa, attr_str);
                        int perm_args[2] = { thing_c, attr_c };
                        int perm_result = h.emit_call(TY_STRING,
                            perm_idx, perm_args, 2);
                        h.ecalls++;
                        h.needs_jit = true;
                        h.known_int[perm_result] = true;

                        // Branch: "0" = ok → inline, nonzero → fallback.
                        int perm_int = h.emit(HIR_ATOI, TY_INT,
                            perm_result);
                        int entry_block = h.cur_block;
                        int fallback_block = h.new_block();
                        int inline_block = h.new_block();
                        // NOTE: merge_block allocated AFTER body lowering
                        // to ensure it has the highest block number.
                        // This prevents the inline→merge BR from being
                        // a backward edge that triggers loop detection.

                        // BRC: if nonzero → fallback, else → inline.
                        h.emit(HIR_BRC, TY_VOID, perm_int,
                               fallback_block, inline_block);
                        h.add_edge(entry_block, fallback_block);
                        h.add_edge(entry_block, inline_block);

                        // --- Fallback block: ECALL fun_u ---
                        // The BR to merge_block uses a placeholder (-1)
                        // that we patch after allocating merge_block.
                        h.cur_block = fallback_block;
                        int fidx_u = engine_api_lookup(fname.c_str());
                        int fb_result = h.emit_call(TY_STRING, fidx_u,
                            u_args.data(),
                            static_cast<int>(u_args.size()));
                        h.ecalls++;
                        int fb_exit = h.cur_block;
                        int fb_br_idx = h.emit(HIR_BR, TY_VOID, -1, -1, -1);
                        // Patched below after merge_block is allocated.

                        // --- Inline block ---
                        h.cur_block = inline_block;

                        // Save CARGS if there are extra args.
                        int cargs_handle = -1;
                        if (nExtra > 0)
                        {
                            int save_idx = engine_api_lookup("_SAVE_CARGS");
                            cargs_handle = h.emit_call(TY_STRING,
                                save_idx, nullptr, 0);
                            h.ecalls++;

                            // Write each extra arg to CARGS slots.
                            int write_idx = engine_api_lookup("_WRITE_CARG");
                            for (int ei = 0; ei < nExtra && ei < 10; ei++)
                            {
                                std::string idx_s = std::to_string(ei);
                                uint64_t ia = rc.pool_str(idx_s);
                                int idx_c = h.emit_sconst(ia, idx_s);
                                int val = u_args[ei + 1];
                                if (h.ty[val] == TY_INT) {
                                    val = h.emit(HIR_ITOA, TY_STRING, val);
                                } else if (h.ty[val] == TY_FLOAT) {
                                    val = h.emit(HIR_FTOA, TY_STRING, val);
                                }
                                int wargs[2] = { idx_c, val };
                                h.emit_call(TY_STRING, write_idx, wargs, 2);
                                h.ecalls++;
                            }

                            // Set %+ to the callee's arg count.
                            int ncargs_idx = engine_api_lookup("_SET_NCARGS");
                            std::string nc_s = std::to_string(nExtra);
                            uint64_t na = rc.pool_str(nc_s);
                            int nc_c = h.emit_sconst(na, nc_s);
                            int ncargs[1] = { nc_c };
                            h.emit_call(TY_STRING, ncargs_idx, ncargs, 1);
                            h.ecalls++;
                        }

                        // ULOCAL: save qregs.
                        int qreg_handle = -1;
                        if (is_local)
                        {
                            int save_q = engine_api_lookup("_SAVE_QREGS");
                            qreg_handle = h.emit_call(TY_STRING,
                                save_q, nullptr, 0);
                            h.ecalls++;
                        }

                        // Inline the body AST.
                        bool saved_fcheck = s_fcheck_available;
                        s_fcheck_available = true;
                        s_inline_depth++;
                        int body_result = hir_lower_node(
                            h, rc, body_ast.get());
                        s_inline_depth--;
                        s_fcheck_available = saved_fcheck;

                        // ULOCAL: restore qregs.
                        if (is_local && qreg_handle >= 0)
                        {
                            int restore_q = engine_api_lookup(
                                "_RESTORE_QREGS");
                            int rqargs[1] = { qreg_handle };
                            h.emit_call(TY_STRING, restore_q, rqargs, 1);
                            h.ecalls++;
                        }

                        // Restore CARGS.
                        if (cargs_handle >= 0)
                        {
                            int restore_c = engine_api_lookup(
                                "_RESTORE_CARGS");
                            int rcargs[1] = { cargs_handle };
                            h.emit_call(TY_STRING, restore_c, rcargs, 1);
                            h.ecalls++;
                        }

                        int inline_exit = h.cur_block;

                        // Allocate merge_block NOW — after the body
                        // has been lowered, so it gets the highest
                        // block number.  All edges to merge are forward.
                        int merge_block = h.new_block();

                        // Patch the fallback BR to target merge_block.
                        h.val[fb_br_idx] = merge_block;
                        h.add_edge(fb_exit, merge_block);

                        h.emit(HIR_BR, TY_VOID, -1, -1, merge_block);
                        h.add_edge(inline_exit, merge_block);

                        // --- Merge block: PHI ---
                        h.cur_block = merge_block;
                        int blocks[2] = { fb_exit, inline_exit };
                        int vals[2] = { fb_result, body_result };
                        return h.emit_phi(TY_STRING, -1, blocks, vals, 2);
                    }
                }
                else
                {
                    free_lbuf(body);
                }
            }
        }
        // Fall through if inlining wasn't possible.
    }

    // ---------------------------------------------------------------
    // General function call lowering.
    // ---------------------------------------------------------------
general_lowering:

    // FN_NOEVAL functions (citer, letq, list, localize, etc.) receive
    // their arguments unevaluated.  The JIT's general ECALL path
    // evaluates all arguments before the call, which is wrong for
    // NOEVAL.  The JIT has native handlers for if/switch/iter/cand/cor
    // (handled above); for any other NOEVAL function, mark the
    // compilation as failed so the AST evaluator handles it.
    {
        int chk_fidx = engine_api_lookup(fname.c_str());
        if (chk_fidx > 0 && chk_fidx < ENGINE_API_MAX_FUNCS) {
            FUN *chk_fp = engine_api_table[chk_fidx];
            if (chk_fp && (chk_fp->flags & FN_NOEVAL)) {
                s_jit_stats.record_noeval_bail(fname.c_str());
                rc.out_exhausted = true;  // force compilation failure
                rc.bail_was_noeval = true;
                uint64_t addr = rc.pool_str("");
                return h.emit_sconst(addr, "");
            }
        }
    }

    // Lower arguments.
    std::vector<int> args;
    for (auto &child : node->children) {
        args.push_back(hir_lower_argument(h, rc, child.get()));
    }
    int nargs = static_cast<int>(args.size());

    // Try constant folding.
    bool all_const = true;
    std::vector<std::string> arg_values;
    for (int ai : args) {
        if (!h.is_const(ai)) { all_const = false; break; }
        arg_values.push_back(h.const_str(ai));
    }
    if (all_const) {
        std::string folded;
        if (try_fold(node->text, arg_values, folded)) {
            uint64_t addr = rc.pool_str(folded);
            h.folds++;
            return h.emit_sconst(addr, folded);
        }
    }

    // Use the uppercase name already computed above.
    const std::string &upper = fname;

    // ---------------------------------------------------------------
    // Native integer arithmetic.
    // ---------------------------------------------------------------

    // Helper: check if all args are provably integer.
    auto all_int = [&]() -> bool {
        for (int ai : args) {
            if (!h.is_int(ai)) return false;
        }
        return true;
    };

    // Helper: ensure arg is TY_INT (emit ATOI or ICONST as needed).
    auto ensure_hi = [&](int ai) -> int {
        if (h.ty[ai] == TY_INT) return ai;
        if (h.kind[ai] == HIR_SCONST) {
            int64_t v = static_cast<int64_t>(mux_atol(u8(h.sval[ai])));
            return h.emit_iconst(v);
        }
        return h.emit(HIR_ATOI, TY_INT, ai);
    };

    // Helper: ensure arg is TY_FLOAT.
    // - FCONST/TY_FLOAT: pass through.
    // - SCONST: compile-time parse → FCONST (no runtime cost).
    // - ICONST: compile-time convert → FCONST.
    // - TY_INT: emit ITOF.
    // - TY_STRING (runtime): emit ATOF.
    //
    auto ensure_float = [&](int ai) -> int {
        if (h.ty[ai] == TY_FLOAT) return ai;
        if (h.kind[ai] == HIR_SCONST && !h.sval[ai].empty()) {
            double v = mux_atof(u8(h.sval[ai]));
            return h.emit_fconst(v);
        }
        if (h.kind[ai] == HIR_ICONST) {
            double v = static_cast<double>(h.val[ai]);
            return h.emit_fconst(v);
        }
        if (h.ty[ai] == TY_INT) {
            return h.emit(HIR_ITOF, TY_FLOAT, ai);
        }
        // TY_STRING at runtime — emit ATOF.
        // Use blob rv64_strtod intrinsic if available (JAL, fast path),
        // otherwise fall back to ECALL_ATOF.
        uint64_t strtod_addr = tier2_sym_addr("rv64_strtod");
        return h.emit(HIR_ATOF, TY_FLOAT, ai, -1,
                       static_cast<int64_t>(strtod_addr));
    };

    // Helper: check if any arg is provably float.
    auto any_float = [&]() -> bool {
        for (int ai : args) {
            if (h.is_float(ai) && !h.is_int(ai)) return true;
        }
        return false;
    };

    // Helper: check if all args are provably numeric (int or float).
    auto all_numeric = [&]() -> bool {
        for (int ai : args) {
            if (!h.is_numeric(ai)) return false;
        }
        return true;
    };

    // Binary ops: ADD, SUB, MUL, MOD.
    if ((upper == "ADD" || upper == "SUB") && nargs >= 2 && all_int()) {
        bool is_add = (upper == "ADD");
        int acc = ensure_hi(args[0]);
        for (int i = 1; i < nargs; i++) {
            int b = ensure_hi(args[i]);
            hir_kind op = (is_add || i > 1) ? HIR_ADD : HIR_SUB;
            acc = h.emit(op, TY_INT, acc, b);
        }
        h.native_ops++;
        h.needs_jit = true;
        return acc;
    }

    if (upper == "MUL" && nargs >= 2 && all_int()) {
        int acc = ensure_hi(args[0]);
        for (int i = 1; i < nargs; i++) {
            int b = ensure_hi(args[i]);
            acc = h.emit(HIR_MUL, TY_INT, acc, b);
        }
        h.native_ops++;
        h.needs_jit = true;
        return acc;
    }

    if (upper == "MOD" && nargs == 2 && all_int()) {
        int a = ensure_hi(args[0]);
        int b = ensure_hi(args[1]);
        int r = h.emit(HIR_REM, TY_INT, a, b);
        h.native_ops++;
        h.needs_jit = true;
        return r;
    }

    // Comparisons: EQ, NEQ, GT, GTE, LT, LTE.
    if ((upper == "EQ" || upper == "NEQ" || upper == "GT" || upper == "GTE"
         || upper == "LT" || upper == "LTE") && nargs == 2
        && h.is_int(args[0]) && h.is_int(args[1])) {
        int a = ensure_hi(args[0]);
        int b = ensure_hi(args[1]);
        hir_kind op;
        if (upper == "EQ")       op = HIR_EQ;
        else if (upper == "NEQ") op = HIR_NE;
        else if (upper == "GT")  op = HIR_GT;
        else if (upper == "GTE") op = HIR_GE;
        else if (upper == "LT")  op = HIR_LT;
        else                     op = HIR_LE;
        int r = h.emit(op, TY_INT, a, b);
        h.native_ops++;
        h.needs_jit = true;
        return r;
    }

    // Float comparisons: EQ, NEQ, GT, GTE, LT, LTE with float args.
    // Synthesize missing opcodes: NEQ→NOT(FEQ), GT→FLT(b,a), GTE→FLE(b,a).
    if ((upper == "EQ" || upper == "NEQ" || upper == "GT" || upper == "GTE"
         || upper == "LT" || upper == "LTE") && nargs == 2
        && all_numeric() && any_float()) {
        int a = ensure_float(args[0]);
        int b = ensure_float(args[1]);
        int r;
        if (upper == "EQ") {
            r = h.emit(HIR_FEQ, TY_INT, a, b);
        } else if (upper == "NEQ") {
            r = h.emit(HIR_FEQ, TY_INT, a, b);
            r = h.emit(HIR_NOT, TY_INT, r);
        } else if (upper == "GT") {
            r = h.emit(HIR_FLT, TY_INT, b, a);  // a > b ≡ b < a
        } else if (upper == "GTE") {
            r = h.emit(HIR_FLE, TY_INT, b, a);   // a >= b ≡ b <= a
        } else if (upper == "LT") {
            r = h.emit(HIR_FLT, TY_INT, a, b);
        } else {
            r = h.emit(HIR_FLE, TY_INT, a, b);   // LTE
        }
        h.native_ops++;
        h.needs_jit = true;
        return r;
    }

    // NOT: not(x) → (x == 0)
    if (upper == "NOT" && nargs == 1 && h.is_int(args[0])) {
        int a = ensure_hi(args[0]);
        int r = h.emit(HIR_NOT, TY_INT, a);
        h.native_ops++;
        h.needs_jit = true;
        return r;
    }

    // INC / DEC.
    if (upper == "INC" && nargs >= 1 && h.is_int(args[0])) {
        int a = ensure_hi(args[0]);
        int r = h.emit(HIR_INC, TY_INT, a);
        h.native_ops++;
        h.needs_jit = true;
        return r;
    }
    if (upper == "DEC" && nargs >= 1 && h.is_int(args[0])) {
        int a = ensure_hi(args[0]);
        int r = h.emit(HIR_DEC, TY_INT, a);
        h.native_ops++;
        h.needs_jit = true;
        return r;
    }

    // T: truthiness (0→0, nonzero→1).
    if (upper == "T" && nargs == 1 && h.is_int(args[0])) {
        int a = ensure_hi(args[0]);
        int r = h.emit(HIR_BOOL, TY_INT, a);
        h.native_ops++;
        h.needs_jit = true;
        return r;
    }

    // ABS: absolute value.
    if (upper == "ABS" && nargs == 1 && h.is_int(args[0])) {
        int a = ensure_hi(args[0]);
        int r = h.emit(HIR_ABS, TY_INT, a);
        h.native_ops++;
        h.needs_jit = true;
        return r;
    }

    // SIGN: sign of integer (-1, 0, 1).
    if (upper == "SIGN" && nargs == 1 && h.is_int(args[0])) {
        int a = ensure_hi(args[0]);
        int r = h.emit(HIR_SIGN, TY_INT, a);
        h.native_ops++;
        h.needs_jit = true;
        return r;
    }

    // MAX / MIN: binary integer max/min.
    if (upper == "MAX" && nargs == 2 && all_int()) {
        int a = ensure_hi(args[0]);
        int b = ensure_hi(args[1]);
        int r = h.emit(HIR_MAX, TY_INT, a, b);
        h.native_ops++;
        h.needs_jit = true;
        return r;
    }
    if (upper == "MIN" && nargs == 2 && all_int()) {
        int a = ensure_hi(args[0]);
        int b = ensure_hi(args[1]);
        int r = h.emit(HIR_MIN, TY_INT, a, b);
        h.native_ops++;
        h.needs_jit = true;
        return r;
    }

    // IDIV: integer division (truncate toward zero).
    if (upper == "IDIV" && nargs == 2 && all_int()) {
        int a = ensure_hi(args[0]);
        int b = ensure_hi(args[1]);
        int r = h.emit(HIR_DIV, TY_INT, a, b);
        h.native_ops++;
        h.needs_jit = true;
        return r;
    }

    // BOUND: clamp x to [lo, hi] — synthesized as max(lo, min(hi, x)).
    if (upper == "BOUND" && nargs == 3 && all_int()) {
        int x = ensure_hi(args[0]);
        int lo = ensure_hi(args[1]);
        int hi = ensure_hi(args[2]);
        int clamped_hi = h.emit(HIR_MIN, TY_INT, x, hi);
        int r = h.emit(HIR_MAX, TY_INT, clamped_hi, lo);
        h.native_ops += 2;
        h.needs_jit = true;
        return r;
    }

    // ---------------------------------------------------------------
    // Native float arithmetic (type-propagated path).
    //
    // When args are numeric but not all integer (at least one float),
    // or for functions that inherently produce floats, operate on
    // doubles directly.  The result stays TY_FLOAT — only converted
    // to string at the boundary where it escapes to non-math context.
    // ---------------------------------------------------------------

    // ADD / SUB with float args: promote to double, emit FADD/FSUB.
    if ((upper == "ADD" || upper == "SUB") && nargs >= 2
        && all_numeric() && any_float()) {
        bool is_add = (upper == "ADD");
        int acc = ensure_float(args[0]);
        for (int i = 1; i < nargs; i++) {
            int b = ensure_float(args[i]);
            hir_kind op = (is_add || i > 1) ? HIR_FADD : HIR_FSUB;
            acc = h.emit(op, TY_FLOAT, acc, b);
        }
        h.native_ops++;
        h.needs_jit = true;
        return acc;
    }

    // MUL with float args.
    if (upper == "MUL" && nargs >= 2 && all_numeric() && any_float()) {
        int acc = ensure_float(args[0]);
        for (int i = 1; i < nargs; i++) {
            int b = ensure_float(args[i]);
            acc = h.emit(HIR_FMUL, TY_FLOAT, acc, b);
        }
        h.native_ops++;
        h.needs_jit = true;
        return acc;
    }

    // FDIV: always produces float.  Promote args to double.
    if (upper == "FDIV" && nargs == 2 && all_numeric()) {
        int a = ensure_float(args[0]);
        int b = ensure_float(args[1]);
        int r = h.emit(HIR_FDIV, TY_FLOAT, a, b);
        h.native_ops++;
        h.needs_jit = true;
        return r;
    }

    // SQRT: always produces float.
    if (upper == "SQRT" && nargs == 1 && h.is_numeric(args[0])) {
        int a = ensure_float(args[0]);
        int r = h.emit(HIR_FSQRT, TY_FLOAT, a);
        h.native_ops++;
        h.needs_jit = true;
        return r;
    }

    // Unary transcendentals: SIN, COS, TAN, etc. → FCALL1.
    // Only when the argument is provably numeric, and the blob
    // has the raw libm symbol registered as an intrinsic.
    //
    for (int ti = 0; s_fp_unary[ti].mux_name; ti++) {
        if (upper == s_fp_unary[ti].mux_name && nargs == 1
            && h.is_numeric(args[0])) {
            uint64_t addr = fp_intrinsic_addr(s_fp_unary[ti].blob_sym);
            if (addr) {
                int a = ensure_float(args[0]);
                int r = h.emit(HIR_FCALL1, TY_FLOAT, a, -1,
                               static_cast<int64_t>(addr));
                h.func_idx[r] = s_fp_unary[ti].fmath;
                h.native_ops++;
                h.needs_jit = true;
                return r;
            }
            break;
        }
    }

    // Binary FP functions: POWER, ATAN2, FMOD → FCALL2.
    for (int ti = 0; s_fp_binary[ti].mux_name; ti++) {
        if (upper == s_fp_binary[ti].mux_name && nargs == 2
            && all_numeric()) {
            uint64_t addr = fp_intrinsic_addr(s_fp_binary[ti].blob_sym);
            if (addr) {
                int a = ensure_float(args[0]);
                int b = ensure_float(args[1]);
                int r = h.emit(HIR_FCALL2, TY_FLOAT, a, b,
                               static_cast<int64_t>(addr));
                h.func_idx[r] = s_fp_binary[ti].fmath;
                h.native_ops++;
                h.needs_jit = true;
                return r;
            }
            break;
        }
    }

    // ---------------------------------------------------------------
    // Fall through to ECALL.
    // ---------------------------------------------------------------

    // Convert any TY_INT or TY_FLOAT args to strings for ECALL.
    for (auto &ai : args) {
        if (h.ty[ai] == TY_INT) {
            ai = h.emit(HIR_ITOA, TY_STRING, ai);
        } else if (h.ty[ai] == TY_FLOAT) {
            ai = h.emit(HIR_FTOA, TY_STRING, ai);
        }
    }

    int fidx = engine_api_lookup(upper.c_str());

    // ---------------------------------------------------------------
    // Unknown function: resolve at compile time.
    //
    // The AST parser creates AST_FUNCCALL for any "name(" pattern,
    // even if the function doesn't exist.  The AST evaluator checks
    // eval flags to decide the result:
    //
    //   EV_FMAND (inside [...]): #-1 FUNCTION (NAME) NOT FOUND
    //   EV_FCHECK (first in seq): literal reconstruction name(args)
    //   Neither (non-first):      literal reconstruction name(args)
    //
    // The compiler resolves this at HIR time so the ECALL/RV64 layer
    // never sees an unknown function.
    // ---------------------------------------------------------------

    if (fidx == 0 && !is_known_function(upper.c_str())) {
        if (s_compile_eval & EV_FMAND) {
            // Mandatory function context: produce error message.
            std::string err = "#-1 FUNCTION (";
            err += upper;
            err += ") NOT FOUND";
            uint64_t addr = rc.pool_str(err);
            return h.emit_sconst(addr, err);
        }

        // Non-mandatory context: reconstruct as literal text.
        // name(arg1,arg2,...) — arguments are still evaluated.
        //
        // If has_close_paren is false (unterminated call), omit ')'.
        //
        std::string lit = node->text;
        lit += '(';
        for (int ai = 0; ai < nargs; ai++) {
            if (ai > 0) lit += ',';
            if (h.is_const(ai < static_cast<int>(args.size()) ? args[ai] : -1)) {
                lit += h.const_str(args[ai]);
            } else {
                // Non-constant arg: must build at runtime via STRCAT.
                goto literal_strcat;
            }
        }
        if (node->has_close_paren) lit += ')';
        {
            uint64_t addr = rc.pool_str(lit);
            return h.emit_sconst(addr, lit);
        }

literal_strcat:
        {
            // Build literal reconstruction with runtime-evaluated args.
            std::vector<int> parts;

            std::string prefix = node->text;
            prefix += '(';
            uint64_t paddr = rc.pool_str(prefix);
            parts.push_back(h.emit_sconst(paddr, prefix));

            for (int ai = 0; ai < nargs; ai++) {
                if (ai > 0) {
                    uint64_t caddr = rc.pool_str(",");
                    parts.push_back(h.emit_sconst(caddr, ","));
                }
                int arg = args[ai];
                if (h.ty[arg] == TY_INT) {
                    arg = h.emit(HIR_ITOA, TY_STRING, arg);
                } else if (h.ty[arg] == TY_FLOAT) {
                    arg = h.emit(HIR_FTOA, TY_STRING, arg);
                }
                parts.push_back(arg);
            }

            if (node->has_close_paren) {
                uint64_t raddr = rc.pool_str(")");
                parts.push_back(h.emit_sconst(raddr, ")"));
            }

            int strcat_idx = engine_api_lookup("STRCAT");
            int r = h.emit_strcat(parts.data(),
                                   static_cast<int>(parts.size()));
            if (r >= 0) h.func_idx[r] = strcat_idx;
            h.ecalls++;
            h.needs_jit = true;
            return r;
        }
    }

    // Validate argument count at compile time.  If the function
    // requires more args than we have, return the same error string
    // the AST evaluator would — not empty.
    if (fidx > 0 && fidx < ENGINE_API_MAX_FUNCS) {
        FUN *fp = engine_api_table[fidx];
        if (fp && nargs < fp->minArgs) {
            char errbuf[256];
            if (fp->minArgs == fp->maxArgs) {
                snprintf(errbuf, sizeof(errbuf),
                    "#-1 FUNCTION (%s) EXPECTS %d ARGUMENTS",
                    upper.c_str(), fp->minArgs);
            } else if (fp->minArgs + 1 == fp->maxArgs) {
                snprintf(errbuf, sizeof(errbuf),
                    "#-1 FUNCTION (%s) EXPECTS %d OR %d ARGUMENTS",
                    upper.c_str(), fp->minArgs, fp->maxArgs);
            } else {
                snprintf(errbuf, sizeof(errbuf),
                    "#-1 FUNCTION (%s) EXPECTS BETWEEN %d AND %d ARGUMENTS",
                    upper.c_str(), fp->minArgs, fp->maxArgs);
            }
            uint64_t addr = rc.pool_str(errbuf);
            return h.emit_sconst(addr, errbuf);
        }
    }

    // Check Tier 2 blob before falling through to ECALL.
    uint64_t t2addr = tier2_lookup(upper);

    // ECALL/Tier2 results are always strings in guest memory.  If the
    // function is known to return integers (strlen, eq, etc.),
    // mark known_int so downstream ops can ATOI and use natively.
    int i = h.emit_call(TY_STRING, fidx,
                         args.data(), nargs,
                         fidx == 0 ? &upper : nullptr);
    if (t2addr) {
        h.tier2_addr[i] = t2addr;
        h.tier2_calls++;
    } else {
        h.ecalls++;
    }
    if (returns_int(upper)) {
        h.known_int[i] = true;
    }
    if (returns_float(upper)) {
        h.known_float[i] = true;
    }
    h.needs_jit = true;
    return i;
}

int hir_lower_node(hir_program &h, rv_compiler &rc,
                    const ASTNode *node) {
    switch (node->type) {
    case AST_LITERAL:
    case AST_SPACE: {
        uint64_t addr = rc.pool_str(node->text);
        return h.emit_sconst(addr, node->text);
    }

    case AST_SEQUENCE:
        return hir_lower_sequence(h, rc, node);

    case AST_FUNCCALL:
        return hir_lower_funccall(h, rc, node);

    case AST_EVALBRACKET:
        if (node->children.size() == 1) {
            // Eval brackets are FMAND context — function calls inside
            // [...] are always dispatched, never literal.
            bool saved_fcheck = s_fcheck_available;
            s_fcheck_available = true;
            int r = hir_lower_node(h, rc, node->children[0].get());
            s_fcheck_available = saved_fcheck;
            return r;
        }
        {
            uint64_t addr = rc.pool_str("");
            return h.emit_sconst(addr, "");
        }

    case AST_ESCAPE:
        // Output the escaped character literally, skipping the backslash.
        if (node->text.size() > 1) {
            std::string lit(1, node->text[1]);
            uint64_t addr = rc.pool_str(lit);
            return h.emit_sconst(addr, lit);
        }
        {
            uint64_t addr = rc.pool_str("");
            return h.emit_sconst(addr, "");
        }

    case AST_SUBST:
        // ## (itext), #@ (inum), #$ (switch token).
        // Resolved from iter/switch context set during lowering.
        if (node->text.size() >= 2 && node->text[0] == '#') {
            if (node->text[1] == '#') {
                if (iter_itext_val >= 0) {
                    return iter_itext_val;
                }
                // Not inside JIT iter — emit ECALL itext(0).
                uint64_t d_addr = rc.pool_str("0");
                int d_val = h.emit_sconst(d_addr, "0");
                int itext_idx = engine_api_lookup("ITEXT");
                int args[1] = { d_val };
                int result = h.emit_call(TY_STRING, itext_idx, args, 1);
                h.ecalls++;
                h.needs_jit = true;
                return result;
            }
            if (node->text[1] == '@') {
                if (iter_inum1_val >= 0) {
                    return iter_inum1_val;
                }
                // Not inside JIT iter — emit ECALL inum(0).
                uint64_t d_addr = rc.pool_str("0");
                int d_val = h.emit_sconst(d_addr, "0");
                int inum_idx = engine_api_lookup("INUM");
                int args[1] = { d_val };
                int result = h.emit_call(TY_STRING, inum_idx, args, 1);
                h.ecalls++;
                h.needs_jit = true;
                return result;
            }
        }

        // %-substitutions.
        if (node->text.size() >= 2 && node->text[0] == '%') {
            char c = node->text[1];

            // %0-%9: runtime cargs at fixed guest memory slots.
            // run_cached_program copies cargs[idx] to CARGS_BASE + idx*256
            // before each dbt_run.  The compiler emits a constant reference
            // to that address — no pointer indirection, no ECALL.
            if (c >= '0' && c <= '9') {
                int idx = c - '0';
                uint64_t carg_addr = rv_compiler::CARGS_BASE
                                   + static_cast<uint64_t>(idx) * rv_compiler::CARGS_SLOT;
                h.needs_jit = true;
                return h.emit_sref(carg_addr);
            }

            // %b = space, %r = newline, %t = tab.
            if (c == 'b' || c == 'B') {
                uint64_t addr = rc.pool_str(" ");
                return h.emit_sconst(addr, " ");
            }
            if (c == 'r' || c == 'R') {
                uint64_t addr = rc.pool_str("\r\n");
                return h.emit_sconst(addr, "\r\n");
            }
            if (c == 't' || c == 'T') {
                uint64_t addr = rc.pool_str("\t");
                return h.emit_sconst(addr, "\t");
            }

            // %# = enactor dbref.  Runtime value at SUBST slot 0.
            if (c == '#') {
                uint64_t addr = rv_compiler::SUBST_BASE
                    + rv_compiler::SUBST_ENACTOR * rv_compiler::SUBST_SLOT;
                h.needs_jit = true;
                return h.emit_sref(addr);
            }

            // %! = executor dbref.  Runtime value at SUBST slot 1.
            if (c == '!') {
                uint64_t addr = rv_compiler::SUBST_BASE
                    + rv_compiler::SUBST_EXECUTOR * rv_compiler::SUBST_SLOT;
                h.needs_jit = true;
                return h.emit_sref(addr);
            }

            // %n/%N = enactor name.  Runtime value at SUBST slot 2.
            if (c == 'n' || c == 'N') {
                uint64_t addr = rv_compiler::SUBST_BASE
                    + rv_compiler::SUBST_NAME * rv_compiler::SUBST_SLOT;
                h.needs_jit = true;
                return h.emit_sref(addr);
            }

            // %l/%L = enactor location.  Runtime value at SUBST slot 3.
            if (c == 'l' || c == 'L') {
                uint64_t addr = rv_compiler::SUBST_BASE
                    + rv_compiler::SUBST_LOCATION * rv_compiler::SUBST_SLOT;
                h.needs_jit = true;
                return h.emit_sref(addr);
            }

            // %c/%x color codes — resolved at compile time.
            // Simple: %xh, %cn, %xr etc. → ColorTable lookup → aColors[].pUTF.
            // Extended: %x<rgb> → parse_rgb + palette lookup.
            if (c == 'c' || c == 'C' || c == 'x' || c == 'X') {
                if (node->text.size() >= 3) {
                    bool bBackground = (c == 'C' || c == 'X');

                    if (node->text[2] == '<') {
                        // Extended color: %x<rgb>, %c<name>.
                        size_t close = node->text.find('>', 3);
                        if (close != std::string::npos) {
                            size_t nColor = close - 3;
                            const UTF8 *pColor = reinterpret_cast<const UTF8 *>(
                                node->text.c_str() + 3);
                            RGB rgb;
                            if (parse_rgb(nColor, pColor, rgb)) {
                                int iColor = FindNearestPaletteEntry(rgb, true);
                                int ci = bBackground ? (iColor + COLOR_INDEX_BG)
                                                     : (iColor + COLOR_INDEX_FG);
                                const UTF8 *pUTF = aColors[ci].pUTF;
                                // For exact palette match, just emit the color.
                                // PUA refinement for non-exact matches is complex;
                                // fall through to ECALL for those.
                                if (pUTF && pUTF[0]) {
                                    std::string cs(reinterpret_cast<const char *>(pUTF));
                                    uint64_t addr = rc.pool_str(cs);
                                    return h.emit_sconst(addr, cs);
                                }
                            }
                        }
                        // Extended parse failed — emit empty.
                        uint64_t addr = rc.pool_str("");
                        return h.emit_sconst(addr, "");
                    } else {
                        // Simple color: %xh, %cn, etc.
                        unsigned int iColor = ColorTable[
                            static_cast<unsigned char>(node->text[2])];
                        if (iColor) {
                            const UTF8 *pUTF = aColors[iColor].pUTF;
                            if (pUTF && pUTF[0]) {
                                std::string cs(reinterpret_cast<const char *>(pUTF));
                                uint64_t addr = rc.pool_str(cs);
                                return h.emit_sconst(addr, cs);
                            }
                        }
                        // Unknown color letter — output it literally.
                        std::string lit(1, node->text[2]);
                        uint64_t addr = rc.pool_str(lit);
                        return h.emit_sconst(addr, lit);
                    }
                }
                // Malformed %c/%x — emit empty.
                uint64_t addr = rc.pool_str("");
                return h.emit_sconst(addr, "");
            }

            // %va-%vz — variable attributes.  Emit ECALL xget(%!, "VA").
            if ((c == 'v' || c == 'V') && node->text.size() >= 3) {
                char letter = node->text[2];
                if ((letter >= 'a' && letter <= 'z')
                    || (letter >= 'A' && letter <= 'Z')) {
                    char upper = static_cast<char>(toupper(
                        static_cast<unsigned char>(letter)));
                    std::string attrname = "V";
                    attrname += upper;

                    uint64_t exec_addr = rv_compiler::SUBST_BASE
                        + rv_compiler::SUBST_EXECUTOR * rv_compiler::SUBST_SLOT;
                    int exec_val = h.emit_sref(exec_addr);
                    uint64_t name_addr = rc.pool_str(attrname);
                    int name_val = h.emit_sconst(name_addr, attrname);

                    int xget_idx = engine_api_lookup("XGET");
                    int args[2] = { exec_val, name_val };
                    int result = h.emit_call(TY_STRING, xget_idx, args, 2);
                    h.ecalls++;
                    h.needs_jit = true;
                    return result;
                }
            }

            // %s/%o/%p/%a — pronouns.  Emit ECALL to subj/obj/poss/aposs(%#).
            if (c == 's' || c == 'S' || c == 'o' || c == 'O'
                || c == 'p' || c == 'P' || c == 'a' || c == 'A') {
                const char *fname;
                switch (c) {
                case 's': case 'S': fname = "SUBJ";  break;
                case 'o': case 'O': fname = "OBJ";   break;
                case 'p': case 'P': fname = "POSS";  break;
                case 'a': case 'A': fname = "APOSS"; break;
                default: fname = "SUBJ"; break;
                }
                // Argument is the enactor dbref from SUBST slot.
                uint64_t enactor_addr = rv_compiler::SUBST_BASE
                    + rv_compiler::SUBST_ENACTOR * rv_compiler::SUBST_SLOT;
                int enactor_val = h.emit_sref(enactor_addr);
                int fidx = engine_api_lookup(fname);
                int args[1] = { enactor_val };
                int result = h.emit_call(TY_STRING, fidx, args, 1);
                h.ecalls++;
                h.needs_jit = true;
                return result;
            }

            // %= — attribute access.
            // %=<name> reads attribute from executor via ECALL xget(executor, name).
            // %=<0> through %=<9> are extended carg references.
            if (c == '=') {
                if (node->text.size() >= 4 && node->text[2] == '<') {
                    size_t close = node->text.find('>', 3);
                    if (close != std::string::npos) {
                        std::string name = node->text.substr(3, close - 3);

                        if (!name.empty() && name[0] >= '0' && name[0] <= '9') {
                            // Numeric arg reference: %=<0> through %=<N>.
                            // Parse the number and reference cargs.
                            int idx = 0;
                            for (char ch : name) {
                                if (ch < '0' || ch > '9') { idx = MAX_ARG; break; }
                                idx = idx * 10 + (ch - '0');
                            }
                            if (idx < rv_compiler::MAX_CARGS) {
                                uint64_t addr = rv_compiler::CARGS_BASE
                                    + static_cast<uint64_t>(idx) * rv_compiler::CARGS_SLOT;
                                h.needs_jit = true;
                                return h.emit_sref(addr);
                            }
                            uint64_t addr = rc.pool_str("");
                            return h.emit_sconst(addr, "");
                        }

                        // Attribute access: emit ECALL xget(%!, name).
                        uint64_t exec_addr = rv_compiler::SUBST_BASE
                            + rv_compiler::SUBST_EXECUTOR * rv_compiler::SUBST_SLOT;
                        int exec_val = h.emit_sref(exec_addr);
                        uint64_t name_addr = rc.pool_str(name);
                        int name_val = h.emit_sconst(name_addr, name);

                        int xget_idx = engine_api_lookup("XGET");
                        int args[2] = { exec_val, name_val };
                        int result = h.emit_call(TY_STRING, xget_idx, args, 2);
                        h.ecalls++;
                        h.needs_jit = true;
                        return result;
                    }
                }
                // Bare %= or malformed — emit empty.
                uint64_t addr = rc.pool_str("");
                return h.emit_sconst(addr, "");
            }

            // %q0-%q9 and %qa-%qz = global register values.
            // Runtime values at SUBST slots 4-13.
            // %q<name> = named register.  Emits ECALL for r("name").
            if ((c == 'q' || c == 'Q') && node->text.size() >= 3) {
                char r = node->text[2];
                int rn = -1;
                if (r >= '0' && r <= '9') {
                    rn = r - '0';
                } else {
                    rn = mux_RegisterSet[static_cast<unsigned char>(r)];
                }
                if (rn >= 0 && rn < MAX_GLOBAL_REGS) {
                    uint64_t addr = rv_compiler::SUBST_BASE
                        + (rv_compiler::SUBST_QREG0 + rn)
                          * rv_compiler::SUBST_SLOT;
                    h.needs_jit = true;
                    return h.emit_sref(addr);
                }
                if (r == '<') {
                    // Named register: %q<name>.
                    // Extract name between < and >.
                    size_t close = node->text.find('>', 3);
                    if (close != std::string::npos) {
                        std::string regname = node->text.substr(3, close - 3);
                        // Emit ECALL for r("name").
                        uint64_t name_addr = rc.pool_str(regname);
                        int name_val = h.emit_sconst(name_addr, regname);
                        int r_idx = engine_api_lookup("R");
                        int args[1] = { name_val };
                        int result = h.emit_call(TY_STRING, r_idx, args, 1);
                        h.ecalls++;
                        h.needs_jit = true;
                        return result;
                    }
                    // Malformed %q<name with no closing > — emit empty.
                    uint64_t addr = rc.pool_str("");
                    return h.emit_sconst(addr, "");
                }
            }

            // %% — literal percent.
            if (c == '%') {
                uint64_t addr = rc.pool_str("%");
                return h.emit_sconst(addr, "%");
            }

            // %m — last command.  Runtime value at SUBST slot.
            if (c == 'm' || c == 'M') {
                uint64_t addr = rv_compiler::SUBST_BASE
                    + rv_compiler::SUBST_LASTCMD * rv_compiler::SUBST_SLOT;
                h.needs_jit = true;
                return h.emit_sref(addr);
            }

            // %k — moniker.  Runtime value at SUBST slot.
            if (c == 'k' || c == 'K') {
                uint64_t addr = rv_compiler::SUBST_BASE
                    + rv_compiler::SUBST_MONIKER * rv_compiler::SUBST_SLOT;
                h.needs_jit = true;
                return h.emit_sref(addr);
            }

            // %| — piped command output.  Runtime value at SUBST slot.
            if (c == '|') {
                uint64_t addr = rv_compiler::SUBST_BASE
                    + rv_compiler::SUBST_POUT * rv_compiler::SUBST_SLOT;
                h.needs_jit = true;
                return h.emit_sref(addr);
            }

            // %+ — number of cargs.  Runtime value at SUBST slot.
            if (c == '+') {
                uint64_t addr = rv_compiler::SUBST_BASE
                    + rv_compiler::SUBST_NCARGS * rv_compiler::SUBST_SLOT;
                h.needs_jit = true;
                return h.emit_sref(addr);
            }

            // %: — enactor objid.  Emit ECALL objid(%#).
            if (c == ':') {
                uint64_t enactor_addr = rv_compiler::SUBST_BASE
                    + rv_compiler::SUBST_ENACTOR * rv_compiler::SUBST_SLOT;
                int enactor_val = h.emit_sref(enactor_addr);
                int objid_idx = engine_api_lookup("OBJID");
                int args[1] = { enactor_val };
                int result = h.emit_call(TY_STRING, objid_idx, args, 1);
                h.ecalls++;
                h.needs_jit = true;
                return result;
            }

            // %i0-%i9 — itext at nesting depth.
            // %i0 = current iter body (same as ##).
            // %i1+ = outer iter levels — emit ECALL itext(N).
            if ((c == 'i' || c == 'I') && node->text.size() >= 3) {
                char d = node->text[2];
                if (d >= '0' && d <= '9') {
                    int depth = d - '0';
                    if (depth == 0 && iter_itext_val >= 0) {
                        // Innermost iter — use compile-time value.
                        return iter_itext_val;
                    }
                    // Outer levels or no compile-time iter context:
                    // emit ECALL itext(depth).
                    std::string ds(1, d);
                    uint64_t d_addr = rc.pool_str(ds);
                    int d_val = h.emit_sconst(d_addr, ds);
                    int itext_idx = engine_api_lookup("ITEXT");
                    int args[1] = { d_val };
                    int result = h.emit_call(TY_STRING, itext_idx, args, 1);
                    h.ecalls++;
                    h.needs_jit = true;
                    return result;
                }
                // %i followed by non-digit — output the char literally.
                std::string lit(1, d);
                uint64_t addr = rc.pool_str(lit);
                return h.emit_sconst(addr, lit);
            }

            // Unknown %-substitution — output the character literally
            // to match the classic mux_exec behavior.
            std::string lit(1, c);
            uint64_t addr = rc.pool_str(lit);
            return h.emit_sconst(addr, lit);
        }

        // Unresolvable substitution — emit empty string.
        {
            uint64_t addr = rc.pool_str("");
            return h.emit_sconst(addr, "");
        }

    case AST_BRACEGROUP:
        // General context (not inside NOEVAL handler): braces are literal.
        // Output {content} with braces preserved.
        if (node->children.empty()) {
            uint64_t addr = rc.pool_str("{}");
            return h.emit_sconst(addr, "{}");
        }
        {
            // Lower content then wrap with literal braces.
            int content = hir_lower_node(h, rc, node->children[0].get());
            if (h.is_const(content)) {
                std::string lit = "{" + h.const_str(content) + "}";
                uint64_t addr = rc.pool_str(lit);
                return h.emit_sconst(addr, lit);
            }
            // Runtime: strcat("{", content, "}").
            uint64_t oaddr = rc.pool_str("{");
            uint64_t caddr = rc.pool_str("}");
            int open = h.emit_sconst(oaddr, "{");
            int close = h.emit_sconst(caddr, "}");
            if (h.ty[content] == TY_INT) {
                content = h.emit(HIR_ITOA, TY_STRING, content);
            } else if (h.ty[content] == TY_FLOAT) {
                content = h.emit(HIR_FTOA, TY_STRING, content);
            }
            int parts[3] = { open, content, close };
            int strcat_idx = engine_api_lookup("STRCAT");
            int r = h.emit_strcat(parts, 3);
            if (r >= 0) h.func_idx[r] = strcat_idx;
            h.ecalls++;
            h.needs_jit = true;
            return r;
        }

    default: {
        uint64_t addr = rc.pool_str("");
        return h.emit_sconst(addr, "");
    }
    }
}

// ===============================================================
// HIR CODEGEN: HIR → RV64
//
