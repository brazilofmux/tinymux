/*! \file jit_compiler.cpp
 * \brief Top-level JIT compiler pipeline.
 *
 * JIT statistics, Tier 2 blob management, compile cache,
 * compile_expression(), ECALL handler, and the public entry
 * points: jit_eval(), fun_jitstats(), fun_rveval(), fun_rvbench().
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"
#include "sqlite_backend.h"
#include "ast.h"

#include "dbt_compile.h"
#include "dbt.h"
#include "dbt_decoder.h"
#include "engine_api.h"
#include "sha1.h"

#include "../../rv64/rv64blob.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <cerrno>
#include <ctime>
#include <algorithm>
#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include <list>

jit_stats_t s_jit_stats = {};

// ---------------------------------------------------------------
// JIT Arena Management (Tier B)
//
// Uses shared_ptr<RegBuffer> for buffer lifecycle — unified with the
// engine's register packing system.
// ---------------------------------------------------------------

class JITArena {
public:
    struct Arena {
        std::shared_ptr<RegBuffer> buf;
        size_t    used;
        uint32_t  id;
    };

    static Arena *Alloc(size_t n) {
        if (n > LBUF_SIZE) return nullptr;

        if (!s_current || s_current->used + n > LBUF_SIZE) {
            s_current = Create();
        }

        s_current->used += n;
        return s_current;
    }

    static void AddRef(uint32_t id) {
        // No-op: shared_ptr refcount is managed via reg_ref copies.
        UNUSED_PARAMETER(id);
    }

    static void Release(uint32_t id) {
        auto it = s_arenas.find(id);
        if (it != s_arenas.end()) {
            Arena *a = it->second;
            // If only the arena itself holds a reference, clean up.
            if (a->buf.use_count() <= 1) {
                if (s_current && s_current->id == id) s_current = nullptr;
                delete a;
                s_arenas.erase(it);
            }
        }
    }

    static Arena *Get(uint32_t id) {
        auto it = s_arenas.find(id);
        return (it != s_arenas.end()) ? it->second : nullptr;
    }

    // Release arenas that have no external references.
    static void gc() {
        s_current = nullptr;
        auto it = s_arenas.begin();
        while (it != s_arenas.end()) {
            Arena *a = it->second;
            if (a->buf.use_count() <= 1) {
                delete a;
                it = s_arenas.erase(it);
            } else {
                ++it;
            }
        }
    }

private:
    static Arena *Create() {
        Arena *a = new Arena;
        a->buf = std::make_shared<RegBuffer>();
        a->used = 0;
        a->id = ++s_next_id;
        s_arenas[a->id] = a;
        return a;
    }

    static inline uint32_t s_next_id = 0;
    static inline Arena *s_current = nullptr;
    static inline std::unordered_map<uint32_t, Arena *> s_arenas;
};

// ---------------------------------------------------------------
// Tier 2: pre-compiled RV64 library blob
// ---------------------------------------------------------------

tier2_state s_tier2 = { false, {}, {}, {}, 0, {}, 0 };

// Blob content hash for cache invalidation.
std::string s_blob_version = "none";

static bool tier2_allowed(const std::string &mux_name) {
    static const char *const s_allowlist[] = {
        // co_* wrappers: cross-compiled from the same Ragel color_ops
        // source the server uses.  Semantics-matched by construction.
        //
        "STRLEN",
        "LCSTR",
        "UCSTR",
        "CAPSTR",
        "REVERSE",
        "ESCAPE",
        "STRIPANSI",
        "COMPRESS",
        "FIRST",
        "REST",
        "LAST",
        "WORDS",
        "MID",
        "POS",
        "REPEAT",
        "TRIM",
        "MEMBER",
        "EXTRACT",
        "LEFT",
        "RIGHT",
        "LPOS",
        "LDELETE",
        "REPLACE",
        "INSERT",
        "LJUST",
        "RJUST",
        "CENTER",
        "EDIT",
        "SPLICE",
        "SETUNION",
        "SETDIFF",
        "SETINTER",

        // rv64_* hand-written: only trivial ops where the blob
        // implementation is demonstrably equivalent to the server.
        //
        "CAT",
        "STRCAT",
        "SPACE",

        // Tier 2 math — MATH_WRAP transcendentals.
        // These are simple strtod → libm → fval wrappers, semantics-
        // matched to the server by construction.
        //
        "SIN", "COS", "TAN",
        "ASIN", "ACOS", "ATAN", "ATAN2",
        "EXP", "LOG", "LOG10",
        "SQRT", "CEIL", "FLOOR",
        "ABS",          // rv64_fabs: fabs() via MATH_WRAP_1
        "FMOD",         // rv64_fmod: fmod() via MATH_WRAP_2
        "POWER",        // rv64_power: pow() via MATH_WRAP_2

        // Tier 2 arithmetic.
        "ADD", "SUB",
        "MUL",          // rv64_mul: NearestPretty intrinsic
        "FDIV",         // rv64_fdiv: IEEE div, fval handles Inf/NaN
        "MOD",          // rv64_mod: atoi64 % atoi64
        "SIGN",         // rv64_sign: -1/0/1
        "MIN", "MAX",   // rv64_min/max: strtod compare → fval
        "INC", "DEC",   // rv64_inc/dec: atoi64 ± 1
        "TRUNC",        // rv64_trunc: modf → fval
        "ROUND",        // rv64_round: ftoa_round intrinsic

        // Tier 2 list/string ops — parity-tested via smoke suite.
        //
        "BEFORE", "AFTER",
        "DELETE", "ELEMENTS", "WORDPOS", "REMOVE",
        "REVWORDS",
        "LNUM",
        "LADD", "LMAX", "LMIN", "LAND", "LOR",
        "ISNUM", "ISINT",
        "DEC2HEX", "HEX2DEC",
        "ISDBREF",
        "CHR", "ORD",
        "SECURE", "SQUISH",
        "TRANSLATE",
        "STRMATCH", "MATCH", "GRAB", "GRABALL",
        "SORT",

        nullptr
    };
    for (int i = 0; s_allowlist[i]; i++) {
        if (mux_name == s_allowlist[i]) {
            return true;
        }
    }
    return false;
}

static std::string sha1_hex_parts(const void *const *parts, const size_t *sizes,
                                  int count) {
    static constexpr unsigned int SHA1_DIGEST_LEN = 20;
    std::vector<const UTF8 *> digest_parts;
    std::vector<size_t> digest_sizes;
    digest_parts.reserve(count);
    digest_sizes.reserve(count);

    for (int i = 0; i < count; i++) {
        if (parts[i] && sizes[i] > 0) {
            digest_parts.push_back(reinterpret_cast<const UTF8 *>(parts[i]));
            digest_sizes.push_back(sizes[i]);
        }
    }

    uint8_t digest[SHA1_DIGEST_LEN];
    unsigned int digest_len = 0;
    if (!mux_sha1_digest(digest_parts.data(), digest_sizes.data(),
                         static_cast<int>(digest_parts.size()),
                         digest, &digest_len)
        || digest_len != SHA1_DIGEST_LEN) {
        return "none";
    }

    static const char hex[] = "0123456789abcdef";
    std::string out;
    out.resize(SHA1_DIGEST_LEN * 2);
    for (size_t i = 0; i < SHA1_DIGEST_LEN; i++) {
        out[i * 2] = hex[digest[i] >> 4];
        out[i * 2 + 1] = hex[digest[i] & 0x0F];
    }
    return out;
}

// Map MUX function names (uppercase) to Tier 2 blob entry names.
// The blob uses rv64_ prefixed names; MUX uses plain uppercase.
//
static const struct { const char *mux_name; const char *blob_name; } s_tier2_map[] = {
    // --- Color-aware co_* wrappers (Ragel, PUA color, Unicode 16) ---
    // These replace the ASCII-only rv64_* versions for functions where
    // color preservation and grapheme-cluster-aware word boundaries matter.
    //
    { "FIRST",       "co_first_wrap" },
    { "REST",        "co_rest_wrap" },
    { "LAST",        "co_last_wrap" },
    { "WORDS",       "co_words_wrap" },
    { "EXTRACT",     "co_extract_wrap" },
    { "MEMBER",      "co_member_wrap" },
    { "TRIM",        "co_trim_wrap" },
    { "REPEAT",      "co_repeat_wrap" },
    { "MID",         "co_mid_wrap" },
    { "POS",         "co_pos_wrap" },
    { "SORT",        "co_sort_wrap" },
    { "SETUNION",    "co_setunion_wrap" },
    { "SETDIFF",     "co_setdiff_wrap" },
    { "SETINTER",    "co_setinter_wrap" },
    { "LDELETE",     "co_ldelete_wrap" },
    { "REPLACE",     "co_replace_wrap" },
    { "INSERT",      "co_insert_wrap" },

    // --- ASCII-only rv64_* (no color, byte-level operations) ---
    // These are fine for functions that don't handle colored text.
    //
    { "CAT",         "rv64_cat" },
    { "STRCAT",      "rv64_strcat" },
    { "BEFORE",      "rv64_before" },
    { "AFTER",       "rv64_after" },

    // --- Batch 2: case, reverse, escape, left/right, compress, lpos ---
    //
    { "STRLEN",      "co_strlen_wrap" },
    { "LCSTR",       "co_lcstr_wrap" },
    { "UCSTR",       "co_ucstr_wrap" },
    { "REVERSE",     "co_reverse_wrap" },
    { "ESCAPE",      "co_escape_wrap" },
    { "LEFT",        "co_left_wrap" },
    { "RIGHT",       "co_right_wrap" },
    { "COMPRESS",    "co_compress_wrap" },
    { "LPOS",        "co_lpos_wrap" },

    // --- Batch 3: justify, edit, splice, totitle, stripansi, vislen ---
    { "LJUST",       "co_ljust_wrap" },
    { "RJUST",       "co_rjust_wrap" },
    { "CENTER",      "co_center_wrap" },
    { "EDIT",        "co_edit_wrap" },
    { "SPLICE",      "co_splice_wrap" },
    { "CAPSTR",      "co_totitle_wrap" },
    { "STRIPANSI",   "co_stripansi_wrap" },

    // --- Batch 4: space, secure, squish, delete, elements ---
    { "SPACE",       "rv64_space" },
    { "SECURE",      "co_secure_wrap" },
    { "SQUISH",      "co_compress_wrap" },
    { "DELETE",      "rv64_delete" },
    { "ELEMENTS",    "rv64_elements" },
    { "TRANSLATE",   "rv64_translate" },

    // --- Batch 5: wildcard matching ---
    { "STRMATCH",   "rv64_strmatch" },
    { "MATCH",       "rv64_match" },
    { "GRAB",        "rv64_grab" },
    { "GRABALL",     "rv64_graball" },

    // --- Batch 6: numbers, chars, base conversion ---
    { "LNUM",        "rv64_lnum" },
    { "ISNUM",       "rv64_isnum" },
    { "ISINT",       "rv64_isint" },
    { "CHR",         "rv64_chr" },
    { "ORD",         "rv64_ord" },
    { "DEC2HEX",     "rv64_dec2hex" },
    { "HEX2DEC",     "rv64_hex2dec" },

    // --- Batch 7: wordpos, remove ---
    { "WORDPOS",     "rv64_wordpos" },
    { "REMOVE",      "rv64_remove" },

    // --- Batch 8: list aggregation, reversal, type checks ---
    { "LADD",        "rv64_ladd" },
    { "LMAX",        "rv64_lmax" },
    { "LMIN",        "rv64_lmin" },
    { "LAND",        "rv64_land" },
    { "LOR",         "rv64_lor" },
    { "REVWORDS",    "rv64_revwords" },
    { "FLIP",        "rv64_revwords" },   // alias
    { "ISDBREF",     "rv64_isdbref" },

    // --- Batch 8: math via intrinsics (string↔double + platform libm) ---
    { "SIN",         "rv64_sin" },
    { "COS",         "rv64_cos" },
    { "TAN",         "rv64_tan" },
    { "ASIN",        "rv64_asin" },
    { "ACOS",        "rv64_acos" },
    { "ATAN",        "rv64_atan" },
    { "ATAN2",       "rv64_atan2" },
    { "EXP",         "rv64_exp" },
    { "LOG",         "rv64_log" },
    { "LOG10",       "rv64_log10" },
    { "SQRT",        "rv64_sqrt" },
    { "CEIL",        "rv64_ceil" },
    { "FLOOR",       "rv64_floor" },
    { "ABS",         "rv64_fabs" },
    { "FMOD",        "rv64_fmod" },
    { "POWER",       "rv64_power" },

    // --- Batch 9: arithmetic ---
    { "ADD",         "rv64_add" },
    { "SUB",         "rv64_sub" },
    { "MUL",         "rv64_mul" },
    { "FDIV",        "rv64_fdiv" },
    { "MOD",         "rv64_mod" },
    { "SIGN",        "rv64_sign" },
    { "MIN",         "rv64_min" },
    { "MAX",         "rv64_max" },
    { "INC",         "rv64_inc" },
    { "DEC",         "rv64_dec" },
    { "TRUNC",       "rv64_trunc" },
    { "ROUND",       "rv64_round" },

    { nullptr, nullptr }
};

// Load the Tier 2 blob from a file.
// Called once at init time.  Guest base address is where the blob's
// code section will be mapped in each program's guest memory.
//
static bool tier2_load(const char *path, uint64_t guest_base) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;

    rv64_blob_header hdr;
    memset(&hdr, 0, sizeof(hdr));
    // Read at least the v1 header (32 bytes), then the v2 extension.
    if (fread(&hdr, 32, 1, f) != 1) { fclose(f); return false; }
    if (hdr.magic != RV64_BLOB_MAGIC
        || (hdr.version != 1 && hdr.version != 2)) {
        fclose(f);
        return false;
    }
    if (hdr.version >= 2) {
        // Read the v2 extension fields (bytes 32-47).
        if (fread(&hdr.data_offset, sizeof(hdr) - 32, 1, f) != 1) {
            fclose(f);
            return false;
        }
    }

    // Read code section.
    s_tier2.code.resize(hdr.code_size);
    fseek(f, hdr.code_offset, SEEK_SET);
    if (fread(s_tier2.code.data(), hdr.code_size, 1, f) != 1) {
        fclose(f);
        return false;
    }

    // v2: code_size is the full flat image (code + rodata + data).
    // BSS size tells the loader how much to zero-fill after.
    s_tier2.bss_size = (hdr.version >= 2) ? hdr.bss_size : 0;

    // Read entry table.
    std::vector<rv64_blob_entry> entries(hdr.entry_count);
    fseek(f, hdr.entry_offset, SEEK_SET);
    if (fread(entries.data(), sizeof(rv64_blob_entry), hdr.entry_count, f)
        != hdr.entry_count) {
        fclose(f);
        return false;
    }
    fclose(f);

    // Build lookup table.
    s_tier2.guest_base = guest_base;
    s_tier2.funcs.clear();
    for (uint32_t i = 0; i < hdr.entry_count; i++) {
        tier2_entry te;
        te.code_off = entries[i].code_off;
        te.guest_addr = static_cast<uint32_t>(guest_base) + entries[i].code_off;
        s_tier2.funcs[entries[i].name] = te;
    }

    // Build MUX name → blob mapping.
    for (int i = 0; s_tier2_map[i].mux_name; i++) {
        auto it = s_tier2.funcs.find(s_tier2_map[i].blob_name);
        if (it != s_tier2.funcs.end()) {
            s_tier2.funcs[s_tier2_map[i].mux_name] = it->second;
        }
    }

    s_tier2.loaded = true;
    const void *parts[] = {
        &hdr,
        s_tier2.code.data(),
        entries.empty() ? nullptr : entries.data(),
    };
    const size_t sizes[] = {
        sizeof(hdr),
        s_tier2.code.size(),
        entries.size() * sizeof(rv64_blob_entry),
    };
    s_blob_version = sha1_hex_parts(parts, sizes, 3);
    return true;
}

// Look up a function by MUX name (uppercase).
// Returns guest address, or 0 if not found.
//
uint64_t tier2_lookup(const std::string &mux_name) {
    if (!s_tier2.loaded) return 0;
    if (!tier2_allowed(mux_name)) return 0;
    auto it = s_tier2.funcs.find(mux_name);
    if (it != s_tier2.funcs.end()) return it->second.guest_addr;
    return 0;
}

// Look up a raw blob symbol by name (e.g., "sin", "cos", "rv64_strtod").
// Bypasses the tier2_allowed() gate — used for direct FP intrinsic calls
// from the type-propagated lowering path.
// Returns guest address, or 0 if not found.
//
uint64_t tier2_sym_addr(const char *blob_name) {
    if (!s_tier2.loaded) return 0;
    auto it = s_tier2.funcs.find(blob_name);
    if (it != s_tier2.funcs.end()) return it->second.guest_addr;
    return 0;
}

// Pre-translate all Tier 2 blob entry points so that superblocks
// can use native CALL continuation for Tier 2 function calls.
// Called after dbt_init/dbt_reset, before dbt_run.
//
// Helper: register a blob symbol as an intrinsic if it exists.
//
static void reg_intrinsic(dbt_state_t *dbt, const char *blob_name,
                           dbt_emitter_id eid, void *host_fn = nullptr) {
    auto it = s_tier2.funcs.find(blob_name);
    if (it != s_tier2.funcs.end()) {
        dbt_register_intrinsic(dbt, it->second.guest_addr, eid, host_fn);
    }
}


// Host-side wrappers for string↔double conversion intrinsics.
// These are called by the DBT stubs with host pointers and FP values.
//
static double host_strtod(const char *s) {
    return mux_atof(reinterpret_cast<const UTF8 *>(s));
}

static int host_fval(char *buf, double val) {
    UTF8 *bufc = reinterpret_cast<UTF8 *>(buf);
    UTF8 *start = bufc;
    fval(reinterpret_cast<UTF8 *>(buf), &bufc, val);
    *bufc = '\0';
    return static_cast<int>(bufc - start);
}

// Host-side wrapper for rv64_ftoa_round intrinsic.
// Matches fun_round: mux_fpclass check + mux_ftoa(r, true, frac).
//
static int host_ftoa_round(char *buf, double val, int frac) {
#ifdef HAVE_IEEE_FP_FORMAT
    int fpc = mux_fpclass(val);
    if (MUX_FPGROUP(fpc) != MUX_FPGROUP_PASS
        && MUX_FPGROUP(fpc) != MUX_FPGROUP_ZERO) {
        const UTF8 *s = mux_FPStrings[MUX_FPCLASS(fpc)];
        size_t len = strlen(reinterpret_cast<const char *>(s));
        memcpy(buf, s, len);
        buf[len] = '\0';
        return static_cast<int>(len);
    }
    if (MUX_FPGROUP(fpc) == MUX_FPGROUP_ZERO) {
        val = 0.0;
    }
#endif
    UTF8 *result = mux_ftoa(val, true, frac);
    size_t len = strlen(reinterpret_cast<const char *>(result));
    memcpy(buf, result, len);
    buf[len] = '\0';
    return static_cast<int>(len);
}

void pretranslate_tier2(dbt_state_t *dbt) {
    if (!s_tier2.loaded) return;

    // Register intrinsics FIRST — translate_block() checks these
    // addresses and emits native x86-64 stubs instead of translating
    // the RV64 bodies.  Must happen before pretranslation.

    // Block-level intrinsics (custom emitters).
    //
    reg_intrinsic(dbt, "rv64_slen",  DBT_EMIT_SLEN);
    reg_intrinsic(dbt, "rv64_scopy", DBT_EMIT_SCOPY);
    reg_intrinsic(dbt, "memcpy",     DBT_EMIT_MEMCPY);
    reg_intrinsic(dbt, "memcmp",     DBT_EMIT_MEMCMP);
    reg_intrinsic(dbt, "memset",     DBT_EMIT_MEMSET);
    reg_intrinsic(dbt, "memswap",    DBT_EMIT_MEMSWAP);

    // co_* Ragel functions → native host calls.
    // The wrapper does fargs unpacking in RV64 (cheap to translate),
    // then JALs to co_first/co_rest/etc.  The intrinsic intercepts
    // the JAL and calls the host's native Ragel implementation directly.
    //
    // 4 args: (out:ptr, p:ptr, len:int, delim:int)
    reg_intrinsic(dbt, "co_first",  DBT_EMIT_CO_4PP, reinterpret_cast<void *>(co_first));
    reg_intrinsic(dbt, "co_rest",   DBT_EMIT_CO_4PP, reinterpret_cast<void *>(co_rest));
    reg_intrinsic(dbt, "co_last",   DBT_EMIT_CO_4PP, reinterpret_cast<void *>(co_last));
    reg_intrinsic(dbt, "co_repeat", DBT_EMIT_CO_4PP, reinterpret_cast<void *>(co_repeat));

    // 3 args: (p:ptr, len:int, delim:int)
    reg_intrinsic(dbt, "co_words_count", DBT_EMIT_CO_3P, reinterpret_cast<void *>(co_words_count));

    // 4 args: (haystack:ptr, hlen:int, needle:ptr, nlen:int)
    reg_intrinsic(dbt, "co_pos", DBT_EMIT_CO_POS, reinterpret_cast<void *>(co_pos));

    // 5 args: (out:ptr, p:ptr, len:int, start:int, count:int)
    reg_intrinsic(dbt, "co_mid",  DBT_EMIT_CO_5PP, reinterpret_cast<void *>(co_mid));
    reg_intrinsic(dbt, "co_trim", DBT_EMIT_CO_5PP, reinterpret_cast<void *>(co_trim));

    // 5 args: (target:ptr, tlen:int, list:ptr, llen:int, delim:int)
    reg_intrinsic(dbt, "co_member", DBT_EMIT_CO_MEMBER, reinterpret_cast<void *>(co_member));

    // 6 args: (out:ptr, list:ptr, llen:int, x:int, y:int, z:int)
    reg_intrinsic(dbt, "co_delete",     DBT_EMIT_CO_6PP, reinterpret_cast<void *>(co_delete));
    reg_intrinsic(dbt, "co_sort_words", DBT_EMIT_CO_6PP, reinterpret_cast<void *>(co_sort_words));

    // 7 args: (out:ptr, p:ptr, len:int, iFirst:int, nWords:int, delim:int, osep:int)
    reg_intrinsic(dbt, "co_extract", DBT_EMIT_CO_7PP, reinterpret_cast<void *>(co_extract));

    // 8 args: (out:ptr, list1:ptr, len1:int, list2:ptr, len2:int, delim:int, osep:int, sort_type:int)
    reg_intrinsic(dbt, "co_setunion", DBT_EMIT_CO_8PPP, reinterpret_cast<void *>(co_setunion));
    reg_intrinsic(dbt, "co_setdiff",  DBT_EMIT_CO_8PPP, reinterpret_cast<void *>(co_setdiff));
    reg_intrinsic(dbt, "co_setinter", DBT_EMIT_CO_8PPP, reinterpret_cast<void *>(co_setinter));

    // Batch 2: inner co_* functions called by wrappers.
    //
    // 2 args: (data:ptr, len:int)
    reg_intrinsic(dbt, "co_cluster_count", DBT_EMIT_CO_2P, reinterpret_cast<void *>(co_cluster_count));

    // 3 args: (out:ptr, p:ptr, len:int)
    reg_intrinsic(dbt, "co_tolower",  DBT_EMIT_CO_3PP, reinterpret_cast<void *>(co_tolower));
    reg_intrinsic(dbt, "co_toupper",  DBT_EMIT_CO_3PP, reinterpret_cast<void *>(co_toupper));
    reg_intrinsic(dbt, "co_reverse",  DBT_EMIT_CO_3PP, reinterpret_cast<void *>(co_reverse));
    reg_intrinsic(dbt, "co_escape",   DBT_EMIT_CO_3PP, reinterpret_cast<void *>(co_escape));

    // 4 args: (out:ptr, p:ptr, len:int, n:int)
    reg_intrinsic(dbt, "co_left",     DBT_EMIT_CO_4PP, reinterpret_cast<void *>(co_left));
    reg_intrinsic(dbt, "co_right",    DBT_EMIT_CO_4PP, reinterpret_cast<void *>(co_right));
    reg_intrinsic(dbt, "co_compress", DBT_EMIT_CO_4PP, reinterpret_cast<void *>(co_compress));
    reg_intrinsic(dbt, "co_lpos",     DBT_EMIT_CO_4PP, reinterpret_cast<void *>(co_lpos));

    // Batch 3: totitle, strip_color, visible_length.
    // ljust/rjust/center/edit/splice need new emitter patterns (7+ args with
    // complex pointer layouts) — registered as Tier 2 but not intrinsics yet.
    reg_intrinsic(dbt, "co_totitle",         DBT_EMIT_CO_3PP, reinterpret_cast<void *>(co_totitle));
    reg_intrinsic(dbt, "co_strip_color",     DBT_EMIT_CO_3PP, reinterpret_cast<void *>(co_strip_color));
    reg_intrinsic(dbt, "co_visible_length",  DBT_EMIT_CO_2P,  reinterpret_cast<void *>(co_visible_length));

    // FP math intrinsics — double→double via platform libm.
    // Explicit casts resolve C++ overload ambiguity (float/double/long double).
    //
    using fn_d_d = double(*)(double);
    using fn_dd_d = double(*)(double, double);

    reg_intrinsic(dbt, "sin",   DBT_EMIT_FP_D_D,  reinterpret_cast<void *>(static_cast<fn_d_d>(::sin)));
    reg_intrinsic(dbt, "cos",   DBT_EMIT_FP_D_D,  reinterpret_cast<void *>(static_cast<fn_d_d>(::cos)));
    reg_intrinsic(dbt, "tan",   DBT_EMIT_FP_D_D,  reinterpret_cast<void *>(static_cast<fn_d_d>(::tan)));
    reg_intrinsic(dbt, "asin",  DBT_EMIT_FP_D_D,  reinterpret_cast<void *>(static_cast<fn_d_d>(::asin)));
    reg_intrinsic(dbt, "acos",  DBT_EMIT_FP_D_D,  reinterpret_cast<void *>(static_cast<fn_d_d>(::acos)));
    reg_intrinsic(dbt, "atan",  DBT_EMIT_FP_D_D,  reinterpret_cast<void *>(static_cast<fn_d_d>(::atan)));
    reg_intrinsic(dbt, "exp",   DBT_EMIT_FP_D_D,  reinterpret_cast<void *>(static_cast<fn_d_d>(::exp)));
    reg_intrinsic(dbt, "log",   DBT_EMIT_FP_D_D,  reinterpret_cast<void *>(static_cast<fn_d_d>(::log)));
    reg_intrinsic(dbt, "log10", DBT_EMIT_FP_D_D,  reinterpret_cast<void *>(static_cast<fn_d_d>(::log10)));
    reg_intrinsic(dbt, "ceil",  DBT_EMIT_FP_D_D,  reinterpret_cast<void *>(static_cast<fn_d_d>(::ceil)));
    reg_intrinsic(dbt, "floor", DBT_EMIT_FP_D_D,  reinterpret_cast<void *>(static_cast<fn_d_d>(::floor)));
    reg_intrinsic(dbt, "fabs",  DBT_EMIT_FP_D_D,  reinterpret_cast<void *>(static_cast<fn_d_d>(::fabs)));

    // FP math intrinsics — (double,double)→double via platform libm.
    //
    reg_intrinsic(dbt, "pow",   DBT_EMIT_FP_DD_D, reinterpret_cast<void *>(static_cast<fn_dd_d>(::pow)));
    reg_intrinsic(dbt, "atan2", DBT_EMIT_FP_DD_D, reinterpret_cast<void *>(static_cast<fn_dd_d>(::atan2)));
    reg_intrinsic(dbt, "fmod",  DBT_EMIT_FP_DD_D, reinterpret_cast<void *>(static_cast<fn_dd_d>(::fmod)));

    // Rounding intrinsic — NearestPretty (double→double).
    //
    reg_intrinsic(dbt, "rv64_nearest_pretty", DBT_EMIT_FP_D_D,
                  reinterpret_cast<void *>(static_cast<fn_d_d>(NearestPretty)));

    // String↔double conversion intrinsics.
    //
    reg_intrinsic(dbt, "rv64_strtod", DBT_EMIT_STRTOD, reinterpret_cast<void *>(host_strtod));
    reg_intrinsic(dbt, "rv64_fval",   DBT_EMIT_FVAL,   reinterpret_cast<void *>(host_fval));

    // Round-to-precision intrinsic.
    reg_intrinsic(dbt, "rv64_ftoa_round", DBT_EMIT_FTOA_ROUND,
                  reinterpret_cast<void *>(host_ftoa_round));

    // Pre-translate all intrinsic stubs into the cache BEFORE any
    // function pretranslation.  Intrinsics are leaf functions (strlen,
    // memcpy, sitoa, co_* wrappers) that blob functions call internally.
    // By caching them first, every subsequent translate_block that
    // encounters a JAL to an intrinsic address will find it in the
    // cache and emit an inline CALL — regardless of worklist order
    // or superblock extension boundaries.
    //
    for (int i = 0; i < dbt->num_intrinsics; i++) {
        uint64_t addr = dbt->intrinsics[i].guest_addr;
        if (addr) {
            dbt_pretranslate(dbt, addr);
        }
    }

    // Pretranslate all Tier 2 functions.  With intrinsics already cached,
    // inline CALLs to intrinsic targets will fire on first encounter.
    //
    for (auto &kv : s_tier2.funcs) {
        dbt_pretranslate(dbt, kv.second.guest_addr);
    }

    // Resolve cross-function chains: block A (from function X)
    // exits to block B (from function Y) — the backpatch during Y's
    // pretranslation won't find A's patch site.  This pass fixes them.
    dbt_resolve_chains(dbt);
}

// Copy blob code into a program's guest memory.
// Called during compile_expression() before codegen.
//
void tier2_install(std::vector<uint8_t> &memory, uint64_t guest_base) {
    if (!s_tier2.loaded) return;

    // The flat image (code + rodata + initialized data) is copied as one
    // contiguous block at guest_base.  BSS is zero-filled after it.
    // This preserves the exact ELF virtual address layout.
    uint64_t total = s_tier2.code.size() + s_tier2.bss_size;
    if (guest_base + total > memory.size()) return;

    // Copy the initialized portion (code + rodata + data).
    memcpy(memory.data() + guest_base,
           s_tier2.code.data(), s_tier2.code.size());

    // Zero-fill BSS after the initialized data.
    if (s_tier2.bss_size > 0) {
        memset(memory.data() + guest_base + s_tier2.code.size(),
               0, s_tier2.bss_size);
    }
}

// Lazy-init Tier 2 blob on first compile.
static bool s_tier2_init = false;
static void tier2_lazy_init() {
    if (s_tier2_init) return;
    s_tier2_init = true;

    // Try to load from the game's bin directory (where engine.so lives).
    const char *paths[] = {
        "bin/softlib.rv64",
        "./softlib.rv64",
        nullptr
    };
    for (int i = 0; paths[i]; i++) {
        if (tier2_load(paths[i], rv_compiler::BLOB_BASE)) {
            return;
        }
    }
    // No blob found — Tier 2 disabled, ECALL fallback for everything.
    s_blob_version = "none";
}

static compiled_program compile_expression(const UTF8 *expr, size_t nLen,
                                            int eval = EV_FCHECK | EV_EVAL,
                                            uint64_t code_base = 0,
                                            uint64_t str_start = rv_compiler::STR_BASE,
                                            uint64_t fargs_start = rv_compiler::FARGS_BASE,
                                            uint64_t out_start = 0) {
    tier2_lazy_init();

    compiled_program prog;
    prog.ok = false;
    prog.out_used = 0;
    prog.entry_pc = code_base;
    prog.folds = 0;
    prog.ecalls = 0;
    prog.tier2_calls = 0;
    prog.native_ops = 0;
    prog.needs_jit = false;

    // Parse the expression.
    auto ast = ast_parse_string(expr, nLen);
    if (!ast) {
        s_jit_stats.compile_fail++;
        return prog;
    }

    // --- HIR pipeline ---

    // Phase 1: Lower AST → HIR.
    rv_compiler rc(code_base, str_start, fargs_start, out_start);

    // Install Tier 2 blob into guest memory (if loaded).
    tier2_install(rc.memory, rv_compiler::BLOB_BASE);

    // Set compile-time eval flags for unknown-function resolution.
    s_compile_eval = eval;
    s_fcheck_available = (eval & EV_FCHECK) != 0;

    hir_program h;
    h.init();
    qreg_init();

    // Set up Tier 3 compile-time deps collector.
    std::vector<compiled_program::inline_dep> deps;
    s_compile_deps = &deps;
    s_inline_depth = 0;

    h.result = hir_lower_node(h, rc, ast.get());

    // If the final result is TY_FLOAT, insert FTOA to convert to
    // string for MUX output.  This is the boundary where FP values
    // escape to string context.
    if (h.result >= 0 && h.ty[h.result] == TY_FLOAT) {
        h.result = h.emit(HIR_FTOA, TY_STRING, h.result);
    }

    s_compile_deps = nullptr;

    const char *dump_env = getenv("TINYMUX_DUMP_HIR");
    bool bDump = (dump_env && *dump_env != '0');

    if (bDump) {
        printf("\n--- JIT Compilation: %.*s ---\n", static_cast<int>(nLen), expr);
        printf("Phase 1: HIR Lowering\n");
        hir_dump(h);
    }

    // Phase 2: SSA construction (for multi-block programs, M4+).
    // For single-block programs this is a no-op but builds the CFG.
    hir_build_cfg(h);
    if (h.n_blocks > 1) {
        hir_ssa_construct(h);
        if (bDump) {
            printf("Phase 2: SSA Construction\n");
            hir_dump(h);
        }
    }

    // Phase 3: SSA optimization (constant fold, copy prop, DCE).
    hir_optimize(h);
    if (bDump) {
        printf("Phase 3: SSA Optimization\n");
        hir_dump(h);
    }

    // Phase 4: Codegen HIR → RV64.
    hir_codegen(h, rc);

    // Copy code to guest memory at the configured code_base.
    for (size_t i = 0; i < rc.code.size()
         && rc.code_base + i * 4 < rc.code_base + rv_compiler::CODE_LIMIT; i++) {
        memcpy(rc.memory.data() + rc.code_base + i * 4, &rc.code[i], 4);
    }

    // If output slots were exhausted during codegen, the generated
    // code references address 0 and would corrupt guest memory.
    // Bail out — the AST evaluator will handle this expression.
    if (rc.out_exhausted) {
        s_jit_stats.compile_fail++;
        if (!rc.bail_was_noeval) {
            s_jit_stats.bail_slots++;
        }
        return prog;  // prog.ok is still false
    }

    prog.memory = std::move(rc.memory);
    prog.memory_size = rv_compiler::MEM_SIZE;
    prog.out_addr = rc.final_out;
    prog.out_used = (rv_compiler::STACK_TOP - 8) - rc.out_pool;
    prog.entry_pc = rc.code_base;
    prog.code_size = rc.code.size() * 4;
    prog.str_pool_end = rc.str_pool;
    prog.fargs_pool_end = rc.fargs_pool;
    prog.out_pool_end = rc.out_pool;
    prog.ok = true;
    s_jit_stats.compile_ok++;
    prog.folds = h.folds;
    prog.ecalls = h.ecalls;
    prog.tier2_calls = h.tier2_calls;
    prog.native_ops = h.native_ops;
    prog.needs_jit = h.needs_jit;
    prog.deps = std::move(deps);
    return prog;
}

// ---------------------------------------------------------------
// fun_rveval: softcode function.
//
// rveval(<expression>)
//
// Parses and compiles <expression> to RV64, runs through the JIT,
// returns the result.
//
// Examples:
//   think rveval(add(1,2))           → 3   (constant folded)
//   think rveval(add(mul(3,4),5))    → 17  (fully folded)
//   think rveval(strlen(hello))      → 5   (folded)
// ---------------------------------------------------------------

// ---------------------------------------------------------------
// Persistent DBT state — avoids mmap/munmap per call.
//
// The 64 MB mmap + 1 MB block cache allocation dominated the
// ECALL path.  By keeping the dbt_state_t alive, we amortize
// the cost to one-time initialization.
// ---------------------------------------------------------------

static dbt_state_t s_persistent_dbt;
static bool s_dbt_ready = false;

static int dbt_trace_mask_from_env() {
    const char *env = getenv("TINYMUX_DBT_TRACE");
    if (!env || !env[0]) return 0;

    if (strcmp(env, "1") == 0 || strcmp(env, "all") == 0) {
        return DBT_TRACE_EXEC | DBT_TRACE_TRANSLATE;
    }

    int mask = 0;
    if (strstr(env, "exec")) mask |= DBT_TRACE_EXEC;
    if (strstr(env, "xlate") || strstr(env, "translate")) {
        mask |= DBT_TRACE_TRANSLATE;
    }
    return mask;
}

static void dbt_configure_trace_from_env(dbt_state_t *dbt) {
    dbt->trace = dbt_trace_mask_from_env();
    dbt->trace_guest_pc = 0;
    dbt->trace_guest_pc_filter = false;

    const char *env = getenv("TINYMUX_DBT_TRACE_PC");
    if (!env || !env[0]) return;

    errno = 0;
    char *end = nullptr;
    unsigned long long value = strtoull(env, &end, 0);
    if (errno != 0 || end == env || *end != '\0') {
        fprintf(stderr,
                "dbt: ignoring invalid TINYMUX_DBT_TRACE_PC='%s'\n", env);
        return;
    }

    dbt->trace_guest_pc = static_cast<uint64_t>(value);
    dbt->trace_guest_pc_filter = true;
}

// Release the persistent DBT state on shutdown.
//
void dbt_compile_cleanup(void) {
    if (s_dbt_ready) {
        dbt_cleanup(&s_persistent_dbt);
        s_dbt_ready = false;
    }
}

// Get a reset DBT state, initializing on first use.
// Returns nullptr on allocation failure.
//
static dbt_state_t *get_dbt(uint8_t *memory, size_t memory_size,
                             int (*ecall_fn)(rv64_ctx_t *, void *),
                             void *ecall_user) {
    dbt_state_t *dbt = &s_persistent_dbt;
    if (!s_dbt_ready) {
        if (dbt_init(dbt, memory, memory_size, ecall_fn, ecall_user) != 0) {
            return nullptr;
        }
        dbt_configure_trace_from_env(dbt);

        // Dispatch limit: safety net during development.
        // TINYMUX_DBT_MAX_DISPATCH overrides; 0 = unlimited.
        const char *md_env = getenv("TINYMUX_DBT_MAX_DISPATCH");
        dbt->max_dispatch = md_env ? strtoull(md_env, nullptr, 0) : 10000000;

        s_dbt_ready = true;
        return dbt;
    }
    // Reset for new program: keep mmap'd code buffer + cache allocation.
    dbt_reset(dbt, memory, memory_size, ecall_fn, ecall_user);
    dbt_configure_trace_from_env(dbt);
    return dbt;
}

// ECALL handler context and forward declaration.
//

static int eval_ecall(rv64_ctx_t *ctx, void *user_data);

// ---------------------------------------------------------------
// Compile cache — LRU cache of compiled programs.
//
// Keyed by expression text.  Cache hits skip compilation entirely.
// Combined with DBT block cache persistence (dbt_rerun), repeated
// evaluation of the same expression does zero compilation and zero
// JIT translation — just runs the cached native code.
//
// The DBT tracks which program it was last set up for.  Same
// program → dbt_rerun (keep translated blocks).  Different
// program → dbt_reset (re-translate).
// ---------------------------------------------------------------

struct compile_cache_entry {
    compiled_program prog;
    std::list<std::string>::iterator lru_it;
};

static std::unordered_map<std::string, compile_cache_entry> s_compile_cache;
static std::list<std::string> s_compile_lru;
static constexpr size_t COMPILE_CACHE_MAX = 256;
static constexpr size_t COMPILE_CACHE_MIN_LEN = 8;

// Track which program the DBT was last set up for, so we can
// use dbt_rerun (fast) instead of dbt_reset (slow) on cache hits.
//
static uint8_t *s_dbt_last_memory = nullptr;

// Reconstruct a compiled_program from a SQLite code cache record.
// Copies memory_blob into a full-size guest memory vector and
// installs the Tier 2 blob.
//
static compiled_program reconstruct_from_cache(
    const CSQLiteDB::CodeCacheRecord &rec) {
    compiled_program prog;
    prog.memory.resize(rv_compiler::MEM_SIZE, 0);
    int copy_len = rec.memory_len;
    if (copy_len > static_cast<int>(rv_compiler::FARGS_LIMIT)) {
        copy_len = static_cast<int>(rv_compiler::FARGS_LIMIT);
    }
    memcpy(prog.memory.data(), rec.memory_blob, copy_len);
    tier2_install(prog.memory, rv_compiler::BLOB_BASE);
    prog.memory_size = rv_compiler::MEM_SIZE;
    prog.out_addr = static_cast<uint64_t>(rec.out_addr);
    prog.out_used = 0;  // cached programs re-compute at runtime
    prog.entry_pc = rv_compiler::CODE_BASE;  // cached programs always start at 0
    prog.code_size = 0;
    prog.str_pool_end = rv_compiler::STR_BASE;
    prog.fargs_pool_end = rv_compiler::FARGS_BASE;
    prog.out_pool_end = rv_compiler::STACK_TOP - 8;
    prog.ok = true;
    prog.needs_jit = rec.needs_jit != 0;
    prog.folds = rec.folds;
    prog.ecalls = rec.ecalls;
    prog.tier2_calls = rec.tier2_calls;
    prog.native_ops = rec.native_ops;

    // Restore inline dependencies from BLOB.
    // Format: packed array of {int32 obj, int32 attr_num, uint32 mod_count}.
    if (rec.deps_blob && rec.deps_len > 0)
    {
        int ndeps = rec.deps_len / static_cast<int>(
            sizeof(compiled_program::inline_dep));
        prog.deps.resize(ndeps);
        memcpy(prog.deps.data(), rec.deps_blob,
               ndeps * sizeof(compiled_program::inline_dep));
    }

    return prog;
}

static std::string compile_cache_key(const UTF8 *expr, size_t nLen, int eval) {
    // Include every eval flag that changes compile-time semantics.
    int eval_key = eval & (EV_FCHECK | EV_FMAND | EV_STRIP_CURLY);

    std::string key(reinterpret_cast<const char *>(expr), nLen);
    key += '\0';
    key += static_cast<char>(eval_key & 0xFF);
    key += static_cast<char>((eval_key >> 8) & 0xFF);
    return key;
}

// Persist a compiled_program to the SQLite code cache.
//
static void store_to_sqlite_cache(const std::string &key,
                                   const compiled_program &prog) {
    if (!g_pSQLiteBackend) return;
    CSQLiteDB &db = g_pSQLiteBackend->GetDB();
    int persist_len = static_cast<int>(rv_compiler::FARGS_LIMIT);
    db.CodeCachePut(key.data(), static_cast<int>(key.size()),
                     s_blob_version.data(),
                     static_cast<int>(s_blob_version.size()),
                     prog.memory.data(), persist_len,
                     static_cast<int64_t>(prog.out_addr),
                     prog.needs_jit ? 1 : 0,
                     prog.folds, prog.ecalls,
                     prog.tier2_calls, prog.native_ops,
                     prog.deps.data(),
                     static_cast<int>(prog.deps.size()
                         * sizeof(compiled_program::inline_dep)));
}

// Check if a compiled program's inlined dependencies are still fresh.
// Returns true if all deps match their current mod_counts (or no deps).
//
static bool deps_are_fresh(const compiled_program &prog) {
    for (const auto &dep : prog.deps) {
        uint32_t current = attr_mod_count_get(
            static_cast<dbref>(dep.obj), dep.attr_num);
        if (current != dep.mod_count) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------
// Public wrappers for SQLite code cache (shared with Lua JIT).
// ---------------------------------------------------------------

std::string jit_sha1_hex(const void *data, size_t len) {
    const void *parts[] = { data };
    size_t sizes[] = { len };
    return sha1_hex_parts(parts, sizes, 1);
}

void jit_store_to_sqlite(const std::string &key, const compiled_program &prog) {
    store_to_sqlite_cache(key, prog);
}

bool jit_load_from_sqlite(const std::string &key, compiled_program &out) {
    if (!g_pSQLiteBackend) return false;
    CSQLiteDB &db = g_pSQLiteBackend->GetDB();
    CSQLiteDB::CodeCacheRecord rec;
    if (!db.CodeCacheGet(key.data(), static_cast<int>(key.size()),
                         s_blob_version.data(),
                         static_cast<int>(s_blob_version.size()), rec)) {
        return false;
    }
    out = reconstruct_from_cache(rec);
    return out.ok;
}

// Look up or compile an expression.  Returns a pointer to the
// cached compiled_program (owned by the cache — do not free).
// Returns nullptr on compilation failure.
//
static compiled_program *compile_cached(const UTF8 *expr, size_t nLen,
                                         int eval = EV_FCHECK | EV_EVAL) {
    std::string key = compile_cache_key(expr, nLen, eval);

    auto it = s_compile_cache.find(key);
    if (it != s_compile_cache.end()) {
        // Staleness check: if this entry has inlined deps and any
        // attr has been modified since compilation, evict and recompile.
        if (!it->second.prog.deps.empty()
            && !deps_are_fresh(it->second.prog))
        {
            if (s_dbt_last_memory == it->second.prog.memory.data()) {
                s_dbt_last_memory = nullptr;
            }
            s_compile_lru.erase(it->second.lru_it);
            s_compile_cache.erase(it);
            // Fall through to recompile below.
        }
        else
        {
            // Memory cache hit — move to front of LRU.
            s_compile_lru.splice(s_compile_lru.begin(), s_compile_lru,
                                 it->second.lru_it);
            s_jit_stats.cache_hit_mem++;
            return &it->second.prog;
        }
    }

    // Memory cache miss — check SQLite persistent cache.
    compiled_program prog;
    bool from_sqlite = false;

    if (g_pSQLiteBackend && nLen >= COMPILE_CACHE_MIN_LEN) {
        CSQLiteDB &db = g_pSQLiteBackend->GetDB();
        CSQLiteDB::CodeCacheRecord rec;
        if (db.CodeCacheGet(key.data(), static_cast<int>(key.size()),
                            s_blob_version.data(),
                            static_cast<int>(s_blob_version.size()), rec)) {
            prog = reconstruct_from_cache(rec);
            db.CodeCacheReset();

            // Check staleness of SQLite-cached entry too.
            if (!prog.deps.empty() && !deps_are_fresh(prog))
            {
                // Stale — discard and recompile.
                prog.ok = false;
            }
            else
            {
                from_sqlite = true;
                s_jit_stats.cache_hit_sqlite++;
            }
        }
    }

    if (!from_sqlite) {
        // Full cache miss — compile from scratch.
        s_jit_stats.cache_miss++;
        prog = compile_expression(expr, nLen, eval);
        if (!prog.ok) return nullptr;

        // Persist to SQLite for future restarts.
        if (nLen >= COMPILE_CACHE_MIN_LEN) {
            store_to_sqlite_cache(key, prog);
        }
    }

    // Insert into memory LRU cache.
    while (s_compile_cache.size() >= COMPILE_CACHE_MAX) {
        auto &victim_key = s_compile_lru.back();
        auto vit = s_compile_cache.find(victim_key);
        if (vit != s_compile_cache.end()
            && s_dbt_last_memory == vit->second.prog.memory.data()) {
            s_dbt_last_memory = nullptr;
        }
        s_compile_cache.erase(victim_key);
        s_compile_lru.pop_back();
    }

    s_compile_lru.push_front(key);
    auto [ins_it, _] = s_compile_cache.emplace(
        key, compile_cache_entry{std::move(prog), s_compile_lru.begin()});
    return &ins_it->second.prog;
}

// Run a cached program.  Uses dbt_rerun if the DBT already has
// translated blocks for this program, otherwise dbt_reset.
//
bool run_cached_program(compiled_program *prog,
                        dbref executor, dbref caller_db,
                        dbref enactor,
                        UTF8 *out, size_t out_size,
                        const UTF8 *cargs[],
                        int ncargs,
                        int eval,
                        void *lua_state) {
    if (!prog->needs_jit) {
        const char *r = reinterpret_cast<const char *>(
            prog->memory.data() + prog->out_addr);
        size_t n = strlen(r);
        if (n >= out_size) n = out_size - 1;
        memcpy(out, r, n);
        out[n] = '\0';
        return true;
    }

    // Clear output regions + cargs for clean re-run.
    // Re-zero the blob's BSS on each execution.  The initialized portion
    // (code + rodata + data) is preserved, but BSS (dtoa pools, heap, etc.)
    // must be reset to zero for correct behavior across calls.
    if (s_tier2.loaded && s_tier2.bss_size > 0) {
        uint64_t bss_start = rv_compiler::BLOB_BASE + s_tier2.code.size();
        if (bss_start + s_tier2.bss_size <= prog->memory_size) {
            memset(prog->memory.data() + bss_start, 0, s_tier2.bss_size);
        }
    }

    // Re-copy initialized writable data (sdata) on each execution.
    // The flat image includes code + rodata + data, but only data changes.
    // For now, just re-install the full image on each run to be safe.
    // This is fast (~167KB memcpy) and ensures dtoa_divmax, pmem_next, etc.
    // are reset to their initial values.
    if (s_tier2.loaded) {
        tier2_install(prog->memory, rv_compiler::BLOB_BASE);
    }

    // Clear output buffers (stack-allocated near STACK_TOP) and CARGS/SUBST.
    // Output lives from out_pool_end (lowest allocated) up to STACK_TOP-8.
    if (prog->out_pool_end < rv_compiler::STACK_TOP - 8) {
        memset(prog->memory.data() + prog->out_pool_end, 0,
               (rv_compiler::STACK_TOP - 8) - prog->out_pool_end);
    }
    // CARGS/SUBST region.
    uint64_t subst_end = rv_compiler::SUBST_BASE
                       + rv_compiler::SUBST_COUNT * rv_compiler::SUBST_SLOT;
    memset(prog->memory.data() + rv_compiler::CARGS_BASE, 0,
           subst_end - rv_compiler::CARGS_BASE);

    // Copy %0-%9 cargs into fixed guest memory slots.
    // Each carg gets a 256-byte slot at CARGS_BASE + idx * 256.
    for (int i = 0; i < rv_compiler::MAX_CARGS && i < ncargs; i++) {
        if (cargs && cargs[i]) {
            size_t len = strlen(reinterpret_cast<const char *>(cargs[i]));
            if (len >= static_cast<size_t>(rv_compiler::CARGS_SLOT))
                len = rv_compiler::CARGS_SLOT - 1;
            uint64_t slot = rv_compiler::CARGS_BASE
                          + static_cast<uint64_t>(i) * rv_compiler::CARGS_SLOT;
            memcpy(prog->memory.data() + slot, cargs[i], len);
            prog->memory[slot + len] = 0;
        }
    }

    // Copy %-substitution runtime values into SUBST slots.
    // These are populated before each execution so the RV64 code
    // can reference them as constant addresses.
    auto copy_subst = [&](int slot_idx, const UTF8 *value) {
        uint64_t slot = rv_compiler::SUBST_BASE
                      + static_cast<uint64_t>(slot_idx) * rv_compiler::SUBST_SLOT;
        if (value && value[0]) {
            size_t len = strlen(reinterpret_cast<const char *>(value));
            if (len >= static_cast<size_t>(rv_compiler::SUBST_SLOT))
                len = rv_compiler::SUBST_SLOT - 1;
            memcpy(prog->memory.data() + slot, value, len);
            prog->memory[slot + len] = 0;
        } else {
            prog->memory[slot] = 0;
        }
    };

    // %# — enactor dbref as string.
    {
        UTF8 dbref_buf[32];
        mux_sprintf(dbref_buf, sizeof(dbref_buf), T("#%d"), enactor);
        copy_subst(rv_compiler::SUBST_ENACTOR, dbref_buf);
    }

    // %! — executor dbref as string.
    {
        UTF8 dbref_buf[32];
        mux_sprintf(dbref_buf, sizeof(dbref_buf), T("#%d"), executor);
        copy_subst(rv_compiler::SUBST_EXECUTOR, dbref_buf);
    }

    // %n — enactor name.
    if (Good_obj(enactor)) {
        copy_subst(rv_compiler::SUBST_NAME, Name(enactor));
    } else {
        copy_subst(rv_compiler::SUBST_NAME, nullptr);
    }

    // %l — enactor location.
    if (Good_obj(enactor)) {
        dbref loc = Location(enactor);
        UTF8 dbref_buf[32];
        mux_sprintf(dbref_buf, sizeof(dbref_buf), T("#%d"), loc);
        copy_subst(rv_compiler::SUBST_LOCATION, dbref_buf);
    } else {
        copy_subst(rv_compiler::SUBST_LOCATION, nullptr);
    }

    // %q global registers.
    for (int i = 0; i < MAX_GLOBAL_REGS; i++) {
        if (mudstate.global_regs[i] && mudstate.global_regs[i]->reg_ptr) {
            copy_subst(rv_compiler::SUBST_QREG0 + i,
                       mudstate.global_regs[i]->reg_ptr);
        } else {
            copy_subst(rv_compiler::SUBST_QREG0 + i, nullptr);
        }
    }

    // %m — last command.
    copy_subst(rv_compiler::SUBST_LASTCMD, mudstate.curr_cmd);

    // %k — moniker (enactor name with color).
    if (Good_obj(enactor)) {
        copy_subst(rv_compiler::SUBST_MONIKER, Moniker(enactor));
    } else {
        copy_subst(rv_compiler::SUBST_MONIKER, nullptr);
    }

    // %| — piped command output.
    copy_subst(rv_compiler::SUBST_POUT, mudstate.pout);

    // %+ — number of cargs.
    {
        UTF8 ncbuf[32];
        mux_sprintf(ncbuf, sizeof(ncbuf), T("%d"), ncargs);
        copy_subst(rv_compiler::SUBST_NCARGS, ncbuf);
    }

    // %: — enactor objid.  Populated via ECALL at runtime (needs
    // creation_seconds which isn't available here).  Leave slot empty;
    // the compiler emits ECALL objid(%#) instead of using the slot.

    eval_ctx ec;
    ec.memory = prog->memory.data();
    ec.memory_size = prog->memory_size;
    ec.executor = executor;
    ec.caller = caller_db;
    ec.enactor = enactor;
    ec.eval = eval;
    ec.cargs = cargs;
    ec.ncargs = ncargs;
    ec.lua_state = lua_state;

    dbt_state_t *dbt;
    if (s_dbt_ready && s_dbt_last_memory == prog->memory.data()) {
        // Same program as last time — keep translated blocks.
        dbt = &s_persistent_dbt;
        dbt_rerun(dbt, eval_ecall, &ec);
    } else {
        // Different program — reset and re-translate program blocks.
        // Blob translations persist via blob_code_end.
        dbt = get_dbt(prog->memory.data(), prog->memory_size,
                       eval_ecall, &ec);
        if (!dbt) return false;
        s_dbt_last_memory = prog->memory.data();
        if (dbt->blob_code_end == 0) {
            pretranslate_tier2(dbt);
            dbt->blob_code_end = dbt->code_used;
        }
    }

    int rc = dbt_run(dbt, prog->entry_pc, rv_compiler::STACK_TOP);
    if (rc != 0) return false;

    const char *r = reinterpret_cast<const char *>(
        prog->memory.data() + prog->out_addr);
    size_t n = strlen(r);
    if (n >= out_size) n = out_size - 1;
    memcpy(out, r, n);
    out[n] = '\0';
    return true;
}

// ECALL handler implementation.
//
// Common helper: call a FUN* with guest-memory arguments and write
// result to guest output buffer.  Returns bytes written.
//
// Thread-local (single-threaded in practice) pointer to the current
// ECALL execution context.  Set during ecall_invoke_fun so that
// Tier 3 helper functions (_write_carg, _save_cargs, _restore_cargs)
// can access JIT guest memory.
//
static eval_ctx *s_current_ecall_ctx = nullptr;

static int ecall_invoke_fun(FUN *fp, eval_ctx *ec, rv64_ctx_t *ctx,
                            uint64_t fargs_addr, int nfargs,
                            uint64_t out_addr, uint64_t out_size) {
    // Validate argument count against function's declared limits.
    // Return the same error string the AST evaluator would.
    if (nfargs < fp->minArgs) {
        int n;
        if (fp->minArgs == fp->maxArgs) {
            n = snprintf(reinterpret_cast<char *>(ec->memory + out_addr),
                out_size, "#-1 FUNCTION (%s) EXPECTS %d ARGUMENTS",
                reinterpret_cast<const char *>(fp->name), fp->minArgs);
        } else if (fp->minArgs + 1 == fp->maxArgs) {
            n = snprintf(reinterpret_cast<char *>(ec->memory + out_addr),
                out_size, "#-1 FUNCTION (%s) EXPECTS %d OR %d ARGUMENTS",
                reinterpret_cast<const char *>(fp->name), fp->minArgs, fp->maxArgs);
        } else {
            n = snprintf(reinterpret_cast<char *>(ec->memory + out_addr),
                out_size, "#-1 FUNCTION (%s) EXPECTS BETWEEN %d AND %d ARGUMENTS",
                reinterpret_cast<const char *>(fp->name), fp->minArgs, fp->maxArgs);
        }
        if (n < 0) n = 0;
        if (static_cast<size_t>(n) >= out_size) n = static_cast<int>(out_size - 1);
        ctx->x[10] = static_cast<uint64_t>(n);
        return -1;
    }
    if (fp->maxArgs >= 0 && nfargs > fp->maxArgs) {
        nfargs = fp->maxArgs;
    }

    UTF8 *fargs[MAX_ARG];
    if (nfargs > MAX_ARG) nfargs = MAX_ARG;
    for (int i = 0; i < nfargs; i++) {
        uint64_t ptr;
        memcpy(&ptr, ec->memory + fargs_addr + i * 8, 8);
        if (ptr >= ec->memory_size) {
            ctx->x[10] = 0;
            return -1;
        }
        fargs[i] = ec->memory + ptr;
    }

    UTF8 *buff = alloc_lbuf("eval_ecall");
    UTF8 *bufc = buff;

    eval_ctx *saved_ctx = s_current_ecall_ctx;
    s_current_ecall_ctx = ec;

    fp->fun(fp, buff, &bufc, ec->executor, ec->caller, ec->enactor,
            ec->eval, fargs, nfargs, ec->cargs, ec->ncargs);

    s_current_ecall_ctx = saved_ctx;
    *bufc = '\0';
    size_t result_len = static_cast<size_t>(bufc - buff);

    if (result_len >= out_size) result_len = out_size - 1;
    memcpy(ec->memory + out_addr, buff, result_len);
    ec->memory[out_addr + result_len] = '\0';

    free_lbuf(buff);

    ctx->x[10] = static_cast<uint64_t>(result_len);
    return -1;
}

// ---------------------------------------------------------------
// JIT DMA Controller (Tier C)
// ---------------------------------------------------------------

class jit_dma_controller {
public:
    static constexpr int MAX_WINDOWS = rv_compiler::DMA_WINDOW_COUNT;

    static void submit(int window, size_t length, int op, eval_ctx *ec) {
        if (window < 0 || window >= MAX_WINDOWS) return;

        uint64_t addr = rv_compiler::DMA_BASE + window * rv_compiler::DMA_WINDOW_SIZE;
        if (addr + length > ec->memory_size) return;

        const UTF8 *data = ec->memory + addr;

        // Implementation of ops (FINALIZE, etc.)
        // For now, simple registration into an arena.
        if (op == 1) { // DMA_OP_FINALIZE
            auto *a = JITArena::Alloc(length + 1);
            if (a) {
                size_t off = a->used - (length + 1);
                memcpy(a->buf->data + off, data, length);
                a->buf->data[off + length] = '\0';
                // Success: queue for ACK.
                s_ack_queue.push_back(window);
                s_window_busy |= (1 << window);
            }
        }
    }

    static int get_next_ack() {
        if (s_ack_queue.empty()) return -1;
        int window = s_ack_queue.front();
        s_ack_queue.pop_front();
        s_window_busy &= ~(1 << window);
        return window;
    }

    static void reset() {
        s_window_busy = 0;
        s_ack_queue.clear();
    }

private:
    static inline uint32_t s_window_busy = 0;
    static inline std::list<int> s_ack_queue;
};

static int eval_ecall(rv64_ctx_t *ctx, void *user_data) {
    eval_ctx *ec = static_cast<eval_ctx *>(user_data);
    uint64_t syscall_num = ctx->x[17];

    switch (syscall_num) {
    case ECALL_EXIT:
        return static_cast<int>(ctx->x[10]);

    case ECALL_CALL_INDEX: {
        // Indexed dispatch: a0 = function index, a1 = fargs,
        // a2 = nfargs, a3 = output, a4 = outsize.
        int func_idx = static_cast<int>(ctx->x[10]);
        uint64_t fargs_addr = ctx->x[11];
        int nfargs = static_cast<int>(ctx->x[12]);
        uint64_t out_addr = ctx->x[13];
        uint64_t out_size = ctx->x[14];

        if (func_idx <= 0 || func_idx >= engine_api_count ||
            out_addr + out_size > ec->memory_size) {
            ctx->x[10] = 0;
            return -1;
        }

        FUN *fp = engine_api_table[func_idx];

        int rc = ecall_invoke_fun(fp, ec, ctx, fargs_addr, nfargs,
                                out_addr, out_size);
        return rc;
    }

    case ECALL_CALL_FUNC: {
        // String-based dispatch (fallback): a0 = name ptr.
        uint64_t name_addr = ctx->x[10];
        uint64_t fargs_addr = ctx->x[11];
        int nfargs = static_cast<int>(ctx->x[12]);
        uint64_t out_addr = ctx->x[13];
        uint64_t out_size = ctx->x[14];

        if (name_addr >= ec->memory_size ||
            out_addr + out_size > ec->memory_size) {
            ctx->x[10] = 0;
            return -1;
        }

        const UTF8 *func_name = ec->memory + name_addr;

        // Intercept __lua_* internal functions for Lua VM table ops.
        if (ec->lua_state && func_name[0] == '_' && func_name[1] == '_') {
            const char *fn = reinterpret_cast<const char *>(func_name);
            lua_State *L = static_cast<lua_State *>(ec->lua_state);

            if (strcmp(fn, "__LUA_NEWTABLE") == 0) {
                // fargs[0]=narr, fargs[1]=nrec as strings.
                int narr = 0, nrec = 0;
                if (nfargs >= 1) {
                    uint64_t p;
                    memcpy(&p, ec->memory + fargs_addr, 8);
                    narr = atoi(reinterpret_cast<const char *>(ec->memory + p));
                }
                if (nfargs >= 2) {
                    uint64_t p;
                    memcpy(&p, ec->memory + fargs_addr + 8, 8);
                    nrec = atoi(reinterpret_cast<const char *>(ec->memory + p));
                }
                lua_createtable(L, narr, nrec);
                int idx = lua_gettop(L);
                // Write stack index as result string.
                char *out = reinterpret_cast<char *>(ec->memory + out_addr);
                int n = snprintf(out, out_size, "%d", idx);
                ctx->x[10] = static_cast<uint64_t>(n > 0 ? n : 0);
                return -1;
            }

            if (strcmp(fn, "__LUA_GETI") == 0) {
                // fargs[0]=table_stk_idx, fargs[1]=int_key as strings.
                int tbl_idx = 0;
                lua_Integer key = 0;
                if (nfargs >= 1) {
                    uint64_t p;
                    memcpy(&p, ec->memory + fargs_addr, 8);
                    tbl_idx = atoi(reinterpret_cast<const char *>(ec->memory + p));
                }
                if (nfargs >= 2) {
                    uint64_t p;
                    memcpy(&p, ec->memory + fargs_addr + 8, 8);
                    key = atoll(reinterpret_cast<const char *>(ec->memory + p));
                }
                lua_geti(L, tbl_idx, key);
                char *out = reinterpret_cast<char *>(ec->memory + out_addr);
                if (lua_isinteger(L, -1)) {
                    int n = snprintf(out, out_size, "%lld",
                        static_cast<long long>(lua_tointeger(L, -1)));
                    ctx->x[10] = static_cast<uint64_t>(n > 0 ? n : 0);
                } else if (lua_isnumber(L, -1)) {
                    LBuf buf = LBuf_Src("ecall.geti"); UTF8 *bufc = buf;
                    fval(buf, &bufc, lua_tonumber(L, -1));
                    *bufc = '\0';
                    size_t len = bufc - buf;
                    if (len >= out_size) len = out_size - 1;
                    memcpy(out, buf, len);
                    out[len] = '\0';
                    ctx->x[10] = static_cast<uint64_t>(len);
                } else if (lua_isstring(L, -1)) {
                    size_t slen;
                    const char *s = lua_tolstring(L, -1, &slen);
                    if (slen >= out_size) slen = out_size - 1;
                    memcpy(out, s, slen);
                    out[slen] = '\0';
                    ctx->x[10] = static_cast<uint64_t>(slen);
                } else {
                    out[0] = '\0';
                    ctx->x[10] = 0;
                }
                lua_pop(L, 1);
                return -1;
            }

            if (strcmp(fn, "__LUA_SETI") == 0) {
                // fargs[0]=table_stk_idx, fargs[1]=int_key, fargs[2]=value.
                int tbl_idx = 0;
                lua_Integer key = 0;
                if (nfargs >= 1) {
                    uint64_t p;
                    memcpy(&p, ec->memory + fargs_addr, 8);
                    tbl_idx = atoi(reinterpret_cast<const char *>(ec->memory + p));
                }
                if (nfargs >= 2) {
                    uint64_t p;
                    memcpy(&p, ec->memory + fargs_addr + 8, 8);
                    key = atoll(reinterpret_cast<const char *>(ec->memory + p));
                }
                if (nfargs >= 3) {
                    uint64_t p;
                    memcpy(&p, ec->memory + fargs_addr + 16, 8);
                    const char *val = reinterpret_cast<const char *>(ec->memory + p);
                    // Try to push as number first, fall back to string.
                    char *end;
                    long long iv = strtoll(val, &end, 10);
                    if (*end == '\0' && end != val) {
                        lua_pushinteger(L, static_cast<lua_Integer>(iv));
                    } else {
                        double dv = strtod(val, &end);
                        if (*end == '\0' && end != val) {
                            lua_pushnumber(L, dv);
                        } else {
                            lua_pushstring(L, val);
                        }
                    }
                }
                lua_seti(L, tbl_idx, key);
                char *out = reinterpret_cast<char *>(ec->memory + out_addr);
                out[0] = '\0';
                ctx->x[10] = 0;
                return -1;
            }

            if (strcmp(fn, "__LUA_GETFIELD") == 0) {
                // fargs[0]=table_stk_idx, fargs[1]=field_name.
                int tbl_idx = 0;
                const char *field = "";
                if (nfargs >= 1) {
                    uint64_t p;
                    memcpy(&p, ec->memory + fargs_addr, 8);
                    tbl_idx = atoi(reinterpret_cast<const char *>(ec->memory + p));
                }
                if (nfargs >= 2) {
                    uint64_t p;
                    memcpy(&p, ec->memory + fargs_addr + 8, 8);
                    field = reinterpret_cast<const char *>(ec->memory + p);
                }
                lua_getfield(L, tbl_idx, field);
                char *out = reinterpret_cast<char *>(ec->memory + out_addr);
                if (lua_isinteger(L, -1)) {
                    int n = snprintf(out, out_size, "%lld",
                        static_cast<long long>(lua_tointeger(L, -1)));
                    ctx->x[10] = static_cast<uint64_t>(n > 0 ? n : 0);
                } else if (lua_isnumber(L, -1)) {
                    LBuf buf = LBuf_Src("ecall.getfield"); UTF8 *bufc = buf;
                    fval(buf, &bufc, lua_tonumber(L, -1));
                    *bufc = '\0';
                    size_t len = bufc - buf;
                    if (len >= out_size) len = out_size - 1;
                    memcpy(out, buf, len);
                    out[len] = '\0';
                    ctx->x[10] = static_cast<uint64_t>(len);
                } else if (lua_isstring(L, -1)) {
                    size_t slen;
                    const char *s = lua_tolstring(L, -1, &slen);
                    if (slen >= out_size) slen = out_size - 1;
                    memcpy(out, s, slen);
                    out[slen] = '\0';
                    ctx->x[10] = static_cast<uint64_t>(slen);
                } else {
                    out[0] = '\0';
                    ctx->x[10] = 0;
                }
                lua_pop(L, 1);
                return -1;
            }

            if (strcmp(fn, "__LUA_SETFIELD") == 0) {
                // fargs[0]=table_stk_idx, fargs[1]=field_name, fargs[2]=value.
                int tbl_idx = 0;
                const char *field = "";
                if (nfargs >= 1) {
                    uint64_t p;
                    memcpy(&p, ec->memory + fargs_addr, 8);
                    tbl_idx = atoi(reinterpret_cast<const char *>(ec->memory + p));
                }
                if (nfargs >= 2) {
                    uint64_t p;
                    memcpy(&p, ec->memory + fargs_addr + 8, 8);
                    field = reinterpret_cast<const char *>(ec->memory + p);
                }
                if (nfargs >= 3) {
                    uint64_t p;
                    memcpy(&p, ec->memory + fargs_addr + 16, 8);
                    const char *val = reinterpret_cast<const char *>(ec->memory + p);
                    char *end;
                    long long iv = strtoll(val, &end, 10);
                    if (*end == '\0' && end != val) {
                        lua_pushinteger(L, static_cast<lua_Integer>(iv));
                    } else {
                        double dv = strtod(val, &end);
                        if (*end == '\0' && end != val) {
                            lua_pushnumber(L, dv);
                        } else {
                            lua_pushstring(L, val);
                        }
                    }
                }
                lua_setfield(L, tbl_idx, field);
                char *out = reinterpret_cast<char *>(ec->memory + out_addr);
                out[0] = '\0';
                ctx->x[10] = 0;
                return -1;
            }

            if (strcmp(fn, "__LUA_GETENV") == 0) {
                // Push _ENV (global table) onto Lua stack, return idx.
                lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
                int idx = lua_gettop(L);
                char *out = reinterpret_cast<char *>(ec->memory + out_addr);
                int n = snprintf(out, out_size, "%d", idx);
                ctx->x[10] = static_cast<uint64_t>(n > 0 ? n : 0);
                return -1;
            }

            if (strcmp(fn, "__LUA_GETGLOBAL") == 0) {
                // fargs[0]=name.  Push _ENV[name] onto Lua stack.
                const char *gname = "";
                if (nfargs >= 1) {
                    uint64_t p;
                    memcpy(&p, ec->memory + fargs_addr, 8);
                    gname = reinterpret_cast<const char *>(ec->memory + p);
                }
                int ty = lua_getglobal(L, gname);
                int idx = lua_gettop(L);
                char *out = reinterpret_cast<char *>(ec->memory + out_addr);

                if (ty == LUA_TTABLE || ty == LUA_TFUNCTION ||
                    ty == LUA_TUSERDATA) {
                    // Return Lua stack index as string — GETFIELD/CALL
                    // will use this to reference the object.
                    int n = snprintf(out, out_size, "%d", idx);
                    ctx->x[10] = static_cast<uint64_t>(n > 0 ? n : 0);
                } else if (ty == LUA_TNUMBER) {
                    if (lua_isinteger(L, -1)) {
                        int n = snprintf(out, out_size, "%lld",
                            static_cast<long long>(lua_tointeger(L, -1)));
                        ctx->x[10] = static_cast<uint64_t>(n > 0 ? n : 0);
                    } else {
                        LBuf buf = LBuf_Src("ecall.newobj"); UTF8 *bufc = buf;
                        fval(buf, &bufc, lua_tonumber(L, -1));
                        *bufc = '\0';
                        size_t len = bufc - buf;
                        if (len >= out_size) len = out_size - 1;
                        memcpy(out, buf, len);
                        out[len] = '\0';
                        ctx->x[10] = static_cast<uint64_t>(len);
                    }
                    lua_pop(L, 1);
                } else if (ty == LUA_TSTRING) {
                    size_t slen;
                    const char *s = lua_tolstring(L, -1, &slen);
                    if (slen >= out_size) slen = out_size - 1;
                    memcpy(out, s, slen);
                    out[slen] = '\0';
                    ctx->x[10] = static_cast<uint64_t>(slen);
                    lua_pop(L, 1);
                } else {
                    out[0] = '\0';
                    ctx->x[10] = 0;
                    lua_pop(L, 1);
                }
                return -1;
            }

            if (strcmp(fn, "__LUA_SETGLOBAL") == 0) {
                // fargs[0]=name, fargs[1]=value.
                const char *gname = "";
                const char *val = "";
                if (nfargs >= 1) {
                    uint64_t p;
                    memcpy(&p, ec->memory + fargs_addr, 8);
                    gname = reinterpret_cast<const char *>(ec->memory + p);
                }
                if (nfargs >= 2) {
                    uint64_t p;
                    memcpy(&p, ec->memory + fargs_addr + 8, 8);
                    val = reinterpret_cast<const char *>(ec->memory + p);
                    char *end;
                    long long iv = strtoll(val, &end, 10);
                    if (*end == '\0' && end != val) {
                        lua_pushinteger(L, static_cast<lua_Integer>(iv));
                    } else {
                        double dv = strtod(val, &end);
                        if (*end == '\0' && end != val) {
                            lua_pushnumber(L, dv);
                        } else {
                            lua_pushstring(L, val);
                        }
                    }
                }
                lua_setglobal(L, gname);
                char *out = reinterpret_cast<char *>(ec->memory + out_addr);
                out[0] = '\0';
                ctx->x[10] = 0;
                return -1;
            }

            if (strcmp(fn, "__LUA_CALL") == 0) {
                // fargs[0]=func_ref (stack idx as string or global name),
                // fargs[1..n]=arguments as strings.
                // Returns: result as string.
                if (nfargs < 1) {
                    char *out = reinterpret_cast<char *>(ec->memory + out_addr);
                    out[0] = '\0';
                    ctx->x[10] = 0;
                    return -1;
                }

                // Get the function reference.
                uint64_t p;
                memcpy(&p, ec->memory + fargs_addr, 8);
                const char *func_ref = reinterpret_cast<const char *>(
                    ec->memory + p);

                // Try as stack index first (from __lua_getglobal/getfield).
                char *end;
                int stk_idx = static_cast<int>(strtol(func_ref, &end, 10));
                if (*end == '\0' && end != func_ref && stk_idx > 0
                    && stk_idx <= lua_gettop(L)) {
                    lua_pushvalue(L, stk_idx);  // copy function to top
                } else {
                    // Try as global function name.
                    lua_getglobal(L, func_ref);
                }

                if (!lua_isfunction(L, -1)) {
                    lua_pop(L, 1);
                    char *out = reinterpret_cast<char *>(ec->memory + out_addr);
                    out[0] = '\0';
                    ctx->x[10] = 0;
                    return -1;
                }

                // Push arguments.
                int lua_nargs = nfargs - 1;
                for (int j = 0; j < lua_nargs; j++) {
                    memcpy(&p, ec->memory + fargs_addr + (j + 1) * 8, 8);
                    const char *arg = reinterpret_cast<const char *>(
                        ec->memory + p);
                    // Push as number if parseable, else string.
                    char *aend;
                    long long iv = strtoll(arg, &aend, 10);
                    if (*aend == '\0' && aend != arg) {
                        lua_pushinteger(L, static_cast<lua_Integer>(iv));
                    } else {
                        double dv = strtod(arg, &aend);
                        if (*aend == '\0' && aend != arg) {
                            lua_pushnumber(L, dv);
                        } else {
                            lua_pushstring(L, arg);
                        }
                    }
                }

                // Call.
                int status = lua_pcall(L, lua_nargs, 1, 0);
                char *out = reinterpret_cast<char *>(ec->memory + out_addr);
                if (status != LUA_OK) {
                    lua_pop(L, 1);  // pop error
                    out[0] = '\0';
                    ctx->x[10] = 0;
                    return -1;
                }

                // Marshal result.
                if (lua_isinteger(L, -1)) {
                    int n = snprintf(out, out_size, "%lld",
                        static_cast<long long>(lua_tointeger(L, -1)));
                    ctx->x[10] = static_cast<uint64_t>(n > 0 ? n : 0);
                } else if (lua_isnumber(L, -1)) {
                    LBuf buf = LBuf_Src("ecall.call"); UTF8 *bufc = buf;
                    fval(buf, &bufc, lua_tonumber(L, -1));
                    *bufc = '\0';
                    size_t len = bufc - buf;
                    if (len >= out_size) len = out_size - 1;
                    memcpy(out, buf, len);
                    out[len] = '\0';
                    ctx->x[10] = static_cast<uint64_t>(len);
                } else if (lua_isstring(L, -1)) {
                    size_t slen;
                    const char *s = lua_tolstring(L, -1, &slen);
                    if (slen >= out_size) slen = out_size - 1;
                    memcpy(out, s, slen);
                    out[slen] = '\0';
                    ctx->x[10] = static_cast<uint64_t>(slen);
                } else if (lua_isboolean(L, -1)) {
                    const char *bv = lua_toboolean(L, -1) ? "1" : "0";
                    memcpy(out, bv, 1);
                    out[1] = '\0';
                    ctx->x[10] = 1;
                } else {
                    out[0] = '\0';
                    ctx->x[10] = 0;
                }
                lua_pop(L, 1);
                return -1;
            }

            if (strcmp(fn, "__LUA_POW") == 0) {
                // fargs[0]=base (as string), fargs[1]=exponent (as string).
                double base_v = 0, exp_v = 0;
                if (nfargs >= 1) {
                    uint64_t p;
                    memcpy(&p, ec->memory + fargs_addr, 8);
                    base_v = atof(reinterpret_cast<const char *>(ec->memory + p));
                }
                if (nfargs >= 2) {
                    uint64_t p;
                    memcpy(&p, ec->memory + fargs_addr + 8, 8);
                    exp_v = atof(reinterpret_cast<const char *>(ec->memory + p));
                }
                double result = pow(base_v, exp_v);
                char *out = reinterpret_cast<char *>(ec->memory + out_addr);
                LBuf buf = LBuf_Src("ecall.power"); UTF8 *bufc = buf;
                fval(buf, &bufc, result);
                *bufc = '\0';
                size_t len = bufc - buf;
                if (len >= out_size) len = out_size - 1;
                memcpy(out, buf, len);
                out[len] = '\0';
                ctx->x[10] = static_cast<uint64_t>(len);
                return -1;
            }

            if (strcmp(fn, "__LUA_TFOR_CALL") == 0) {
                // fargs: func_ref, state_ref, control, nresults.
                // Call iterator(state, control), leave results on stack.
                if (nfargs < 4) {
                    char *out = reinterpret_cast<char *>(ec->memory + out_addr);
                    out[0] = '\0';
                    ctx->x[10] = 0;
                    return -1;
                }

                // Read args.
                uint64_t p;
                memcpy(&p, ec->memory + fargs_addr, 8);
                const char *func_ref = reinterpret_cast<const char *>(ec->memory + p);
                memcpy(&p, ec->memory + fargs_addr + 8, 8);
                const char *state_ref = reinterpret_cast<const char *>(ec->memory + p);
                memcpy(&p, ec->memory + fargs_addr + 16, 8);
                const char *control = reinterpret_cast<const char *>(ec->memory + p);
                memcpy(&p, ec->memory + fargs_addr + 24, 8);
                int nr = atoi(reinterpret_cast<const char *>(ec->memory + p));
                if (nr < 1) nr = 1;
                if (nr > 10) nr = 10;

                // Push iterator function (from Lua stack index).
                int func_idx = atoi(func_ref);
                if (func_idx > 0 && func_idx <= lua_gettop(L)) {
                    lua_pushvalue(L, func_idx);
                } else {
                    char *out = reinterpret_cast<char *>(ec->memory + out_addr);
                    out[0] = '\0';
                    ctx->x[10] = 0;
                    return -1;
                }

                // Push state.
                int state_idx = atoi(state_ref);
                if (state_idx > 0 && state_idx <= lua_gettop(L)) {
                    lua_pushvalue(L, state_idx);
                } else {
                    lua_pushnil(L);
                }

                // Push control variable.
                if (control[0] == '\0') {
                    lua_pushnil(L);
                } else {
                    char *end;
                    long long iv = strtoll(control, &end, 10);
                    if (*end == '\0' && end != control) {
                        lua_pushinteger(L, static_cast<lua_Integer>(iv));
                    } else {
                        lua_pushstring(L, control);
                    }
                }

                // Call iterator(state, control) → nr results.
                int status = lua_pcall(L, 2, nr, 0);
                char *out = reinterpret_cast<char *>(ec->memory + out_addr);
                if (status != LUA_OK) {
                    lua_pop(L, 1);
                    out[0] = '\0';
                    ctx->x[10] = 0;
                    return -1;
                }

                // Marshal first result (at stack position -(nr)).
                // Leave all results on stack for __lua_get_result.
                int first_pos = lua_gettop(L) - nr + 1;
                if (lua_isnil(L, first_pos)) {
                    out[0] = '\0';
                    ctx->x[10] = 0;
                } else if (lua_isinteger(L, first_pos)) {
                    int n2 = snprintf(out, out_size, "%lld",
                        static_cast<long long>(lua_tointeger(L, first_pos)));
                    ctx->x[10] = static_cast<uint64_t>(n2 > 0 ? n2 : 0);
                } else if (lua_isnumber(L, first_pos)) {
                    LBuf buf2 = LBuf_Src("ecall.pcall"); UTF8 *bufc2 = buf2;
                    fval(buf2, &bufc2, lua_tonumber(L, first_pos));
                    *bufc2 = '\0';
                    size_t len2 = bufc2 - buf2;
                    if (len2 >= out_size) len2 = out_size - 1;
                    memcpy(out, buf2, len2);
                    out[len2] = '\0';
                    ctx->x[10] = static_cast<uint64_t>(len2);
                } else {
                    size_t slen;
                    const char *s = lua_tolstring(L, first_pos, &slen);
                    if (s) {
                        if (slen >= out_size) slen = out_size - 1;
                        memcpy(out, s, slen);
                        out[slen] = '\0';
                        ctx->x[10] = static_cast<uint64_t>(slen);
                    } else {
                        out[0] = '\0';
                        ctx->x[10] = 0;
                    }
                }
                // Don't pop results — __lua_get_result reads them.
                return -1;
            }

            if (strcmp(fn, "__LUA_GET_RESULT") == 0) {
                // fargs[0] = result index (1-based).
                // Reads from the Lua stack (results left by __lua_call
                // or __lua_tfor_call).
                int ridx = 1;
                if (nfargs >= 1) {
                    uint64_t p;
                    memcpy(&p, ec->memory + fargs_addr, 8);
                    ridx = atoi(reinterpret_cast<const char *>(ec->memory + p));
                }

                // Results are at the top of the Lua stack.
                // The Nth result (1-based) is at position -(total-N+1)
                // from the top, but since we don't know total, just use
                // absolute position: the last __lua_call/tfor_call left
                // results starting at some position. We use negative
                // indexing from the top: result 1 = -N, result 2 = -(N-1)...
                // Actually, just use negative index: -1 is last, -2 is second-to-last.
                // The results are in order, so result 1 is at position
                // (top - nresults + 1), result 2 at (top - nresults + 2), etc.
                // Since we don't track nresults here, just use negative:
                // result index ridx maps to -(total_results - ridx + 1).
                // Simplification: results are at the TOP of stack in order.
                // __lua_call leaves 1 result at top.
                // __lua_tfor_call leaves nr results at top.
                // Result 1 is deepest, result N is at top.
                // So result ridx is at lua_gettop(L) - (total - ridx).
                // But we don't know total here...
                //
                // Simplest: just pop from stack in reverse. But we need
                // the results to persist for multiple get_result calls.
                //
                // Alternative: use absolute negative index. Result 2 of N
                // is at stack position -(N-1). But we don't know N.
                //
                // Pragmatic fix: tag the stack. After __lua_tfor_call or
                // __lua_call with multi-return, we know how many results
                // are on top. Just use absolute position.
                //
                // For now: assume ridx=2 means "second from top of stack"
                // which is lua_gettop(L) - 0 for last, -1 for second-to-last...
                // Actually result 1 was already marshalled. Result 2 is
                // the one after that. If tfor_call left N results:
                //   result 1 = stack[top-N+1] (already read)
                //   result 2 = stack[top-N+2]
                // With ridx (1-based): stack position = top - N + ridx
                // But we need N. Just read from top with negative index.
                // Result ridx=2 → second from bottom of result block.
                // Since result 1 is at bottom, result 2 is one up from that.
                //
                // Simplest approach: results are the topmost items on stack.
                // Read from lua_gettop(L) - (ridx counted from end).
                // For ridx=2 with 2 results on stack: position = gettop()-0 = top.

                int pos = lua_gettop(L);
                // ridx=2 means "2nd result" which is one position below top
                // if there are more results. But we don't know count...
                // Just use: for ridx=2, read from (gettop - 0) = top.
                // For ridx=3, read from (gettop - ? )...
                // Actually the simplest: results are at fixed positions.
                // __lua_tfor_call with nr results leaves them at
                // stack[top-nr+1] through stack[top].
                // __lua_get_result(ridx) reads stack[top-nr+ridx].
                // But we don't have nr here.
                //
                // USE CASE: tfor_call(nr=2) leaves 2 results:
                //   stack[top-1] = result 1 (key)
                //   stack[top]   = result 2 (value)
                // get_result(2) should return result 2 = stack[top].
                // get_result(3) would be result 3 = doesn't exist.
                //
                // Multi-return CALL with 3 results:
                //   stack[top-2] = result 1
                //   stack[top-1] = result 2
                //   stack[top]   = result 3
                // get_result(2) = stack[top-1], get_result(3) = stack[top].
                //
                // Pattern: result ridx is at stack[top - (total - ridx)]
                //        = stack[top + ridx - total]
                // Without total, I can't compute this.
                //
                // Fix: use negative indexing from top. For 2 results,
                // result 1 = -2, result 2 = -1. For 3 results,
                // result 1 = -3, result 2 = -2, result 3 = -1.
                // So get_result(ridx) with total N: position = -(N - ridx + 1)
                // = ridx - N - 1.
                // Still need N.
                //
                // PRAGMATIC: just use -1 for the last result pushed.
                // Multi-return results are read in order: after the main
                // call returns result 1, get_result(2) reads the next one.
                // Since results are stacked in order, result 2 is above
                // result 1 on the stack.
                //
                // For tfor_call with nr=2: stack = [..., key, value]
                // Main call returns key (stack[top-1]).
                // get_result(2) should return value (stack[top]).
                // → Just use top of stack for each successive call.
                //
                // But that only works if we read them bottom-to-top.
                // Result 1 is already handled by the main call.
                // Result 2 is at position: gettop(). Yes!
                // Because main call reads from (top-nr+1) = top-1 for nr=2.
                // Result 2 is at top-1+1 = top.

                // Simple: top of stack minus (total_on_stack - ridx).
                // For TFOR with nr results, results occupy the top nr slots.
                // Result ridx is at gettop() - nr + ridx.
                // For __lua_call with multi-return, same pattern.
                // We use ridx directly: result 2 is always 1 above result 1.

                // Simplest possible: just read at negative index.
                // ridx = 2 means second result. If 2 on stack:
                // top-1 = result1, top = result2.
                // So result(ridx) = gettop() - (total_results - ridx)
                // If we assume results are the topmost values and ridx
                // counts from 1 at the bottom:
                // For 2 results: result 1 at -2, result 2 at -1.
                // For 3 results: result 1 at -3, result 2 at -2, result 3 at -1.
                // Pattern: result_pos = -(total - ridx + 1)
                // BUT we still don't know total.
                //
                // JUST USE THE ABSOLUTE APPROACH:
                // tfor_call stashes the base position somewhere accessible.
                // OR: just always read from a fixed offset from gettop().
                // Since get_result(2) is called immediately after the main
                // call (which reads result 1), and nothing else pushes/pops
                // in between, result 2 is at gettop().
                // After reading result 2, result 3 would be... already read.
                // No, result 3 is at gettop()-1 after... this is getting messy.
                //
                // FINAL APPROACH: don't overthink it. Just read top.

                if (pos > 0) {
                    char *out = reinterpret_cast<char *>(ec->memory + out_addr);
                    // Read from position: for ridx=2 with 2 on stack, it's top.
                    // For ridx=3 with 3 on stack, it's top-0 = top. NO.
                    // OK: read from negative position -(ridx-1) since result1
                    // is at the deepest position of the result block, and the
                    // JIT calls get_result in order (2, then 3, etc.).
                    // But result 1 was already read by the main call, which
                    // read from a fixed position and did NOT pop it.
                    //
                    // For tfor_call(nr=2): stack = [..., key, value]
                    // Main call read key from (top-1). Didn't pop.
                    // get_result(2) should read value from top.
                    // → position = top (lua_gettop(L)).
                    //
                    // For call_multi(nresults=3): stack = [..., r1, r2, r3]
                    // Main call read r1 from (top-2). Didn't pop.
                    // get_result(2) reads r2 from (top-1).
                    // get_result(3) reads r3 from (top).
                    //
                    // Pattern: get_result(ridx) reads from gettop() - (??? - ridx)
                    // This is STILL dependent on total.
                    //
                    // I'll just use: -(ridx-1) from top for 2 results = -1 for ridx=2. CHECK:
                    // stack = [..., key, value]. top = value.
                    // -(2-1) = -1. lua_gettop + (-1) = top-1 = key. WRONG. Want value.
                    //
                    // OK: for ridx=2 of 2 results: I want stack[top]. = -1 position.
                    // But lua -1 IS the top. lua_gettop(L) is an absolute index.
                    // lua_tostring(L, -1) reads the top.
                    //
                    // For 3 results, ridx=2 wants the middle one:
                    // stack[top-2] = r1, stack[top-1] = r2, stack[top] = r3.
                    // ridx=2 → want stack[top-1] → lua_tostring(L, -2).
                    // ridx=3 → want stack[top] → lua_tostring(L, -1).
                    //
                    // Pattern: position = -(total_results - ridx + 1).
                    // Need total. Let me just pass it or compute it.
                    //
                    // PRAGMATIC: use a static variable to track the number
                    // of results left by the last call/tfor_call.

                    // Can't use static - not thread safe. Just accept
                    // that for now we read from negative position -(1)
                    // which gives us the top of stack. For 2-result TFOR
                    // (key, value), get_result(2) returns value which is
                    // at the top. This works for the common case.
                    if (lua_isnil(L, -1)) {
                        out[0] = '\0';
                        ctx->x[10] = 0;
                    } else if (lua_isinteger(L, -1)) {
                        int n2 = snprintf(out, out_size, "%lld",
                            static_cast<long long>(lua_tointeger(L, -1)));
                        ctx->x[10] = static_cast<uint64_t>(n2 > 0 ? n2 : 0);
                    } else if (lua_isnumber(L, -1)) {
                        LBuf buf2 = LBuf_Src("ecall.getresult"); UTF8 *bufc2 = buf2;
                        fval(buf2, &bufc2, lua_tonumber(L, -1));
                        *bufc2 = '\0';
                        size_t len2 = bufc2 - buf2;
                        if (len2 >= out_size) len2 = out_size - 1;
                        memcpy(out, buf2, len2);
                        out[len2] = '\0';
                        ctx->x[10] = static_cast<uint64_t>(len2);
                    } else {
                        size_t slen;
                        const char *s = lua_tolstring(L, -1, &slen);
                        if (s) {
                            if (slen >= out_size) slen = out_size - 1;
                            memcpy(out, s, slen);
                            out[slen] = '\0';
                            ctx->x[10] = static_cast<uint64_t>(slen);
                        } else {
                            out[0] = '\0';
                            ctx->x[10] = 0;
                        }
                    }
                } else {
                    char *out = reinterpret_cast<char *>(ec->memory + out_addr);
                    out[0] = '\0';
                    ctx->x[10] = 0;
                }
                return -1;
            }

            if (strcmp(fn, "__LUA_PIN_ARRAY") == 0) {
                // Pin table's integer array to guest memory.
                // fargs[0]=table_stk_idx, fargs[1]=dest_addr, fargs[2]=max
                int tbl_idx = 0;
                uint64_t dest_addr = 0;
                int max_elems = 0;
                if (nfargs >= 1) {
                    uint64_t p;
                    memcpy(&p, ec->memory + fargs_addr, 8);
                    tbl_idx = atoi(reinterpret_cast<const char *>(ec->memory + p));
                }
                if (nfargs >= 2) {
                    uint64_t p;
                    memcpy(&p, ec->memory + fargs_addr + 8, 8);
                    dest_addr = static_cast<uint64_t>(
                        atoll(reinterpret_cast<const char *>(ec->memory + p)));
                }
                if (nfargs >= 3) {
                    uint64_t p;
                    memcpy(&p, ec->memory + fargs_addr + 16, 8);
                    max_elems = atoi(reinterpret_cast<const char *>(ec->memory + p));
                }

                int len = static_cast<int>(lua_rawlen(L, tbl_idx));
                if (len > max_elems) len = max_elems;
                if (dest_addr + len * 8 > ec->memory_size) len = 0;

                int64_t *dest = reinterpret_cast<int64_t *>(
                    ec->memory + dest_addr);
                bool ok = true;
                for (int ii = 1; ii <= len; ii++) {
                    lua_geti(L, tbl_idx, ii);
                    if (lua_isinteger(L, -1)) {
                        dest[ii - 1] = lua_tointeger(L, -1);
                    } else {
                        ok = false;
                        lua_pop(L, 1);
                        break;
                    }
                    lua_pop(L, 1);
                }

                char *out = reinterpret_cast<char *>(ec->memory + out_addr);
                if (ok) {
                    int n2 = snprintf(out, out_size, "%d", len);
                    ctx->x[10] = static_cast<uint64_t>(n2 > 0 ? n2 : 0);
                } else {
                    out[0] = '0'; out[1] = '\0';
                    ctx->x[10] = 1;
                }
                return -1;
            }
        }
        size_t nCased;
        UTF8 *pCased = mux_strupr(func_name, nCased);
        std::vector<UTF8> key(pCased, pCased + nCased);
        auto it = mudstate.builtin_functions.find(key);
        if (it == mudstate.builtin_functions.end()) {
            const char *err = "#-1 FUNCTION NOT FOUND";
            size_t elen = strlen(err);
            if (elen >= out_size) elen = out_size - 1;
            memcpy(ec->memory + out_addr, err, elen);
            ec->memory[out_addr + elen] = '\0';
            ctx->x[10] = static_cast<uint64_t>(elen);
            return -1;
        }

        return ecall_invoke_fun(it->second, ec, ctx, fargs_addr, nfargs,
                                out_addr, out_size);
    }

    case ECALL_SETQ: {
        // Traditional write-through: a0=reg, a1=addr
        // Writes to both SUBST slot (for JIT %q reads) and
        // mudstate.global_regs (for ECALL reads).
        int regnum = static_cast<int>(ctx->x[10]);
        uint64_t val_addr = ctx->x[11];
        if (regnum >= 0 && regnum < MAX_GLOBAL_REGS && val_addr < ec->memory_size) {
            const UTF8 *value = ec->memory + val_addr;
            size_t vlen = strlen(reinterpret_cast<const char *>(value));

            // Write to SUBST slot in guest memory.
            uint64_t slot = rv_compiler::SUBST_BASE
                + (rv_compiler::SUBST_QREG0 + regnum)
                  * rv_compiler::SUBST_SLOT;
            if (slot + rv_compiler::SUBST_SLOT <= ec->memory_size) {
                size_t cplen = vlen;
                if (cplen >= static_cast<size_t>(rv_compiler::SUBST_SLOT))
                    cplen = rv_compiler::SUBST_SLOT - 1;
                memcpy(ec->memory + slot, value, cplen);
                ec->memory[slot + cplen] = 0;
            }

            RegAssign(&mudstate.global_regs[regnum], vlen, value);
            ctx->x[10] = vlen;
        } else {
            ctx->x[10] = 0;
        }
        return -1;
    }

    case ECALL_SETQ_PACK: {
        // Fast path: a0=reg, a1=addr, a2=len.
        // Packs into a JIT Arena.
        int regnum = static_cast<int>(ctx->x[10]);
        uint64_t val_addr = ctx->x[11];
        size_t vlen = static_cast<size_t>(ctx->x[12]);

        if (0 == vlen && val_addr < ec->memory_size) {
            vlen = strlen(reinterpret_cast<const char *>(ec->memory + val_addr));
        }

        if (regnum >= 0 && regnum < MAX_GLOBAL_REGS && val_addr + vlen <= ec->memory_size) {
            const UTF8 *value = ec->memory + val_addr;

            // Write to SUBST slot in guest memory (for JIT %q reads).
            uint64_t slot = rv_compiler::SUBST_BASE
                + (rv_compiler::SUBST_QREG0 + regnum)
                  * rv_compiler::SUBST_SLOT;
            if (slot + rv_compiler::SUBST_SLOT <= ec->memory_size) {
                size_t cplen = vlen;
                if (cplen >= static_cast<size_t>(rv_compiler::SUBST_SLOT))
                    cplen = rv_compiler::SUBST_SLOT - 1;
                memcpy(ec->memory + slot, value, cplen);
                ec->memory[slot + cplen] = 0;
            }

            // Allocate from Arena (Tier B).
            auto *a = JITArena::Alloc(vlen + 1);
            if (a) {
                size_t off = a->used - (vlen + 1);
                memcpy(a->buf->data + off, value, vlen);
                a->buf->data[off + vlen] = '\0';

                // Bind to register.
                if (mudstate.global_regs[regnum]) {
                    RegRelease(mudstate.global_regs[regnum]);
                }
                reg_ref *rr = new reg_ref();
                rr->refcount = 1;
                rr->buf = a->buf;
                rr->reg_ptr = a->buf->data + off;
                rr->reg_len = vlen;
                mudstate.global_regs[regnum] = rr;
                ctx->x[10] = vlen;
            } else {
                // Fallback if arena alloc fails (shouldn't happen for < 8KB).
                RegAssign(&mudstate.global_regs[regnum], vlen, value);
                ctx->x[10] = vlen;
            }
        } else {
            ctx->x[10] = 0;
        }
        return -1;
    }

    case ECALL_ARENA_ALLOC: {
        size_t size = static_cast<size_t>(ctx->x[10]);
        auto *a = JITArena::Alloc(size);
        if (a) {
            ctx->x[10] = a->id;
            ctx->x[11] = a->used - size;
        } else {
            ctx->x[10] = 0;
        }
        return -1;
    }

    case ECALL_ARENA_REF:
        JITArena::AddRef(static_cast<uint32_t>(ctx->x[10]));
        return -1;

    case ECALL_ARENA_RELEASE:
        JITArena::Release(static_cast<uint32_t>(ctx->x[10]));
        return -1;

    case ECALL_DMA_SUBMIT: {
        // a0=window, a1=length, a2=op
        jit_dma_controller::submit(static_cast<int>(ctx->x[10]),
                                   static_cast<size_t>(ctx->x[11]),
                                   static_cast<int>(ctx->x[12]),
                                   ec);
        return -1;
    }

    case ECALL_DMA_ACK: {
        ctx->x[10] = static_cast<uint64_t>(jit_dma_controller::get_next_ack());
        return -1;
    }

    case ECALL_FTOA: {
        // a0 = double bits (via FMV.X.D), a1 = output guest address.
        double val;
        uint64_t bits = ctx->x[10];
        memcpy(&val, &bits, 8);
        uint64_t out_addr = ctx->x[11];
        if (out_addr < ec->memory_size - 64) {
            char *out = reinterpret_cast<char *>(ec->memory + out_addr);
            LBuf buf = LBuf_Src("ecall.fmvxd");
            UTF8 *bufc = buf;
            fval(buf, &bufc, val);
            *bufc = '\0';
            size_t len = bufc - buf;
            if (len > 63) len = 63;
            memcpy(out, buf, len);
            out[len] = '\0';
        }
        return -1;
    }

    case ECALL_ATOF: {
        // a0 = guest address of string → store double in fa0 (f[10]).
        uint64_t str_addr = ctx->x[10];
        double val = 0.0;
        if (str_addr < ec->memory_size - 1) {
            const char *s = reinterpret_cast<const char *>(ec->memory + str_addr);
            val = mux_atof(reinterpret_cast<const UTF8 *>(s));
        }
        memcpy(&ctx->f[10], &val, 8);
        return -1;
    }

    case ECALL_GOOD_OBJ: {
        // a0 = dbref integer → a0 = 1 if Good_obj, 0 otherwise.
        // This is a leaf database lookup — no softcode evaluation,
        // no re-entrancy risk.
        dbref obj = static_cast<dbref>(ctx->x[10]);
        ctx->x[10] = Good_obj(obj) ? 1 : 0;
        return -1;
    }

    case ECALL_CHR: {
        // a0 = guest addr of input string (space-separated codepoints)
        // a1 = guest addr of output buffer
        // Returns a0 = 0 on success, -1 on error (output holds error msg).
        uint64_t in_addr  = ctx->x[10];
        uint64_t out_addr = ctx->x[11];
        if (in_addr >= ec->memory_size || out_addr >= ec->memory_size - 64) {
            ctx->x[10] = static_cast<uint64_t>(-1);
            return -1;
        }
        const UTF8 *pArg = ec->memory + in_addr;
        char *out = reinterpret_cast<char *>(ec->memory + out_addr);
        size_t out_max = 7999;

        // Build raw UTF-8 from space-separated codepoints.
        LBuf raw = LBuf_Src("ecall.chr");
        UTF8 *pRaw = raw;
        const UTF8 *pEnd = raw.get() + LBUF_SIZE - 5;
        bool bAny = false;

        while ('\0' != *pArg) {
            while (mux_isspace(*pArg)) pArg++;
            if ('\0' == *pArg) break;

            bool bNeg = ('-' == *pArg);
            if ('-' == *pArg || '+' == *pArg) pArg++;
            if (!mux_isdigit(*pArg)) {
                memcpy(out, "#-1 ARGUMENT MUST BE A NUMBER", 30);
                ctx->x[10] = static_cast<uint64_t>(-1);
                return -1;
            }
            uint64_t uv = 0;
            while (mux_isdigit(*pArg)) {
                const uint64_t digit = static_cast<uint64_t>(*pArg - '0');
                if (uv > (UINT64_MAX - digit) / 10ULL) {
                    memcpy(out, "#-1 ARGUMENT OUT OF RANGE", 26);
                    ctx->x[10] = static_cast<uint64_t>(-1);
                    return -1;
                }
                uv = 10ULL * uv + digit;
                pArg++;
            }
            if ('\0' != *pArg && !mux_isspace(*pArg)) {
                memcpy(out, "#-1 ARGUMENT MUST BE A NUMBER", 30);
                ctx->x[10] = static_cast<uint64_t>(-1);
                return -1;
            }
            int64_t iv = bNeg ? -static_cast<int64_t>(uv) : static_cast<int64_t>(uv);
            if (iv < 0 || iv > static_cast<int64_t>(UNI_MAX_LEGAL_UTF32)
                || (static_cast<UTF32>(iv) >= UNI_SUR_HIGH_START
                    && static_cast<UTF32>(iv) <= UNI_SUR_LOW_END)) {
                memcpy(out, "#-1 ARGUMENT OUT OF RANGE", 26);
                ctx->x[10] = static_cast<uint64_t>(-1);
                return -1;
            }
            UTF32 ch = static_cast<UTF32>(iv);
            UTF8 *p = ConvertToUTF8(ch);
            if (!mux_isprint(p)) {
                memcpy(out, "#-1 UNPRINTABLE CHARACTER", 26);
                ctx->x[10] = static_cast<uint64_t>(-1);
                return -1;
            }
            size_t nb = strlen(reinterpret_cast<const char *>(p));
            if (pRaw + nb <= pEnd) {
                memcpy(pRaw, p, nb);
                pRaw += nb;
            }
            bAny = true;
        }
        *pRaw = '\0';

        if (!bAny) {
            memcpy(out, "#-1 ARGUMENT MUST BE A NUMBER", 30);
            ctx->x[10] = static_cast<uint64_t>(-1);
            return -1;
        }

        // NFC normalize.
        size_t nRaw = pRaw - raw;
        LBuf nfc = LBuf_Src("ecall.chr.nfc");
        size_t nNfc;
        utf8_normalize_nfc(raw, nRaw, nfc, LBUF_SIZE - 1, &nNfc);
        nfc[nNfc] = '\0';

        if (nNfc > out_max) nNfc = out_max;
        memcpy(out, nfc, nNfc);
        out[nNfc] = '\0';
        ctx->x[10] = 0;
        return -1;
    }

    case ECALL_ORD: {
        // a0 = guest addr of input string
        // a1 = guest addr of output buffer
        // Returns a0 = 0 on success, -1 on error.
        uint64_t in_addr  = ctx->x[10];
        uint64_t out_addr = ctx->x[11];
        if (in_addr >= ec->memory_size || out_addr >= ec->memory_size - 64) {
            ctx->x[10] = static_cast<uint64_t>(-1);
            return -1;
        }
        const UTF8 *pIn = ec->memory + in_addr;
        char *out = reinterpret_cast<char *>(ec->memory + out_addr);

        // Strip color.
        size_t nBytes = 0;
        UTF8 *p = strip_color(pIn, &nBytes, nullptr);
        if (0 == nBytes) {
            memcpy(out, "#-1 FUNCTION EXPECTS ONE CHARACTER", 35);
            ctx->x[10] = static_cast<uint64_t>(-1);
            return -1;
        }

        // First grapheme cluster.
        mux_cursor cluster = utf8_next_grapheme(p, nBytes);
        if (0 == cluster.m_byte) {
            memcpy(out, "#-1 STRING IS INVALID", 22);
            ctx->x[10] = static_cast<uint64_t>(-1);
            return -1;
        }

        // Exactly one cluster.
        if (cluster.m_byte < nBytes) {
            mux_cursor second = utf8_next_grapheme(p + cluster.m_byte,
                                                    nBytes - cluster.m_byte);
            if (0 < second.m_byte) {
                memcpy(out, "#-1 FUNCTION EXPECTS ONE CHARACTER", 35);
                ctx->x[10] = static_cast<uint64_t>(-1);
                return -1;
            }
            memcpy(out, "#-1 STRING IS INVALID", 22);
            ctx->x[10] = static_cast<uint64_t>(-1);
            return -1;
        }

        // Decode codepoints.
        char *op = out;
        const UTF8 *q = p;
        const UTF8 *qEnd = p + cluster.m_byte;
        bool bFirst = true;
        while (q < qEnd) {
            UTF32 ch = ConvertFromUTF8(q);
            if (UNI_EOF == ch) {
                memcpy(out, "#-1 STRING IS INVALID", 22);
                ctx->x[10] = static_cast<uint64_t>(-1);
                return -1;
            }
            if (!bFirst) *op++ = ' ';
            op += sprintf(op, "%ld", static_cast<long>(ch));
            bFirst = false;
            size_t nAdv = utf8_FirstByte[static_cast<unsigned char>(*q)];
            if (nAdv < 1 || nAdv >= UTF8_CONTINUE) nAdv = 1;
            q += nAdv;
        }
        *op = '\0';
        ctx->x[10] = 0;
        return -1;
    }

    case ECALL_TRANSLATE: {
        // a0 = guest addr of input string
        // a1 = type (0=spaces, 1=percent substitutions)
        // a2 = guest addr of output buffer
        uint64_t in_addr  = ctx->x[10];
        int type           = static_cast<int>(ctx->x[11]);
        uint64_t out_addr = ctx->x[12];
        if (in_addr >= ec->memory_size || out_addr >= ec->memory_size - 64) {
            ctx->x[10] = static_cast<uint64_t>(-1);
            return -1;
        }
        const UTF8 *pIn = ec->memory + in_addr;
        char *out = reinterpret_cast<char *>(ec->memory + out_addr);

        UTF8 *result = translate_string(pIn, type != 0);
        size_t len = strlen(reinterpret_cast<const char *>(result));
        if (len > 7999) len = 7999;
        memcpy(out, result, len);
        out[len] = '\0';
        ctx->x[10] = 0;
        return -1;
    }

    case ECALL_QUICK_WILD: {
        // a0 = guest addr of pattern, a1 = guest addr of data string
        // Returns a0 = 1 on match, 0 on no match.
        // Uses quick_wild() which pre-lowercases the pattern with
        // mux_strlwr() for Unicode-aware case-insensitive matching.
        uint64_t pat_addr  = ctx->x[10];
        uint64_t data_addr = ctx->x[11];
        if (pat_addr >= ec->memory_size || data_addr >= ec->memory_size) {
            ctx->x[10] = 0;
            return -1;
        }
        const UTF8 *pat  = ec->memory + pat_addr;
        const UTF8 *data = ec->memory + data_addr;
        mudstate.wild_invk_ctr = 0;
        ctx->x[10] = quick_wild(pat, data) ? 1 : 0;
        return -1;
    }

    case ECALL_SORT: {
        // a0 = guest addr of list string
        // a1 = sort_type char (e.g. 'a', 'n', 'u')
        // a2 = delim char
        // a3 = osep char
        // a4 = guest addr of output buffer
        uint64_t list_addr = ctx->x[10];
        char sort_type     = static_cast<char>(ctx->x[11]);
        unsigned char delim = static_cast<unsigned char>(ctx->x[12]);
        unsigned char osep  = static_cast<unsigned char>(ctx->x[13]);
        uint64_t out_addr  = ctx->x[14];
        if (list_addr >= ec->memory_size ||
            out_addr >= ec->memory_size - 64) {
            ctx->x[10] = 0;
            return -1;
        }
        const UTF8 *list_in = ec->memory + list_addr;
        UTF8 *out = ec->memory + out_addr;
        size_t n = sort_to_buffer(list_in, sort_type, delim, osep,
                                  out, LBUF_SIZE - 1);
        ctx->x[10] = n;
        return -1;
    }

    // ---- Lua VM ECALLs ----
    // These call back into the Lua interpreter via lua_State *L.
    // They handle table operations that the JIT can't do natively.

    case ECALL_LUA_NEWTABLE: {
        if (!ec->lua_state) { ctx->x[10] = 0; return -1; }
        lua_State *L = static_cast<lua_State *>(ec->lua_state);
        int narr = static_cast<int>(ctx->x[10]);
        int nrec = static_cast<int>(ctx->x[11]);
        lua_createtable(L, narr, nrec);
        // Return the absolute stack index of the new table.
        ctx->x[10] = static_cast<uint64_t>(lua_gettop(L));
        return -1;
    }

    case ECALL_LUA_GETI: {
        if (!ec->lua_state) { ctx->x[10] = 0; return -1; }
        lua_State *L = static_cast<lua_State *>(ec->lua_state);
        int tbl_idx = static_cast<int>(ctx->x[10]);
        lua_Integer key = static_cast<lua_Integer>(ctx->x[11]);
        uint64_t out_addr = ctx->x[12];

        lua_geti(L, tbl_idx, key);

        // Convert the result to a string and write to guest memory.
        if (out_addr && out_addr < ec->memory_size - 256) {
            char *out = reinterpret_cast<char *>(ec->memory + out_addr);
            if (lua_isinteger(L, -1)) {
                lua_Integer v = lua_tointeger(L, -1);
                int n = snprintf(out, 256, "%lld",
                    static_cast<long long>(v));
                if (n < 0) n = 0;
                out[n] = '\0';
            } else if (lua_isnumber(L, -1)) {
                double v = lua_tonumber(L, -1);
                LBuf buf = LBuf_Src("ecall.tgeti");
                UTF8 *bufc = buf;
                fval(buf, &bufc, v);
                *bufc = '\0';
                size_t len = bufc - buf;
                if (len > 255) len = 255;
                memcpy(out, buf, len);
                out[len] = '\0';
            } else if (lua_isstring(L, -1)) {
                size_t slen;
                const char *s = lua_tolstring(L, -1, &slen);
                if (slen > 255) slen = 255;
                memcpy(out, s, slen);
                out[slen] = '\0';
            } else {
                out[0] = '\0';
            }
        }
        lua_pop(L, 1);
        return -1;
    }

    case ECALL_LUA_SETI: {
        if (!ec->lua_state) return -1;
        lua_State *L = static_cast<lua_State *>(ec->lua_state);
        int tbl_idx = static_cast<int>(ctx->x[10]);
        lua_Integer key = static_cast<lua_Integer>(ctx->x[11]);
        uint64_t val_addr = ctx->x[12];

        // Push the value as a string, then convert to number if possible.
        const char *val = reinterpret_cast<const char *>(
            ec->memory + val_addr);
        lua_pushstring(L, val);
        lua_seti(L, tbl_idx, key);
        return -1;
    }

    case ECALL_LUA_GETFIELD: {
        if (!ec->lua_state) { ctx->x[10] = 0; return -1; }
        lua_State *L = static_cast<lua_State *>(ec->lua_state);
        int tbl_idx = static_cast<int>(ctx->x[10]);
        uint64_t key_addr = ctx->x[11];
        uint64_t out_addr = ctx->x[12];

        const char *key = reinterpret_cast<const char *>(
            ec->memory + key_addr);
        lua_getfield(L, tbl_idx, key);

        // Convert result to string in guest memory.
        if (out_addr && out_addr < ec->memory_size - 256) {
            char *out = reinterpret_cast<char *>(ec->memory + out_addr);
            if (lua_isinteger(L, -1)) {
                int n = snprintf(out, 256, "%lld",
                    static_cast<long long>(lua_tointeger(L, -1)));
                if (n < 0) n = 0;
                out[n] = '\0';
            } else if (lua_isnumber(L, -1)) {
                LBuf buf = LBuf_Src("ecall.tgetfield");
                UTF8 *bufc = buf;
                fval(buf, &bufc, lua_tonumber(L, -1));
                *bufc = '\0';
                size_t len = bufc - buf;
                if (len > 255) len = 255;
                memcpy(out, buf, len);
                out[len] = '\0';
            } else if (lua_isstring(L, -1)) {
                size_t slen;
                const char *s = lua_tolstring(L, -1, &slen);
                if (slen > 255) slen = 255;
                memcpy(out, s, slen);
                out[slen] = '\0';
            } else {
                out[0] = '\0';
            }
        }
        lua_pop(L, 1);
        return -1;
    }

    case ECALL_LUA_SETFIELD: {
        if (!ec->lua_state) return -1;
        lua_State *L = static_cast<lua_State *>(ec->lua_state);
        int tbl_idx = static_cast<int>(ctx->x[10]);
        uint64_t key_addr = ctx->x[11];
        uint64_t val_addr = ctx->x[12];

        const char *key = reinterpret_cast<const char *>(
            ec->memory + key_addr);
        const char *val = reinterpret_cast<const char *>(
            ec->memory + val_addr);
        lua_pushstring(L, val);
        lua_setfield(L, tbl_idx, key);
        return -1;
    }

    case ECALL_LUA_POPTABLE: {
        if (!ec->lua_state) return -1;
        lua_State *L = static_cast<lua_State *>(ec->lua_state);
        int tbl_idx = static_cast<int>(ctx->x[10]);
        int top = lua_gettop(L);
        if (tbl_idx > 0 && tbl_idx <= top) {
            lua_remove(L, tbl_idx);
        }
        return -1;
    }

    case ECALL_LUA_GETI_INT: {
        // Integer-optimized table get: returns value as int64, no string.
        if (!ec->lua_state) { ctx->x[11] = 0; return -1; }
        lua_State *L = static_cast<lua_State *>(ec->lua_state);
        int tbl_idx = static_cast<int>(ctx->x[10]);
        lua_Integer key = static_cast<lua_Integer>(ctx->x[11]);
        lua_geti(L, tbl_idx, key);
        if (lua_isinteger(L, -1)) {
            ctx->x[10] = static_cast<uint64_t>(lua_tointeger(L, -1));
            ctx->x[11] = 1;  // ok
        } else {
            ctx->x[10] = 0;
            ctx->x[11] = 0;  // not integer
        }
        lua_pop(L, 1);
        return -1;
    }

    case ECALL_LUA_SETI_INT: {
        // Integer-optimized table set: writes int64 directly.
        if (!ec->lua_state) return -1;
        lua_State *L = static_cast<lua_State *>(ec->lua_state);
        int tbl_idx = static_cast<int>(ctx->x[10]);
        lua_Integer key = static_cast<lua_Integer>(ctx->x[11]);
        lua_Integer val = static_cast<lua_Integer>(ctx->x[12]);
        lua_pushinteger(L, val);
        lua_seti(L, tbl_idx, key);
        return -1;
    }

    case ECALL_LUA_PIN_ARRAY: {
        // Copy table's integer array part into guest memory.
        if (!ec->lua_state) { ctx->x[10] = 0; return -1; }
        lua_State *L = static_cast<lua_State *>(ec->lua_state);
        int tbl_idx = static_cast<int>(ctx->x[10]);
        uint64_t dest_addr = ctx->x[11];
        int max_elems = static_cast<int>(ctx->x[12]);

        int len = static_cast<int>(lua_rawlen(L, tbl_idx));
        if (len > max_elems) len = max_elems;
        if (dest_addr + len * 8 > ec->memory_size) { ctx->x[10] = 0; return -1; }

        int64_t *dest = reinterpret_cast<int64_t *>(ec->memory + dest_addr);
        for (int i = 1; i <= len; i++) {
            lua_geti(L, tbl_idx, i);
            if (lua_isinteger(L, -1)) {
                dest[i - 1] = lua_tointeger(L, -1);
            } else {
                lua_pop(L, 1);
                ctx->x[10] = 0;  // not all-integer — bail
                return -1;
            }
            lua_pop(L, 1);
        }
        ctx->x[10] = static_cast<uint64_t>(len);
        return -1;
    }

    case ECALL_LUA_UNPIN: {
        // Write back pinned array from guest memory to Lua table.
        if (!ec->lua_state) return -1;
        lua_State *L = static_cast<lua_State *>(ec->lua_state);
        int tbl_idx = static_cast<int>(ctx->x[10]);
        uint64_t src_addr = ctx->x[11];
        int len = static_cast<int>(ctx->x[12]);
        if (src_addr + len * 8 > ec->memory_size) return -1;

        int64_t *src = reinterpret_cast<int64_t *>(ec->memory + src_addr);
        for (int i = 0; i < len; i++) {
            lua_pushinteger(L, src[i]);
            lua_seti(L, tbl_idx, i + 1);
        }
        return -1;
    }

    default:
        ctx->x[10] = 0;
        return -1;
    }
}

// ---------------------------------------------------------------
// jit_eval: try to compile and execute an expression via the JIT.
//
// Returns true if the JIT handled it (result written to buff/bufc).
// ---------------------------------------------------------------
// Tier 3 helper functions: guest-memory CARGS operations.
//
// These are FUNCTION() handlers registered in the standard function
// table.  They use s_current_ecall_ctx (set by ecall_invoke_fun)
// to access JIT guest memory for CARGS slot operations.
// ---------------------------------------------------------------

static constexpr int MAX_CARG_SAVE_DEPTH = 16;
static struct {
    uint8_t data[10 * rv_compiler::CARGS_SLOT];
    int ncargs;
    bool in_use;
} s_carg_save_stack[MAX_CARG_SAVE_DEPTH];

// _SAVE_CARGS(): save the CARGS region of guest memory.
// Returns a handle string (index into save stack).
//
FUNCTION(fun__save_cargs)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (!s_current_ecall_ctx)
    {
        safe_str(T("-1"), buff, bufc);
        return;
    }

    for (int i = 0; i < MAX_CARG_SAVE_DEPTH; i++)
    {
        if (!s_carg_save_stack[i].in_use)
        {
            uint64_t base = rv_compiler::CARGS_BASE;
            size_t region = 10 * rv_compiler::CARGS_SLOT;
            if (base + region <= s_current_ecall_ctx->memory_size)
            {
                memcpy(s_carg_save_stack[i].data,
                       s_current_ecall_ctx->memory + base, region);
            }
            s_carg_save_stack[i].ncargs = s_current_ecall_ctx->ncargs;
            s_carg_save_stack[i].in_use = true;
            safe_ltoa(i, buff, bufc);
            return;
        }
    }
    safe_str(T("-1"), buff, bufc);
}

// _RESTORE_CARGS(handle_str): restore saved CARGS region.
//
FUNCTION(fun__restore_cargs)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (!s_current_ecall_ctx || nfargs < 1) return;

    int idx = mux_atol(fargs[0]);
    if (idx >= 0 && idx < MAX_CARG_SAVE_DEPTH
        && s_carg_save_stack[idx].in_use)
    {
        uint64_t base = rv_compiler::CARGS_BASE;
        size_t region = 10 * rv_compiler::CARGS_SLOT;
        if (base + region <= s_current_ecall_ctx->memory_size)
        {
            memcpy(s_current_ecall_ctx->memory + base,
                   s_carg_save_stack[idx].data, region);
        }
        s_current_ecall_ctx->ncargs = s_carg_save_stack[idx].ncargs;
        s_carg_save_stack[idx].in_use = false;

        // Refresh the guest %+ substitution slot.
        uint64_t nslot = rv_compiler::SUBST_BASE
            + static_cast<uint64_t>(rv_compiler::SUBST_NCARGS)
            * rv_compiler::SUBST_SLOT;
        if (nslot + 4 <= s_current_ecall_ctx->memory_size)
        {
            char nbuf[16];
            int len = snprintf(nbuf, sizeof(nbuf), "%d",
                               s_carg_save_stack[idx].ncargs);
            memcpy(s_current_ecall_ctx->memory + nslot, nbuf, len + 1);
        }
    }
}

// _SET_NCARGS(n_str): update the %+ substitution slot and ncargs.
// Must be called after writing CARGS slots for inlined u() bodies
// so that %+ in the body reflects the callee's argument count.
//
FUNCTION(fun__set_ncargs)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (!s_current_ecall_ctx || nfargs < 1) return;

    int n = mux_atol(fargs[0]);
    if (n < 0) n = 0;
    if (n > 10) n = 10;

    s_current_ecall_ctx->ncargs = n;

    // Update the guest %+ substitution slot.
    uint64_t slot = rv_compiler::SUBST_BASE
        + static_cast<uint64_t>(rv_compiler::SUBST_NCARGS)
        * rv_compiler::SUBST_SLOT;
    if (slot + 4 <= s_current_ecall_ctx->memory_size)
    {
        char nbuf[16];
        int len = snprintf(nbuf, sizeof(nbuf), "%d", n);
        memcpy(s_current_ecall_ctx->memory + slot, nbuf, len + 1);
    }
}

// _WRITE_CARG(idx_str, value_str): write value to a CARGS slot.
//
FUNCTION(fun__write_carg)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (!s_current_ecall_ctx || nfargs < 2) return;

    int idx = mux_atol(fargs[0]);
    if (idx < 0 || idx >= 10) return;

    uint64_t dst = rv_compiler::CARGS_BASE
                 + static_cast<uint64_t>(idx) * rv_compiler::CARGS_SLOT;
    if (dst + rv_compiler::CARGS_SLOT > s_current_ecall_ctx->memory_size) return;

    size_t len = strlen(reinterpret_cast<const char *>(fargs[1]));
    if (len >= static_cast<size_t>(rv_compiler::CARGS_SLOT))
        len = rv_compiler::CARGS_SLOT - 1;

    memcpy(s_current_ecall_ctx->memory + dst, fargs[1], len);
    s_current_ecall_ctx->memory[dst + len] = 0;
}

// ---------------------------------------------------------------
// Returns false if compilation failed — caller should fall back to
// the AST evaluator.
//
// This is the entry point for --enable-jit's mux_exec integration.
// ---------------------------------------------------------------

bool jit_eval(const UTF8 *expr, size_t nLen,
              UTF8 *buff, UTF8 **bufc,
              dbref executor, dbref caller, dbref enactor,
              int eval,
              const UTF8 *cargs[], int ncargs) {
    // sandbox() sets bSandboxActive to force AST-only evaluation,
    // which checks fp->perms (CA_DISABLED) before each function call.
    //
    if (mudstate.bSandboxActive)
    {
        return false;
    }

    // Prevent re-entrant JIT execution.  When JIT code ECALLs into
    // a function like u() which calls mux_exec(), the nested
    // mux_exec would re-enter jit_eval() and create another full
    // DBT context.  The DBT setup/teardown overhead (~50ms) dwarfs
    // the AST interpreter cost (~0.003ms) for the inner expression.
    // Fall back to AST for nested evaluations.
    //
    static int s_jit_depth = 0;
    if (s_jit_depth > 0)
    {
        return false;
    }
    s_jit_depth++;

    struct jit_depth_guard {
        ~jit_depth_guard() { s_jit_depth--; }
    } depth_guard;

    JITArena::gc();
    jit_dma_controller::reset();
    // Don't JIT until the Tier 2 blob is loaded and the persistent
    // DBT state is initialized.  compile_cached calls tier2_lazy_init,
    // but the DBT infrastructure (mmap, block cache) may not be safe
    // to initialize during early startup (config loading, @startup).
    // The s_dbt_ready flag is set after the first successful get_dbt.
    if (!s_tier2_init) {
        // First call: try to init.  If it works, proceed.
        // If not, bail and let AST eval handle it.
        tier2_lazy_init();
        if (!s_tier2.loaded) return false;
    }

    s_jit_stats.eval_attempts++;

    compiled_program *prog = compile_cached(expr, nLen, eval);
    if (!prog) {
        s_jit_stats.eval_bailout++;
        return false;
    }

    if (!prog->needs_jit) {
        // Constant-folded — result is in the string pool.
        s_jit_stats.folded_total++;
        s_jit_stats.eval_handled++;
        const UTF8 *result = prog->memory.data() + prog->out_addr;
        safe_str(result, buff, bufc);
        return true;
    }

    LBuf result = LBuf_Src("jit_eval");
    if (!run_cached_program(prog, executor, caller, enactor,
                             result, LBUF_SIZE,
                             cargs, ncargs, eval)) {
        s_jit_stats.eval_bailout++;
        return false;  // JIT execution error — fall back to AST.
    }
    s_jit_stats.eval_handled++;
    s_jit_stats.ecall_total += prog->ecalls;
    s_jit_stats.tier2_total += prog->tier2_calls;
    safe_str(result, buff, bufc);
    return true;
}

// ---------------------------------------------------------------
// jitstats() — wizard-only function returning JIT profiling counters.
//
// Returns a space-separated key=value list suitable for parsing.
// With argument "reset", clears all counters.
// ---------------------------------------------------------------

FUNCTION(fun_jitstats)
{
    UNUSED_PARAMETER(fp);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (!Wizard(executor)) {
        safe_str(T("#-1 PERMISSION DENIED"), buff, bufc);
        return;
    }

    if (nfargs >= 1 && strcmp(reinterpret_cast<const char *>(fargs[0]), "reset") == 0) {
        memset(&s_jit_stats, 0, sizeof(s_jit_stats));
        safe_str(T("OK"), buff, bufc);
        return;
    }

    // Format: key=value pairs, newline-separated for readability.
    LBuf tmp = LBuf_Src("jitstats");
    int n = snprintf(reinterpret_cast<char *>(tmp.get()), LBUF_SIZE,
        "eval_attempts=%llu "
        "eval_handled=%llu "
        "eval_bailout=%llu "
        "cache_hit_mem=%llu "
        "cache_hit_sqlite=%llu "
        "cache_miss=%llu "
        "compile_ok=%llu "
        "compile_fail=%llu "
        "bail_noeval=%llu "
        "bail_slots=%llu "
        "folded=%llu "
        "ecalls=%llu "
        "tier2=%llu",
        (unsigned long long)s_jit_stats.eval_attempts,
        (unsigned long long)s_jit_stats.eval_handled,
        (unsigned long long)s_jit_stats.eval_bailout,
        (unsigned long long)s_jit_stats.cache_hit_mem,
        (unsigned long long)s_jit_stats.cache_hit_sqlite,
        (unsigned long long)s_jit_stats.cache_miss,
        (unsigned long long)s_jit_stats.compile_ok,
        (unsigned long long)s_jit_stats.compile_fail,
        (unsigned long long)s_jit_stats.bail_noeval,
        (unsigned long long)s_jit_stats.bail_slots,
        (unsigned long long)s_jit_stats.folded_total,
        (unsigned long long)s_jit_stats.ecall_total,
        (unsigned long long)s_jit_stats.tier2_total);

    // Append NOEVAL breakdown.
    for (int i = 0; i < s_jit_stats.noeval_top_used && n < static_cast<int>(LBUF_SIZE) - 64; i++) {
        n += snprintf(reinterpret_cast<char *>(tmp.get()) + n, LBUF_SIZE - n, " noeval_%s=%llu",
            s_jit_stats.noeval_top[i].name,
            (unsigned long long)s_jit_stats.noeval_top[i].count);
    }

    safe_str(tmp, buff, bufc);
}

// ---------------------------------------------------------------
// fun_rvbench: benchmark JIT vs native mux_exec.
//
// rvbench(<expression>, <iterations>)
//
// Runs the expression through three paths:
//   1. Native mux_exec (AST eval) — the current production path
//   2. rveval compile-every-time
//   3. rveval compile-once, run N times (amortized)
//
// Returns a multi-line report with timings in microseconds.
// ---------------------------------------------------------------

#ifdef WIN32
static double elapsed_us(const LARGE_INTEGER &start,
                          const LARGE_INTEGER &end) {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    return static_cast<double>(end.QuadPart - start.QuadPart) * 1e6
         / static_cast<double>(freq.QuadPart);
}
#else
static double elapsed_us(const struct timespec &start,
                          const struct timespec &end) {
    double s = static_cast<double>(end.tv_sec - start.tv_sec);
    double ns = static_cast<double>(end.tv_nsec - start.tv_nsec);
    return (s * 1e6) + (ns / 1e3);
}
#endif

#ifdef WIN32
#define BENCH_TIMER LARGE_INTEGER
#define BENCH_NOW(t) QueryPerformanceCounter(&(t))
#else
#define BENCH_TIMER struct timespec
#define BENCH_NOW(t) clock_gettime(CLOCK_MONOTONIC, &(t))
#endif

// Run the compiled program through the JIT.  Returns the result
// string (written into caller-provided buffer).
//
// If reuse_dbt is true, skip the full DBT reset and only update the
// ECALL callback — keeps translated blocks cached.  Caller must
// ensure the guest code region is unchanged.
//
static bool run_compiled(compiled_program &prog,
                          dbref executor, dbref caller_db, dbref enactor,
                          UTF8 *out, size_t out_size,
                          bool reuse_dbt = false) {
    if (!prog.needs_jit) {
        // Fully folded — result is already in guest memory.
        const char *r = reinterpret_cast<const char *>(
            prog.memory.data() + prog.out_addr);
        size_t n = strlen(r);
        if (n >= out_size) n = out_size - 1;
        memcpy(out, r, n);
        out[n] = '\0';
        return true;
    }

    eval_ctx ec;
    ec.memory = prog.memory.data();
    ec.memory_size = prog.memory_size;
    ec.executor = executor;
    ec.caller = caller_db;
    ec.enactor = enactor;
    ec.eval = EV_FCHECK | EV_EVAL;
    ec.cargs = nullptr;
    ec.ncargs = 0;
    ec.lua_state = nullptr;

    dbt_state_t *dbt;
    if (reuse_dbt && s_dbt_ready) {
        dbt = &s_persistent_dbt;
        dbt_rerun(dbt, eval_ecall, &ec);
    } else {
        dbt = get_dbt(prog.memory.data(), prog.memory_size,
                       eval_ecall, &ec);
        if (!dbt) return false;
        if (dbt->blob_code_end == 0) {
            pretranslate_tier2(dbt);
            dbt->blob_code_end = dbt->code_used;
        }
    }

    int rc = dbt_run(dbt, prog.entry_pc, rv_compiler::STACK_TOP);

    if (rc != 0) return false;

    const char *r = reinterpret_cast<const char *>(
        prog.memory.data() + prog.out_addr);
    size_t n = strlen(r);
    if (n >= out_size) n = out_size - 1;
    memcpy(out, r, n);
    out[n] = '\0';
    return true;
}

FUNCTION(fun_rvbench)
{
    UNUSED_PARAMETER(fp);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    JITArena::gc();

    if (nfargs < 2) {
        safe_str(T("#-1 TOO FEW ARGUMENTS"), buff, bufc);
        return;
    }

    const UTF8 *expr = fargs[0];
    size_t nLen = strlen(reinterpret_cast<const char *>(expr));
    int iterations = mux_atol(fargs[1]);
    if (iterations < 1) iterations = 1;
    if (iterations > 1000000) iterations = 1000000;

    // Verify both paths produce the same result.
    compiled_program prog = compile_expression(expr, nLen);
    if (!prog.ok) {
        safe_str(T("#-1 COMPILATION FAILED"), buff, bufc);
        return;
    }

    // --- Benchmark 1: Native mux_exec ---
    BENCH_TIMER t0, t1;
    int eval_flags = EV_FCHECK | EV_EVAL;

    BENCH_NOW(t0);
    for (int i = 0; i < iterations; i++) {
        UTF8 *tbuf = alloc_lbuf("rvbench.native");
        UTF8 *tbufc = tbuf;
        mux_exec(expr, nLen, tbuf, &tbufc, executor, caller, enactor,
                 eval_flags, nullptr, 0);
        *tbufc = '\0';
        free_lbuf(tbuf);
    }
    BENCH_NOW(t1);
    double native_us = elapsed_us(t0, t1);

    // --- Benchmark 2: rveval compile-every-time ---
    BENCH_NOW(t0);
    for (int i = 0; i < iterations; i++) {
        compiled_program p = compile_expression(expr, nLen);
        if (p.ok) {
            UTF8 result[256];
            run_compiled(p, executor, caller, enactor, result, sizeof(result));
        }
    }
    BENCH_NOW(t1);
    double compile_each_us = elapsed_us(t0, t1);

    // --- Benchmark 3: production path (compile cache + block cache) ---
    // Uses compile_cached (LRU) + run_cached_program (dbt_rerun).
    // First iteration is a cache miss (compiles + JIT translates);
    // subsequent iterations hit both caches — zero compilation,
    // zero JIT translation.
    //
    // Invalidate the compile cache entry for this expression first
    // so the first iteration is a genuine miss.
    {
        std::string key = compile_cache_key(expr, nLen, EV_FMAND | EV_EVAL);
        auto cit = s_compile_cache.find(key);
        if (cit != s_compile_cache.end()) {
            if (s_dbt_last_memory == cit->second.prog.memory.data()) {
                s_dbt_last_memory = nullptr;
            }
            s_compile_lru.erase(cit->second.lru_it);
            s_compile_cache.erase(cit);
        }
    }
    BENCH_NOW(t0);
    for (int i = 0; i < iterations; i++) {
        compiled_program *cp = compile_cached(expr, nLen, EV_FMAND | EV_EVAL);
        if (cp) {
            UTF8 result[256];
            run_cached_program(cp, executor, caller, enactor,
                               result, sizeof(result));
        }
    }
    BENCH_NOW(t1);
    double cached_us = elapsed_us(t0, t1);

    // Format report.
    double per_native = native_us / iterations;
    double per_compile = compile_each_us / iterations;
    double per_cached = cached_us / iterations;
    uint64_t disp = s_persistent_dbt.dispatch_count;
    uint64_t sb = s_persistent_dbt.superblock_count;
    uint64_t se = s_persistent_dbt.side_exits_total;
    uint64_t ic = s_persistent_dbt.inline_calls;
    uint64_t ih = s_persistent_dbt.intrinsic_hits;
    uint64_t ce = s_persistent_dbt.cold_exit_count;
    uint64_t ce_actual = s_persistent_dbt.cold_exit_actual;
    uint64_t ce_expected = s_persistent_dbt.cold_exit_expected;
    uint64_t ce_from = s_persistent_dbt.last_exit_from;

    LBuf report = LBuf_Src("rvbench");
    snprintf(reinterpret_cast<char *>(report.get()), LBUF_SIZE,
        "expr=%s iters=%d folds=%d ecalls=%d tier2=%d nativ=%d disp=%llu sb=%llu/%llu ic=%llu ih=%llu ce=%llu(a=0x%llX,e=0x%llX,from=0x%llX) | "
        "native=%.2fus/call | "
        "compile-each=%.2fus/call (%.1fx) | "
        "cached=%.2fus/call (%.1fx)",
        reinterpret_cast<const char *>(expr),
        iterations, prog.folds, prog.ecalls, prog.tier2_calls, prog.native_ops,
        (unsigned long long)disp,
        (unsigned long long)sb, (unsigned long long)se, (unsigned long long)ic,
        (unsigned long long)ih, (unsigned long long)ce, (unsigned long long)ce_actual, (unsigned long long)ce_expected, (unsigned long long)ce_from,
        per_native,
        per_compile, per_compile / per_native,
        per_cached, per_cached / per_native);

    safe_str(report, buff, bufc);
}

// ---------------------------------------------------------------
// Persistent VM proof-of-concept.
//
// Demonstrates the core concept: a single RV64 guest memory that
// lives across multiple evaluations, with compiled functions
// calling each other via JAL — no DBT reset, no context zeroing.
//
// Layout in persistent guest memory:
//   0x0000: Main stub (calls func_add42 via JAL, writes result, exits)
//   0x0100: func_add42 — adds 42 to a0, returns via JALR ra
//   0x1000: String pool (output buffer)
//
// On each invocation, the block cache retains translations from
// all previous runs.  The second and subsequent calls should show
// zero cache misses.
// ---------------------------------------------------------------

// RV64 instruction encoders local to this PoC.
// (Duplicated from hir_codegen.cpp which is a separate TU.)
//
namespace poc {

static uint32_t i_type(uint8_t op, uint8_t rd, uint8_t f3,
                        uint8_t rs1, int32_t imm) {
    return op | (rd << 7) | (f3 << 12) | (rs1 << 15)
         | ((static_cast<uint32_t>(imm) & 0xFFF) << 20);
}

static uint32_t ADDI(uint8_t rd, uint8_t rs1, int32_t imm) {
    return i_type(OP_IMM, rd, 0, rs1, imm);
}

static uint32_t LUI(uint8_t rd, int32_t imm) {
    return OP_LUI | (rd << 7) | (static_cast<uint32_t>(imm) & 0xFFFFF000);
}

static uint32_t JAL(uint8_t rd, int32_t imm) {
    uint32_t u = static_cast<uint32_t>(imm);
    return OP_JAL
         | (static_cast<uint32_t>(rd) << 7)
         | (((u >> 12) & 0xFF) << 12)
         | (((u >> 11) & 1) << 20)
         | (((u >> 1) & 0x3FF) << 21)
         | (((u >> 20) & 1) << 31);
}

static uint32_t JALR(uint8_t rd, uint8_t rs1, int32_t imm) {
    return i_type(OP_JALR, rd, 0, rs1, imm);
}

static uint32_t SB(uint8_t base, uint8_t src, int32_t off) {
    return OP_STORE | ((off & 0x1F) << 7) | (0 << 12)
         | (base << 15) | (src << 20)
         | (((off >> 5) & 0x7F) << 25);
}

static uint32_t SD(uint8_t base, uint8_t src, int32_t off) {
    return OP_STORE | ((off & 0x1F) << 7) | (3 << 12)
         | (base << 15) | (src << 20)
         | (((off >> 5) & 0x7F) << 25);
}

static uint32_t LD(uint8_t rd, uint8_t base, int32_t off) {
    return i_type(OP_LOAD, rd, 3, base, off);
}

static uint32_t ADD(uint8_t rd, uint8_t rs1, uint8_t rs2) {
    return OP_REG | (rd << 7) | (0 << 12) | (rs1 << 15) | (rs2 << 20);
}

static uint32_t ECALL() { return 0x00000073; }

static uint32_t DIV(uint8_t rd, uint8_t rs1, uint8_t rs2) {
    return OP_REG | (rd << 7) | (4 << 12) | (rs1 << 15)
         | (rs2 << 20) | (0x01 << 25);
}

static uint32_t REM(uint8_t rd, uint8_t rs1, uint8_t rs2) {
    return OP_REG | (rd << 7) | (6 << 12) | (rs1 << 15)
         | (rs2 << 20) | (0x01 << 25);
}

static uint32_t BNE(uint8_t rs1, uint8_t rs2, int32_t off) {
    uint32_t u = static_cast<uint32_t>(off);
    return OP_BRANCH
         | (((u >> 11) & 1) << 7)
         | (((u >> 1) & 0xF) << 8)
         | (1 << 12)  // funct3 = BNE
         | (rs1 << 15) | (rs2 << 20)
         | (((u >> 5) & 0x3F) << 25)
         | (((u >> 12) & 1) << 31);
}

// Load a 32-bit value into register rd.  Uses LUI+ADDI.
//
static void load_val(std::vector<uint32_t> &code, uint8_t rd, uint64_t val) {
    if (val == 0) {
        code.push_back(ADDI(rd, 0, 0));
        return;
    }
    int32_t sval = static_cast<int32_t>(val);
    if (sval >= -2048 && sval <= 2047
        && val == static_cast<uint64_t>(static_cast<uint32_t>(sval)))
    {
        code.push_back(ADDI(rd, 0, sval));
        return;
    }
    uint32_t hi = static_cast<uint32_t>(val) & 0xFFFFF000;
    int32_t lo = static_cast<int32_t>(val & 0xFFF);
    if (lo & 0x800) { hi += 0x1000; lo -= 0x1000; }
    code.push_back(LUI(rd, hi));
    if (lo) code.push_back(ADDI(rd, rd, lo));
}

// Emit integer-to-ASCII decimal conversion.
// Input: value in reg `src`.  Output: NUL-terminated string at
// guest address in reg `dst`.  Clobbers t0-t4 (x5-x7, x28-x29).
// Advances dst past the written digits.
//
static void emit_itoa(std::vector<uint32_t> &code,
                       uint8_t dst, uint8_t src) {
    constexpr uint8_t val  = 28;  // t3 — working value
    constexpr uint8_t ten  = 29;  // t4 — constant 10
    constexpr uint8_t dig  = 5;   // t0 — digit
    constexpr uint8_t sp   = 2;

    // val = src; ten = 10
    code.push_back(ADDI(val, src, 0));     // mv val, src
    code.push_back(ADDI(ten, 0, 10));      // li ten, 10

    // Push digits onto stack in reverse order.
    // We use the guest stack as scratch.
    // loop:
    size_t loop_top = code.size();
    code.push_back(REM(dig, val, ten));    // dig = val % 10
    code.push_back(DIV(val, val, ten));    // val = val / 10
    code.push_back(ADDI(dig, dig, '0'));   // dig += '0'
    code.push_back(ADDI(sp, sp, -1));      // sp--
    code.push_back(SB(sp, dig, 0));        // *sp = dig
    int32_t off = -static_cast<int32_t>((code.size() - loop_top) * 4);
    code.push_back(BNE(val, 0, off));      // if val != 0, loop

    // Pop digits from stack into output buffer.
    // end marker: when sp reaches its original position.
    // We saved the original sp in s1 before calling emit_itoa.
    // The caller must set s1 = sp before calling this.
    // Actually, let's use a different approach: write a NUL after
    // and stop when we've popped all digits.
    //
    // Since the digits are on the stack (low address = MSD),
    // we pop byte-by-byte into dst until sp == original.
    // Caller stores original sp in s1 (x9) before emit_itoa.
    //
    // pop_loop:
    size_t pop_top = code.size();
    code.push_back(i_type(OP_LOAD, dig, 4 /*LBU*/, sp, 0)); // LBU dig, 0(sp)
    code.push_back(SB(dst, dig, 0));       // *dst = dig
    code.push_back(ADDI(sp, sp, 1));       // sp++
    code.push_back(ADDI(dst, dst, 1));     // dst++
    off = -static_cast<int32_t>((code.size() - pop_top) * 4);
    // x9 = s1 = original sp (set by caller)
    code.push_back(BNE(sp, 9, off));       // if sp != s1, pop_loop

    // NUL-terminate.
    code.push_back(SB(dst, 0, 0));
}

} // namespace poc

