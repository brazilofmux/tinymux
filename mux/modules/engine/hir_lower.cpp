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

#include "jit_tier1_stamp.h"

// Tier 1 build stamp for this unit (#2061).  Folded into the persisted
// code_cache's staleness key so a codegen change here invalidates entries
// compiled by the previous build.  Updates when THIS unit is recompiled,
// which is what makes it work under incremental make.
TIER1_STAMP_DEFINE(TIER1_STAMP_HIR_LOWER);

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
        va = mux_atoi64(u8(a));
        vb = mux_atoi64(u8(b));
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
//
// This MUST match the real xlate() (functions.cpp) exactly, including for
// every zero-valued numeric form (0.000, 00000.0, 0E+100, ...), so the JIT's
// constant fold of t()/not() agrees with the interpreter.  An earlier version
// approximated with mux_atof plus a tiny hardcoded zero list ({0,0.0,+0,-0})
// and wrongly returned true for any other float zero such as "0.000" (#824).
// Use ParseFloat, exactly as xlate() does.
//
static bool const_xlate(const std::string &s) {
    const UTF8 *p = u8(s);
    if (p[0] == '#') {
        // '#-...' is false; any other '#...' is true.
        return p[1] != '-';
    }
    PARSE_FLOAT_RESULT pfr;
    if (ParseFloat(&pfr, p)) {
        if (pfr.iString) {
            // NaN, +Inf, -Inf, Ind.
            return false;
        }
        // A number is false only if every mantissa digit (before and
        // after the decimal point) is '0'.
        for (size_t i = 0; i < pfr.nDigitsA; i++) {
            if (pfr.pDigitsA[i] != '0') return true;
        }
        for (size_t i = 0; i < pfr.nDigitsB; i++) {
            if (pfr.pDigitsB[i] != '0') return true;
        }
        return false;
    }
    // Not a number: true unless it is empty / whitespace-only.
    while (mux_isspace(*p)) {
        p++;
    }
    return p[0] != '\0';
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
            for (int i = 0; i < nargs; i++) sum += mux_atoi64(u8(args[i]));
            result = format_long(sum);
        } else {
            std::vector<double> vals;
            vals.reserve(nargs);
            for (int i = 0; i < nargs; i++) {
                vals.push_back(mux_atof(u8(args[i])));
            }
            result = format_double(AddDoubles(nargs, vals.data()));
        }
        return true;
    }

    // --- SUB(a, b) ---
    if (upper == "SUB" && nargs == 2) {
        long va, vb;
        if (two_int9(args[0], args[1], va, vb)) {
            result = format_long(va - vb);
        } else {
            double vals[2];
            vals[0] = mux_atof(u8(args[0]));
            vals[1] = -mux_atof(u8(args[1]));
            result = format_double(AddDoubles(2, vals));
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
    // Defined two's-complement wrap (matches ADDI / overflow_inject TC005).
    // Do not use signed +1/-1 — that is C++ UB at INT64_MAX/MIN (#1259).
    if (upper == "INC") {
        int64_t v = (nargs >= 1) ? mux_atoi64(u8(args[0])) : 0;
        UTF8 buf[64]; UTF8 *bufc = buf;
        safe_i64toa(static_cast<int64_t>(static_cast<uint64_t>(v) + 1u),
                    buf, &bufc);
        *bufc = '\0';
        result = reinterpret_cast<const char *>(buf);
        return true;
    }
    if (upper == "DEC") {
        int64_t v = (nargs >= 1) ? mux_atoi64(u8(args[0])) : 0;
        UTF8 buf[64]; UTF8 *bufc = buf;
        safe_i64toa(static_cast<int64_t>(static_cast<uint64_t>(v) - 1u),
                    buf, &bufc);
        *bufc = '\0';
        result = reinterpret_cast<const char *>(buf);
        return true;
    }

    // --- ABS(a) ---
    if (upper == "ABS" && nargs == 1) {
        // #1255: |INT64_MIN| cannot format via fval as a non-negative int64.
        // Match fun_abs / iabs and refuse the domain at const-fold too.
        //
        int nDigits = 0;
        if (  is_integer(u8(args[0]), &nDigits)
           && 0 < nDigits
           && mux_atoi64(u8(args[0])) == INT64_MIN)
        {
            result = reinterpret_cast<const char *>(OUT_OF_RANGE);
            return true;
        }
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
    if (upper == "ROUND" && nargs == 2) {
        double r = mux_atof(u8(args[0]));
#ifdef HAVE_IEEE_FP_FORMAT
        int fpc = mux_fpclass(r);
        if (  MUX_FPGROUP(fpc) != MUX_FPGROUP_PASS
           && MUX_FPGROUP(fpc) != MUX_FPGROUP_ZERO) {
            result = reinterpret_cast<const char *>(
                mux_FPStrings[MUX_FPCLASS(fpc)]);
            return true;
        }
        if (MUX_FPGROUP(fpc) == MUX_FPGROUP_ZERO) {
            r = 0.0;
        }
#endif
        int64_t frac = mux_atoi64(u8(args[1]));
        result = reinterpret_cast<const char *>(mux_ftoa(r, true, frac));
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
    if (upper == "BOUND" && (nargs == 2 || nargs == 3)) {
        // Match fun_bound: a FLOAT clamp via mux_atof(non-strict) + fval, and
        // the max arg is optional.  The old fold used mux_atol and so
        // truncated float values (bound(2.5,1,3) folded to 2 not 2.5) and
        // only handled the 3-arg form (#824 sibling).
        double val = mux_atof(u8(args[0]), false);
        double lo  = mux_atof(u8(args[1]), false);
        if (val < lo) val = lo;
        if (nargs == 3) {
            double hi = mux_atof(u8(args[2]), false);
            if (val > hi) val = hi;
        }
        result = format_double(val);
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

    // --- COMP(a, b[, mode]) — string comparison, returns -1/0/1 ---
    if (upper == "COMP" && (nargs == 2 || nargs == 3)) {
        // Match fun_comp exactly: default (and 2-arg) use Unicode collation,
        // mode 'c' uses case-insensitive collation, mode 'a' uses ASCII
        // strcmp.  The old fold used strcmp for everything and ignored the
        // mode arg (#824 sibling): comp(a,A) folded to 1 where collation gives
        // -1, and comp(X,x,c) folded to -1 instead of 0.
        const UTF8 *a = u8(args[0]);
        const UTF8 *b = u8(args[1]);
        size_t nA = args[0].size();
        size_t nB = args[1].size();
        char mode = (nargs == 3 && !args[2].empty()) ? args[2][0] : '\0';
        int cmp;
        if (mode == 'a' || mode == 'A') {
            cmp = strcmp(reinterpret_cast<const char *>(a),
                         reinterpret_cast<const char *>(b));
        } else if (mode == 'c' || mode == 'C') {
            cmp = mux_collate_cmp_ci(a, nA, b, nB);
        } else {
            cmp = mux_collate_cmp(a, nA, b, nB);
        }
        if (cmp < 0) result = "-1";
        else if (cmp != 0) result = "1";
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
        // Use co_cluster_count: strip color, then count grapheme
        // clusters — matches fun_strlen's utf8_cluster_count.
        size_t n = co_cluster_count(
            reinterpret_cast<const unsigned char *>(args[0].data()),
            args[0].size());
        result = format_long(static_cast<long>(n));
        return true;
    }

    // CAT/STRCAT fold to a buffer the runtime would have truncated.
    //
    // safe_str stops at LBUF_SIZE-1, and #1915 bounded the blob's
    // rv64_strcat to match, so a fold that concatenates past that limit
    // disagrees with both routes it is supposed to be replacing.  It also
    // hands an over-LBUF string to the co_* folders below, which strip
    // into an LBUF_SIZE stack buffer -- that is how #1930 reached the
    // WP_SAFE spin.  Cap while appending rather than after: the result is
    // identical and the intermediate stays bounded.
    if (upper == "CAT") {
        std::string merged;
        for (int i = 0; i < nargs && merged.size() < LBUF_SIZE - 1; i++) {
            if (i > 0) merged += ' ';
            merged += args[i];
        }
        if (merged.size() > LBUF_SIZE - 1) merged.resize(LBUF_SIZE - 1);
        result = merged;
        return true;
    }

    if (upper == "STRCAT") {
        std::string merged;
        for (int i = 0; i < nargs && merged.size() < LBUF_SIZE - 1; i++) {
            merged += args[i];
        }
        if (merged.size() > LBUF_SIZE - 1) merged.resize(LBUF_SIZE - 1);
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

    // String functions via co_* Ragel wrappers — semantics-matched
    // to the server by construction (same Ragel source).
    // Default delimiter is space (0x20).

    if (upper == "WORDS" && (nargs == 1 || nargs == 2)
        && (nargs == 1 || args[1].size() <= 1)) {
        unsigned char delim = (nargs == 2 && !args[1].empty())
            ? static_cast<unsigned char>(args[1][0]) : ' ';
        size_t n = co_words_count(
            reinterpret_cast<const unsigned char *>(args[0].data()),
            args[0].size(), delim);
        result = format_long(static_cast<long>(n));
        return true;
    }

    if (upper == "FIRST" && (nargs == 1 || nargs == 2)
        && (nargs == 1 || args[1].size() <= 1)) {
        unsigned char delim = (nargs == 2 && !args[1].empty())
            ? static_cast<unsigned char>(args[1][0]) : ' ';
        LBuf out = LBuf_Src("hir_first");
        size_t n = co_first(reinterpret_cast<unsigned char *>(out.get()),
            reinterpret_cast<const unsigned char *>(args[0].data()),
            args[0].size(), delim);
        result.assign(reinterpret_cast<const char *>(out.get()), n);
        return true;
    }

    if (upper == "REST" && (nargs == 1 || nargs == 2)
        && (nargs == 1 || args[1].size() <= 1)) {
        unsigned char delim = (nargs == 2 && !args[1].empty())
            ? static_cast<unsigned char>(args[1][0]) : ' ';
        // Match interpreter: trim_space_sep strips leading+trailing
        // spaces for space delimiter before split_token.
        const char *p = args[0].data();
        size_t slen = args[0].size();
        if (delim == ' ') {
            while (slen > 0 && *p == ' ') { p++; slen--; }
            while (slen > 0 && p[slen - 1] == ' ') { slen--; }
        }
        LBuf out = LBuf_Src("hir_rest");
        size_t n = co_rest(reinterpret_cast<unsigned char *>(out.get()),
            reinterpret_cast<const unsigned char *>(p), slen, delim);
        // Match interpreter: split_token skips consecutive spaces
        // after the delimiter for space-delimited lists.
        const char *r = reinterpret_cast<const char *>(out.get());
        if (delim == ' ') {
            while (n > 0 && *r == ' ') { r++; n--; }
        }
        result.assign(r, n);
        return true;
    }

    if (upper == "LAST" && (nargs == 1 || nargs == 2)
        && (nargs == 1 || args[1].size() <= 1)) {
        unsigned char delim = (nargs == 2 && !args[1].empty())
            ? static_cast<unsigned char>(args[1][0]) : ' ';
        // Match interpreter: trim_space_sep for space delimiter.
        const char *p = args[0].data();
        size_t slen = args[0].size();
        if (delim == ' ') {
            while (slen > 0 && *p == ' ') { p++; slen--; }
            while (slen > 0 && p[slen - 1] == ' ') { slen--; }
        }
        LBuf out = LBuf_Src("hir_last");
        size_t n = co_last(reinterpret_cast<unsigned char *>(out.get()),
            reinterpret_cast<const unsigned char *>(p), slen, delim);
        result.assign(reinterpret_cast<const char *>(out.get()), n);
        return true;
    }

    if (upper == "STRIPANSI" && nargs == 1) {
        LBuf out = LBuf_Src("hir_strip");
        size_t n = co_strip_color(reinterpret_cast<unsigned char *>(out.get()),
            reinterpret_cast<const unsigned char *>(args[0].data()),
            args[0].size());
        result.assign(reinterpret_cast<const char *>(out.get()), n);
        return true;
    }

    if (upper == "SQUISH" && (nargs == 1
        || (nargs == 2 && args[1].size() <= 1))) {
        // Squish compresses consecutive delimiters to one.
        unsigned char ch = (nargs == 2 && !args[1].empty())
            ? static_cast<unsigned char>(args[1][0]) : ' ';
        LBuf out = LBuf_Src("hir_squish");
        size_t n = co_compress(reinterpret_cast<unsigned char *>(out.get()),
            reinterpret_cast<const unsigned char *>(args[0].data()),
            args[0].size(), ch);
        result.assign(reinterpret_cast<const char *>(out.get()), n);
        return true;
    }

    // --- MID(string, start, count) ---
    // Uses co_mid_cluster (grapheme clusters) to match fun_mid.
    if (upper == "MID" && nargs == 3) {
        int64_t iStart = mux_atoi64(u8(args[1]));
        int64_t nMid = mux_atoi64(u8(args[2]));
        if (nMid < 0) {
            iStart += 1 + nMid;
            nMid = -nMid;
        }
        if (iStart < 0) {
            nMid += iStart;
            iStart = 0;
        }
        if (nMid <= 0) {
            result = "";
            return true;
        }
        LBuf out = LBuf_Src("hir_mid");
        size_t n = co_mid_cluster(reinterpret_cast<unsigned char *>(out.get()),
            reinterpret_cast<const unsigned char *>(args[0].data()),
            args[0].size(),
            static_cast<size_t>(iStart), static_cast<size_t>(nMid));
        result.assign(reinterpret_cast<const char *>(out.get()), n);
        return true;
    }

    // --- POS(needle, haystack) ---
    if (upper == "POS" && nargs == 2) {
        size_t n = co_pos(
            reinterpret_cast<const unsigned char *>(args[1].data()),
            args[1].size(),
            reinterpret_cast<const unsigned char *>(args[0].data()),
            args[0].size());
        // co_pos returns 0 for "not found"; fun_pos returns the string "#-1".
        // Match the interpreter so @if pos(...) truthiness agrees.
        result = (0 == n) ? "#-1" : format_long(static_cast<long>(n));
        return true;
    }

    // --- MEMBER(list, target[, delim]) ---
    if (upper == "MEMBER" && (nargs == 2 || nargs == 3)
        && (nargs == 2 || args[2].size() <= 1)) {
        unsigned char delim = (nargs == 3 && !args[2].empty())
            ? static_cast<unsigned char>(args[2][0]) : ' ';
        size_t n = co_member(
            reinterpret_cast<const unsigned char *>(args[1].data()),
            args[1].size(),
            reinterpret_cast<const unsigned char *>(args[0].data()),
            args[0].size(), delim);
        result = format_long(static_cast<long>(n));
        return true;
    }

    // --- EXTRACT(list, first, count[, delim[, osep]]) ---
    // Multi-char delim/osep can't be represented in the single-byte co_extract
    // fast path; skip folding so the general lowering falls back to the
    // interpreter (matches fun_extract).  (#768 audit sibling.)
    if (upper == "EXTRACT" && nargs >= 3 && nargs <= 5
        && (nargs < 4 || args[3].size() <= 1)
        && (nargs < 5 || args[4].size() <= 1)) {
        int64_t iFirst = mux_atoi64(u8(args[1]));
        int64_t nWords = mux_atoi64(u8(args[2]));
        if (iFirst < 1 || nWords < 1) {
            result = "";
            return true;
        }
        unsigned char delim = (nargs >= 4 && !args[3].empty())
            ? static_cast<unsigned char>(args[3][0]) : ' ';
        // Absent osep defaults to the delimiter (DELIM_INIT), but an
        // explicit-empty osep means SPACE (delim_check tlen==0).
        unsigned char osep = (nargs >= 5)
            ? (!args[4].empty() ? static_cast<unsigned char>(args[4][0]) : ' ')
            : delim;
        LBuf out = LBuf_Src("hir_extract");
        size_t n = co_extract(reinterpret_cast<unsigned char *>(out.get()),
            reinterpret_cast<const unsigned char *>(args[0].data()),
            args[0].size(),
            static_cast<size_t>(iFirst), static_cast<size_t>(nWords),
            delim, osep);
        result.assign(reinterpret_cast<const char *>(out.get()), n);
        return true;
    }

    // --- REPEAT(string, count) ---
    if (upper == "REPEAT" && nargs == 2) {
        int64_t count = mux_atoi64(u8(args[1]));
        if (count <= 0) {
            result = "";
            return true;
        }
        // Decline to fold a result that would not fit (#1954).
        //
        // co_repeat refuses an over-long repeat by returning empty.
        // fun_repeat does neither of those things: a single character goes
        // through safe_fill and TRUNCATES to LBUF_SIZE-1, and anything longer
        // returns #-1 STRING TOO LONG.  Folding here therefore substituted an
        // empty string for both, silently, on the default-on JIT path.
        //
        // Declining rather than reproducing those two behaviours is
        // deliberate.  Reimplementing fun_repeat's truncate-vs-error split
        // here would put it in a second place that can drift from the first
        // -- which is exactly how #1948's duplicated safe_chr hid a defect
        // for as long as the copy existed.  A fold that declines cannot
        // disagree with the interpreter, because the interpreter answers.
        //
        // Costs nothing for real softcode: this only fires where the result
        // already cannot fit in an LBUF, so repeat(-,78) still folds.
        const size_t nRepeatLen = args[0].size();
        if (0 != nRepeatLen
            && static_cast<uint64_t>(count) > (LBUF_SIZE - 1) / nRepeatLen) {
            return false;
        }
        LBuf out = LBuf_Src("hir_repeat");
        size_t n = co_repeat(reinterpret_cast<unsigned char *>(out.get()),
            reinterpret_cast<const unsigned char *>(args[0].data()),
            args[0].size(), static_cast<size_t>(count));
        result.assign(reinterpret_cast<const char *>(out.get()), n);
        return true;
    }

    // --- TRIM(string[, type[, char]]) ---
    // type: b=both (default), l=left, r=right
    // Only fold single-character trim patterns.  Multi-character
    // patterns use co_trim_pattern() at runtime which has different
    // semantics — let those go through the ECALL path.
    if (upper == "TRIM" && nargs >= 1 && nargs <= 3
        && (nargs < 3 || args[2].size() <= 1)) {
        unsigned char trim_char = (nargs >= 3 && !args[2].empty())
            ? static_cast<unsigned char>(args[2][0]) : ' ';
        int trim_flags = 3;  // both (1=left, 2=right, 3=both)
        if (nargs >= 2 && !args[1].empty()) {
            char t = static_cast<char>(tolower(
                static_cast<unsigned char>(args[1][0])));
            if (t == 'l') trim_flags = 1;
            else if (t == 'r') trim_flags = 2;
        }
        LBuf out = LBuf_Src("hir_trim");
        size_t n = co_trim(reinterpret_cast<unsigned char *>(out.get()),
            reinterpret_cast<const unsigned char *>(args[0].data()),
            args[0].size(), trim_char, trim_flags);
        result.assign(reinterpret_cast<const char *>(out.get()), n);
        return true;
    }

    // --- EDIT(string, from, to[, from2, to2, ...]) ---
    // Fold constant edit operations including anchor semantics.
    if (upper == "EDIT" && nargs >= 3) {
        std::string cur = args[0];
        for (int i = 1; i + 1 < nargs; i += 2) {
            size_t fLen = args[i].size();
            if (fLen == 1 && args[i][0] == '^') {
                // Prepend.
                cur = args[i + 1] + cur;
            } else if (fLen == 1 && args[i][0] == '$') {
                // Append.
                cur = cur + args[i + 1];
            } else {
                // Handle escaped anchors (\^ %^ \$ %$).
                const char *pFrom = args[i].data();
                size_t fLenActual = fLen;
                std::string fromBuf;
                if (fLen == 2
                    && (pFrom[0] == '\\' || pFrom[0] == '%')
                    && (pFrom[1] == '^' || pFrom[1] == '$')) {
                    fromBuf = std::string(1, pFrom[1]);
                    pFrom = fromBuf.data();
                    fLenActual = 1;
                }
                LBuf out = LBuf_Src("hir_edit");
                size_t n = co_edit(
                    reinterpret_cast<unsigned char *>(out.get()),
                    reinterpret_cast<const unsigned char *>(cur.data()),
                    cur.size(),
                    reinterpret_cast<const unsigned char *>(pFrom),
                    fLenActual,
                    reinterpret_cast<const unsigned char *>(args[i+1].data()),
                    args[i+1].size());
                cur.assign(reinterpret_cast<const char *>(out.get()), n);
            }
        }
        result = cur;
        return true;
    }

    // --- DELETE(string, start, count) ---
    // Uses co_delete_cluster (grapheme clusters) to match fun_delete.
    if (upper == "DELETE" && nargs == 3) {
        int64_t iStart = mux_atoi64(u8(args[1]));
        int64_t nDel = mux_atoi64(u8(args[2]));
        if (nDel < 0) {
            iStart += 1 + nDel;
            nDel = -nDel;
        }
        if (iStart < 0) {
            nDel += iStart;
            iStart = 0;
        }
        if (nDel <= 0) {
            result = args[0];
            return true;
        }
        LBuf out = LBuf_Src("hir_delete");
        size_t n = co_delete_cluster(reinterpret_cast<unsigned char *>(out.get()),
            reinterpret_cast<const unsigned char *>(args[0].data()),
            args[0].size(),
            static_cast<size_t>(iStart), static_cast<size_t>(nDel));
        result.assign(reinterpret_cast<const char *>(out.get()), n);
        return true;
    }

    // --- RIGHT(string, count) ---
    // Uses co_cluster_count + co_mid_cluster to match fun_right.
    if (upper == "RIGHT" && nargs == 2) {
        int64_t nRight = mux_atoi64(u8(args[1]));
        if (nRight < 0) {
            result = "#-1 OUT OF RANGE";
            return true;
        }
        if (nRight == 0) {
            result = "";
            return true;
        }
        size_t nClusters = co_cluster_count(
            reinterpret_cast<const unsigned char *>(args[0].data()),
            args[0].size());
        if (static_cast<size_t>(nRight) >= nClusters) {
            result = args[0];
        } else {
            LBuf out = LBuf_Src("hir_right");
            size_t n = co_mid_cluster(
                reinterpret_cast<unsigned char *>(out.get()),
                reinterpret_cast<const unsigned char *>(args[0].data()),
                args[0].size(),
                nClusters - static_cast<size_t>(nRight),
                static_cast<size_t>(nRight));
            result.assign(reinterpret_cast<const char *>(out.get()), n);
        }
        return true;
    }

    // --- ISNUM(string) ---
    if (upper == "ISNUM" && nargs == 1) {
        result = is_real(u8(args[0])) ? "1" : "0";
        return true;
    }

    // --- ISINT(string) ---
    if (upper == "ISINT" && nargs == 1) {
        result = is_integer(u8(args[0]), nullptr) ? "1" : "0";
        return true;
    }

    // --- LPOS(list, char) ---
    if (upper == "LPOS" && nargs == 2) {
        // co_lpos matches a single byte; fun_lpos matches the FULL
        // pattern via color-aware co_search.  Fold only the one-byte
        // pattern case (empty defaults to space) — anything longer
        // goes to the runtime wrapper, which mirrors fun_lpos
        // (wrapper audit: lpos(abxab,bx) folded to every 'b').
        if (args[1].size() > 1) {
            return false;
        }
        unsigned char target = (!args[1].empty())
            ? static_cast<unsigned char>(args[1][0]) : ' ';
        LBuf out = LBuf_Src("hir_lpos");
        size_t n = co_lpos(reinterpret_cast<unsigned char *>(out.get()),
            reinterpret_cast<const unsigned char *>(args[0].data()),
            args[0].size(), target);
        result.assign(reinterpret_cast<const char *>(out.get()), n);
        return true;
    }

    // =============================================================
    // Pure constants (0-arg functions returning fixed values)
    // =============================================================

    if (upper == "PI" && nargs == 0) {
        result = "3.141592653589793";
        return true;
    }
    if (upper == "E" && nargs == 0) {
        result = "2.718281828459045";
        return true;
    }

    // Server constants — immutable after startup.
    if (upper == "MUDNAME" && nargs == 0) {
        result = reinterpret_cast<const char *>(mudconf.mud_name);
        return true;
    }
    if (upper == "VERSION" && nargs == 0) {
        result = reinterpret_cast<const char *>(mudstate.version);
        return true;
    }

    return false;
}

// ---------------------------------------------------------------
// Type tracking for native integer arithmetic
// ---------------------------------------------------------------

// Functions known to always return integer strings.
// POS is excluded: not-found yields the string "#-1" (#770).
// ABS/MAX/MIN/BOUND are NOT here: they are the float variants
// (fun_abs/fun_max/fun_min/fun_bound use mux_atof + fval), so e.g.
// abs(2.5)=2.5 — marking them known_int caused a downstream native int op
// to ATOI-truncate the float (#826).  They live in returns_float instead.
// SIGN stays: fun_sign is float-computed (#1260) but always emits -1/0/1.
//
bool returns_int(const std::string &upper) {
    return upper == "RAND" || upper == "STRLEN" || upper == "WORDS"
        || upper == "EQ" || upper == "NEQ"
        || upper == "GT" || upper == "GTE" || upper == "LT" || upper == "LTE"
        || upper == "NOT" || upper == "T" || upper == "COMP"
        || upper == "INC" || upper == "DEC" || upper == "SIGN"
        || upper == "MOD" || upper == "IDIV"
        || upper == "STRMATCH" || upper == "MEMBER";
}

// Functions known to always return floating-point strings.
// (Marking an integer-valued result as float is harmless — float ops format
// whole numbers identically — but marking a float result as int truncates,
// so the float-capable abs/max/min/bound belong here.)
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
        || upper == "MEAN" || upper == "MEDIAN" || upper == "STDDEV"
        || upper == "ABS" || upper == "MAX" || upper == "MIN"
        || upper == "BOUND";
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
    { "LOG",   "log10", FMATH_LOG10 },  // MUX log() defaults to common (base 10)
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
    // max()/min() use float compare (mux_atof+fval); restore a native
    // path via libm fmax/fmin after #1260 removed integer HIR_MAX/MIN
    // (#1273).  Multi-arg forms chain FCALL2 below.
    //
    { "MAX",   "fmax",  FMATH_FMAX  },
    { "MIN",   "fmin",  FMATH_FMIN  },
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

// Invalidate compile-time %q tracking after emitting an ECALL whose
// callee may mutate mudstate.global_regs (u()/ufuns, edefault, any
// opaque host function).  Subsequent tracked r(n) reads then fall back
// to the ECALL fun_r path, which reads the authoritative global_regs
// (docs/plan-jit-evalbracket-lift.md, Phase 3).
static void qreg_clobber() {
    for (int i = 0; i < HIR_NUM_QREGS; i++) qreg[i] = -1;
}

// Single choke point for runtime %q register reads (#996 step 2).
// Every lowering that reads a SUBST_QREG slot MUST come through here:
// the slot holds at most SUBST_SLOT-1 bytes, so a register whose
// authoritative value is longer has bit rn set in the guest
// QREG_LONGBITS word and the read must fetch via the fun_r ECALL
// (authoritative global_regs) instead of the truncated slot.
//
// Cost when the bit is clear: a native u64 load + shift/and + branch
// and a diamond in the block structure, on top of the Phase 2
// slot-materialization STRCAT.
static int emit_qreg_read(hir_program &h, rv_compiler &rc, int rn)
{
    h.needs_jit = true;

    // bit = (LONGBITS >> rn) & 1.  HIR_LUA_ALOAD is a plain native
    // "load int64 at base + (key-1)*8" despite the name — with key=1
    // it reads exactly the u64 at QREG_LONGBITS.
    int key1 = h.emit_iconst(1);
    int word = h.emit(HIR_LUA_ALOAD, TY_INT, key1, -1,
                      static_cast<int64_t>(rv_compiler::QREG_LONGBITS));
    int shn  = h.emit_iconst(rn);
    int shr  = h.emit(HIR_SHR, TY_INT, word, shn);
    int one  = h.emit_iconst(1);
    int bit  = h.emit(HIR_BAND, TY_INT, shr, one);

    int long_block  = h.new_block();
    int short_block = h.new_block();
    int entry_block = h.cur_block;
    h.emit(HIR_BRC, TY_VOID, bit, short_block, long_block);
    h.add_edge(entry_block, long_block);
    h.add_edge(entry_block, short_block);

    // Long path: fun_r ECALL — reads the full value from global_regs.
    h.cur_block = long_block;
    char name[2] = { static_cast<char>(
        (rn < 10) ? ('0' + rn) : ('a' + rn - 10)), 0 };
    uint64_t naddr = rc.pool_str(name);
    int nval = h.emit_sconst(naddr, name);
    int r_idx = engine_api_lookup("R");
    int rargs[1] = { nval };
    int long_val = h.emit_call(TY_STRING, r_idx, rargs, 1);
    h.ecalls++;
    int long_exit = h.cur_block;

    // Short path: materialize the slot at this sequence point
    // (Phase 2 semantics — see the read-write-read notes).
    h.cur_block = short_block;
    uint64_t addr = rv_compiler::SUBST_BASE
        + static_cast<uint64_t>(rv_compiler::SUBST_QREG0 + rn)
          * rv_compiler::SUBST_SLOT;
    int sref = h.emit_sref(addr);
    int short_val = h.emit_strcat(&sref, 1);
    int short_exit = h.cur_block;

    int merge_block = h.new_block();
    h.cur_block = long_exit;
    h.emit(HIR_BR, TY_VOID, -1, -1, merge_block);
    h.add_edge(long_exit, merge_block);
    h.cur_block = short_exit;
    h.emit(HIR_BR, TY_VOID, -1, -1, merge_block);
    h.add_edge(short_exit, merge_block);

    h.cur_block = merge_block;
    int blocks[2] = { long_exit, short_exit };
    int vals[2] = { long_val, short_val };
    return h.emit_phi(TY_STRING, -1, blocks, vals, 2);
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

// s_fmand_abort: set when a lookup fails under EV_FMAND, to stop the rest
// of the region from being emitted (#1247).  The AST route signals this
// from the funccall up to its sequence; here the unknown function is
// resolved at lowering time, so the sequence simply stops concatenating
// children after the one that failed.  Scoped like s_fcheck_available:
// saved and cleared at an eval-bracket boundary, consumed by the nearest
// enclosing EV_FMAND sequence.
bool s_fmand_abort;

// Tier 3 compile-time state: deps collector and inline depth.
// Set by compile_expression() before calling hir_lower_node().
//
std::vector<compiled_program::inline_dep> *s_compile_deps = nullptr;
int s_inline_depth = 0;
static constexpr int MAX_INLINE_DEPTH = 3;

// Static FUNCCALL watermarks for inlined attribute bodies (#1056).
// Mirrors jit_compiler's ast_max_funccall_depth / ast_funccall_count.
//
static int hir_ast_max_funccall_depth(const ASTNode *node)
{
    if (!node) return 0;
    int child_max = 0;
    for (const auto &c : node->children) {
        int d = hir_ast_max_funccall_depth(c.get());
        if (d > child_max) child_max = d;
    }
    return child_max + (node->type == AST_FUNCCALL ? 1 : 0);
}

static int hir_ast_funccall_count(const ASTNode *node)
{
    if (!node) return 0;
    int n = (node->type == AST_FUNCCALL) ? 1 : 0;
    for (const auto &c : node->children) {
        n += hir_ast_funccall_count(c.get());
    }
    return n;
}

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
static constexpr int QREG_ITER_CURSOR = 12;  // byte offset into list (TY_INT, #2052)
static constexpr int QREG_FILTER_KEPT = 13;  // kept-element count (TY_INT, #2080).
                                             // filter()'s osep prints between
                                             // KEPT elements, and a kept empty
                                             // element claims its slot, so
                                             // "first" cannot be keyed on the
                                             // accumulator length -- it needs
                                             // its own counter.

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
        // Leading spaces too: ast_eval_node skips both leading and
        // trailing AST_SPACE children (mux_exec's at_space=1).  Only
        // the trailing trim was mirrored here, so a sequence starting
        // with literal whitespace kept it on the JIT route —
        // objeval(*p, ncon(#1)) returned " 0" vs the AST's "0"
        // (#991 error-path cluster).
        while (first < last && node->children[first]->type == AST_SPACE) {
            first++;
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

        // A failed mandatory lookup ends the region (#1247).  Everything
        // after it is literal text by both engines' rules anyway, so no
        // evaluation is lost -- but emitting it makes the diagnostic read
        // as though part of the region had succeeded.
        if (  (s_compile_eval & EV_FMAND)
           && s_fmand_abort) {
            s_fmand_abort = false;
            break;
        }

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
            // Same refusal sentinel as the near-identical loop below, which
            // #1440 guarded; this copy was missed because the two blocks are
            // separated by ~80 lines and differ only in which node they walk
            // (#1457).
            int part = hir_lower_node(h, rc, inner->children[i].get());
            if (part < 0) {
                return -1;
            }
            parts.push_back(part);
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

// True if the argument has a brace group at its own top level — i.e. the
// argument is itself a brace group, or a sequence one of whose direct
// children is a brace group.  Brace groups nested inside sub-expressions
// (function calls, eval brackets) belong to those contexts and are not
// considered here.
static bool arg_has_toplevel_bracegroup(const ASTNode *child) {
    if (child->type == AST_BRACEGROUP) {
        return true;
    }
    if (child->type == AST_SEQUENCE) {
        for (const auto &c : child->children) {
            if (c->type == AST_BRACEGROUP) {
                return true;
            }
        }
    }
    return false;
}

// Lower a normal function argument, trimming only top-level surrounding
// spaces to match parse_arglist()/parse_to() comma argument handling.
static int hir_lower_argument(hir_program &h, rv_compiler &rc,
                              const ASTNode *child) {
    // A brace group at the top level of a normal (non-NOEVAL) function
    // argument must have its outer braces stripped, with the contents
    // passed as un-function-checked literal text — the classic
    // EV_STRIP_CURLY behavior that the AST evaluator implements in its
    // AST_BRACEGROUP handling.  The JIT emits brace groups as literal
    // "{...}" (hir_lower_node/AST_BRACEGROUP), which would diverge: e.g.
    // isjson({"a":1}) or add({1},{2}) would see the braces under the JIT
    // but not under the AST evaluator.  Rather than replicate the
    // stripping in the JIT, bail the compile so the whole expression is
    // evaluated by the AST path, which strips correctly.  NOEVAL handlers
    // (if/switch/iter/...) take their own hir_lower_trimmed() path and are
    // unaffected.
    if (arg_has_toplevel_bracegroup(child)) {
        rc.out_exhausted = true;  // force AST fallback (brace stripping)
        uint64_t addr = rc.pool_str("");
        return h.emit_sconst(addr, "");
    }

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
        // A refused child lowering leaves -1 in the vector; indexing the
        // per-instruction arrays with it is out of bounds (#1440).
        for (int p : parts) {
            if (p < 0) return -1;
        }
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
            // is_const() guards a negative index internally, so this loop
            // does not fault -- but a -1 still reaches emit_strcat below,
            // which copies it into carg[] unchecked and leaves a malformed
            // operand in the HIR for codegen to walk (#1457).
            int a = hir_lower_node(h, rc, child.get());
            if (a < 0) {
                return -1;
            }
            args.push_back(a);
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
    // Only single-char register names (0-9, a-z via mux_RegisterSet);
    // multi-char / named registers fall through to ECALL (#1054).
    if (fname == "R" && node->children.size() == 1) {
        const ASTNode *arg0 = node->children[0].get();
        if (arg0->type == AST_LITERAL && arg0->text.size() == 1) {
            int rn = mux_RegisterSet[
                static_cast<unsigned char>(arg0->text[0])];
            if (rn >= 0 && rn < HIR_NUM_QREGS && qreg[rn] >= 0) {
                qreg_used = true;
                return qreg[rn];
            }
        }
        // Fall through to ECALL if register number unknown or not set.
    }

    // setq(n, value) — set %q register, return empty string.
    // Match letq: require a single-char register name via mux_RegisterSet
    // so multi-char literals (e.g. "10") do not take only the first
    // character (#1054).  Letter regs a-z still emit SETQ_SYNC; only
    // slots in [0, HIR_NUM_QREGS) are tracked for compile-time r(n).
    if (fname == "SETQ" && node->children.size() == 2) {
        const ASTNode *arg0 = node->children[0].get();
        if (arg0->type == AST_LITERAL && arg0->text.size() == 1) {
            int rn = mux_RegisterSet[
                static_cast<unsigned char>(arg0->text[0])];
            if (rn >= 0 && rn < MAX_GLOBAL_REGS) {
                int val = hir_lower_argument(h, rc, node->children[1].get());
                if (rn < HIR_NUM_QREGS) {
                    qreg[rn] = val;
                    qreg_used = true;
                }

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
    // Same single-char / mux_RegisterSet rules as setq (#1054).
    if (fname == "SETR" && node->children.size() == 2) {
        const ASTNode *arg0 = node->children[0].get();
        if (arg0->type == AST_LITERAL && arg0->text.size() == 1) {
            int rn = mux_RegisterSet[
                static_cast<unsigned char>(arg0->text[0])];
            if (rn >= 0 && rn < MAX_GLOBAL_REGS) {
                int val = hir_lower_argument(h, rc, node->children[1].get());
                if (rn < HIR_NUM_QREGS) {
                    qreg[rn] = val;
                    qreg_used = true;
                }

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

    // if() takes 2 or 3 arguments (same handler as ifelse); keying has_else
    // off the NAME instead of the arity is what declined every 3-argument
    // if() — the shape fell through to general_lowering, whose FN_NOEVAL
    // check bails the whole compilation (#2162).
    if ((fname == "IFELSE" && node->children.size() == 3)
        || (fname == "IF" && (node->children.size() == 2
                           || node->children.size() == 3))) {
        bool has_else = (node->children.size() == 3);

        // Lower the condition (always evaluated).
        int cond = hir_lower_node(h, rc, node->children[0].get());

        // hir_lower_node returns -1 to REFUSE the compile (unknown AST node
        // type, #1242) and sets h.overflowed.  Indexing with it reads before the
        // per-instruction arrays: kind[] is the FIRST member of hir_program, so
        // h.kind[-1] is four bytes before the struct.  AddressSanitizer reports it
        // as a stack-buffer-overflow; without a sanitizer it is a silent garbage
        // read that decides a branch.  Propagate the refusal -- no rollback is
        // needed, since overflowed abandons the compile before codegen and the AST
        // evaluator takes the expression.
        //
        if (cond < 0) {
            return -1;
        }

        // Ensure condition is integer.  The interpreter decides this
        // condition with xlate() (fun_ifelse in funceval.cpp), so anything
        // we fold or emit here has to agree with xlate() exactly (#1157).
        if (h.ty[cond] != TY_INT) {
            if (h.is_const(cond)) {
                // Genuine compile-time literal: fold using the interpreter's
                // own truth test.  mux_atoi64() is not xlate() — "abc", "#5"
                // and "0abc" are all true but atol to 0, so the fold used to
                // pick the else arm for them.
                //
                // is_const() also excludes emit_sref() slots (%0-%9 and
                // SUBST), which are HIR_SCONST with an empty sval and a
                // runtime_ref marking; their value is only known once
                // run_cached_program fills CARGS_BASE.  Folding those read
                // "" and chose the else arm on every call — the reported
                // defect.  They now fall through to the ECALL path below.
                //
                // c_str() is well-defined for empty strings; xlate only reads.
                std::string cval = h.sval[cond];
                bool truth = xlate(const_cast<UTF8 *>(
                    reinterpret_cast<const UTF8 *>(cval.c_str())));
                cond = h.emit_iconst(truth ? 1 : 0);
            } else if (h.known_int[cond]) {
                cond = h.emit(HIR_ATOI, TY_INT, cond);
            } else {
                // Runtime string, or a float.  A float cannot use FTOI here:
                // xlate() calls any nonzero float true, but FTOI truncates
                // 0.5 and -0.5 to 0, and xlate() calls NaN/+Inf/-Inf false
                // where a plain nonzero test would call them true.  There is
                // no HIR shape that reproduces xlate() for these, so defer
                // to the ECALL, which runs the real thing.
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
        // Lower both arms first (no BR yet), then pick a PHI type and
        // coerce each arm in its exit block before branching to merge.
        // Float arms used to collapse to TY_STRING and then strcpy the
        // double bits as a C string (#1143).
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

        // Pick PHI type, then coerce arms in their exit blocks.
        hir_type rty;
        if (h.ty[true_val] == TY_INT && h.ty[false_val] == TY_INT) {
            rty = TY_INT;
        } else if ((h.ty[true_val] == TY_FLOAT || h.ty[true_val] == TY_INT)
                   && (h.ty[false_val] == TY_FLOAT || h.ty[false_val] == TY_INT)
                   && (h.ty[true_val] == TY_FLOAT || h.ty[false_val] == TY_FLOAT)) {
            rty = TY_FLOAT;
        } else {
            rty = TY_STRING;
        }

        auto coerce_phi_arm = [&](int &val, int exit_blk) {
            h.cur_block = exit_blk;
            if (rty == TY_FLOAT && h.ty[val] == TY_INT) {
                val = h.emit(HIR_ITOF, TY_FLOAT, val);
            } else if (rty == TY_STRING) {
                if (h.ty[val] == TY_FLOAT) {
                    val = h.emit(HIR_FTOA, TY_STRING, val);
                } else if (h.ty[val] == TY_INT) {
                    val = h.emit(HIR_ITOA, TY_STRING, val);
                }
            }
            h.emit(HIR_BR, TY_VOID, -1, -1, merge_block);
            h.add_edge(exit_blk, merge_block);
        };
        coerce_phi_arm(true_val, true_exit);
        coerce_phi_arm(false_val, false_exit);

        // Merge block with PHI.
        h.cur_block = merge_block;
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

        // Snapshot for rollback: a mid-chain bail to general_lowering
        // must not leave already-emitted BRC/BR placeholder (-1) targets
        // behind — codegen has no valid block to resolve them to (#858).
        // Also snapshot n_cargs: child lowering may advance the carg pool
        // via CALL/STRCAT; re-lowering after rollback would double-count.
        int save_insns  = h.n_insns;
        int save_blocks = h.n_blocks;
        int save_cur    = h.cur_block;
        int save_pargs  = h.n_pargs;
        int save_cargs  = h.n_cargs;
        size_t save_srefs = h.sref_addrs.size();

        for (int ai = 0; ai < nfargs; ai++) {
            int cond = hir_lower_node(h, rc, node->children[ai].get());

            // hir_lower_node returns -1 to REFUSE the compile (unknown AST node
            // type, #1242) and sets h.overflowed.  Indexing with it reads before the
            // per-instruction arrays: kind[] is the FIRST member of hir_program, so
            // h.kind[-1] is four bytes before the struct.  AddressSanitizer reports it
            // as a stack-buffer-overflow; without a sanitizer it is a silent garbage
            // read that decides a branch.  Propagate the refusal -- no rollback is
            // needed, since overflowed abandons the compile before codegen and the AST
            // evaluator takes the expression.
            //
            if (cond < 0) {
                return -1;
            }

            // Ensure condition is integer.
            if (h.ty[cond] != TY_INT) {
                if (h.kind[cond] == HIR_SCONST) {
                    int64_t v = static_cast<int64_t>(
                        mux_atoi64(u8(h.sval[cond])));
                    cond = h.emit_iconst(v);
                } else if (h.ty[cond] == TY_FLOAT) {
                    cond = h.emit(HIR_FTOI, TY_INT, cond);
                } else if (h.known_int[cond]) {
                    cond = h.emit(HIR_ATOI, TY_INT, cond);
                } else {
                    // A non-integer arg after branches were already
                    // emitted would orphan their placeholder targets;
                    // roll the partial lowering back and let the ECALL
                    // path lower the whole node instead (#858).
                    h.n_insns   = save_insns;
                    h.n_blocks  = save_blocks;
                    h.cur_block = save_cur;
                    h.n_pargs   = save_pargs;
                    h.n_cargs   = save_cargs;
                    h.sval.resize(save_insns);
                    h.call_name.resize(save_insns);
                    h.sref_addrs.resize(save_srefs);
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

    // switch()/case() are min 2 in the table: switch(target, default) has
    // zero pattern/result pairs and only the default arm.  The >= 3 guard
    // excluded that form, and the FN_NOEVAL check in general_lowering then
    // declined the whole surrounding expression (#2165 — same class as
    // #2162's if() arity gap).  npairs = 0 / has_default = true lowers it
    // with the machinery below unchanged.
    if ((fname == "SWITCH" || fname == "CASE"
         || fname == "SWITCHALL" || fname == "CASEALL")
        && node->children.size() >= 2) {
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

        // Zero pairs — switch(target, default), reachable because the
        // table minimum is 2 (#2165).  The target was already lowered
        // above (evaluation order and side effects match the
        // interpreter); the default is the value.  Return it as
        // straight-line code: routing the one-armed case through the
        // block machinery below builds a single-input PHI, which came
        // back EMPTY on the compiled route while benchmark() reported
        // jit_handled=10/10 — the value was wrong and the liveness
        // counter had no way to say so.
        if (0 == npairs) {
            int dv = hir_lower_trimmed(h, rc,
                node->children[nfargs - 1].get());
            h.needs_jit = true;
            return dv;
        }

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

        // Pick PHI type (#1143): all INT → INT; pure numeric with at
        // least one FLOAT → FLOAT; otherwise STRING with coercions.
        hir_type rty = TY_INT;
        bool any_float = false;
        bool any_non_num = false;
        for (int rv : result_vals) {
            if (h.ty[rv] == TY_FLOAT) {
                any_float = true;
            } else if (h.ty[rv] != TY_INT) {
                any_non_num = true;
            }
        }
        if (any_non_num) {
            rty = TY_STRING;
        } else if (any_float) {
            rty = TY_FLOAT;
        }

        // Patch BRs to merge; coerce float/int arms in-place when the
        // PHI is STRING or FLOAT so PHI operands match rty.
        for (int ri = 0; ri < static_cast<int>(result_exits.size()); ri++) {
            int blk = result_exits[ri];
            int &rv = result_vals[ri];
            int br_idx = -1;
            for (int ii = h.n_insns - 1; ii >= 0; ii--) {
                if (h.blk[ii] == blk && h.kind[ii] == HIR_BR
                    && h.val[ii] == -1) {
                    br_idx = ii;
                    break;
                }
            }
            bool need_coerce =
                (rty == TY_STRING
                 && (h.ty[rv] == TY_FLOAT || h.ty[rv] == TY_INT))
                || (rty == TY_FLOAT && h.ty[rv] == TY_INT);
            if (need_coerce && br_idx >= 0) {
                // Drop placeholder BR; re-emit coerce + BR at block end.
                h.kind[br_idx] = HIR_NOP;
                h.src1[br_idx] = h.src2[br_idx] = -1;
                h.cur_block = blk;
                if (rty == TY_FLOAT) {
                    rv = h.emit(HIR_ITOF, TY_FLOAT, rv);
                } else if (h.ty[rv] == TY_FLOAT) {
                    rv = h.emit(HIR_FTOA, TY_STRING, rv);
                } else {
                    rv = h.emit(HIR_ITOA, TY_STRING, rv);
                }
                h.emit(HIR_BR, TY_VOID, -1, -1, merge_blk);
            } else if (br_idx >= 0) {
                h.val[br_idx] = merge_blk;
            }
            h.add_edge(blk, merge_blk);
        }

        h.cur_block = merge_blk;
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

        // Tier 2's SPLIT_TOKEN matches only the FIRST BYTE of a delimiter
        // (get_delim, mux/rv64/src/softlib.c), while the interpreter matches
        // the whole string via DELIM_STRING/strstr.  So a multi-byte
        // delimiter splits differently on the two routes: iter(a::b,##,::,+)
        // gives the interpreter's "a+b" but the inline loop's "a++b", three
        // elements instead of two (#2127).
        //
        // MAP/FILTER/FOLD gate this at RUNTIME, because they have an ECALL
        // fallback arm to send the bad case to.  ITER cannot: its body is
        // NOEVAL and cannot be lowered as a value, which is why it has no
        // fallback block.  So it declines the whole lowering here instead,
        // and the AST evaluator -- which handles NOEVAL correctly -- answers.
        //
        // Conservative on purpose: an explicit delimiter must be a literal we
        // can measure.  A computed one (e.g. [strcat(:,:)]) is unknowable at
        // compile time, and declining it costs the lowering while guessing
        // costs a wrong answer.  The overwhelmingly common cases -- no
        // delimiter at all, or a one-character literal -- keep the lowering.
        //
        if (nfargs >= 3) {
            const ASTNode *dn = node->children[2].get();
            if (nullptr == dn
                || dn->type != AST_LITERAL
                || dn->text.size() > 1) {
                return -1;
            }
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

        // Initialize loop state.
        int inum_init = h.emit_iconst(0);
        h.emit(HIR_STORE_Q, TY_VOID, inum_init, -1, QREG_ITER_INUM);

        // The cursor is a byte offset into the list, carried in a q-register
        // exactly like the counter above (#2052).  It starts at 0.
        int cursor_init = h.emit_iconst(0);
        h.emit(HIR_STORE_Q, TY_VOID, cursor_init, -1, QREG_ITER_CURSOR);

        // The accumulator (#2072).  It used to be an SSA string carried in
        // QREG_ITER_ACC: rebuilt by STRCAT every iteration -- O(len) per
        // element, O(len^2) per loop -- and then copied AGAIN by the string
        // PHI's per-iteration strcpy.  Both costs go away together: the
        // accumulated text lives in ONE pinned buffer for the whole loop,
        // and what rides in QREG_ITER_ACC is its LENGTH, as an int.
        //
        // The pinned slot comes straight from rc.alloc_output(), which the
        // liveness allocator never recycles (it reuses only slots it freed
        // itself), so no temporary can alias it.  References to it are
        // emit_sref, not emit_sconst: the buffer's compile-time contents
        // ("") are a lie at runtime, and emit_sref is the existing #1309
        // machinery that keeps such a reference out of ATOI constant
        // folding.  APPEND is registered but unreachable from softcode,
        // like SPLIT_TOKEN.
        uint64_t t2append = tier2_lookup("APPEND");
        uint64_t acc_addr = 0;
        uint64_t empty_addr = rc.pool_str("");
        int len_init = -1;
        if (t2append) {
            acc_addr = rc.alloc_output();
            len_init = h.emit_iconst(0);
            h.emit(HIR_STORE_Q, TY_VOID, len_init, -1, QREG_ITER_ACC);

            // Reset: an append of "" with first=1 writes acc[0]='\0' and
            // returns "0".  Without it a zero-iteration loop would read
            // whatever the previous program left in the frame slot.
            uint64_t zero_addr = rc.pool_str("0");
            int zero_str = h.emit_sconst(zero_addr, "0");
            int empty_str = h.emit_sconst(empty_addr, "");
            int acc_ref0 = h.emit_sref(acc_addr);
            int rargs[5] = { acc_ref0, zero_str, zero_str,
                             empty_str, empty_str };
            int reset = h.emit_call(TY_STRING, 0, rargs, 5);
            h.tier2_addr[reset] = t2append;
            h.tier2_calls++;
        } else {
            // Blob predates APPEND: keep the SSA-string accumulator.
            int acc_init = h.emit_sconst(empty_addr, "");
            h.emit(HIR_STORE_Q, TY_VOID, acc_init, -1, QREG_ITER_ACC);
        }

        // entry → header.
        int entry_block = h.cur_block;
        int header_block = h.new_block();
        h.emit(HIR_BR, TY_VOID, -1, -1, header_block);
        h.add_edge(entry_block, header_block);

        // Header: load inum, check < nwords, branch.
        h.cur_block = header_block;
        int inum = h.emit(HIR_LOAD_Q, TY_INT, -1, -1, QREG_ITER_INUM);
        // With APPEND, QREG_ITER_ACC holds the accumulator LENGTH (int);
        // without it, the legacy SSA accumulator string.
        int acc = h.emit(HIR_LOAD_Q, t2append ? TY_INT : TY_STRING,
                         -1, -1, QREG_ITER_ACC);
        // The cursor is loop-carried and must be loaded HERE, in the header,
        // for the same reason inum is: that is where the loop's PHI lives.
        // Loading it in the body instead compiled and produced correct output
        // for a lone iter(), then dropped all but the last element once two
        // iter() loops shared one compiled program (parser_fn TC020).
        int cursor = h.emit(HIR_LOAD_Q, TY_INT, -1, -1, QREG_ITER_CURSOR);
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

        // Element extraction.
        //
        // This used to be EXTRACT(list, inum+1, 1, delim), and co_extract
        // reaches element i by scanning from the head of the list counting
        // delimiters -- O(len) per element, so the loop was O(len^2).  The
        // interpreter's fun_iter has never done that: it keeps a cursor and
        // calls split_token.  Measured, the compiled route was 65x slower
        // than a build with no JIT at all at 2000 elements (#2068).
        //
        // A SPLIT_TOKEN fast path was written here but never wired: nothing
        // registered it in s_tier2_map, so tier2_lookup always returned 0 and
        // the branch never once executed.  It is replaced rather than enabled
        // -- it passed the cursor as an emit_sconst whose storage the callee
        // was expected to mutate, and called emit_call with a hardcoded
        // func_idx of 0.  Neither had ever run.
        //
        // The cursor now lives in QREG_ITER_CURSOR as a plain integer, which
        // is the same machinery the loop counter already uses.  Two calls per
        // element: one for the element, one for the next offset.  Both are
        // O(element length), so the loop is linear; see rv64_split_token in
        // mux/rv64/src/softlib.c for why that beats one mutating callee.
        // THE FALLBACK LADDER, and why ITER's differs from MAP/FILTER/FOLD's
        // (#2155).  This is a deliberate asymmetry, not an oversight:
        //
        //   ITER (here):   SPLIT_STEP (int ABI, #2132)
        //                  -> SPLIT_TOKEN (string ABI — LIVE, blob-compat)
        //                  -> EXTRACT (the O(n^2) shape)
        //   MAP/FILTER/    integer trio in the ENTRY GATE
        //   FOLD:          -> arm does not fire; generic ECALL fallback
        //
        // ITER is the core loop every list expression reaches, so it keeps
        // a graceful middle rung for a blob predating the integer trio.
        // The M/F/F arms are inline optimizations over an ECALL path that
        // already exists and stays exercised, so they decline wholesale
        // rather than carry a second, unexercised emission path per arm —
        // which is exactly the kind of code that rots.
        //
        // The observable consequence of "new engine, old blob" (a state the
        // two-copy checked-in softlib.rv64 makes reachable by accident):
        // ITER looks normal while MAP/FILTER/FOLD run correct but ~3-4x
        // slower with ecalls and tier2 both collapsed to 1 (one generic
        // ECALL, no guest loop) in rvbench's counters.  That signature —
        // not elevated ecalls — means a stale blob, not a broken lowering.
        //
        // (If you arrived here from `grep SPLIT_TOKEN`: the else-if below
        // is the only live string-ABI call site in this file; the other
        // hits are commentary.)
        uint64_t t2step  = tier2_lookup("SPLIT_STEP");
        uint64_t t2split = tier2_lookup("SPLIT_TOKEN");
        int elem;
        int next_cursor = -1;
        if (t2step) {
            // Integer-ABI walk (#2132): ONE call per element.  The cursor
            // rides in a register, the element lands in the call's output
            // slot, and the next cursor comes back in a0 as an integer.
            // The string route below crossed the boundary twice per
            // element and converted the cursor to decimal and back at
            // every crossing — profiled as the largest single component
            // of per-element cost, and, because offsets grow with the
            // list, the whole of the residual N-climb.
            //
            // next_cursor stays an SSA value stored in the LATCH, exactly
            // like the string route's: a nested iter in the body clobbers
            // the shared cursor q-register, and the latch store is what
            // makes that safe.
            int sargs[3] = { list_val, cursor, delim_val };
            int step = h.emit_call_t2i(t2step, 1, sargs, 3);
            h.tier2_calls++;
            elem = h.emit(HIR_T2I_STR, TY_STRING, step);
            next_cursor = step;
        } else if (t2split) {
            int cursor_str = h.emit(HIR_ITOA, TY_STRING, cursor);

            uint64_t m_elem_addr = rc.pool_str("0");
            int mode_elem = h.emit_sconst(m_elem_addr, "0");
            int stargs[4] = { list_val, cursor_str, delim_val, mode_elem };
            elem = h.emit_call(TY_STRING, 0, stargs, 4);
            h.tier2_addr[elem] = t2split;
            h.tier2_calls++;

            uint64_t m_next_addr = rc.pool_str("1");
            int mode_next = h.emit_sconst(m_next_addr, "1");
            int nxargs[4] = { list_val, cursor_str, delim_val, mode_next };
            int next_str = h.emit_call(TY_STRING, 0, nxargs, 4);
            h.tier2_addr[next_str] = t2split;
            h.tier2_calls++;

            // Computed here, stored in the LATCH alongside inum_next -- the
            // loop-carried registers are all updated in one place.
            next_cursor = h.emit(HIR_ATOI, TY_INT, next_str);
        } else {
            // Kept as a fallback for a blob that predates SPLIT_TOKEN, and
            // it is the O(n^2) shape -- if this branch is running, that is
            // the reason iter() is slow.
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

        // Accumulate (#2072).
        uint64_t t2appi = tier2_lookup("APPEND_I");
        if (t2appi) {
            // Integer-ABI append (#2132): length and iteration number in
            // registers, new length back in a0.  Same in-place semantics
            // as the string route below — rv64_append_i mirrors
            // rv64_append minus the decimal plumbing.
            int acc_ref = h.emit_sref(acc_addr);
            int aargs[5] = { acc_ref, acc, inum, osep_val, body_val };
            int newlen = h.emit_call_t2i(t2appi, 0, aargs, 5);
            h.tier2_calls++;
            h.emit(HIR_STORE_Q, TY_VOID, newlen, -1, QREG_ITER_ACC);

            // Latch stores, mirroring the string arm's.
            int inum_next = h.emit(HIR_ADD, TY_INT, inum, one_int);
            h.native_ops++;
            h.emit(HIR_STORE_Q, TY_VOID, inum_next, -1, QREG_ITER_INUM);
            if (next_cursor >= 0) {
                h.emit(HIR_STORE_Q, TY_VOID, next_cursor, -1,
                       QREG_ITER_CURSOR);
            }
            h.emit(HIR_BR, TY_VOID, -1, -1, header_block);
            h.add_edge(h.cur_block, header_block);
        } else if (t2append) {
            // One in-place append: writes body_val (osep-prefixed unless
            // this is iteration 0 -- the callee tests the iteration number,
            // which removes the old first/cat block diamond entirely) at
            // acc+len, and returns the new length.  O(append), not O(acc).
            //
            // `acc` here is the LENGTH loaded in the header.  A fresh
            // emit_sref per use: sref values are runtime references and
            // must not be shared where CSE could not prove them equal
            // anyway.
            int len_str = h.emit(HIR_ITOA, TY_STRING, acc);
            int inum_str = h.emit(HIR_ITOA, TY_STRING, inum);
            int acc_ref = h.emit_sref(acc_addr);
            int aargs[5] = { acc_ref, len_str, inum_str, osep_val, body_val };
            int newlen_str = h.emit_call(TY_STRING, 0, aargs, 5);
            h.tier2_addr[newlen_str] = t2append;
            h.tier2_calls++;
            int newlen = h.emit(HIR_ATOI, TY_INT, newlen_str);
            h.emit(HIR_STORE_Q, TY_VOID, newlen, -1, QREG_ITER_ACC);

            // Latch stores, at the end of the (now diamond-free) body.
            int inum_next = h.emit(HIR_ADD, TY_INT, inum, one_int);
            h.native_ops++;
            h.emit(HIR_STORE_Q, TY_VOID, inum_next, -1, QREG_ITER_INUM);
            if (next_cursor >= 0) {
                h.emit(HIR_STORE_Q, TY_VOID, next_cursor, -1,
                       QREG_ITER_CURSOR);
            }
            h.emit(HIR_BR, TY_VOID, -1, -1, header_block);
            h.add_edge(h.cur_block, header_block);
        } else {
            // Legacy accumulate: first iteration → body_val,
            //                    otherwise → strcat(acc, osep, body_val).
            // O(n^2) both in the STRCAT rebuild and in the string PHI's
            // per-iteration copy; kept only for a blob without APPEND.
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
            if (next_cursor >= 0) {
                h.emit(HIR_STORE_Q, TY_VOID, next_cursor, -1,
                       QREG_ITER_CURSOR);
            }
            h.emit(HIR_BR, TY_VOID, -1, -1, header_block);
            h.add_edge(latch_block, header_block);
        }

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
        // With APPEND the result is the pinned buffer itself, read through
        // a runtime reference (emit_sref -- never plain emit_sconst, whose
        // compile-time "" would fold to 0 under ATOI if the caller wraps
        // iter() in arithmetic, #1309).  Without APPEND, the legacy path's
        // SSA accumulator comes back out of the q-register.
        int result = t2append
            ? h.emit_sref(acc_addr)
            : h.emit(HIR_LOAD_Q, TY_STRING, -1, -1, QREG_ITER_ACC);

        h.needs_jit = true;
        return result;
    }

    // ---------------------------------------------------------------
    // map(#dbref/attr, list[, delim[, osep[, extras...]]]) — the ITER
    // loop machinery composed with the Tier 3 u()-inline (#2080).
    //
    // fun_map evaluates the attribute body through mux_exec once per
    // element; inside a compiled program that was an ECALL back into
    // fun_map with the body interpreted per element — measured at
    // ~1.1us/element against ~610ns interpreted, the worst of the
    // three routes.  This lowers the loop to the #2072 shape (cursor
    // walk, pinned-buffer accumulator) and the body to inlined HIR.
    //
    // Gates, compile time: literal #dbref/attr (same constraint as the
    // u()-inline), the attr resolves and parses, not AF_TRACE (fun_map
    // propagates AttrTrace, which only the interpreter can honor), at
    // most 9 extras (CARGS slots 1..9), and the blob provides the
    // integer trio SPLIT_STEP/APPEND_I/BYTELEN_I (#2152).  Missing any
    // of those (older softlib.rv64) means the arm never opens and the
    // call is one generic ECALL — correct, but unlike ITER's three-rung
    // ladder (int → string → EXTRACT) there is no moderate string-route
    // middle: that asymmetry is deliberate (no dual emission paths to
    // rot).  Anything else falls through to the ECALL exactly as today.
    //
    // Gates, runtime (one diamond around the whole loop):
    //   - _CHECK_U_PERM(thing, attr) == 0
    //   - executor == thing: fun_map runs the body with THING as
    //     executor, but an inlined body runs as the program's executor
    //     -- and a cached program can be re-run by anyone, so this
    //     cannot be settled at compile time.  %! is compared against
    //     the compile-time "#thing" by native STRCMP.  (The u()-inline
    //     lives with this hole; MAP does not add another copy of it.)
    //   - every extra fits its 256-byte CARGS slot (BYTELEN < 256):
    //     _WRITE_CARG rejects oversized values, leaving the slot
    //     stale, so an unchecked long extra would silently feed the
    //     body a previous value.
    //
    // Per element, a second diamond for the same slot limit: elements
    // that fit take the inlined body; oversized ones ECALL u() with the
    // element as %0 -- semantically identical since the gate already
    // pinned executor == thing (map_fn TC012 is the case that catches
    // a stale or truncated %0 here).
    // ---------------------------------------------------------------

    if (fname == "MAP"
        && node->children.size() >= 2
        && s_compile_deps != nullptr
        && s_inline_depth < MAX_INLINE_DEPTH)
    {
        // arg0 must be a compile-time literal #dbref/attr.
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

        int nExtra = static_cast<int>(node->children.size()) - 4;
        if (nExtra < 0) nExtra = 0;

        // Integer-ABI trio (#2152, mechanics from #2132).  Required in the
        // entry gate: with an older blob the arm simply does not fire and
        // MAP takes its generic ECALL fallback — correct but ~3-4x slower
        // with ecalls/tier2 both 1; DELIBERATELY without a string-ABI
        // middle rung; see the fallback-ladder comment at ITER's arm
        // (#2155) for the asymmetry and the stale-blob signature.
        uint64_t t2split_m  = tier2_lookup("SPLIT_STEP");
        uint64_t t2append_m = tier2_lookup("APPEND_I");
        uint64_t t2blen_m   = tier2_lookup("BYTELEN_I");

        dbref thing = NOTHING;
        ATTR *pattr = nullptr;
        if (arg0_const && !arg0_str.empty() && arg0_str[0] == '#'
            && nExtra <= 9 && t2split_m && t2append_m && t2blen_m
            && parse_attrib(GOD,
                   reinterpret_cast<const UTF8 *>(arg0_str.c_str()),
                   &thing, &pattr)
            && pattr && Good_obj(thing))
        {
            dbref aowner;
            int aflags;
            size_t nBodyLen = 0;
            UTF8 *body = atr_pget_LEN(thing, pattr->number,
                                       &aowner, &aflags, &nBodyLen);
            std::unique_ptr<ASTNode> body_ast;
            if (body && nBodyLen > 0 && !(aflags & AF_TRACE)) {
                body_ast = ast_parse_string(body, nBodyLen);
            }
            if (body) free_lbuf(body);

            if (body_ast) {
                // Watermarks: the inlined body's call depth/count are
                // invisible to the outer AST watermark (#1056).
                h.inline_extra_depth +=
                    hir_ast_max_funccall_depth(body_ast.get());
                h.inline_extra_calls +=
                    hir_ast_funccall_count(body_ast.get());

                // Cache staleness: recompile when the attr changes.
                uint32_t mc = attr_mod_count_get(thing, pattr->number);
                s_compile_deps->push_back({
                    static_cast<int32_t>(thing),
                    static_cast<int32_t>(pattr->number),
                    mc
                });

                // Lower the original arguments once: the fallback ECALL
                // uses all of them; the inline path reuses the list,
                // delim, osep and extras.  The list is AST-trimmed to
                // match the ITER lowering's existing choice.
                std::vector<int> m_args;
                for (size_t ci = 0; ci < node->children.size(); ci++) {
                    int v = (ci == 1)
                        ? hir_lower_trimmed(h, rc,
                              node->children[ci].get())
                        : hir_lower_argument(h, rc,
                              node->children[ci].get());
                    if (v < 0) return -1;
                    if (h.ty[v] == TY_INT) {
                        v = h.emit(HIR_ITOA, TY_STRING, v);
                    } else if (h.ty[v] == TY_FLOAT) {
                        v = h.emit(HIR_FTOA, TY_STRING, v);
                    }
                    m_args.push_back(v);
                }
                int list_val = m_args[1];
                int delim_val;
                if (node->children.size() >= 3) {
                    delim_val = m_args[2];
                } else {
                    uint64_t da = rc.pool_str(" ");
                    delim_val = h.emit_sconst(da, " ");
                }
                // fun_map's osep DEFAULTS TO THE DELIMITER (DELIM_INIT),
                // unlike iter()'s space.
                int osep_val = (node->children.size() >= 4)
                    ? m_args[3] : delim_val;

                // ---- runtime gate ----
                int perm_idx = engine_api_lookup("_CHECK_U_PERM");
                std::string thing_s = std::to_string(thing);
                std::string attr_s = std::to_string(pattr->number);
                uint64_t ta = rc.pool_str(thing_s);
                uint64_t aa = rc.pool_str(attr_s);
                int thing_c = h.emit_sconst(ta, thing_s);
                int attr_c = h.emit_sconst(aa, attr_s);
                int perm_args[2] = { thing_c, attr_c };
                int perm_res = h.emit_call(TY_STRING, perm_idx,
                                           perm_args, 2);
                h.ecalls++;
                h.known_int[perm_res] = true;
                int perm_int = h.emit(HIR_ATOI, TY_INT, perm_res);
                int zero_i = h.emit_iconst(0);
                int perm_ok = h.emit(HIR_EQ, TY_INT, perm_int, zero_i);
                h.native_ops++;

                // executor == thing: %! vs "#<thing>".
                uint64_t ex_addr = rv_compiler::SUBST_BASE
                    + rv_compiler::SUBST_EXECUTOR * rv_compiler::SUBST_SLOT;
                int ex_ref = h.emit_sref(ex_addr);
                std::string hash_thing = "#" + thing_s;
                uint64_t ha = rc.pool_str(hash_thing);
                int hash_c = h.emit_sconst(ha, hash_thing);
                int ex_cmp = h.emit(HIR_STRCMP, TY_INT, ex_ref, hash_c);
                int ex_ok = h.emit(HIR_EQ, TY_INT, ex_cmp, zero_i);
                h.native_ops += 2;
                int gate = h.emit(HIR_BAND, TY_INT, perm_ok, ex_ok);
                h.native_ops++;

                // Tier 2's SPLIT_TOKEN matches only the FIRST BYTE of a
                // delimiter (get_delim, softlib.c) while the interpreter
                // matches the whole string, so a multi-byte delimiter splits
                // differently on the two routes -- map(o/F,a::b,::) yields
                // three elements inline against the interpreter's two
                // (#2127).  The delimiter is usually a runtime value, so this
                // has to be a runtime test; BYTELEN <= 1 sends the rest down
                // the ECALL fallback, which is interpreter-correct.
                //
                // Only the SPLIT needs gating.  The join is already correct:
                // the divergent "a::::b" is three elements joined by the full
                // "::", so osep handling is not implicated.
                {
                    int dargs[1] = { delim_val };
                    int dlen_i = h.emit_call_t2i(t2blen_m, 0, dargs, 1);
                    h.tier2_calls++;
                    int c2 = h.emit_iconst(2);
                    int delim_ok = h.emit(HIR_LT, TY_INT, dlen_i, c2);
                    gate = h.emit(HIR_BAND, TY_INT, gate, delim_ok);
                    h.native_ops += 2;
                }

                // Every extra must fit its CARGS slot.
                int c256 = h.emit_iconst(256);
                for (int ei = 0; ei < nExtra; ei++) {
                    int bargs[1] = { m_args[4 + ei] };
                    int bl_i = h.emit_call_t2i(t2blen_m, 0, bargs, 1);
                    h.tier2_calls++;
                    int fits = h.emit(HIR_LT, TY_INT, bl_i, c256);
                    gate = h.emit(HIR_BAND, TY_INT, gate, fits);
                    h.native_ops += 2;
                }

                int entry_blk = h.cur_block;
                int fallback_blk = h.new_block();
                int inline_blk = h.new_block();
                // emit(BRC, cond, FALSE_target, TRUE_target) -- the
                // argument order that was inverted in the u()-inline
                // for its whole life.  gate true → inline.
                h.emit(HIR_BRC, TY_VOID, gate, fallback_blk, inline_blk);
                h.add_edge(entry_blk, fallback_blk);
                h.add_edge(entry_blk, inline_blk);

                // ---- fallback: ECALL fun_map with the original args --
                h.cur_block = fallback_blk;
                int fidx_map = engine_api_lookup("MAP");
                int fb_res = h.emit_call(TY_STRING, fidx_map,
                    m_args.data(), static_cast<int>(m_args.size()));
                h.ecalls++;
                int fb_exit = h.cur_block;
                int fb_br = h.emit(HIR_BR, TY_VOID, -1, -1, -1);

                // ---- inline path ----
                h.cur_block = inline_blk;

                // CARGS save; extras into slots 1..; %+ count.
                int save_idx = engine_api_lookup("_SAVE_CARGS");
                int cargs_handle = h.emit_call(TY_STRING, save_idx,
                                               nullptr, 0);
                h.ecalls++;
                int write_idx = engine_api_lookup("_WRITE_CARG");
                for (int ei = 0; ei < nExtra; ei++) {
                    std::string is = std::to_string(ei + 1);
                    uint64_t ia = rc.pool_str(is);
                    int idx_c = h.emit_sconst(ia, is);
                    int wargs[2] = { idx_c, m_args[4 + ei] };
                    h.emit_call(TY_STRING, write_idx, wargs, 2);
                    h.ecalls++;
                }
                int ncargs_idx = engine_api_lookup("_SET_NCARGS");
                std::string nc_s = std::to_string(nExtra + 1);
                uint64_t na = rc.pool_str(nc_s);
                int nc_c = h.emit_sconst(na, nc_s);
                int ncarg_arg[1] = { nc_c };
                h.emit_call(TY_STRING, ncargs_idx, ncarg_arg, 1);
                h.ecalls++;

                // WORDS(list, delim) — loop bound.
                int words_idx = engine_api_lookup("WORDS");
                int wargs2[2] = { list_val, delim_val };
                int nwords_str = h.emit_call(TY_STRING, words_idx,
                                             wargs2, 2);
                h.known_int[nwords_str] = true;
                uint64_t t2words = tier2_lookup("WORDS");
                if (t2words) {
                    h.tier2_addr[nwords_str] = t2words;
                    h.tier2_calls++;
                } else {
                    h.ecalls++;
                }
                int nwords_int = h.emit(HIR_ATOI, TY_INT, nwords_str);

                // Loop state: counter, cursor, pinned accumulator.
                int inum_init = h.emit_iconst(0);
                h.emit(HIR_STORE_Q, TY_VOID, inum_init, -1,
                       QREG_ITER_INUM);
                int cur_init = h.emit_iconst(0);
                h.emit(HIR_STORE_Q, TY_VOID, cur_init, -1,
                       QREG_ITER_CURSOR);
                uint64_t acc_addr = rc.alloc_output();
                int len_init = h.emit_iconst(0);
                h.emit(HIR_STORE_Q, TY_VOID, len_init, -1,
                       QREG_ITER_ACC);
                uint64_t e_addr = rc.pool_str("");
                int e_str = h.emit_sconst(e_addr, "");
                int acc_r0 = h.emit_sref(acc_addr);
                int rst_args[5] = { acc_r0, zero_i, zero_i, e_str, e_str };
                int rst = h.emit_call_t2i(t2append_m, 0, rst_args, 5);
                (void)rst;
                h.tier2_calls++;

                int pre_hdr = h.cur_block;
                int hdr_blk = h.new_block();
                h.emit(HIR_BR, TY_VOID, -1, -1, hdr_blk);
                h.add_edge(pre_hdr, hdr_blk);

                // Header: loop-carried loads live here (TC020's lesson).
                h.cur_block = hdr_blk;
                int inum = h.emit(HIR_LOAD_Q, TY_INT, -1, -1,
                                  QREG_ITER_INUM);
                int mlen = h.emit(HIR_LOAD_Q, TY_INT, -1, -1,
                                  QREG_ITER_ACC);
                int mcur = h.emit(HIR_LOAD_Q, TY_INT, -1, -1,
                                  QREG_ITER_CURSOR);
                int cond = h.emit(HIR_LT, TY_INT, inum, nwords_int);
                h.native_ops++;
                int body_blk = h.new_block();
                int exit_blk = h.new_block();
                h.emit(HIR_BRC, TY_VOID, cond, exit_blk, body_blk);
                h.add_edge(hdr_blk, body_blk);
                h.add_edge(hdr_blk, exit_blk);

                // Body: cursor walk.
                h.cur_block = body_blk;
                int one_i = h.emit_iconst(1);
                int inum1 = h.emit(HIR_ADD, TY_INT, inum, one_i);
                h.native_ops++;
                // Integer-ABI walk (#2152): one call, cursor in a register,
                // element in the out slot, next cursor back in a0.
                int st0[3] = { list_val, mcur, delim_val };
                int step = h.emit_call_t2i(t2split_m, 1, st0, 3);
                h.tier2_calls++;
                int elem = h.emit(HIR_T2I_STR, TY_STRING, step);
                int nxt_cur = step;

                // Element-size diamond: fits → inlined body,
                // oversized → ECALL u(#thing/attr, elem).
                int eb_args[1] = { elem };
                int eb_i = h.emit_call_t2i(t2blen_m, 0, eb_args, 1);
                h.tier2_calls++;
                int e_fits = h.emit(HIR_LT, TY_INT, eb_i, c256);
                h.native_ops++;
                int uarm_blk = h.new_block();
                int ibody_blk = h.new_block();
                h.emit(HIR_BRC, TY_VOID, e_fits, uarm_blk, ibody_blk);
                h.add_edge(h.cur_block, uarm_blk);
                h.add_edge(h.cur_block, ibody_blk);

                // Inlined body arm.
                h.cur_block = ibody_blk;
                uint64_t i0a = rc.pool_str("0");
                int i0c = h.emit_sconst(i0a, "0");
                int w0[2] = { i0c, elem };
                h.emit_call(TY_STRING, write_idx, w0, 2);
                h.ecalls++;
                bool saved_fcheck = s_fcheck_available;
                s_fcheck_available = true;
                s_inline_depth++;
                int ib_val = hir_lower_node(h, rc, body_ast.get());
                s_inline_depth--;
                s_fcheck_available = saved_fcheck;
                if (ib_val < 0) return -1;
                if (h.ty[ib_val] == TY_INT) {
                    ib_val = h.emit(HIR_ITOA, TY_STRING, ib_val);
                } else if (h.ty[ib_val] == TY_FLOAT) {
                    ib_val = h.emit(HIR_FTOA, TY_STRING, ib_val);
                }
                int ib_exit = h.cur_block;
                int ib_br = h.emit(HIR_BR, TY_VOID, -1, -1, -1);

                // Oversized-element arm: ECALL u().
                h.cur_block = uarm_blk;
                int fidx_u2 = engine_api_lookup("U");
                std::string uref = arg0_str;
                uint64_t ua = rc.pool_str(uref);
                int uref_c = h.emit_sconst(ua, uref);
                int uargs[2] = { uref_c, elem };
                int ua_val = h.emit_call(TY_STRING, fidx_u2, uargs, 2);
                h.ecalls++;
                int ua_exit = h.cur_block;

                // Element merge — allocated after both arms.
                int emerge_blk = h.new_block();
                h.val[ib_br] = emerge_blk;
                h.add_edge(ib_exit, emerge_blk);
                h.emit(HIR_BR, TY_VOID, -1, -1, emerge_blk);
                h.add_edge(ua_exit, emerge_blk);

                h.cur_block = emerge_blk;
                int eblocks[2] = { ib_exit, ua_exit };
                int evals[2] = { ib_val, ua_val };
                int body_val = h.emit_phi(TY_STRING, -1,
                                          eblocks, evals, 2);

                // Append and latch stores — integer ABI (#2152).
                int acc_r = h.emit_sref(acc_addr);
                int ap_args[5] = { acc_r, mlen, inum,
                                   osep_val, body_val };
                int nl = h.emit_call_t2i(t2append_m, 0, ap_args, 5);
                h.tier2_calls++;
                h.emit(HIR_STORE_Q, TY_VOID, nl, -1, QREG_ITER_ACC);
                h.emit(HIR_STORE_Q, TY_VOID, inum1, -1, QREG_ITER_INUM);
                h.emit(HIR_STORE_Q, TY_VOID, nxt_cur, -1,
                       QREG_ITER_CURSOR);
                h.emit(HIR_BR, TY_VOID, -1, -1, hdr_blk);
                h.add_edge(h.cur_block, hdr_blk);

                // Exit: restore CARGS, read the pinned buffer.
                h.cur_block = exit_blk;
                int restore_idx = engine_api_lookup("_RESTORE_CARGS");
                int rc_args[1] = { cargs_handle };
                h.emit_call(TY_STRING, restore_idx, rc_args, 1);
                h.ecalls++;
                int in_res = h.emit_sref(acc_addr);
                int in_exit = h.cur_block;

                // Merge with the fallback — allocated last, all edges
                // forward.
                int merge_blk = h.new_block();
                h.val[fb_br] = merge_blk;
                h.add_edge(fb_exit, merge_blk);
                h.cur_block = in_exit;
                h.emit(HIR_BR, TY_VOID, -1, -1, merge_blk);
                h.add_edge(in_exit, merge_blk);

                h.cur_block = merge_blk;
                int mblocks[2] = { fb_exit, in_exit };
                int mvals[2] = { fb_res, in_res };
                int phi = h.emit_phi(TY_STRING, -1, mblocks, mvals, 2);

                // The body may setq; like non-local u(), mutations leak
                // by design and which path ran is a runtime fact.
                qreg_clobber();
                h.needs_jit = true;
                return phi;
            }
        }
        // Fall through: ECALL fun_map, exactly as before.
    }

    // ---------------------------------------------------------------
    // filter(#dbref/attr, list[, delim[, osep[, extras...]]]) — MAP's
    // shape with a keep test (#2080).
    //
    // Two facts filter_fn.mux pins that shape this lowering:
    //   - filter() keeps an element iff the predicate result is EXACTLY
    //     the string "1" (result[0]=='1' && result[1]=='\0') -- a native
    //     STRCMP, deliberately NOT xlate truth.  filterbool() IS xlate
    //     truth, and there is no HIR shape that reproduces xlate() for
    //     runtime strings (floats, NaN -- see the ifelse note, #1157),
    //     so filterbool stays on the ECALL and is not handled here.
    //   - osep prints between KEPT elements, and a kept EMPTY element
    //     claims its slot, so "first" is keyed on QREG_FILTER_KEPT --
    //     its own counter -- never on the accumulator length.
    //
    // What is appended is the ELEMENT, which is a guest string of any
    // length; the 256-byte CARGS limit applies only to the predicate's
    // %0, so the per-element size diamond routes oversized elements
    // through ECALL u() for the TEST while the append stays native.
    //
    // Gates and structure otherwise identical to MAP above; the two
    // (plus FOLD, next) will be factored over a shared loop skeleton
    // once all three consumers exist and the shape is fully known.
    // ---------------------------------------------------------------

    if (fname == "FILTER"
        && node->children.size() >= 2
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

        int nExtra = static_cast<int>(node->children.size()) - 4;
        if (nExtra < 0) nExtra = 0;

        // Integer-ABI trio (#2152); entry-gated — old blob means this arm
        // declines to the generic ECALL fallback (correct, ~3-4x slower,
        // ecalls/tier2 both 1; see the fallback-ladder comment at ITER's
        // arm, #2155).
        uint64_t t2split_f  = tier2_lookup("SPLIT_STEP");
        uint64_t t2append_f = tier2_lookup("APPEND_I");
        uint64_t t2blen_f   = tier2_lookup("BYTELEN_I");

        dbref thing = NOTHING;
        ATTR *pattr = nullptr;
        if (arg0_const && !arg0_str.empty() && arg0_str[0] == '#'
            && nExtra <= 9 && t2split_f && t2append_f && t2blen_f
            && parse_attrib(GOD,
                   reinterpret_cast<const UTF8 *>(arg0_str.c_str()),
                   &thing, &pattr)
            && pattr && Good_obj(thing))
        {
            dbref aowner;
            int aflags;
            size_t nBodyLen = 0;
            UTF8 *body = atr_pget_LEN(thing, pattr->number,
                                       &aowner, &aflags, &nBodyLen);
            std::unique_ptr<ASTNode> body_ast;
            if (body && nBodyLen > 0 && !(aflags & AF_TRACE)) {
                body_ast = ast_parse_string(body, nBodyLen);
            }
            if (body) free_lbuf(body);

            if (body_ast) {
                h.inline_extra_depth +=
                    hir_ast_max_funccall_depth(body_ast.get());
                h.inline_extra_calls +=
                    hir_ast_funccall_count(body_ast.get());

                uint32_t mc = attr_mod_count_get(thing, pattr->number);
                s_compile_deps->push_back({
                    static_cast<int32_t>(thing),
                    static_cast<int32_t>(pattr->number),
                    mc
                });

                std::vector<int> m_args;
                for (size_t ci = 0; ci < node->children.size(); ci++) {
                    int v = (ci == 1)
                        ? hir_lower_trimmed(h, rc,
                              node->children[ci].get())
                        : hir_lower_argument(h, rc,
                              node->children[ci].get());
                    if (v < 0) return -1;
                    if (h.ty[v] == TY_INT) {
                        v = h.emit(HIR_ITOA, TY_STRING, v);
                    } else if (h.ty[v] == TY_FLOAT) {
                        v = h.emit(HIR_FTOA, TY_STRING, v);
                    }
                    m_args.push_back(v);
                }
                int list_val = m_args[1];
                int delim_val;
                if (node->children.size() >= 3) {
                    delim_val = m_args[2];
                } else {
                    uint64_t da = rc.pool_str(" ");
                    delim_val = h.emit_sconst(da, " ");
                }
                int osep_val = (node->children.size() >= 4)
                    ? m_args[3] : delim_val;

                // ---- runtime gate (same as MAP) ----
                int zero_i = h.emit_iconst(0);
                int perm_idx = engine_api_lookup("_CHECK_U_PERM");
                std::string thing_s = std::to_string(thing);
                std::string attr_s = std::to_string(pattr->number);
                uint64_t ta = rc.pool_str(thing_s);
                uint64_t aa = rc.pool_str(attr_s);
                int thing_c = h.emit_sconst(ta, thing_s);
                int attr_c = h.emit_sconst(aa, attr_s);
                int perm_args[2] = { thing_c, attr_c };
                int perm_res = h.emit_call(TY_STRING, perm_idx,
                                           perm_args, 2);
                h.ecalls++;
                h.known_int[perm_res] = true;
                int perm_int = h.emit(HIR_ATOI, TY_INT, perm_res);
                int perm_ok = h.emit(HIR_EQ, TY_INT, perm_int, zero_i);
                h.native_ops++;

                uint64_t ex_addr = rv_compiler::SUBST_BASE
                    + rv_compiler::SUBST_EXECUTOR * rv_compiler::SUBST_SLOT;
                int ex_ref = h.emit_sref(ex_addr);
                std::string hash_thing = "#" + thing_s;
                uint64_t ha = rc.pool_str(hash_thing);
                int hash_c = h.emit_sconst(ha, hash_thing);
                int ex_cmp = h.emit(HIR_STRCMP, TY_INT, ex_ref, hash_c);
                int ex_ok = h.emit(HIR_EQ, TY_INT, ex_cmp, zero_i);
                h.native_ops += 2;
                int gate = h.emit(HIR_BAND, TY_INT, perm_ok, ex_ok);
                h.native_ops++;

                // Tier 2's SPLIT_TOKEN matches only the FIRST BYTE of a
                // delimiter (get_delim, softlib.c) while the interpreter
                // matches the whole string, so a multi-byte delimiter splits
                // differently on the two routes -- map(o/F,a::b,::) yields
                // three elements inline against the interpreter's two
                // (#2127).  The delimiter is usually a runtime value, so this
                // has to be a runtime test; BYTELEN <= 1 sends the rest down
                // the ECALL fallback, which is interpreter-correct.
                //
                // Only the SPLIT needs gating.  The join is already correct:
                // the divergent "a::::b" is three elements joined by the full
                // "::", so osep handling is not implicated.
                {
                    int dargs[1] = { delim_val };
                    int dlen_i = h.emit_call_t2i(t2blen_f, 0, dargs, 1);
                    h.tier2_calls++;
                    int c2 = h.emit_iconst(2);
                    int delim_ok = h.emit(HIR_LT, TY_INT, dlen_i, c2);
                    gate = h.emit(HIR_BAND, TY_INT, gate, delim_ok);
                    h.native_ops += 2;
                }

                int c256 = h.emit_iconst(256);
                for (int ei = 0; ei < nExtra; ei++) {
                    int bargs[1] = { m_args[4 + ei] };
                    int bl_i = h.emit_call_t2i(t2blen_f, 0, bargs, 1);
                    h.tier2_calls++;
                    int fits = h.emit(HIR_LT, TY_INT, bl_i, c256);
                    gate = h.emit(HIR_BAND, TY_INT, gate, fits);
                    h.native_ops += 2;
                }

                int entry_blk = h.cur_block;
                int fallback_blk = h.new_block();
                int inline_blk = h.new_block();
                h.emit(HIR_BRC, TY_VOID, gate, fallback_blk, inline_blk);
                h.add_edge(entry_blk, fallback_blk);
                h.add_edge(entry_blk, inline_blk);

                // ---- fallback: ECALL fun_filter ----
                h.cur_block = fallback_blk;
                int fidx_filter = engine_api_lookup("FILTER");
                int fb_res = h.emit_call(TY_STRING, fidx_filter,
                    m_args.data(), static_cast<int>(m_args.size()));
                h.ecalls++;
                int fb_exit = h.cur_block;
                int fb_br = h.emit(HIR_BR, TY_VOID, -1, -1, -1);

                // ---- inline path ----
                h.cur_block = inline_blk;
                int save_idx = engine_api_lookup("_SAVE_CARGS");
                int cargs_handle = h.emit_call(TY_STRING, save_idx,
                                               nullptr, 0);
                h.ecalls++;
                int write_idx = engine_api_lookup("_WRITE_CARG");
                for (int ei = 0; ei < nExtra; ei++) {
                    std::string is = std::to_string(ei + 1);
                    uint64_t ia = rc.pool_str(is);
                    int idx_c = h.emit_sconst(ia, is);
                    int wargs[2] = { idx_c, m_args[4 + ei] };
                    h.emit_call(TY_STRING, write_idx, wargs, 2);
                    h.ecalls++;
                }
                int ncargs_idx = engine_api_lookup("_SET_NCARGS");
                std::string nc_s = std::to_string(nExtra + 1);
                uint64_t na = rc.pool_str(nc_s);
                int nc_c = h.emit_sconst(na, nc_s);
                int ncarg_arg[1] = { nc_c };
                h.emit_call(TY_STRING, ncargs_idx, ncarg_arg, 1);
                h.ecalls++;

                int words_idx = engine_api_lookup("WORDS");
                int wargs2[2] = { list_val, delim_val };
                int nwords_str = h.emit_call(TY_STRING, words_idx,
                                             wargs2, 2);
                h.known_int[nwords_str] = true;
                uint64_t t2words = tier2_lookup("WORDS");
                if (t2words) {
                    h.tier2_addr[nwords_str] = t2words;
                    h.tier2_calls++;
                } else {
                    h.ecalls++;
                }
                int nwords_int = h.emit(HIR_ATOI, TY_INT, nwords_str);

                int inum_init = h.emit_iconst(0);
                h.emit(HIR_STORE_Q, TY_VOID, inum_init, -1,
                       QREG_ITER_INUM);
                int cur_init = h.emit_iconst(0);
                h.emit(HIR_STORE_Q, TY_VOID, cur_init, -1,
                       QREG_ITER_CURSOR);
                uint64_t acc_addr = rc.alloc_output();
                int len_init = h.emit_iconst(0);
                h.emit(HIR_STORE_Q, TY_VOID, len_init, -1,
                       QREG_ITER_ACC);
                int kept_init = h.emit_iconst(0);
                h.emit(HIR_STORE_Q, TY_VOID, kept_init, -1,
                       QREG_FILTER_KEPT);
                uint64_t e_addr = rc.pool_str("");
                int e_str = h.emit_sconst(e_addr, "");
                int acc_r0 = h.emit_sref(acc_addr);
                int rst_args[5] = { acc_r0, zero_i, zero_i, e_str, e_str };
                int rst = h.emit_call_t2i(t2append_f, 0, rst_args, 5);
                (void)rst;
                h.tier2_calls++;

                int pre_hdr = h.cur_block;
                int hdr_blk = h.new_block();
                h.emit(HIR_BR, TY_VOID, -1, -1, hdr_blk);
                h.add_edge(pre_hdr, hdr_blk);

                // Header: every loop-carried q-reg loads HERE.
                h.cur_block = hdr_blk;
                int inum = h.emit(HIR_LOAD_Q, TY_INT, -1, -1,
                                  QREG_ITER_INUM);
                int flen = h.emit(HIR_LOAD_Q, TY_INT, -1, -1,
                                  QREG_ITER_ACC);
                int fcur = h.emit(HIR_LOAD_Q, TY_INT, -1, -1,
                                  QREG_ITER_CURSOR);
                int kept = h.emit(HIR_LOAD_Q, TY_INT, -1, -1,
                                  QREG_FILTER_KEPT);
                int cond = h.emit(HIR_LT, TY_INT, inum, nwords_int);
                h.native_ops++;
                int body_blk = h.new_block();
                int exit_blk = h.new_block();
                h.emit(HIR_BRC, TY_VOID, cond, exit_blk, body_blk);
                h.add_edge(hdr_blk, body_blk);
                h.add_edge(hdr_blk, exit_blk);

                // Body: cursor walk.
                h.cur_block = body_blk;
                int one_i = h.emit_iconst(1);
                int inum1 = h.emit(HIR_ADD, TY_INT, inum, one_i);
                h.native_ops++;
                // Integer-ABI walk (#2152): one call per element.
                int st0[3] = { list_val, fcur, delim_val };
                int step = h.emit_call_t2i(t2split_f, 1, st0, 3);
                h.tier2_calls++;
                int elem = h.emit(HIR_T2I_STR, TY_STRING, step);
                int nxt_cur = step;

                // Element-size diamond for the PREDICATE's %0 only.
                int eb_args[1] = { elem };
                int eb_i = h.emit_call_t2i(t2blen_f, 0, eb_args, 1);
                h.tier2_calls++;
                int e_fits = h.emit(HIR_LT, TY_INT, eb_i, c256);
                h.native_ops++;
                int uarm_blk = h.new_block();
                int ibody_blk = h.new_block();
                h.emit(HIR_BRC, TY_VOID, e_fits, uarm_blk, ibody_blk);
                h.add_edge(h.cur_block, uarm_blk);
                h.add_edge(h.cur_block, ibody_blk);

                h.cur_block = ibody_blk;
                uint64_t i0a = rc.pool_str("0");
                int i0c = h.emit_sconst(i0a, "0");
                int w0[2] = { i0c, elem };
                h.emit_call(TY_STRING, write_idx, w0, 2);
                h.ecalls++;
                bool saved_fcheck = s_fcheck_available;
                s_fcheck_available = true;
                s_inline_depth++;
                int ib_val = hir_lower_node(h, rc, body_ast.get());
                s_inline_depth--;
                s_fcheck_available = saved_fcheck;
                if (ib_val < 0) return -1;
                if (h.ty[ib_val] == TY_INT) {
                    ib_val = h.emit(HIR_ITOA, TY_STRING, ib_val);
                } else if (h.ty[ib_val] == TY_FLOAT) {
                    ib_val = h.emit(HIR_FTOA, TY_STRING, ib_val);
                }
                int ib_exit = h.cur_block;
                int ib_br = h.emit(HIR_BR, TY_VOID, -1, -1, -1);

                h.cur_block = uarm_blk;
                int fidx_u2 = engine_api_lookup("U");
                uint64_t ua = rc.pool_str(arg0_str);
                int uref_c = h.emit_sconst(ua, arg0_str);
                int uargs[2] = { uref_c, elem };
                int ua_val = h.emit_call(TY_STRING, fidx_u2, uargs, 2);
                h.ecalls++;
                int ua_exit = h.cur_block;

                int emerge_blk = h.new_block();
                h.val[ib_br] = emerge_blk;
                h.add_edge(ib_exit, emerge_blk);
                h.emit(HIR_BR, TY_VOID, -1, -1, emerge_blk);
                h.add_edge(ua_exit, emerge_blk);

                h.cur_block = emerge_blk;
                int eblocks[2] = { ib_exit, ua_exit };
                int evals[2] = { ib_val, ua_val };
                int pred_val = h.emit_phi(TY_STRING, -1,
                                          eblocks, evals, 2);

                // Keep test: predicate result EXACTLY "1".
                uint64_t one_s_addr = rc.pool_str("1");
                int one_s = h.emit_sconst(one_s_addr, "1");
                int keep_cmp = h.emit(HIR_STRCMP, TY_INT,
                                      pred_val, one_s);
                int keep = h.emit(HIR_EQ, TY_INT, keep_cmp, zero_i);
                h.native_ops += 2;

                int skip_blk = h.new_block();
                int keep_blk = h.new_block();
                h.emit(HIR_BRC, TY_VOID, keep, skip_blk, keep_blk);
                h.add_edge(h.cur_block, skip_blk);
                h.add_edge(h.cur_block, keep_blk);

                // Keep: append the ELEMENT; osep gating and the "first"
                // decision come from the kept-count, not the iteration
                // number and not the accumulator length -- filter_fn
                // TC007 (kept empty elements) is the case that fails
                // anything else.
                h.cur_block = keep_blk;
                int acc_r = h.emit_sref(acc_addr);
                int ap_args[5] = { acc_r, flen, kept,
                                   osep_val, elem };
                int nl = h.emit_call_t2i(t2append_f, 0, ap_args, 5);
                h.tier2_calls++;
                h.emit(HIR_STORE_Q, TY_VOID, nl, -1, QREG_ITER_ACC);
                int kept1 = h.emit(HIR_ADD, TY_INT, kept, one_i);
                h.native_ops++;
                h.emit(HIR_STORE_Q, TY_VOID, kept1, -1,
                       QREG_FILTER_KEPT);
                int latch_blk = h.new_block();
                h.emit(HIR_BR, TY_VOID, -1, -1, latch_blk);
                h.add_edge(keep_blk, latch_blk);

                // Skip: no stores -- the header's values reach the latch
                // unchanged and SSA merges them at the header PHIs.
                h.cur_block = skip_blk;
                h.emit(HIR_BR, TY_VOID, -1, -1, latch_blk);
                h.add_edge(skip_blk, latch_blk);

                h.cur_block = latch_blk;
                h.emit(HIR_STORE_Q, TY_VOID, inum1, -1, QREG_ITER_INUM);
                h.emit(HIR_STORE_Q, TY_VOID, nxt_cur, -1,
                       QREG_ITER_CURSOR);
                h.emit(HIR_BR, TY_VOID, -1, -1, hdr_blk);
                h.add_edge(latch_blk, hdr_blk);

                h.cur_block = exit_blk;
                int restore_idx = engine_api_lookup("_RESTORE_CARGS");
                int rc_args[1] = { cargs_handle };
                h.emit_call(TY_STRING, restore_idx, rc_args, 1);
                h.ecalls++;
                int in_res = h.emit_sref(acc_addr);
                int in_exit = h.cur_block;

                int merge_blk = h.new_block();
                h.val[fb_br] = merge_blk;
                h.add_edge(fb_exit, merge_blk);
                h.cur_block = in_exit;
                h.emit(HIR_BR, TY_VOID, -1, -1, merge_blk);
                h.add_edge(in_exit, merge_blk);

                h.cur_block = merge_blk;
                int mblocks[2] = { fb_exit, in_exit };
                int mvals[2] = { fb_res, in_res };
                int phi = h.emit_phi(TY_STRING, -1, mblocks, mvals, 2);

                qreg_clobber();
                h.needs_jit = true;
                return phi;
            }
        }
        // Fall through: ECALL fun_filter, exactly as before.
    }

    // ---------------------------------------------------------------
    // fold(#dbref/attr, list[, base[, delim]]) — the loop again, with a
    // reduction instead of an accumulation (#2080).
    //
    // Three things fold_fn.mux pins that this had to be built around:
    //
    //   - THE DELIMITER IS ARGUMENT 4.  iter/map/filter all take it at 3;
    //     fold cannot, because argument 3 is the base.  Reading it at 3
    //     would treat the delimiter as the base and split on spaces.
    //   - The accumulator is REPLACED each iteration, not appended, so
    //     the pinned buffer is written at offset 0 every time -- which is
    //     exactly rv64_append at len=0, first=1.  No new blob primitive.
    //   - Seeding differs: WITH a base the loop starts at element 0 with
    //     acc = base; WITHOUT one it starts at element 1 with acc =
    //     element 0.  Both are handled by seeding before the loop and
    //     entering with the right cursor, so the loop body itself is
    //     uniform.
    //
    // The accumulator is %0 and the element is %1.  %0 therefore carries
    // a value that GROWS for a string-building fold, and the guest CARGS
    // slots are 256 bytes -- so unlike map/filter, where an oversized
    // element is the exception, here the fallback arm becomes the steady
    // state partway through (fold_fn TC007).  The per-element size
    // diamond tests the ACCUMULATOR as well as the element for that
    // reason.
    //
    // No new q-register: INUM and CURSOR are reused, and the accumulator
    // needs no length carried across iterations because every write is at
    // offset 0.  HIR_NUM_QREGS stays at 14.
    // ---------------------------------------------------------------

    if (fname == "FOLD"
        && node->children.size() >= 2
        // The function table caps fold at 4 arguments and the checker
        // rejects more BEFORE fun_fold runs -- an inline path with no
        // ceiling would compute a value where every other route errors.
        && node->children.size() <= 4
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

        const bool has_base = (node->children.size() >= 3);

        // Integer-ABI trio (#2153); entry-gated — old blob means this arm
        // declines to the generic ECALL fallback (correct, ~3-4x slower,
        // ecalls/tier2 both 1; see the fallback-ladder comment at ITER's
        // arm, #2155).
        uint64_t t2split_d  = tier2_lookup("SPLIT_STEP");
        uint64_t t2append_d = tier2_lookup("APPEND_I");
        uint64_t t2blen_d   = tier2_lookup("BYTELEN_I");

        dbref thing = NOTHING;
        ATTR *pattr = nullptr;
        if (arg0_const && !arg0_str.empty() && arg0_str[0] == '#'
            && t2split_d && t2append_d && t2blen_d
            && parse_attrib(GOD,
                   reinterpret_cast<const UTF8 *>(arg0_str.c_str()),
                   &thing, &pattr)
            && pattr && Good_obj(thing))
        {
            dbref aowner;
            int aflags;
            size_t nBodyLen = 0;
            UTF8 *body = atr_pget_LEN(thing, pattr->number,
                                       &aowner, &aflags, &nBodyLen);
            std::unique_ptr<ASTNode> body_ast;
            if (body && nBodyLen > 0 && !(aflags & AF_TRACE)) {
                body_ast = ast_parse_string(body, nBodyLen);
            }
            if (body) free_lbuf(body);

            if (body_ast) {
                h.inline_extra_depth +=
                    hir_ast_max_funccall_depth(body_ast.get());
                h.inline_extra_calls +=
                    hir_ast_funccall_count(body_ast.get());

                uint32_t mc = attr_mod_count_get(thing, pattr->number);
                s_compile_deps->push_back({
                    static_cast<int32_t>(thing),
                    static_cast<int32_t>(pattr->number),
                    mc
                });

                std::vector<int> m_args;
                for (size_t ci = 0; ci < node->children.size(); ci++) {
                    int v = (ci == 1)
                        ? hir_lower_trimmed(h, rc,
                              node->children[ci].get())
                        : hir_lower_argument(h, rc,
                              node->children[ci].get());
                    if (v < 0) return -1;
                    if (h.ty[v] == TY_INT) {
                        v = h.emit(HIR_ITOA, TY_STRING, v);
                    } else if (h.ty[v] == TY_FLOAT) {
                        v = h.emit(HIR_FTOA, TY_STRING, v);
                    }
                    m_args.push_back(v);
                }
                int list_val = m_args[1];
                // ARGUMENT 4 -- see the note above.
                int delim_val;
                if (node->children.size() >= 4) {
                    delim_val = m_args[3];
                } else {
                    uint64_t da = rc.pool_str(" ");
                    delim_val = h.emit_sconst(da, " ");
                }

                // ---- runtime gate (identical to MAP/FILTER) ----
                int zero_i = h.emit_iconst(0);
                int perm_idx = engine_api_lookup("_CHECK_U_PERM");
                std::string thing_s = std::to_string(thing);
                std::string attr_s = std::to_string(pattr->number);
                uint64_t ta = rc.pool_str(thing_s);
                uint64_t aa = rc.pool_str(attr_s);
                int thing_c = h.emit_sconst(ta, thing_s);
                int attr_c = h.emit_sconst(aa, attr_s);
                int perm_args[2] = { thing_c, attr_c };
                int perm_res = h.emit_call(TY_STRING, perm_idx,
                                           perm_args, 2);
                h.ecalls++;
                h.known_int[perm_res] = true;
                int perm_int = h.emit(HIR_ATOI, TY_INT, perm_res);
                int perm_ok = h.emit(HIR_EQ, TY_INT, perm_int, zero_i);
                h.native_ops++;

                uint64_t ex_addr = rv_compiler::SUBST_BASE
                    + rv_compiler::SUBST_EXECUTOR * rv_compiler::SUBST_SLOT;
                int ex_ref = h.emit_sref(ex_addr);
                std::string hash_thing = "#" + thing_s;
                uint64_t ha = rc.pool_str(hash_thing);
                int hash_c = h.emit_sconst(ha, hash_thing);
                int ex_cmp = h.emit(HIR_STRCMP, TY_INT, ex_ref, hash_c);
                int ex_ok = h.emit(HIR_EQ, TY_INT, ex_cmp, zero_i);
                h.native_ops += 2;
                int gate = h.emit(HIR_BAND, TY_INT, perm_ok, ex_ok);
                h.native_ops++;

                // Tier 2's SPLIT_TOKEN matches only the FIRST BYTE of a
                // delimiter (get_delim, softlib.c) while the interpreter
                // matches the whole string, so a multi-byte delimiter splits
                // differently on the two routes -- map(o/F,a::b,::) yields
                // three elements inline against the interpreter's two
                // (#2127).  The delimiter is usually a runtime value, so this
                // has to be a runtime test; BYTELEN <= 1 sends the rest down
                // the ECALL fallback, which is interpreter-correct.
                //
                // Only the SPLIT needs gating.  The join is already correct:
                // the divergent "a::::b" is three elements joined by the full
                // "::", so osep handling is not implicated.
                {
                    int dargs[1] = { delim_val };
                    int dlen_i = h.emit_call_t2i(t2blen_d, 0, dargs, 1);
                    h.tier2_calls++;
                    int c2 = h.emit_iconst(2);
                    int delim_ok = h.emit(HIR_LT, TY_INT, dlen_i, c2);
                    gate = h.emit(HIR_BAND, TY_INT, gate, delim_ok);
                    h.native_ops += 2;
                }

                int c256 = h.emit_iconst(256);

                int entry_blk = h.cur_block;
                int fallback_blk = h.new_block();
                int inline_blk = h.new_block();
                h.emit(HIR_BRC, TY_VOID, gate, fallback_blk, inline_blk);
                h.add_edge(entry_blk, fallback_blk);
                h.add_edge(entry_blk, inline_blk);

                // ---- fallback: ECALL fun_fold ----
                h.cur_block = fallback_blk;
                int fidx_fold = engine_api_lookup("FOLD");
                int fb_res = h.emit_call(TY_STRING, fidx_fold,
                    m_args.data(), static_cast<int>(m_args.size()));
                h.ecalls++;
                int fb_exit = h.cur_block;
                int fb_br = h.emit(HIR_BR, TY_VOID, -1, -1, -1);

                // ---- inline path ----
                h.cur_block = inline_blk;
                int save_idx = engine_api_lookup("_SAVE_CARGS");
                int cargs_handle = h.emit_call(TY_STRING, save_idx,
                                               nullptr, 0);
                h.ecalls++;
                int ncargs_idx = engine_api_lookup("_SET_NCARGS");
                uint64_t two_a = rc.pool_str("2");
                int two_c = h.emit_sconst(two_a, "2");
                int nc_arg[1] = { two_c };
                h.emit_call(TY_STRING, ncargs_idx, nc_arg, 1);
                h.ecalls++;
                int write_idx = engine_api_lookup("_WRITE_CARG");

                int words_idx = engine_api_lookup("WORDS");
                int wargs2[2] = { list_val, delim_val };
                int nwords_str = h.emit_call(TY_STRING, words_idx,
                                             wargs2, 2);
                h.known_int[nwords_str] = true;
                uint64_t t2words = tier2_lookup("WORDS");
                if (t2words) {
                    h.tier2_addr[nwords_str] = t2words;
                    h.tier2_calls++;
                } else {
                    h.ecalls++;
                }
                int nwords_int = h.emit(HIR_ATOI, TY_INT, nwords_str);

                // The pinned accumulator, written at offset 0 always.
                uint64_t acc_addr = rc.alloc_output();
                uint64_t e_addr = rc.pool_str("");
                int e_str = h.emit_sconst(e_addr, "");

                // ---- seed ----
                //
                // With a base:    acc = base,        cursor = 0, inum = 0
                // Without a base: acc = element 0,   cursor = next, inum = 1
                //
                // Seeding before the loop keeps the body uniform; the only
                // difference is what lands in the accumulator and where the
                // cursor starts.
                //
                // fun_fold's FIRST application is unconditional: it fires
                // even when the list is empty (base form) or has fewer than
                // two elements (no-base form), with %1 the empty string --
                // split_token on an exhausted list returns ""/NULL and
                // mux_exec runs regardless.  The loop below must therefore
                // always run its first iteration; the header ORs the count
                // check with inum == seed value.  rv64_split_token at the
                // end-of-list cursor returns "", which is exactly the %1
                // the interpreter passes.
                int seed_val;
                int seed_cursor;
                int seed_inum;
                if (has_base) {
                    seed_val = m_args[2];
                    seed_cursor = h.emit_iconst(0);
                    seed_inum = h.emit_iconst(0);
                } else {
                    // Integer-ABI seed walk (#2153): one call at cursor 0.
                    int c0_i = h.emit_iconst(0);
                    int s0[3] = { list_val, c0_i, delim_val };
                    int sstep = h.emit_call_t2i(t2split_d, 1, s0, 3);
                    h.tier2_calls++;
                    seed_val = h.emit(HIR_T2I_STR, TY_STRING, sstep);
                    seed_cursor = sstep;
                    seed_inum = h.emit_iconst(1);
                }
                int acc_seed_ref = h.emit_sref(acc_addr);
                int sd_args[5] = { acc_seed_ref, zero_i, zero_i,
                                   e_str, seed_val };
                int sd = h.emit_call_t2i(t2append_d, 0, sd_args, 5);
                (void)sd;
                h.tier2_calls++;

                h.emit(HIR_STORE_Q, TY_VOID, seed_inum, -1,
                       QREG_ITER_INUM);
                h.emit(HIR_STORE_Q, TY_VOID, seed_cursor, -1,
                       QREG_ITER_CURSOR);

                int pre_hdr = h.cur_block;
                int hdr_blk = h.new_block();
                h.emit(HIR_BR, TY_VOID, -1, -1, hdr_blk);
                h.add_edge(pre_hdr, hdr_blk);

                // Header: loop-carried loads live HERE (TC020's lesson).
                h.cur_block = hdr_blk;
                int inum = h.emit(HIR_LOAD_Q, TY_INT, -1, -1,
                                  QREG_ITER_INUM);
                int dcur = h.emit(HIR_LOAD_Q, TY_INT, -1, -1,
                                  QREG_ITER_CURSOR);
                // inum == seed forces the first iteration: fun_fold applies
                // the body once even on an empty or one-element list (see
                // the seed comment above).
                int lt_n = h.emit(HIR_LT, TY_INT, inum, nwords_int);
                int at_seed = h.emit(HIR_EQ, TY_INT, inum, seed_inum);
                int cond = h.emit(HIR_BOR, TY_INT, lt_n, at_seed);
                h.native_ops += 3;
                int body_blk = h.new_block();
                int exit_blk = h.new_block();
                h.emit(HIR_BRC, TY_VOID, cond, exit_blk, body_blk);
                h.add_edge(hdr_blk, body_blk);
                h.add_edge(hdr_blk, exit_blk);

                // Body: cursor walk for the element.
                h.cur_block = body_blk;
                int one_i = h.emit_iconst(1);
                int inum1 = h.emit(HIR_ADD, TY_INT, inum, one_i);
                h.native_ops++;
                // Integer-ABI walk (#2153): one call per element.
                int st0[3] = { list_val, dcur, delim_val };
                int step = h.emit_call_t2i(t2split_d, 1, st0, 3);
                h.tier2_calls++;
                int elem = h.emit(HIR_T2I_STR, TY_STRING, step);
                int nxt_cur = step;

                // Size diamond.  BOTH the accumulator and the element must
                // fit their CARGS slots -- the accumulator is the one that
                // grows, and for a string-building fold it is what crosses
                // the line (fold_fn TC007).
                int acc_ref_r = h.emit_sref(acc_addr);
                int ab_args[1] = { acc_ref_r };
                int ab_i = h.emit_call_t2i(t2blen_d, 0, ab_args, 1);
                h.tier2_calls++;
                int a_fits = h.emit(HIR_LT, TY_INT, ab_i, c256);
                int eb_args[1] = { elem };
                int eb_i = h.emit_call_t2i(t2blen_d, 0, eb_args, 1);
                h.tier2_calls++;
                int e_fits = h.emit(HIR_LT, TY_INT, eb_i, c256);
                int both_fit = h.emit(HIR_BAND, TY_INT, a_fits, e_fits);
                h.native_ops += 3;

                int uarm_blk = h.new_block();
                int ibody_blk = h.new_block();
                h.emit(HIR_BRC, TY_VOID, both_fit, uarm_blk, ibody_blk);
                h.add_edge(h.cur_block, uarm_blk);
                h.add_edge(h.cur_block, ibody_blk);

                // Inlined body: %0 = accumulator, %1 = element.
                h.cur_block = ibody_blk;
                uint64_t i0a = rc.pool_str("0");
                int i0c = h.emit_sconst(i0a, "0");
                int acc_ref_w = h.emit_sref(acc_addr);
                int w0[2] = { i0c, acc_ref_w };
                h.emit_call(TY_STRING, write_idx, w0, 2);
                h.ecalls++;
                uint64_t i1a = rc.pool_str("1");
                int i1c = h.emit_sconst(i1a, "1");
                int w1[2] = { i1c, elem };
                h.emit_call(TY_STRING, write_idx, w1, 2);
                h.ecalls++;
                bool saved_fcheck = s_fcheck_available;
                s_fcheck_available = true;
                s_inline_depth++;
                int ib_val = hir_lower_node(h, rc, body_ast.get());
                s_inline_depth--;
                s_fcheck_available = saved_fcheck;
                if (ib_val < 0) return -1;
                if (h.ty[ib_val] == TY_INT) {
                    ib_val = h.emit(HIR_ITOA, TY_STRING, ib_val);
                } else if (h.ty[ib_val] == TY_FLOAT) {
                    ib_val = h.emit(HIR_FTOA, TY_STRING, ib_val);
                }
                int ib_exit = h.cur_block;
                int ib_br = h.emit(HIR_BR, TY_VOID, -1, -1, -1);

                // Oversized arm: ECALL u(#thing/attr, acc, elem) -- exactly
                // one application of the body with %0/%1 in place, through
                // fun_u's LBUF-sized handling, same as MAP's oversized arm.
                //
                // NOT fold(#thing/attr, elem, acc): fun_fold re-splits its
                // list argument with the DEFAULT delimiter, so a custom-
                // delimiter fold whose element contains spaces would be
                // folded word-by-word here instead of applied once.  (And
                // fun_u keeps executor = thing, which the runtime gate has
                // already pinned to the executor -- same context either
                // way.)
                h.cur_block = uarm_blk;
                int fidx_u2 = engine_api_lookup("U");
                uint64_t ua = rc.pool_str(arg0_str);
                int uref_c = h.emit_sconst(ua, arg0_str);
                int acc_ref_u = h.emit_sref(acc_addr);
                int uargs[3] = { uref_c, acc_ref_u, elem };
                int ua_val = h.emit_call(TY_STRING, fidx_u2, uargs, 3);
                h.ecalls++;
                int ua_exit = h.cur_block;

                int emerge_blk = h.new_block();
                h.val[ib_br] = emerge_blk;
                h.add_edge(ib_exit, emerge_blk);
                h.emit(HIR_BR, TY_VOID, -1, -1, emerge_blk);
                h.add_edge(ua_exit, emerge_blk);

                h.cur_block = emerge_blk;
                int eblocks[2] = { ib_exit, ua_exit };
                int evals[2] = { ib_val, ua_val };
                int new_acc = h.emit_phi(TY_STRING, -1,
                                         eblocks, evals, 2);

                // Replace the accumulator: write at offset 0, no osep.
                int acc_ref_s = h.emit_sref(acc_addr);
                int zero_r = h.emit_iconst(0);
                int sa[5] = { acc_ref_s, zero_r, zero_r, e_str, new_acc };
                int st = h.emit_call_t2i(t2append_d, 0, sa, 5);
                (void)st;
                h.tier2_calls++;

                h.emit(HIR_STORE_Q, TY_VOID, inum1, -1, QREG_ITER_INUM);
                h.emit(HIR_STORE_Q, TY_VOID, nxt_cur, -1,
                       QREG_ITER_CURSOR);
                h.emit(HIR_BR, TY_VOID, -1, -1, hdr_blk);
                h.add_edge(h.cur_block, hdr_blk);

                h.cur_block = exit_blk;
                int restore_idx = engine_api_lookup("_RESTORE_CARGS");
                int rc_args[1] = { cargs_handle };
                h.emit_call(TY_STRING, restore_idx, rc_args, 1);
                h.ecalls++;
                int in_res = h.emit_sref(acc_addr);
                int in_exit = h.cur_block;

                int merge_blk = h.new_block();
                h.val[fb_br] = merge_blk;
                h.add_edge(fb_exit, merge_blk);
                h.cur_block = in_exit;
                h.emit(HIR_BR, TY_VOID, -1, -1, merge_blk);
                h.add_edge(in_exit, merge_blk);

                h.cur_block = merge_blk;
                int mblocks[2] = { fb_exit, in_exit };
                int mvals[2] = { fb_res, in_res };
                int phi = h.emit_phi(TY_STRING, -1, mblocks, mvals, 2);

                qreg_clobber();
                h.needs_jit = true;
                return phi;
            }
        }
        // Fall through: ECALL fun_fold, exactly as before.
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
    //
    // AF_TRACE is a compile-time decline (#2098).  fun_u propagates
    // AttrTrace into EV_TRACE so the interpreter emits trace lines;
    // an inlined body has no channel for that side-channel, so a
    // TRACE'd attr would evaluate silently on the compiled route.
    // Flag changes go through atr_set_flags → atr_add → mod_count,
    // so setting TRACE after a prior compile already invalidates the
    // cache; the gate then declines the recompile.  _CHECK_U_PERM
    // also returns denied on AF_TRACE as belt-and-braces, forcing the
    // fun_u ECALL if a program ever ran against a post-compile TRACE.
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

                // Decline AF_TRACE at compile time (#2098): same gate
                // MAP carries.  Parsing only when the body is eligible
                // keeps free_lbuf outside the success path so TRACE and
                // empty bodies free cleanly.
                std::unique_ptr<ASTNode> body_ast;
                if (body && nBodyLen > 0 && !(aflags & AF_TRACE))
                {
                    body_ast = ast_parse_string(body, nBodyLen);
                }
                if (body)
                {
                    free_lbuf(body);
                }

                if (body_ast)
                {
                        bool is_local = (fname == "ULOCAL");
                        int nExtra = static_cast<int>(
                            node->children.size()) - 1;

                        // Inlined body is flattened out of a real u()
                        // call frame, so its FUNCCALL nest/count are
                        // invisible to the outer AST watermark.
                        // Accumulate so compile_expression can fold
                        // them into prog.max_func_depth / n_func_calls
                        // (#1056).
                        h.inline_extra_depth +=
                            hir_ast_max_funccall_depth(body_ast.get());
                        h.inline_extra_calls +=
                            hir_ast_funccall_count(body_ast.get());

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

                        // BRC: nonzero (denied) → fallback, 0 (ok) → inline.
                        //
                        // emit()'s 4th argument is src2 = the FALSE target
                        // and the 5th is val = the TRUE target (codegen:
                        // true_blk = val[i]).  This call had them REVERSED
                        // from the day the inline was written: denied went
                        // to the inline arm -- so a NOEVAL'd or unreadable
                        // attr had its body evaluated anyway on the
                        // compiled route (u(#2/FN,9) after @set NOEVAL
                        // returned 11, where fun_u returns the literal) --
                        // and permitted calls took the ECALL fallback,
                        // meaning the Tier 3 inline had never executed for
                        // an allowed call at all.  Nothing caught it
                        // because both arms produce identical output for
                        // the permitted case, and no corpus case exercised
                        // a denied one (#2080 review).
                        h.emit(HIR_BRC, TY_VOID, perm_int,
                               inline_block, fallback_block);
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
                        int saved_qreg[HIR_NUM_QREGS];
                        if (is_local)
                        {
                            int save_q = engine_api_lookup("_SAVE_QREGS");
                            qreg_handle = h.emit_call(TY_STRING,
                                save_q, nullptr, 0);
                            h.ecalls++;

                            // Snapshot compile-time %q tracking: body
                            // setq/setr are reverted by the runtime
                            // restore, so post-scope r(n) reads must
                            // resolve to the pre-scope SSA values
                            // (docs/plan-jit-evalbracket-lift.md, Ph 2).
                            memcpy(saved_qreg, qreg, sizeof(qreg));
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
                        if (is_local)
                        {
                            memcpy(qreg, saved_qreg, sizeof(qreg));
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

                        // Same refusal check as the ifelse and and/or
                        // sites above.  h.ty[-1] lands INSIDE the struct
                        // (ty[] is not the first member), so this one reads
                        // garbage silently rather than tripping a sanitizer
                        // -- which is exactly why it wants the guard rather
                        // than waiting to be caught.
                        //
                        if (body_result < 0) {
                            return -1;
                        }

                        // u() merge is always TY_STRING (fallback is
                        // fun_u). Coerce a native float/int body before
                        // the BR so PHI arms are real C strings (#1143).
                        if (h.ty[body_result] == TY_FLOAT
                            || h.ty[body_result] == TY_INT) {
                            h.cur_block = inline_exit;
                            if (h.ty[body_result] == TY_FLOAT) {
                                body_result = h.emit(HIR_FTOA, TY_STRING,
                                                     body_result);
                            } else {
                                body_result = h.emit(HIR_ITOA, TY_STRING,
                                                     body_result);
                            }
                            inline_exit = h.cur_block;
                        }

                        // Patch the fallback BR to target merge_block.
                        h.val[fb_br_idx] = merge_block;
                        h.add_edge(fb_exit, merge_block);

                        h.cur_block = inline_exit;
                        h.emit(HIR_BR, TY_VOID, -1, -1, merge_block);
                        h.add_edge(inline_exit, merge_block);

                        // --- Merge block: PHI ---
                        h.cur_block = merge_block;
                        int blocks[2] = { fb_exit, inline_exit };
                        int vals[2] = { fb_result, body_result };
                        int phi = h.emit_phi(TY_STRING, -1,
                                             blocks, vals, 2);

                        // Non-local u(): register mutations leak by
                        // design, but WHICH mutations depends on the
                        // runtime path (inline body vs fun_u
                        // fallback), so tracked values lowered along
                        // the inline path are not valid post-merge.
                        // (ulocal restores registers on both paths;
                        // its snapshot restore above handles it.)
                        if (!is_local) {
                            qreg_clobber();
                        }
                        return phi;
                }
            }
        }
        // Fall through if inlining wasn't possible.
    }

    // ---------------------------------------------------------------
    // letq(name, value, ..., body)
    //
    // Scoped q-register assignment.  Save all registers, assign
    // name/value pairs, lower the body, restore registers.
    //
    // Only handles literal single-char register names (0-9, a-z).
    // Named registers or dynamic names bail to AST.
    // ---------------------------------------------------------------

    if (fname == "LETQ" && node->children.size() >= 3
        && (node->children.size() % 2) == 1) {

        int nfargs = static_cast<int>(node->children.size());
        int npairs = (nfargs - 1) / 2;

        // Pre-check: all register names must be literal single-char.
        // Bail if any are dynamic or named registers.
        std::vector<int> regnums;
        for (int i = 0; i < npairs; i++) {
            const ASTNode *name_node = node->children[i * 2].get();

            // Strip brace groups for the register name literal.
            const ASTNode *inner = name_node;
            if (inner->type == AST_BRACEGROUP && !inner->children.empty())
                inner = inner->children[0].get();
            // Unwrap single-child sequence.
            if (inner->type == AST_SEQUENCE && inner->children.size() == 1)
                inner = inner->children[0].get();

            if (inner->type != AST_LITERAL || inner->text.size() != 1)
                goto general_lowering;  // dynamic or named register

            int rn = mux_RegisterSet[
                static_cast<unsigned char>(inner->text[0])];
            if (rn < 0 || rn >= MAX_GLOBAL_REGS)
                goto general_lowering;  // invalid register

            regnums.push_back(rn);
        }

        // Save q-registers via ECALL.
        int save_idx = engine_api_lookup("_SAVE_QREGS");
        int save_handle = h.emit_call(TY_STRING, save_idx, nullptr, 0);
        h.ecalls++;
        h.needs_jit = true;

        // Snapshot compile-time %q tracking around the scope: the
        // assignments below and any body setq/setr are reverted by the
        // runtime restore, so post-scope r(n) reads must resolve to
        // the pre-scope SSA values
        // (docs/plan-jit-evalbracket-lift.md, Phase 2).
        int saved_qreg[HIR_NUM_QREGS];
        memcpy(saved_qreg, qreg, sizeof(qreg));

        // Evaluate and assign each value.
        for (int i = 0; i < npairs; i++) {
            int val = hir_lower_trimmed(h, rc,
                node->children[i * 2 + 1].get());

            // Track the assignment so r(n) reads inside the body see
            // the letq-bound value, not a stale pre-letq one (mirrors
            // the setq lowering; slots > digit range are never read
            // via tracked r(n), so the guard is just bounds safety).
            if (regnums[i] < HIR_NUM_QREGS) {
                qreg[regnums[i]] = val;
                qreg_used = true;
            }

            // Convert to string for SETQ_SYNC if needed.
            int sval = val;
            if (h.ty[sval] == TY_INT) {
                sval = h.emit(HIR_ITOA, TY_STRING, sval);
            } else if (h.ty[sval] == TY_FLOAT) {
                sval = h.emit(HIR_FTOA, TY_STRING, sval);
            }
            h.emit(HIR_SETQ_SYNC, TY_VOID, sval, -1, regnums[i]);
        }

        // Lower the body (last argument).
        int body_result = hir_lower_trimmed(h, rc,
            node->children[nfargs - 1].get());

        // Restore q-registers via ECALL.
        int restore_idx = engine_api_lookup("_RESTORE_QREGS");
        int rqargs[1] = { save_handle };
        h.emit_call(TY_STRING, restore_idx, rqargs, 1);
        h.ecalls++;

        memcpy(qreg, saved_qreg, sizeof(qreg));

        return body_result;
    }

    // ---------------------------------------------------------------
    // default(obj/attr, default-expr)
    // edefault(obj/attr, default-expr)
    //
    // Look up an attribute.  If non-empty, return its value (default)
    // or evaluate it (edefault).  Otherwise evaluate the default expr.
    //
    // Compiled as: result = ECALL _DEFAULT_GET(arg0)
    //              if result non-empty → return result
    //              else → lower arg1 (default body)
    // ---------------------------------------------------------------

    if ((fname == "DEFAULT" || fname == "EDEFAULT")
        && node->children.size() == 2) {

        bool is_edefault = (fname == "EDEFAULT");

        // Evaluate arg 0 (the obj/attr reference).
        int arg0 = hir_lower_trimmed(h, rc, node->children[0].get());
        if (h.ty[arg0] == TY_INT) {
            arg0 = h.emit(HIR_ITOA, TY_STRING, arg0);
        } else if (h.ty[arg0] == TY_FLOAT) {
            arg0 = h.emit(HIR_FTOA, TY_STRING, arg0);
        }

        // ECALL _DEFAULT_GET or _EDEFAULT_GET.
        const char *helper = is_edefault ? "_EDEFAULT_GET" : "_DEFAULT_GET";
        int helper_idx = engine_api_lookup(helper);
        int hargs[1] = { arg0 };
        int lookup_result = h.emit_call(TY_STRING, helper_idx, hargs, 1);
        h.ecalls++;
        h.needs_jit = true;
        if (is_edefault) {
            // _EDEFAULT_GET evaluates the attribute body, which may
            // setq — invalidate compile-time %q tracking.
            qreg_clobber();
        }

        // Check if result is non-empty: strlen(result) > 0.
        int len = h.emit(HIR_STRCMP, TY_INT, lookup_result,
            h.emit_sconst(rc.pool_str(""), ""));

        // Branch: non-empty (len != 0) → use lookup result, empty → default body.
        int entry_block = h.cur_block;
        int found_block = h.new_block();
        int default_block = h.new_block();
        int merge_block = h.new_block();

        h.emit(HIR_BRC, TY_VOID, len, default_block, found_block);
        h.add_edge(entry_block, found_block);
        h.add_edge(entry_block, default_block);

        // Found block: return the lookup result.
        h.cur_block = found_block;
        int found_val = lookup_result;
        h.emit(HIR_BR, TY_VOID, -1, -1, merge_block);
        h.add_edge(found_block, merge_block);

        // Default block: lower the default body.
        h.cur_block = default_block;
        int default_val = hir_lower_trimmed(h, rc,
            node->children[1].get());
        if (h.ty[default_val] == TY_INT) {
            default_val = h.emit(HIR_ITOA, TY_STRING, default_val);
        } else if (h.ty[default_val] == TY_FLOAT) {
            default_val = h.emit(HIR_FTOA, TY_STRING, default_val);
        }
        int default_exit = h.cur_block;
        h.emit(HIR_BR, TY_VOID, -1, -1, merge_block);
        h.add_edge(default_exit, merge_block);

        // Merge with PHI.
        h.cur_block = merge_block;
        int blocks[2] = { found_block, default_exit };
        int vals[2] = { found_val, default_val };
        return h.emit_phi(TY_STRING, -1, blocks, vals, 2);
    }

    // ---------------------------------------------------------------
    // localize(body)
    //
    // Save all q-registers, evaluate body, restore registers.
    // Same as letq but without name/value assignments.
    // ---------------------------------------------------------------

    if (fname == "LOCALIZE" && node->children.size() == 1) {
        int save_idx = engine_api_lookup("_SAVE_QREGS");
        int save_handle = h.emit_call(TY_STRING, save_idx, nullptr, 0);
        h.ecalls++;
        h.needs_jit = true;

        // Snapshot compile-time %q tracking: body setq/setr update
        // qreg[], but the runtime restore reverts the registers, so
        // post-scope r(n) reads must resolve to the pre-scope SSA
        // values (docs/plan-jit-evalbracket-lift.md, Phase 2).
        int saved_qreg[HIR_NUM_QREGS];
        memcpy(saved_qreg, qreg, sizeof(qreg));

        int body_result = hir_lower_trimmed(h, rc,
            node->children[0].get());

        int restore_idx = engine_api_lookup("_RESTORE_QREGS");
        int rqargs[1] = { save_handle };
        h.emit_call(TY_STRING, restore_idx, rqargs, 1);
        h.ecalls++;

        memcpy(qreg, saved_qreg, sizeof(qreg));

        return body_result;
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

    // Function-nesting introspection (fdepth/fcount) reads
    // mudstate.func_nest_lev / func_invk_ctr, which JIT-compiled
    // programs do not maintain — native lowering flattens the nest at
    // compile time, so the counters would read 0/stale.  Bail the
    // compilation; these are rare diagnostics and the AST answers
    // them correctly.  (Surfaced by the Phase 5 flip via
    // nested_depth.mux's Makesmoke-time setup: [cat(cat(cat(
    // fdepth())))] baked "0" instead of "4".)
    if (fname == "FDEPTH" || fname == "FCOUNT") {
        rc.out_exhausted = true;  // force compilation failure
        uint64_t addr = rc.pool_str("");
        return h.emit_sconst(addr, "");
    }

    // Lower arguments.
    //
    // This is the loop that feeds the entire builtin dispatch below, and it
    // is the reason per-site guards keep missing cases (#1457): #1440 guarded
    // the two loops in the ECALL fall-through, but every fast path BEFORE
    // those -- ensure_hi's h.ty[ai], the IDIV h.kind[args[1]]/h.val[args[1]]
    // constant check, and their siblings -- indexes these same values first.
    // Refusing here covers all of them at once.
    //
    // (all_int() is not the exposure: h.is_int() guards a negative index
    // internally, as does h.is_const(). ensure_hi() does not.)
    //
    std::vector<int> args;
    for (auto &child : node->children) {
        int a = hir_lower_argument(h, rc, child.get());
        if (a < 0) {
            return -1;
        }
        args.push_back(a);
    }
    int nargs = static_cast<int>(args.size());

    // maxArgsParsed comma-catenation (#988): the AST parser always
    // splits on commas, and for builtins whose maxArgsParsed limits
    // splitting, ast_eval_node re-catenates the excess args (with
    // commas) into the last slot.  Mirror that here — without it the
    // ECALL layer silently clamps and sha1(abc,def) computes
    // sha1("abc").  Constant pieces join at compile time so the fold
    // path below still sees a foldable SCONST.
    {
        int fidx0 = engine_api_lookup(fname.c_str());
        FUN *fp0 = (fidx0 > 0 && fidx0 < ENGINE_API_MAX_FUNCS)
                   ? engine_api_table[fidx0] : nullptr;
        if (fp0 != nullptr
            && fp0->maxArgsParsed > 0
            && nargs > fp0->maxArgsParsed) {
            int m = fp0->maxArgsParsed;
            bool tail_const = true;
            for (int k = m - 1; k < nargs; k++) {
                if (!h.is_const(args[k])) { tail_const = false; break; }
            }
            if (tail_const) {
                std::string joined;
                for (int k = m - 1; k < nargs; k++) {
                    if (k > m - 1) joined += ",";
                    joined += h.const_str(args[k]);
                }
                uint64_t addr = rc.pool_str(joined);
                args[m - 1] = h.emit_sconst(addr, joined);
            } else {
                std::vector<int> pieces;
                uint64_t caddr = rc.pool_str(",");
                for (int k = m - 1; k < nargs; k++) {
                    if (k > m - 1) {
                        pieces.push_back(h.emit_sconst(caddr, ","));
                    }
                    pieces.push_back(args[k]);
                }
                args[m - 1] = h.emit_strcat(
                    pieces.data(), static_cast<int>(pieces.size()));
            }
            args.resize(m);
            nargs = m;
        }
    }

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
            int64_t v = static_cast<int64_t>(mux_atoi64(u8(h.sval[ai])));
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

    // Binary ops: ADD, SUB, MOD.
    //
    // The interpreter's add()/sub() use a fast integer path only when
    // all arguments have <= 9 digits (fitting in a 32-bit long).  For
    // larger values, they fall back to double arithmetic via mux_atof.
    // The JIT must match: only use HIR_ADD/HIR_SUB when all constant
    // args are small enough.  Runtime-valued args whose magnitude is
    // unknown fall through to ECALL to match interpreter behavior.
    //
    // Do NOT widen the 9-digit threshold.  It looks like a stale 32-bit-long
    // artifact, but it is actually the range in which exact integer math still
    // reproduces add()/sub()'s floating-point result bit-for-bit, which is the
    // whole contract: these functions must behave AS IF floating-point.  The
    // binding limit is not 2^53 (the double's exact-integer range) but the
    // precision of mux_atof(), the parser the double path uses, which is exact
    // only to ~15 significant digits.  A coordinated interpreter+JIT extension
    // to int64 was prototyped and measured against the engine: results match
    // through 14-digit operands but diverge at 15, e.g.
    //   add(999999999999999, 999999999999999)
    //     -> 1999999999999998 (exact int)  vs  1999999999999997 (as-if-float)
    // so widening silently changes observable output.  Softcoders who want
    // exact wide-integer math use iadd()/isub() instead (no cap, full int64).
    // See issue #734.
    //
    // Note: mul() always uses doubles in the interpreter.  Only imul()
    // does integer multiply.  So mul() is NOT handled here — it falls
    // through to ECALL or the float path.
    //
    if ((upper == "ADD" || upper == "SUB") && nargs >= 2 && all_int()) {
        // Check that all constant args have <= 9 digits (interpreter
        // fast-path threshold).  If any constant is too large, fall
        // through to ECALL which will use the double path.
        bool all_small = true;
        for (int ai : args) {
            if (h.kind[ai] == HIR_ICONST) {
                int64_t v = h.val[ai];
                if (v > 999999999LL || v < -999999999LL) {
                    all_small = false;
                    break;
                }
            } else if (h.kind[ai] == HIR_SCONST && !h.sval[ai].empty()) {
                const char *s = h.sval[ai].c_str();
                if (*s == '-') s++;
                int nDigits = 0;
                while (*s >= '0' && *s <= '9') { s++; nDigits++; }
                if (nDigits > 9) {
                    all_small = false;
                    break;
                }
            }
        }
        if (all_small) {
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
    }

    // MOD is NOT lowered to a native HIR_REM: HIR_REM is C++ % (truncate /
    // sign-of-dividend), but mux mod() is floor-mod (i64Mod, sign-of-divisor)
    // and diverged for negative operands (#828).  Constant mod() folds via
    // try_fold's i64Mod; runtime mod() routes to the tier2 blob rv64_mod, which
    // implements floor-mod.  (IDIV stays native: HIR_DIV == i64Division.)

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

    // Float ordering comparisons: GT, GTE, LT, LTE with float args.
    //
    // Deliberately exclude EQ/NEQ here. The interpreter's fun_eq/fun_neq
    // are string-first and only fall back to numeric comparison if the
    // rendered strings differ. Lowering float EQ/NEQ to raw FEQ breaks
    // parity for values that stringify identically after TinyMUX's dtoa/
    // pretty-rounding path but differ by a few ulps internally.
    //
    // Leave EQ/NEQ on the general call path for now so we preserve
    // interpreter semantics. We can revisit a faster lowering later if
    // we prove it is semantically equivalent.
    if ((upper == "GT" || upper == "GTE"
         || upper == "LT" || upper == "LTE") && nargs == 2
        && all_numeric() && any_float()) {
        int a = ensure_float(args[0]);
        int b = ensure_float(args[1]);
        int r;
        if (upper == "GT") {
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

    // ABS: softcode abs() is float — see s_fp_unary (fabs).  Integer
    // HIR_ABS was removed (#1150 / #1256).  iabs() remains an ECALL (#1114).

    // SIGN / BOUND are intentionally NOT lowered to integer HIR_SIGN /
    // HIR_MAX / HIR_MIN (#1260).  Interpreter fun_sign / fun_bound are
    // float (mux_atof + fval).  MAX/MIN use the float s_fp_binary path
    // (fmax/fmin) when args are numeric (#1273).  Integer isign() stays
    // ECALL.

    // IDIV: integer division (truncate toward zero).
    // Match interpreter: idiv(x,0) returns "#-1 DIVIDE BY ZERO".
    // Only emit bare HIR_DIV when the divisor is a known non-zero
    // constant — a runtime zero yields all-ones (-1) on RV64/x86,
    // not the MUX error string (#1146). Non-const divisors fall
    // through to fun_idiv via the general ECALL path.
    if (upper == "IDIV" && nargs == 2 && all_int()
        && h.kind[args[1]] == HIR_ICONST) {
        if (h.val[args[1]] == 0) {
            uint64_t addr = rc.pool_str("#-1 DIVIDE BY ZERO");
            return h.emit_sconst(addr, "#-1 DIVIDE BY ZERO");
        }
        int a = ensure_hi(args[0]);
        int b = ensure_hi(args[1]);
        int r = h.emit(HIR_DIV, TY_INT, a, b);
        h.native_ops++;
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

    // float ADD/SUB are NOT lowered to a sequential FADD/FSUB chain: the
    // interpreter's fun_add/fun_sub use AddDoubles (|x|-sorted, error-
    // compensated, NearestPretty), so a raw chain diverged both on cancellation
    // and on ordinary decimals (no NearestPretty) -- #829.  Constant add()/sub()
    // fold via try_fold's AddDoubles; runtime float add()/sub() routes to the
    // tier2 blob rv64_add/rv64_sub, which now call the AddDoubles intrinsic.

    // float MUL is NOT lowered to a native FMUL chain, for the same reason
    // float ADD/SUB are not (#829): the interpreter's fun_mul finishes with
    // fval(NearestPretty(prod)), and NearestPretty may move the result by up
    // to 4 ulp to reach a shorter decimal rendering.  A raw FMUL chain skips
    // that step, so the JIT returned the exact IEEE product where the
    // interpreter returned its prettified neighbour -- #1171:
    //
    //   mul(sqrt(100.125),3)   interpreter 30.01874414428424
    //                          native FMUL 30.018744144284252
    //
    // Constant mul() still folds above via try_fold, which applies
    // NearestPretty itself; runtime float mul() falls through to the tier2
    // blob rv64_mul, which mirrors fun_mul exactly (strtod, multiply,
    // rv64_nearest_pretty, fval).  Integer mul() is unaffected: it never
    // reached this path, and NearestPretty returns integral values
    // unchanged via its own fast path.

    // FDIV: always produces float.  Promote args to double.
#ifdef HAVE_IEEE_FP_SNAN
    if (upper == "FDIV" && nargs == 2 && all_numeric()) {
        int a = ensure_float(args[0]);
        int b = ensure_float(args[1]);
        int r = h.emit(HIR_FDIV, TY_FLOAT, a, b);
        h.native_ops++;
        h.needs_jit = true;
        return r;
    }
#endif

    // SQRT: always produces float.
#ifdef HAVE_IEEE_FP_SNAN
    if (upper == "SQRT" && nargs == 1 && h.is_numeric(args[0])) {
        int a = ensure_float(args[0]);
        int r = h.emit(HIR_FSQRT, TY_FLOAT, a);
        h.native_ops++;
        h.needs_jit = true;
        return r;
    }
#endif

    // Unary transcendentals: SIN, COS, TAN, etc. → FCALL1.
    // Only when the argument is provably numeric, and the blob
    // has the raw libm symbol registered as an intrinsic.
    //
    for (int ti = 0; s_fp_unary[ti].mux_name; ti++) {
        if (upper == s_fp_unary[ti].mux_name && nargs == 1
            && h.is_numeric(args[0])) {
#ifndef HAVE_IEEE_FP_SNAN
            // On non-IEEE systems, ASIN/ACOS/LOG/LOG10/SQRT have
            // domain guards in the interpreter that return "Ind".
            // The JIT bypasses those guards, so fall through to
            // ECALL for these functions.
            //
            if (  s_fp_unary[ti].fmath == FMATH_ASIN
               || s_fp_unary[ti].fmath == FMATH_ACOS
               || s_fp_unary[ti].fmath == FMATH_LOG10
               || s_fp_unary[ti].fmath == FMATH_SQRT)
            {
                break;
            }
#endif
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

    // LOG(value, base) — 2-arg form with constant base.
    // Resolve the base at compile time and emit the appropriate intrinsic.
    // Dynamic base falls through to ECALL.
    //
    if (upper == "LOG" && nargs == 2
        && h.is_numeric(args[0]) && h.is_const(args[1])) {
#ifndef HAVE_IEEE_FP_SNAN
        // On non-IEEE systems, the interpreter has domain guards that
        // return "Ind" for negative values and "-Inf" for zero.  The
        // native intrinsics bypass those guards, so fall through to
        // ECALL to preserve parity.
#else
        std::string base_str = h.const_str(args[1]);
        uint64_t addr = 0;
        int fmath_id = 0;

        if (base_str == "10") {
            addr = fp_intrinsic_addr("log10");
            fmath_id = FMATH_LOG10;
        } else if (base_str == "e") {
            addr = fp_intrinsic_addr("log");
            fmath_id = FMATH_LOG;
        } else if (base_str == "2") {
            // log_2(x) = log(x) / log(2)
            uint64_t ln_addr = fp_intrinsic_addr("log");
            if (ln_addr) {
                int a = ensure_float(args[0]);
                int lnx = h.emit(HIR_FCALL1, TY_FLOAT, a, -1,
                                 static_cast<int64_t>(ln_addr));
                h.func_idx[lnx] = FMATH_LOG;
                int ln2 = h.emit_fconst(::log(2.0));
                int r = h.emit(HIR_FDIV, TY_FLOAT, lnx, ln2);
                h.native_ops++;
                h.needs_jit = true;
                return r;
            }
        } else {
            // General case: log_b(x) = log(x) / log(b)
            double bval = mux_atof(u8(base_str));
            if (bval > 1.0) {
                uint64_t ln_addr = fp_intrinsic_addr("log");
                if (ln_addr) {
                    int a = ensure_float(args[0]);
                    int lnx = h.emit(HIR_FCALL1, TY_FLOAT, a, -1,
                                     static_cast<int64_t>(ln_addr));
                    h.func_idx[lnx] = FMATH_LOG;
                    int lnb = h.emit_fconst(::log(bval));
                    int r = h.emit(HIR_FDIV, TY_FLOAT, lnx, lnb);
                    h.native_ops++;
                    h.needs_jit = true;
                    return r;
                }
            }
            // base <= 1 → fall through to ECALL for error handling.
        }

        if (addr) {
            int a = ensure_float(args[0]);
            int r = h.emit(HIR_FCALL1, TY_FLOAT, a, -1,
                           static_cast<int64_t>(addr));
            h.func_idx[r] = fmath_id;
            h.native_ops++;
            h.needs_jit = true;
            return r;
        }
#endif
    }

    // SIN/COS/TAN(value, unit) — 2-arg form with constant angle unit.
    // Convert the input from degrees/gradians to radians, then call
    // the intrinsic.  "r" or unknown unit → identity (no conversion).
    //
    // ASIN/ACOS/ATAN(value, unit) — inverse trig with output conversion.
    // Call the intrinsic first, then convert the result from radians
    // to degrees/gradians.
    //
    // ATAN2(y, x, unit) — 3-arg form with output conversion.
    //
    if (nargs == 2 && h.is_numeric(args[0]) && h.is_const(args[1])
        && (upper == "SIN" || upper == "COS" || upper == "TAN"
            || upper == "ASIN" || upper == "ACOS" || upper == "ATAN")) {
#ifdef HAVE_IEEE_FP_SNAN

        std::string unit_str = h.const_str(args[1]);
        char unit = unit_str.empty() ? 'r' : static_cast<char>(
            tolower(static_cast<unsigned char>(unit_str[0])));

        // Determine conversion factor.
        double pre_factor = 1.0;   // input multiplier (for sin/cos/tan)
        double post_factor = 1.0;  // output multiplier (for asin/acos/atan)
        if (unit == 'd') {
            pre_factor = 0.017453292519943295;    // deg → rad
            post_factor = 57.29577951308232;       // rad → deg
        } else if (unit == 'g') {
            pre_factor = 0.015707963267948967;    // grad → rad
            post_factor = 63.66197723675813;       // rad → grad
        }
        // 'r' or anything else → factors stay 1.0 (identity).

        // Look up the intrinsic.
        const char *sym = nullptr;
        int fmath_id = 0;
        bool is_inverse = false;

        if (upper == "SIN")       { sym = "sin";  fmath_id = FMATH_SIN;  }
        else if (upper == "COS")  { sym = "cos";  fmath_id = FMATH_COS;  }
        else if (upper == "TAN")  { sym = "tan";  fmath_id = FMATH_TAN;  }
        else if (upper == "ASIN") { sym = "asin"; fmath_id = FMATH_ASIN; is_inverse = true; }
        else if (upper == "ACOS") { sym = "acos"; fmath_id = FMATH_ACOS; is_inverse = true; }
        else if (upper == "ATAN") { sym = "atan"; fmath_id = FMATH_ATAN; is_inverse = true; }

        uint64_t addr = fp_intrinsic_addr(sym);
        if (addr) {
            int a = ensure_float(args[0]);

            if (!is_inverse && pre_factor != 1.0) {
                // sin/cos/tan: convert input to radians first.
                int factor = h.emit_fconst(pre_factor);
                a = h.emit(HIR_FMUL, TY_FLOAT, a, factor);
            }

            int r = h.emit(HIR_FCALL1, TY_FLOAT, a, -1,
                           static_cast<int64_t>(addr));
            h.func_idx[r] = fmath_id;

            if (is_inverse && post_factor != 1.0) {
                // asin/acos/atan: convert output from radians.
                int factor = h.emit_fconst(post_factor);
                r = h.emit(HIR_FMUL, TY_FLOAT, r, factor);
            }

            h.native_ops++;
            h.needs_jit = true;
            return r;
        }
#endif
    }

    // ATAN2(y, x, unit) — 3-arg form with output conversion.
    if (upper == "ATAN2" && nargs == 3
        && h.is_numeric(args[0]) && h.is_numeric(args[1])
        && h.is_const(args[2])) {
#ifdef HAVE_IEEE_FP_SNAN

        std::string unit_str = h.const_str(args[2]);
        char unit = unit_str.empty() ? 'r' : static_cast<char>(
            tolower(static_cast<unsigned char>(unit_str[0])));

        double post_factor = 1.0;
        if (unit == 'd')      post_factor = 57.29577951308232;
        else if (unit == 'g') post_factor = 63.66197723675813;

        uint64_t addr = fp_intrinsic_addr("atan2");
        if (addr) {
            int a = ensure_float(args[0]);
            int b = ensure_float(args[1]);
            int r = h.emit(HIR_FCALL2, TY_FLOAT, a, b,
                           static_cast<int64_t>(addr));
            h.func_idx[r] = FMATH_ATAN2;

            if (post_factor != 1.0) {
                int factor = h.emit_fconst(post_factor);
                r = h.emit(HIR_FMUL, TY_FLOAT, r, factor);
            }

            h.native_ops++;
            h.needs_jit = true;
            return r;
        }
#endif
    }

    // Binary FP functions: POWER, ATAN2, FMOD, MAX, MIN → FCALL2.
    // POWER/ATAN2/FMOD require exactly 2 args.  MAX/MIN accept N>=2 and
    // chain fmax/fmin left-to-right (#1273).
    //
    for (int ti = 0; s_fp_binary[ti].mux_name; ti++) {
        const bool is_maxmin = (  s_fp_binary[ti].fmath == FMATH_FMAX
                               || s_fp_binary[ti].fmath == FMATH_FMIN);
        if (upper != s_fp_binary[ti].mux_name || !all_numeric()) {
            continue;
        }
        if (is_maxmin ? nargs < 2 : nargs != 2) {
            continue;
        }
#ifndef HAVE_IEEE_FP_SNAN
        // On non-IEEE systems, POWER and FMOD have domain guards
        // in the interpreter.  Fall through to ECALL.
        //
        if (  s_fp_binary[ti].fmath == FMATH_POW
           || s_fp_binary[ti].fmath == FMATH_FMOD)
        {
            break;
        }
#endif
        uint64_t addr = fp_intrinsic_addr(s_fp_binary[ti].blob_sym);
        if (addr) {
            int r = ensure_float(args[0]);
            for (int ai = 1; ai < nargs; ai++) {
                int b = ensure_float(args[ai]);
                r = h.emit(HIR_FCALL2, TY_FLOAT, r, b,
                           static_cast<int64_t>(addr));
                h.func_idx[r] = s_fp_binary[ti].fmath;
            }
            h.native_ops++;
            h.needs_jit = true;
            return r;
        }
        break;
    }

    // ---------------------------------------------------------------
    // Fall through to ECALL.
    // ---------------------------------------------------------------

    // Convert any TY_INT or TY_FLOAT args to strings for ECALL.
    // A refused child lowering leaves -1 here; see #1440.
    for (int ai : args) {
        if (ai < 0) return -1;
    }
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
            // Mandatory function context: produce error message, and end
            // the region -- see s_fmand_abort (#1247).
            std::string err = "#-1 FUNCTION (";
            err += upper;
            err += ") NOT FOUND";
            uint64_t addr = rc.pool_str(err);
            s_fmand_abort = true;
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

    if (upper == "CAT") {
        if (nargs == 0) {
            uint64_t addr = rc.pool_str("");
            return h.emit_sconst(addr, "");
        }
        if (nargs == 1) {
            return args[0];
        }

        std::vector<int> parts;
        parts.reserve(static_cast<size_t>(nargs) * 2 - 1);

        uint64_t spaddr = rc.pool_str(" ");
        int sp = h.emit_sconst(spaddr, " ");
        for (int ai = 0; ai < nargs; ai++) {
            if (ai > 0) {
                parts.push_back(sp);
            }
            parts.push_back(args[ai]);
        }

        int strcat_idx = engine_api_lookup("STRCAT");
        int r = h.emit_strcat(parts.data(), static_cast<int>(parts.size()));
        if (r >= 0) {
            h.func_idx[r] = strcat_idx;
        }
        if (tier2_lookup("STRCAT")) {
            h.tier2_calls++;
        } else {
            h.ecalls++;
        }
        h.needs_jit = true;
        return r;
    }

    // Validate argument count at compile time, in BOTH directions, and
    // return the same error string the AST evaluator would — not empty.
    //
    // Only the too-few case was checked, so an over-supplied call was
    // compiled and silently evaluated with the arguments the callee
    // happened to read: right(hello,3,x) returned "llo" on the JIT route
    // while the AST route reported the arity error.  maxArgsParsed
    // comma-catenation has already run above, so nargs here is the count
    // the callee will actually see; a function declared with maxArgs of
    // MAX_ARG is unbounded and cannot trip the upper test.
    if (fidx > 0 && fidx < ENGINE_API_MAX_FUNCS) {
        FUN *fp = engine_api_table[fidx];
        if (fp && (nargs < fp->minArgs || nargs > fp->maxArgs)) {
            char errbuf[256];
            if (fp->minArgs == fp->maxArgs) {
                mux_snprintf(reinterpret_cast<UTF8 *>(errbuf), sizeof(errbuf),
                    T("#-1 FUNCTION (%s) EXPECTS %d ARGUMENTS"),
                    upper.c_str(), fp->minArgs);
            } else if (fp->minArgs + 1 == fp->maxArgs) {
                mux_snprintf(reinterpret_cast<UTF8 *>(errbuf), sizeof(errbuf),
                    T("#-1 FUNCTION (%s) EXPECTS %d OR %d ARGUMENTS"),
                    upper.c_str(), fp->minArgs, fp->maxArgs);
            } else {
                mux_snprintf(reinterpret_cast<UTF8 *>(errbuf), sizeof(errbuf),
                    T("#-1 FUNCTION (%s) EXPECTS BETWEEN %d AND %d ARGUMENTS"),
                    upper.c_str(), fp->minArgs, fp->maxArgs);
            }
            uint64_t addr = rc.pool_str(errbuf);
            return h.emit_sconst(addr, errbuf);
        }
    }

    // Convert non-string args to strings before the ECALL.  ECALL args
    // are passed as guest memory pointers, so TY_INT/TY_FLOAT values
    // (which live in registers) must be written to memory first via
    // ITOA/FTOA.  Without this, the fargs array contains garbage
    // addresses for register-resident values and the callee sees
    // truncated or corrupted argument lists.
    //
    for (int ai = 0; ai < nargs; ai++) {
        if (args[ai] < 0) return -1;      // refused child lowering (#1440)
    }
    for (int ai = 0; ai < nargs; ai++) {
        if (h.ty[args[ai]] == TY_INT) {
            args[ai] = h.emit(HIR_ITOA, TY_STRING, args[ai]);
        } else if (h.ty[args[ai]] == TY_FLOAT) {
            args[ai] = h.emit(HIR_FTOA, TY_STRING, args[ai]);
        }
    }

    // Check Tier 2 blob before falling through to ECALL.
    uint64_t t2addr = tier2_lookup(upper);

    // Tier 2 blobs have arg-count and delimiter-width limitations.
    // Fall through to ECALL when the blob can't handle the call.
    if (t2addr) {
        // Math intrinsics: only handle minimum-arg form.
        if ((upper == "LOG" || upper == "SIN" || upper == "COS"
             || upper == "TAN" || upper == "ASIN" || upper == "ACOS"
             || upper == "ATAN") && nargs != 1) {
            t2addr = 0;
        }
        if (upper == "ATAN2" && nargs != 2) {
            t2addr = 0;
        }
        // List-function wrappers only handle single-byte delimiters.
        // Multi-char string delimiters (DELIM_STRING) need the
        // interpreter's multi-char path.
        //
        // Check the delimiter arg (position varies by function):
        //   FIRST/REST/LAST/SQUISH: delimiter is arg[1] (nargs >= 2)
        //   ELEMENTS: delimiter is arg[2] (nargs >= 3)
        {
            int delim_idx = -1;
            if ((upper == "FIRST" || upper == "REST" || upper == "LAST"
                 || upper == "SQUISH" || upper == "WORDS"
                 || upper == "REVWORDS"
                 || upper == "LADD"
                 || upper == "LMAX" || upper == "LMIN"
                 || upper == "LAND" || upper == "LOR") && nargs >= 2) {
                delim_idx = 1;
            } else if (upper == "MEMBER" && nargs >= 3) {
                delim_idx = 2;
            } else if (upper == "ELEMENTS" && nargs >= 3) {
                delim_idx = 2;
            } else if (upper == "REMOVE" && nargs >= 3) {
                delim_idx = 2;
            } else if ((upper == "REPLACE" || upper == "INSERT"
                        || upper == "SPLICE") && nargs >= 4) {
                delim_idx = 3;
            } else if (upper == "LDELETE" && nargs >= 3) {
                delim_idx = 2;
            } else if (upper == "EXTRACT" && nargs >= 4) {
                delim_idx = 3;
            } else if ((upper == "SETUNION" || upper == "SETDIFF"
                        || upper == "SETINTER") && nargs >= 3) {
                delim_idx = 2;
            } else if ((upper == "WORDPOS" || upper == "MATCH"
                        || upper == "GRAB" || upper == "GRABALL"
                        || upper == "SORT") && nargs >= 3) {
                delim_idx = 2;
            } else if (upper == "TRIM" && nargs >= 3) {
                // arg[2] is the trim character set, not a list delimiter,
                // but the same single-byte wrapper limitation applies: the
                // interpreter handles multi-char sets via co_trim_pattern.
                delim_idx = 2;
            }
            // ELEMENTS osep (arg[3]), REPLACE/INSERT osep (arg[4]),
            // LDELETE osep (arg[3]), EXTRACT osep (arg[4]) are single-byte
            // only; multi-char separators must fall back to the interpreter.
            {
                int osep_idx = -1;
                if (upper == "ELEMENTS" && nargs >= 4) osep_idx = 3;
                else if (upper == "REMOVE" && nargs >= 4) osep_idx = 3;
                else if ((upper == "REPLACE" || upper == "INSERT"
                          || upper == "SPLICE") && nargs >= 5) osep_idx = 4;
                else if (upper == "LDELETE" && nargs >= 4) osep_idx = 3;
                else if (upper == "EXTRACT" && nargs >= 5) osep_idx = 4;
                else if ((upper == "SETUNION" || upper == "SETDIFF"
                          || upper == "SETINTER") && nargs >= 4) osep_idx = 3;
                else if ((upper == "REVWORDS" || upper == "LNUM")
                         && nargs >= 3) osep_idx = 2;
                else if ((upper == "GRABALL" || upper == "SORT")
                         && nargs >= 4) osep_idx = 3;
                if (osep_idx >= 0) {
                    if (!h.is_const(args[osep_idx])) {
                        t2addr = 0;
                    } else {
                        std::string ostr = h.const_str(args[osep_idx]);
                        // != 1, not > 1: an explicit-empty osep means
                        // SPACE in the interpreter (delim_check tlen==0),
                        // but the blob wrappers default it to the
                        // delimiter -- fall back for that case too.
                        if (ostr.size() != 1) {
                            t2addr = 0;
                        }
                    }
                }
            }
            // SETUNION/SETDIFF/SETINTER sort type (arg[4]): the blob's
            // get_cmp implements only a/i/n/d — no AutoDetect ('?' or
            // present-but-empty) and no f/u/c comparators (handle_sets
            // maps those to f_comp/u_collate).  Fall back for anything
            // else (wrapper audit: setunion(2 10,1 3,%b,%b,?) sorted
            // ASCII on the blob, numeric on the interpreter).
            if ((upper == "SETUNION" || upper == "SETDIFF"
                 || upper == "SETINTER") && nargs >= 5) {
                if (!h.is_const(args[4])) {
                    t2addr = 0;
                } else {
                    std::string tstr = h.const_str(args[4]);
                    if (tstr.size() != 1
                        || strchr("aAiInNdD", tstr[0]) == nullptr) {
                        t2addr = 0;
                    }
                }
            }
            if (delim_idx >= 0) {
                if (!h.is_const(args[delim_idx])) {
                    t2addr = 0;
                } else {
                    std::string dstr = h.const_str(args[delim_idx]);
                    if (dstr.size() > 1) {
                        t2addr = 0;
                    }
                }
            }
        }
    }

    // ECALL/Tier2 results are always strings in guest memory.  If the
    // function is known to return integers (strlen, eq, etc.),
    // mark known_int so downstream ops can ATOI and use natively.
    int i = h.emit_call(TY_STRING, fidx,
                         args.data(), nargs,
                         fidx == 0 ? &upper : nullptr);
    // emit_call returns -1 when the instruction or carg pool overflows and
    // sets h.overflowed; indexing with it is out of bounds (#1440).
    if (i < 0) {
        return -1;
    }
    if (t2addr) {
        h.tier2_addr[i] = t2addr;
        h.tier2_calls++;
    } else {
        h.ecalls++;
        // Host ECALL: the callee may mutate global registers (u(),
        // regmatch(), any ufun) — invalidate compile-time %q tracking.
        // Tier2 wrappers above are pure string/math and skip this.
        qreg_clobber();
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
    case AST_SPACE:
    // A semicolon is ordinary text here.  It separates commands in a
    // queued command list, but inside an expression it is just a
    // character, and ast_eval_node emits it as one (ast.cpp's
    // AST_SEMICOLON arm).  Lowering had no case for it, so it fell to
    // the default arm below and compiled to an empty string — every ';'
    // inside a function argument silently disappeared on the compiled
    // route while the AST route and 2.13 kept it (#1237):
    //
    //     [strcat(hello; world)]  ->  hello world
    //     [ansi(r,a;b)]           ->  ab
    //
    case AST_SEMICOLON: {
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
            bool saved_abort = s_fmand_abort;
            s_fcheck_available = true;
            s_fmand_abort = false;
            int r = hir_lower_node(h, rc, node->children[0].get());
            s_fcheck_available = saved_fcheck;
            s_fmand_abort = saved_abort;
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
            if (node->text[1] == '$') {
                // #$ — switch token (mudstate.switch_token).  The JIT
                // does not model the switch-token context, and the old
                // fall-through emitted the literal text "#$" — under
                // the default-on flip, @switch actions like
                // [idiv(#$,2)] evaluated idiv("#$",2) = 0 (caught by
                // repeat_fn.mux's Makesmoke-time setup).  Bail the
                // compilation so #$-carrying programs stay on the AST.
                rc.out_exhausted = true;  // force compilation failure
                uint64_t addr = rc.pool_str("");
                return h.emit_sconst(addr, "");
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
                                // Match the interpreter / ansi() path:
                                // LettersToBinary → v5 SMP two-codepoint
                                // encoding (#1933).  Previously this only
                                // emitted the nearest palette entry and
                                // dropped truecolor refinement ("complex").
                                UTF8 letters[32];
                                size_t li = 0;
                                if (bBackground) {
                                    letters[li++] = '/';
                                }
                                letters[li++] = '<';
                                if (li + nColor + 1 < sizeof(letters)) {
                                    memcpy(letters + li, pColor, nColor);
                                    li += nColor;
                                    letters[li++] = '>';
                                    letters[li] = '\0';
                                    const UTF8 *pUTF = LettersToBinary(letters);
                                    if (pUTF && pUTF[0]) {
                                        std::string cs(
                                            reinterpret_cast<const char *>(pUTF));
                                        uint64_t addr = rc.pool_str(cs);
                                        return h.emit_sconst(addr, cs);
                                    }
                                }
                            }
                            // parse_rgb failed (or emit produced nothing) with a
                            // closing '>' — consume silently.  The interpreter
                            // has no else on the parse_rgb success path, so
                            // %x<196> / %x<red> vanish rather than appearing
                            // as literal text.  Emitting the raw token here
                            // was the JIT/interp divergence (#1934).
                            uint64_t empty = rc.pool_str("");
                            return h.emit_sconst(empty, "");
                        }
                        // Malformed %x<... with no closing > — emit literally
                        // (same as the interpreter).
                        uint64_t addr = rc.pool_str(node->text);
                        return h.emit_sconst(addr, node->text);
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
                // Malformed %c/%x — emit raw text literally.
                uint64_t addr = rc.pool_str(node->text);
                return h.emit_sconst(addr, node->text);
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
                // Bare %= or malformed — emit raw text literally.
                uint64_t addr = rc.pool_str(node->text);
                return h.emit_sconst(addr, node->text);
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
                    // All runtime %q reads go through the single
                    // choke point: it materializes the slot at this
                    // sequence point (Phase 2 read-write-read
                    // semantics) and branches to the fun_r ECALL when
                    // the register's long bit is set (#996 step 2) —
                    // the 256-byte slot would be a truncated copy.
                    return emit_qreg_read(h, rc, rn);
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
                    // Malformed %q<name with no closing > — emit raw text literally.
                    uint64_t addr = rc.pool_str(node->text);
                    return h.emit_sconst(addr, node->text);
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

        // Unresolvable substitution — emit raw text literally.
        {
            uint64_t addr = rc.pool_str(node->text);
            return h.emit_sconst(addr, node->text);
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

    // #1242: Unknown AST node types must refuse the compile, not
    // lower to an empty string.  Emitting "" deleted user text with no
    // error — the failure mode behind the #1237 semicolon drop, and
    // the worst outcome on the parity ranking (silent corruption).
    // overflowed bails before codegen; the AST evaluator handles it.
    //
    default: {
        h.overflowed = true;
        return -1;
    }
    }
}

// ===============================================================
// HIR CODEGEN: HIR → RV64
//
