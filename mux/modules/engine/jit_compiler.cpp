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
// Unifies with the engine's lbuf_ref/reg_ref packing system.
// ---------------------------------------------------------------

class JITArena {
public:
    struct Arena {
        lbuf_ref *lr;
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
        auto it = s_arenas.find(id);
        if (it != s_arenas.end()) {
            it->second->lr->refcount++;
        }
    }

    static void Release(uint32_t id) {
        auto it = s_arenas.find(id);
        if (it != s_arenas.end()) {
            Arena *a = it->second;
            // Check refcount before BufRelease potentially frees lr.
            bool last_ref = (a->lr->refcount <= 1);
            BufRelease(a->lr);
            if (last_ref) {
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

    // Release arenas that have no external references (refcount == 1
    // means only the arena's self-ownership remains).  Called between
    // evaluations to bound arena lifetime.
    static void gc() {
        s_current = nullptr;
        auto it = s_arenas.begin();
        while (it != s_arenas.end()) {
            Arena *a = it->second;
            if (a->lr->refcount <= 1) {
                BufRelease(a->lr);
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
        a->lr = alloc_lbufref("JITArena");
        a->lr->lbuf_ptr = alloc_lbuf("JITArena");
        a->lr->refcount = 1;
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

tier2_state s_tier2 = { false, {}, {}, {}, 0 };

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

        // Blocked — rv64_* diverges from server:
        //   SORT       Shellsort vs DUCET collation
        //   BEFORE/AFTER  byte match vs color-aware search
        //   ISNUM/ISINT   hand parser vs is_real()/is_integer()
        //   CHR/ORD       ASCII-only vs Unicode/grapheme-aware
        //   SECURE/SQUISH/TRANSLATE  byte-level vs Unicode
        //   STRMATCH/MATCH/GRAB/GRABALL  may diverge on Unicode
        //   DELETE/ELEMENTS/WORDPOS/REMOVE/REVWORDS/FLIP  untested
        //   LNUM/DEC2HEX/HEX2DEC/ISDBREF  untested
        //   LADD/LMAX/LMIN/LAND/LOR  untested
        // These need parity tests in testcases/ before re-enabling.

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
    { "SECURE",      "rv64_secure" },
    { "SQUISH",      "rv64_squish" },
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
    if (fread(&hdr, sizeof(hdr), 1, f) != 1) { fclose(f); return false; }
    if (hdr.magic != RV64_BLOB_MAGIC || hdr.version != RV64_BLOB_VERSION) {
        fclose(f);
        return false;
    }

    // Read code section.
    s_tier2.code.resize(hdr.code_size);
    fseek(f, hdr.code_offset, SEEK_SET);
    if (fread(s_tier2.code.data(), hdr.code_size, 1, f) != 1) {
        fclose(f);
        return false;
    }

    // Read rodata section (if present).
    if (hdr.rodata_size > 0 && hdr.rodata_offset > 0) {
        s_tier2.rodata.resize(hdr.rodata_size);
        fseek(f, hdr.rodata_offset, SEEK_SET);
        if (fread(s_tier2.rodata.data(), hdr.rodata_size, 1, f) != 1) {
            fclose(f);
            return false;
        }
    }

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
        s_tier2.rodata.empty() ? nullptr : s_tier2.rodata.data(),
        entries.empty() ? nullptr : entries.data(),
    };
    const size_t sizes[] = {
        sizeof(hdr),
        s_tier2.code.size(),
        s_tier2.rodata.size(),
        entries.size() * sizeof(rv64_blob_entry),
    };
    s_blob_version = sha1_hex_parts(parts, sizes, 4);
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
    if (guest_base + s_tier2.code.size() > memory.size()) return;
    memcpy(memory.data() + guest_base,
           s_tier2.code.data(), s_tier2.code.size());
    if (!s_tier2.rodata.empty()) {
        uint64_t rodata_base = guest_base + s_tier2.code.size();
        rodata_base = (rodata_base + 7) & ~7ULL;
        if (rodata_base + s_tier2.rodata.size() <= memory.size()) {
            memcpy(memory.data() + rodata_base,
                   s_tier2.rodata.data(), s_tier2.rodata.size());
        }
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
                                            int eval = EV_FCHECK | EV_EVAL) {
    tier2_lazy_init();

    compiled_program prog;
    prog.ok = false;
    prog.out_used = 0;
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
    rv_compiler rc;

    // Install Tier 2 blob into guest memory (if loaded).
    tier2_install(rc.memory, rv_compiler::BLOB_BASE);

    // Set compile-time eval flags for unknown-function resolution.
    s_compile_eval = eval;
    s_fcheck_available = (eval & EV_FCHECK) != 0;

    hir_program h;
    h.init();
    qreg_init();
    h.result = hir_lower_node(h, rc, ast.get());

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

    // Copy code to guest memory.
    for (size_t i = 0; i < rc.code.size() && i * 4 < rv_compiler::CODE_LIMIT; i++) {
        memcpy(rc.memory.data() + i * 4, &rc.code[i], 4);
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
    prog.out_used = rc.out_pool - rv_compiler::OUT_BASE;
    prog.ok = true;
    s_jit_stats.compile_ok++;
    prog.folds = h.folds;
    prog.ecalls = h.ecalls;
    prog.tier2_calls = h.tier2_calls;
    prog.native_ops = h.native_ops;
    prog.needs_jit = h.needs_jit;
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
    prog.out_used = rv_compiler::OUT_LIMIT - rv_compiler::OUT_BASE;  // conservative
    prog.ok = true;
    prog.needs_jit = rec.needs_jit != 0;
    prog.folds = rec.folds;
    prog.ecalls = rec.ecalls;
    prog.tier2_calls = rec.tier2_calls;
    prog.native_ops = rec.native_ops;
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
                     prog.tier2_calls, prog.native_ops);
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
        // Memory cache hit — move to front of LRU.
        s_compile_lru.splice(s_compile_lru.begin(), s_compile_lru,
                             it->second.lru_it);
        s_jit_stats.cache_hit_mem++;
        return &it->second.prog;
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
            // rec.memory_blob points into SQLite statement memory.
            // reconstruct_from_cache copies it; then we must release
            // the statement to avoid holding a read lock.
            prog = reconstruct_from_cache(rec);
            db.CodeCacheReset();
            from_sqlite = true;
            s_jit_stats.cache_hit_sqlite++;
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
    // Two output ranges around the blob gap:
    //   Range 1: OUT_BASE (0x8000) to OUT_GAP_LO (0x10000) — below blob
    //   Range 2: OUT_GAP_HI (0x30000) to CARGS end — above blob
    // Do NOT zero the blob region (0x10000-0x2FFFF)!
    memset(prog->memory.data() + rv_compiler::OUT_BASE, 0,
           rv_compiler::OUT_GAP_LO - rv_compiler::OUT_BASE);
    // Clear from above-blob to end of SUBST region (covers output range 2,
    // CARGS, and SUBST slots).
    uint64_t subst_end = rv_compiler::SUBST_BASE
                       + rv_compiler::SUBST_COUNT * rv_compiler::SUBST_SLOT;
    memset(prog->memory.data() + rv_compiler::OUT_GAP_HI, 0,
           subst_end - rv_compiler::OUT_GAP_HI);

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

    int rc = dbt_run(dbt, 0, rv_compiler::STACK_TOP);
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

    fp->fun(fp, buff, &bufc, ec->executor, ec->caller, ec->enactor,
            ec->eval, fargs, nfargs, ec->cargs, ec->ncargs);

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
                memcpy(a->lr->lbuf_ptr + off, data, length);
                a->lr->lbuf_ptr[off + length] = '\0';
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
                    UTF8 buf[LBUF_SIZE]; UTF8 *bufc = buf;
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
                    UTF8 buf[LBUF_SIZE]; UTF8 *bufc = buf;
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
                        UTF8 buf[LBUF_SIZE]; UTF8 *bufc = buf;
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
                    UTF8 buf[LBUF_SIZE]; UTF8 *bufc = buf;
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
                UTF8 buf[LBUF_SIZE]; UTF8 *bufc = buf;
                fval(buf, &bufc, result);
                *bufc = '\0';
                size_t len = bufc - buf;
                if (len >= out_size) len = out_size - 1;
                memcpy(out, buf, len);
                out[len] = '\0';
                ctx->x[10] = static_cast<uint64_t>(len);
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
                memcpy(a->lr->lbuf_ptr + off, value, vlen);
                a->lr->lbuf_ptr[off + vlen] = '\0';

                // Bind to register.
                if (mudstate.global_regs[regnum]) {
                    RegRelease(mudstate.global_regs[regnum]);
                }
                reg_ref *rr = alloc_regref("JITArena.pack");
                rr->refcount = 1;
                rr->lbuf = a->lr;
                a->lr->refcount++;
                rr->reg_ptr = a->lr->lbuf_ptr + off;
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
            UTF8 buf[LBUF_SIZE];
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
                UTF8 buf[LBUF_SIZE];
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
                UTF8 buf[LBUF_SIZE];
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

    default:
        ctx->x[10] = 0;
        return -1;
    }
}

// ---------------------------------------------------------------
// jit_eval: try to compile and execute an expression via the JIT.
//
// Returns true if the JIT handled it (result written to buff/bufc).
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

    UTF8 result[LBUF_SIZE];
    if (!run_cached_program(prog, executor, caller, enactor,
                             result, sizeof(result),
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
    char tmp[LBUF_SIZE];
    int n = snprintf(tmp, sizeof(tmp),
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
    for (int i = 0; i < s_jit_stats.noeval_top_used && n < static_cast<int>(sizeof(tmp)) - 64; i++) {
        n += snprintf(tmp + n, sizeof(tmp) - n, " noeval_%s=%llu",
            s_jit_stats.noeval_top[i].name,
            (unsigned long long)s_jit_stats.noeval_top[i].count);
    }

    safe_str(reinterpret_cast<UTF8 *>(tmp), buff, bufc);
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

static double elapsed_us(const struct timespec &start,
                          const struct timespec &end) {
    double s = static_cast<double>(end.tv_sec - start.tv_sec);
    double ns = static_cast<double>(end.tv_nsec - start.tv_nsec);
    return (s * 1e6) + (ns / 1e3);
}

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

    int rc = dbt_run(dbt, 0, rv_compiler::STACK_TOP);

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
    struct timespec t0, t1;
    int eval_flags = EV_FCHECK | EV_EVAL;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < iterations; i++) {
        UTF8 *tbuf = alloc_lbuf("rvbench.native");
        UTF8 *tbufc = tbuf;
        mux_exec(expr, nLen, tbuf, &tbufc, executor, caller, enactor,
                 eval_flags, nullptr, 0);
        *tbufc = '\0';
        free_lbuf(tbuf);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double native_us = elapsed_us(t0, t1);

    // --- Benchmark 2: rveval compile-every-time ---
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < iterations; i++) {
        compiled_program p = compile_expression(expr, nLen);
        if (p.ok) {
            UTF8 result[256];
            run_compiled(p, executor, caller, enactor, result, sizeof(result));
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
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
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < iterations; i++) {
        compiled_program *cp = compile_cached(expr, nLen, EV_FMAND | EV_EVAL);
        if (cp) {
            UTF8 result[256];
            run_cached_program(cp, executor, caller, enactor,
                               result, sizeof(result));
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
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

    UTF8 report[LBUF_SIZE];
    snprintf(reinterpret_cast<char *>(report), sizeof(report),
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