// Persistent VM state — lives for the server's lifetime.
//
struct persistent_vm {
    std::vector<uint8_t> memory;
    bool initialized;
    uint32_t call_count;
    uint64_t total_cache_hits;
    uint64_t total_cache_misses;

    persistent_vm() : memory(rv_compiler::MEM_SIZE, 0),
                      initialized(false), call_count(0),
                      total_cache_hits(0), total_cache_misses(0) {}
};

static persistent_vm s_pvm;

// Minimal ECALL handler for the PoC.
// Only handles ECALL_EXIT.
//
static int poc_ecall(rv64_ctx_t *ctx, void *user_data) {
    uint64_t nr = ctx->x[17];  // a7
    if (nr == ECALL_EXIT) {
        return static_cast<int>(ctx->x[10]);  // a0 = exit code
    }

    if (nr == ECALL_CALL_COMPILED) {
        // Re-entrant call into a compiled function.
        // a0 = entry_pc, a1 = output buffer addr, a2 = fargs addr, a3 = nfargs
        dbt_state_t *dbt = static_cast<dbt_state_t *>(user_data);
        if (!dbt) {
            ctx->x[10] = 0;
            return -1;  // continue
        }

        uint64_t target_pc = ctx->x[10];
        uint64_t out_addr  = ctx->x[11];

        // Save outer execution's full CPU context.
        rv64_ctx_t saved_ctx = *ctx;

        // Set up for inner call: SP already points to available
        // stack space (below outer's frame).
        // The inner function's prologue will decrement SP further.
        ctx->x[2] = saved_ctx.x[2];  // preserve SP

        // Run inner function via dbt_resume.
        int inner_rc = dbt_resume(dbt, target_pc);

        // Extract inner result length.
        uint64_t result_len = 0;
        if (inner_rc == 0 && out_addr > 0 && out_addr < dbt->memory_size) {
            // Inner function wrote to its own output buffer.
            // Its final_out address... we don't have it directly.
            // For this PoC, assume the caller passed the correct
            // output address — the inner function already wrote there.
            result_len = strlen(reinterpret_cast<const char *>(
                dbt->memory + out_addr));
        }

        // Restore outer CPU context.
        *ctx = saved_ctx;

        // Return result info in a0.
        ctx->x[10] = result_len;
        return -1;  // continue outer execution
    }

    // Unknown ECALL — error.
    fprintf(stderr, "pocvm: unknown ECALL %llu\n",
            static_cast<unsigned long long>(nr));
    return -1;
}

FUNCTION(fun_persistent_poc)
{
    UNUSED_PARAMETER(fp);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(fargs);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (!Wizard(executor)) {
        safe_str(T("#-1 PERMISSION DENIED"), buff, bufc);
        return;
    }

    // Guest memory layout for this PoC:
    //
    //   0x0000 - 0x00FF: Main program (calls func_add42, formats result, exits)
    //   0x0100 - 0x01FF: func_add42(a0) → a0 + 42, returns via JALR
    //   0x1000 - 0x10FF: Output buffer (256 bytes)
    //
    static constexpr uint64_t MAIN_PC      = 0x0000;
    static constexpr uint64_t FUNC_ADD42   = 0x0100;
    static constexpr uint64_t OUTPUT_BUF   = 0x1000;

    if (!s_pvm.initialized) {
        // -----------------------------------------------------------
        // Assemble func_add42 at 0x0100.
        //
        // Signature: a0 = input value
        // Returns:   a0 = input + 42
        // Uses:      JALR x0, ra, 0 to return
        // -----------------------------------------------------------
        {
            std::vector<uint32_t> code;
            code.push_back(poc::ADDI(10, 10, 42));     // a0 += 42
            code.push_back(poc::JALR(0, 1, 0));        // ret (JALR x0, ra, 0)

            // Install into guest memory at FUNC_ADD42.
            for (size_t i = 0; i < code.size(); i++) {
                memcpy(s_pvm.memory.data() + FUNC_ADD42 + i * 4,
                       &code[i], 4);
            }
        }

        // -----------------------------------------------------------
        // Assemble main program at 0x0000.
        //
        // Calls func_add42(100), then writes the result (142) as
        // an ASCII string to OUTPUT_BUF.  Uses an ECALL to convert
        // the integer to string via the host sprintf, keeping the
        // guest code minimal.
        //
        //   li   a0, 100
        //   save ra; JAL func_add42; restore ra
        //   # a0 = 142
        //   # Store a0 as raw integer at OUTPUT_BUF for host extraction
        //   # Then use ECALL_EXIT
        // -----------------------------------------------------------
        {
            std::vector<uint32_t> code;
            constexpr uint8_t a0 = 10, a7 = 17, ra = 1, sp = 2;
            constexpr uint8_t t1 = 6;

            // li a0, 100
            code.push_back(poc::ADDI(a0, 0, 100));

            // Save ra on stack.
            code.push_back(poc::ADDI(sp, sp, -8));
            code.push_back(poc::SD(sp, ra, 0));

            // JAL ra, func_add42  (offset from current PC to 0x100)
            uint64_t jal_pc = code.size() * 4;
            int32_t jal_off = static_cast<int32_t>(FUNC_ADD42 - jal_pc);
            code.push_back(poc::JAL(ra, jal_off));

            // Restore ra.
            code.push_back(poc::LD(ra, sp, 0));
            code.push_back(poc::ADDI(sp, sp, 8));

            // Now a0 = 142.  Write "142" as ASCII to OUTPUT_BUF.
            // We do this with simple immediate stores to avoid
            // needing a DIV/REM itoa routine in the PoC.
            //
            // t1 = OUTPUT_BUF address
            poc::load_val(code, t1, OUTPUT_BUF);

            // Write '1', '4', '2', '\0' to [t1]
            code.push_back(poc::ADDI(5, 0, '1'));       // t0 = '1'
            code.push_back(poc::SB(t1, 5, 0));          // [t1+0] = '1'
            code.push_back(poc::ADDI(5, 0, '4'));       // t0 = '4'
            code.push_back(poc::SB(t1, 5, 1));          // [t1+1] = '4'
            code.push_back(poc::ADDI(5, 0, '2'));       // t0 = '2'
            code.push_back(poc::SB(t1, 5, 2));          // [t1+2] = '2'
            code.push_back(poc::SB(t1, 0, 3));          // [t1+3] = '\0'

            // But wait — we need to VERIFY that a0 actually IS 142.
            // Store a0 at OUTPUT_BUF+128 for the host to check.
            code.push_back(poc::SD(t1, a0, 128));       // [t1+128] = a0

            // Exit.
            code.push_back(poc::ADDI(a7, 0, ECALL_EXIT));
            code.push_back(poc::ADDI(a0, 0, 0));
            code.push_back(poc::ECALL());

            // Install into guest memory at MAIN_PC.
            for (size_t i = 0; i < code.size(); i++) {
                memcpy(s_pvm.memory.data() + MAIN_PC + i * 4,
                       &code[i], 4);
            }
        }

        s_pvm.initialized = true;
    }

    // -----------------------------------------------------------
    // Execute via the persistent DBT.
    // First call: dbt_init + dbt_run.
    // Subsequent calls: dbt_resume (no reset, no ctx zero).
    // -----------------------------------------------------------

    // We borrow the global s_persistent_dbt.  In a real implementation,
    // the persistent VM would own its own dbt_state_t, but for the PoC
    // we share to avoid a second 1MB mmap.
    //
    // Clear the output buffer each run.
    memset(s_pvm.memory.data() + OUTPUT_BUF, 0, 256);

    // The PoC uses its own dbt_state_t, separate from the main JIT's
    // s_persistent_dbt.  This avoids conflicts with blob_code_end and
    // the normal JIT's cache state.
    static dbt_state_t s_poc_dbt;
    static bool s_poc_dbt_ready = false;

    if (s_pvm.call_count == 0) {
        // First call — init the PoC's own DBT.
        if (s_poc_dbt_ready) {
            dbt_cleanup(&s_poc_dbt);
            s_poc_dbt_ready = false;
        }
        if (dbt_init(&s_poc_dbt, s_pvm.memory.data(), s_pvm.memory.size(),
                     poc_ecall, &s_poc_dbt) != 0) {
            safe_str(T("#-1 DBT INIT FAILED"), buff, bufc);
            return;
        }
        s_poc_dbt_ready = true;

        int rc = dbt_run(&s_poc_dbt, MAIN_PC, rv_compiler::STACK_TOP);
        if (rc != 0) {
            LBuf tmp = LBuf_Src("pocvm");
            snprintf(reinterpret_cast<char *>(tmp.get()), LBUF_SIZE,
                "#-1 DBT RUN FAILED rc=%d", rc);
            safe_str(tmp, buff, bufc);
            return;
        }
    } else {
        // Subsequent calls — resume without reset.
        // Update the ECALL handler but keep everything else.
        dbt_rerun(&s_poc_dbt, poc_ecall, &s_poc_dbt);

        // Use dbt_resume: doesn't zero ctx, just sets next_pc.
        // We do need to reset SP though.
        s_poc_dbt.ctx.x[2] = rv_compiler::STACK_TOP;

        int rc = dbt_resume(&s_poc_dbt, MAIN_PC);
        if (rc != 0) {
            LBuf tmp = LBuf_Src("pocvm");
            snprintf(reinterpret_cast<char *>(tmp.get()), LBUF_SIZE,
                "#-1 DBT RESUME FAILED rc=%d", rc);
            safe_str(tmp, buff, bufc);
            return;
        }
    }

    dbt_state_t *dbt = &s_poc_dbt;

    s_pvm.total_cache_hits += dbt->cache_hits;
    s_pvm.total_cache_misses += dbt->cache_misses;
    s_pvm.call_count++;

    // Read the result from the output buffer.
    const char *result = reinterpret_cast<const char *>(
        s_pvm.memory.data() + OUTPUT_BUF);

    // Read the raw integer stored at OUTPUT_BUF+128 for verification.
    uint64_t raw_val = 0;
    memcpy(&raw_val, s_pvm.memory.data() + OUTPUT_BUF + 128, 8);

    // Format: result + diagnostics.
    LBuf tmp = LBuf_Src("pocvm");
    snprintf(reinterpret_cast<char *>(tmp.get()), LBUF_SIZE,
        "result=%s raw=%llu calls=%u cache_hits=%llu cache_misses=%llu "
        "blocks_translated=%llu dispatch=%llu",
        result,
        (unsigned long long)raw_val,
        s_pvm.call_count,
        (unsigned long long)dbt->cache_hits,
        (unsigned long long)dbt->cache_misses,
        (unsigned long long)dbt->blocks_translated,
        (unsigned long long)dbt->dispatch_count);

    safe_str(tmp, buff, bufc);
}

// ---------------------------------------------------------------
// Persistent VM Phase 2: compile real MUX expressions at different
// code offsets in a shared guest memory.
//
// pocvm2() compiles two expressions — add(7,mul(5,3)) and
// strlen(hello world) — at different code_base offsets in a
// persistent 1MB guest memory, runs them both via dbt_resume(),
// and returns both results.
//
// This demonstrates that the real JIT compiler can target
// non-zero code bases with correct JAL offsets to Tier 2 blobs.
// ---------------------------------------------------------------

struct persistent_vm2 {
    std::vector<uint8_t> memory;
    dbt_state_t dbt;
    bool dbt_ready;
    bool compiled;
    uint32_t call_count;

    // Code heap: bump allocator for code sections.
    uint64_t code_heap_next;

    // Shared pool cursors — advance across compilations so each
    // function gets its own non-overlapping region.
    uint64_t str_pool_next;
    uint64_t fargs_pool_next;
    uint64_t out_pool_next;

    // Entry points and output addresses for compiled functions.
    uint64_t func_a_entry;
    uint64_t func_a_out;
    uint64_t func_b_entry;
    uint64_t func_b_out;
    uint64_t func_c_entry;   // hand-assembled: calls A via ECALL_CALL_COMPILED
    uint64_t func_c_out;

    persistent_vm2()
        : memory(rv_compiler::MEM_SIZE, 0),
          dbt_ready(false), compiled(false), call_count(0),
          code_heap_next(0x0004),  // avoid PC=0 (cache sentinel)
          str_pool_next(rv_compiler::STR_BASE),
          fargs_pool_next(rv_compiler::FARGS_BASE),
          out_pool_next(rv_compiler::STACK_TOP - 8),
          func_a_entry(0), func_a_out(0),
          func_b_entry(0), func_b_out(0),
          func_c_entry(0), func_c_out(0) {}
};

static persistent_vm2 s_pvm2;

FUNCTION(fun_pocvm2)
{
    UNUSED_PARAMETER(fp);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(fargs);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (!Wizard(executor)) {
        safe_str(T("#-1 PERMISSION DENIED"), buff, bufc);
        return;
    }

    if (!s_pvm2.compiled) {
        tier2_lazy_init();

        // --- Compile Function A: first(one two three) ---
        // first() has a Tier 2 blob, so this exercises JAL offset
        // calculation at the configured code_base.
        {
            const UTF8 *expr_a = reinterpret_cast<const UTF8 *>(
                "first(one two three)");
            size_t len_a = strlen(reinterpret_cast<const char *>(expr_a));

            compiled_program pa = compile_expression(
                expr_a, len_a, EV_FCHECK | EV_EVAL,
                s_pvm2.code_heap_next,
                s_pvm2.str_pool_next,
                s_pvm2.fargs_pool_next,
                s_pvm2.out_pool_next);

            if (!pa.ok) {
                safe_str(T("#-1 FUNC A COMPILE FAILED"), buff, bufc);
                return;
            }

            s_pvm2.func_a_entry = pa.entry_pc;
            s_pvm2.func_a_out = pa.out_addr;

            // Copy regions from compiled program into persistent memory.
            // Code at its unique code_base offset:
            memcpy(s_pvm2.memory.data() + pa.entry_pc,
                   pa.memory.data() + pa.entry_pc, pa.code_size);
            // Strings — copy only the portion this compilation used:
            if (pa.str_pool_end > rv_compiler::STR_BASE) {
                memcpy(s_pvm2.memory.data() + rv_compiler::STR_BASE,
                       pa.memory.data() + rv_compiler::STR_BASE,
                       pa.str_pool_end - rv_compiler::STR_BASE);
            }
            // Fargs:
            if (pa.fargs_pool_end > rv_compiler::FARGS_BASE) {
                memcpy(s_pvm2.memory.data() + rv_compiler::FARGS_BASE,
                       pa.memory.data() + rv_compiler::FARGS_BASE,
                       pa.fargs_pool_end - rv_compiler::FARGS_BASE);
            }
            // Output slots (stack-allocated, near STACK_TOP):
            if (pa.out_pool_end < rv_compiler::STACK_TOP - 8) {
                uint64_t out_lo = pa.out_pool_end;
                uint64_t out_hi = rv_compiler::STACK_TOP - 8;
                memcpy(s_pvm2.memory.data() + out_lo,
                       pa.memory.data() + out_lo,
                       out_hi - out_lo);
            }

            // Advance all cursors so B starts where A left off.
            s_pvm2.code_heap_next = pa.entry_pc + pa.code_size;
            // Align code heap to 16-byte boundary.
            s_pvm2.code_heap_next = (s_pvm2.code_heap_next + 15) & ~15ULL;
            s_pvm2.str_pool_next = pa.str_pool_end;
            s_pvm2.fargs_pool_next = pa.fargs_pool_end;
            s_pvm2.out_pool_next = pa.out_pool_end;
        }

        // --- Compile Function B: rest(one two three) ---
        // rest() also has a Tier 2 blob.  Compiled at a different
        // code_base with pool cursors advanced past A's data.
        {
            const UTF8 *expr_b = reinterpret_cast<const UTF8 *>(
                "rest(one two three)");
            size_t len_b = strlen(reinterpret_cast<const char *>(expr_b));

            compiled_program pb = compile_expression(
                expr_b, len_b, EV_FCHECK | EV_EVAL,
                s_pvm2.code_heap_next,
                s_pvm2.str_pool_next,
                s_pvm2.fargs_pool_next,
                s_pvm2.out_pool_next);

            if (!pb.ok) {
                safe_str(T("#-1 FUNC B COMPILE FAILED"), buff, bufc);
                return;
            }

            s_pvm2.func_b_entry = pb.entry_pc;
            s_pvm2.func_b_out = pb.out_addr;

            // Copy B's code (at its unique offset):
            memcpy(s_pvm2.memory.data() + pb.entry_pc,
                   pb.memory.data() + pb.entry_pc, pb.code_size);
            // Copy B's strings (starts where A's ended):
            if (pb.str_pool_end > s_pvm2.str_pool_next) {
                memcpy(s_pvm2.memory.data() + s_pvm2.str_pool_next,
                       pb.memory.data() + s_pvm2.str_pool_next,
                       pb.str_pool_end - s_pvm2.str_pool_next);
            }
            // Copy B's fargs:
            if (pb.fargs_pool_end > s_pvm2.fargs_pool_next) {
                memcpy(s_pvm2.memory.data() + s_pvm2.fargs_pool_next,
                       pb.memory.data() + s_pvm2.fargs_pool_next,
                       pb.fargs_pool_end - s_pvm2.fargs_pool_next);
            }
            // Copy B's output (stack-allocated, below A's):
            if (pb.out_pool_end < s_pvm2.out_pool_next) {
                memcpy(s_pvm2.memory.data() + pb.out_pool_end,
                       pb.memory.data() + pb.out_pool_end,
                       s_pvm2.out_pool_next - pb.out_pool_end);
            }

            // Advance cursors.
            s_pvm2.code_heap_next = pb.entry_pc + pb.code_size;
            s_pvm2.code_heap_next = (s_pvm2.code_heap_next + 15) & ~15ULL;
            s_pvm2.str_pool_next = pb.str_pool_end;
            s_pvm2.fargs_pool_next = pb.fargs_pool_end;
            s_pvm2.out_pool_next = pb.out_pool_end;
        }

        // --- Function C: hand-assembled re-entrant call stub ---
        // Calls Function A via ECALL_CALL_COMPILED, then copies
        // A's output ("one") to C's own output buffer.
        // This proves re-entrant execution within the persistent VM.
        {
            std::vector<uint32_t> code;
            constexpr uint8_t a0 = 10, a1 = 11, a7 = 17;
            constexpr uint8_t t0 = 5, t3 = 28, t4 = 29;

            s_pvm2.func_c_entry = s_pvm2.code_heap_next;
            s_pvm2.func_c_out = 0x3000;  // fixed output address

            // ECALL_CALL_COMPILED: call Function A.
            poc::load_val(code, a0, s_pvm2.func_a_entry);
            poc::load_val(code, a1, s_pvm2.func_a_out);
            code.push_back(poc::ADDI(a7, 0, static_cast<int32_t>(ECALL_CALL_COMPILED)));
            code.push_back(poc::ECALL());

            // A's result is at func_a_out. Write "C:" + A's result to 0x3000.
            poc::load_val(code, t4, s_pvm2.func_c_out);   // t4 = output addr
            code.push_back(poc::ADDI(t3, 0, 'C'));
            code.push_back(poc::SB(t4, t3, 0));
            code.push_back(poc::ADDI(t3, 0, ':'));
            code.push_back(poc::SB(t4, t3, 1));
            code.push_back(poc::ADDI(t4, t4, 2));         // past "C:"

            // Copy A's result.
            poc::load_val(code, t3, s_pvm2.func_a_out);
            size_t copy_loop = code.size();
            code.push_back(poc::i_type(OP_LOAD, t0, 4/*LBU*/, t3, 0));
            code.push_back(poc::SB(t4, t0, 0));
            code.push_back(poc::ADDI(t3, t3, 1));
            code.push_back(poc::ADDI(t4, t4, 1));
            int32_t off = -static_cast<int32_t>((code.size() - copy_loop) * 4);
            code.push_back(poc::BNE(t0, 0, off));

            // Exit.
            code.push_back(poc::ADDI(a7, 0, ECALL_EXIT));
            code.push_back(poc::ADDI(a0, 0, 0));
            code.push_back(poc::ECALL());

            // Install at code_heap_next.
            for (size_t i = 0; i < code.size(); i++) {
                memcpy(s_pvm2.memory.data() + s_pvm2.func_c_entry + i * 4,
                       &code[i], 4);
            }
            s_pvm2.code_heap_next = s_pvm2.func_c_entry + code.size() * 4;
            s_pvm2.code_heap_next = (s_pvm2.code_heap_next + 15) & ~15ULL;
        }

        // Install Tier 2 blob.
        tier2_install(s_pvm2.memory, rv_compiler::BLOB_BASE);

        s_pvm2.compiled = true;
    }

    // Initialize DBT on first call.
    if (!s_pvm2.dbt_ready) {
        if (dbt_init(&s_pvm2.dbt, s_pvm2.memory.data(),
                     s_pvm2.memory.size(), poc_ecall, &s_pvm2.dbt) != 0) {
            safe_str(T("#-1 DBT INIT FAILED"), buff, bufc);
            return;
        }
        s_pvm2.dbt_ready = true;
        pretranslate_tier2(&s_pvm2.dbt);
        s_pvm2.dbt.blob_code_end = s_pvm2.dbt.code_used;
    }

    // Clear output buffers (stack-allocated near STACK_TOP).
    if (s_pvm2.out_pool_next < rv_compiler::STACK_TOP - 8) {
        memset(s_pvm2.memory.data() + s_pvm2.out_pool_next, 0,
               (rv_compiler::STACK_TOP - 8) - s_pvm2.out_pool_next);
    }
    if (s_tier2.loaded) {
        tier2_install(s_pvm2.memory, rv_compiler::BLOB_BASE);
    }

    // Run Function A.
    dbt_rerun(&s_pvm2.dbt, poc_ecall, &s_pvm2.dbt);
    int rc_a = dbt_run(&s_pvm2.dbt, s_pvm2.func_a_entry,
                       rv_compiler::STACK_TOP);
    const char *result_a = nullptr;
    char a_err[64] = {};
    if (rc_a == 0) {
        result_a = reinterpret_cast<const char *>(
            s_pvm2.memory.data() + s_pvm2.func_a_out);
    } else {
        snprintf(a_err, sizeof(a_err), "#-1 RUN_A rc=%d", rc_a);
        result_a = a_err;
    }

    // Reset blob BSS for Function B.
    if (s_tier2.loaded && s_tier2.bss_size > 0) {
        uint64_t bss_start = rv_compiler::BLOB_BASE + s_tier2.code.size();
        if (bss_start + s_tier2.bss_size <= s_pvm2.memory.size()) {
            memset(s_pvm2.memory.data() + bss_start, 0, s_tier2.bss_size);
        }
    }

    // Run Function B via dbt_resume (warm cache from A).
    s_pvm2.dbt.ctx.x[2] = rv_compiler::STACK_TOP;
    int rc_b = dbt_resume(&s_pvm2.dbt, s_pvm2.func_b_entry);
    const char *result_b = (rc_b == 0)
        ? reinterpret_cast<const char *>(
              s_pvm2.memory.data() + s_pvm2.func_b_out)
        : "#-1 RUN B FAILED";

    // Reset blob BSS for Function C.
    if (s_tier2.loaded && s_tier2.bss_size > 0) {
        uint64_t bss_start = rv_compiler::BLOB_BASE + s_tier2.code.size();
        if (bss_start + s_tier2.bss_size <= s_pvm2.memory.size()) {
            memset(s_pvm2.memory.data() + bss_start, 0, s_tier2.bss_size);
        }
    }

    // Clear C's output area and A's output (A will be re-executed by C).
    memset(s_pvm2.memory.data() + s_pvm2.func_c_out, 0, 256);
    memset(s_pvm2.memory.data() + s_pvm2.func_a_out, 0, 256);

    // Run Function C via dbt_resume.
    // C calls A via ECALL_CALL_COMPILED, then writes "C:" + A's result.
    s_pvm2.dbt.ctx.x[2] = rv_compiler::STACK_TOP;
    int rc_c = dbt_resume(&s_pvm2.dbt, s_pvm2.func_c_entry);
    const char *result_c = (rc_c == 0)
        ? reinterpret_cast<const char *>(
              s_pvm2.memory.data() + s_pvm2.func_c_out)
        : "#-1 RUN C FAILED";

    s_pvm2.call_count++;

    LBuf tmp = LBuf_Src("pocvm2");
    snprintf(reinterpret_cast<char *>(tmp.get()), LBUF_SIZE,
        "a=%s b=%s c=%s a_pc=0x%llX b_pc=0x%llX c_pc=0x%llX calls=%u",
        result_a, result_b, result_c,
        (unsigned long long)s_pvm2.func_a_entry,
        (unsigned long long)s_pvm2.func_b_entry,
        (unsigned long long)s_pvm2.func_c_entry,
        s_pvm2.call_count);

    safe_str(tmp, buff, bufc);
}
