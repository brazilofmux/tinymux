/*! \file dbt_compile.cpp
 * \brief Compiler spike: softcode expression → RV64 via AST.
 *
 * Takes a MUX expression string, parses it via the existing AST
 * parser, and compiles it to RV64 instructions that call engine
 * functions through the ECALL convention.
 *
 * Optimization: constant folding.  When a function call has all
 * compile-time-constant arguments, the compiler evaluates it at
 * compile time using libmux functions (is_integer, mux_atof, fval,
 * etc.) and emits only the result string.  This eliminates ECALL
 * overhead, lbuf allocation, hash lookup, and JIT translation for
 * the folded sub-expressions.  For fully-constant expressions like
 * add(mul(3,4),5), the entire program reduces to a string literal
 * with zero RV64 instructions executed.
 *
 * Supported AST node types:
 *   - AST_FUNCCALL with literal or compiled arguments
 *   - AST_LITERAL, AST_SPACE (string constants)
 *   - AST_SEQUENCE (concatenation)
 *   - AST_EVALBRACKET (evaluate and use result)
 *
 * Guest memory layout:
 *   0x0000..0x0FFF  code (up to 1024 instructions)
 *   0x1000..0x3FFF  string pool (literals, function names)
 *   0x4000..0x7FFF  fargs arrays (one per call)
 *   0x8000..0xEFFF  output buffers (one per sub-expression, 256 each)
 *   0xF000..0xFFFF  stack
 *
 * Compilation strategy (bottom-up):
 *   1. Leaf nodes (literals) → constant with known value
 *   2. Function calls with all-constant args → try constant fold
 *   3. If fold succeeds → result is a new constant (no code emitted)
 *   4. If fold fails → emit ECALL (runtime dispatch)
 *   5. Sequences of constants → merge at compile time
 *   6. Mixed sequences → emit strcat() ECALL
 *
 * The final result is either a string pool constant or the last
 * output slot allocated.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"
#include "sqlite_backend.h"
#include "ast.h"

#include "dbt.h"
#include "dbt_decoder.h"
#include "engine_api.h"
#include "hir.h"

#include "../../rv64/rv64blob.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <cmath>
#include <ctime>
#include <algorithm>
#include <vector>
#include <string>
#include <map>

// ---------------------------------------------------------------
// Tier 2: pre-compiled RV64 library blob
// ---------------------------------------------------------------

struct tier2_entry {
    uint32_t code_off;    // offset within blob code section
    uint32_t guest_addr;  // absolute guest address after loading
};

static struct {
    bool loaded;
    std::vector<uint8_t> code;           // code section bytes
    std::vector<uint8_t> rodata;         // rodata section bytes
    std::map<std::string, tier2_entry> funcs;  // name → entry
    uint64_t guest_base;                 // where code is loaded in guest memory
} s_tier2 = { false, {}, {}, {}, 0 };

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
    return true;
}

// Look up a function by MUX name (uppercase).
// Returns guest address, or 0 if not found.
//
static uint64_t tier2_lookup(const std::string &mux_name) {
    if (!s_tier2.loaded) return 0;
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

// Forward declarations for host co_* functions (in libmux.so).
// These are the Ragel-generated native implementations.
//
extern "C" {
    size_t co_first(unsigned char *, const unsigned char *, size_t, unsigned char);
    size_t co_rest(unsigned char *, const unsigned char *, size_t, unsigned char);
    size_t co_last(unsigned char *, const unsigned char *, size_t, unsigned char);
    size_t co_words_count(const unsigned char *, size_t, unsigned char);
    size_t co_repeat(unsigned char *, const unsigned char *, size_t, size_t);
    size_t co_mid(unsigned char *, const unsigned char *, size_t, size_t, size_t);
    size_t co_pos(const unsigned char *, size_t, const unsigned char *, size_t);
    size_t co_member(const unsigned char *, size_t, const unsigned char *, size_t, unsigned char);
    size_t co_trim(unsigned char *, const unsigned char *, size_t, unsigned char, int);
    size_t co_delete(unsigned char *, const unsigned char *, size_t, size_t, unsigned char, unsigned char);
    size_t co_sort_words(unsigned char *, const unsigned char *, size_t, unsigned char, unsigned char, char);
    size_t co_extract(unsigned char *, const unsigned char *, size_t, size_t, size_t, unsigned char, unsigned char);
    size_t co_setunion(unsigned char *, const unsigned char *, size_t, const unsigned char *, size_t, unsigned char, unsigned char, char);
    size_t co_setdiff(unsigned char *, const unsigned char *, size_t, const unsigned char *, size_t, unsigned char, unsigned char, char);
    size_t co_setinter(unsigned char *, const unsigned char *, size_t, const unsigned char *, size_t, unsigned char, unsigned char, char);

    // Batch 2: inner co_* functions called by wrappers.
    size_t co_cluster_count(const unsigned char *, size_t);
    size_t co_tolower(unsigned char *, const unsigned char *, size_t);
    size_t co_toupper(unsigned char *, const unsigned char *, size_t);
    size_t co_reverse(unsigned char *, const unsigned char *, size_t);
    size_t co_escape(unsigned char *, const unsigned char *, size_t);
    size_t co_left(unsigned char *, const unsigned char *, size_t, size_t);
    size_t co_right(unsigned char *, const unsigned char *, size_t, size_t);
    size_t co_compress(unsigned char *, const unsigned char *, size_t, unsigned char);
    size_t co_lpos(unsigned char *, const unsigned char *, size_t, unsigned char);
}

static void pretranslate_tier2(dbt_state_t *dbt) {
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
static void tier2_install(std::vector<uint8_t> &memory, uint64_t guest_base) {
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

// ---------------------------------------------------------------
// Blob version for cache invalidation
// ---------------------------------------------------------------

// Simple blob version: size + entry count.  If the blob changes
// (new functions, recompiled at different offsets), this changes.
//
static std::string s_blob_version;

static std::string compute_blob_version() {
    if (!s_tier2.loaded || s_tier2.code.empty()) return "none";
    char buf[64];
    snprintf(buf, sizeof(buf), "%zu:%zu",
             s_tier2.code.size(), s_tier2.funcs.size());
    return buf;
}

// ---------------------------------------------------------------
// Compiler state
// ---------------------------------------------------------------

struct rv_compiler {
    std::vector<uint8_t> memory;
    std::vector<uint32_t> code;

    // Pool allocators (bump pointers into guest memory).
    uint64_t str_pool;      // next free byte in string pool
    uint64_t fargs_pool;    // next free byte in fargs area
    uint64_t out_pool;      // next free byte in output area
    uint64_t final_out;     // guest addr of final result

    // Statistics.
    int folds;              // number of constant-folded calls
    int ecalls;             // number of runtime ECALL calls
    int native_ops;         // number of native arithmetic ops
    bool needs_jit;         // true if any runtime code was emitted

    static constexpr size_t MEM_SIZE     = 256 * 1024;
    static constexpr uint64_t CODE_BASE  = 0x0000;
    static constexpr uint64_t CODE_LIMIT = 0x1000;
    static constexpr uint64_t STR_BASE   = 0x1000;
    static constexpr uint64_t STR_LIMIT  = 0x4000;
    static constexpr uint64_t FARGS_BASE = 0x4000;
    static constexpr uint64_t FARGS_LIMIT= 0x8000;
    static constexpr uint64_t OUT_BASE   = 0x8000;
    static constexpr uint64_t OUT_LIMIT  = 0xF000;
    static constexpr int      OUT_SLOT   = 256;
    static constexpr uint64_t STACK_TOP  = 0xFFF0;

    // Tier 2 blob loaded at 0x10000 (above the per-expression region).
    static constexpr uint64_t BLOB_BASE  = 0x10000;
    static constexpr uint64_t BLOB_LIMIT = 0x40000;  // 192KB for blob + rodata

    rv_compiler() : memory(MEM_SIZE, 0),
                    str_pool(STR_BASE),
                    fargs_pool(FARGS_BASE),
                    out_pool(OUT_BASE),
                    final_out(0),
                    folds(0),
                    ecalls(0),
                    native_ops(0),
                    needs_jit(false) {}

    // Allocate string in pool, return guest addr.
    uint64_t pool_str(const char *s, size_t len) {
        uint64_t addr = str_pool;
        if (addr + len + 1 > STR_LIMIT) return 0;
        memcpy(memory.data() + addr, s, len);
        memory[addr + len] = '\0';
        str_pool = (addr + len + 1 + 7) & ~7ULL;
        return addr;
    }

    uint64_t pool_str(const std::string &s) {
        return pool_str(s.c_str(), s.size());
    }

    // Allocate fargs array, return guest addr.
    // Writes the pointer values into guest memory.
    uint64_t alloc_fargs(const std::vector<uint64_t> &ptrs) {
        uint64_t addr = fargs_pool;
        size_t sz = ptrs.size() * 8;
        if (addr + sz > FARGS_LIMIT) return 0;
        for (size_t i = 0; i < ptrs.size(); i++) {
            memcpy(memory.data() + addr + i * 8, &ptrs[i], 8);
        }
        fargs_pool = (addr + sz + 7) & ~7ULL;
        return addr;
    }

    // Allocate output buffer slot, return guest addr.
    uint64_t alloc_output() {
        uint64_t addr = out_pool;
        if (addr + OUT_SLOT > OUT_LIMIT) return 0;
        out_pool += OUT_SLOT;
        return addr;
    }
};


// ---------------------------------------------------------------
// RV64 instruction encoding
// ---------------------------------------------------------------

static uint32_t rv_i_type(uint8_t opcode, uint8_t rd, uint8_t funct3,
                           uint8_t rs1, int32_t imm) {
    return opcode | (rd << 7) | (funct3 << 12) | (rs1 << 15)
         | ((static_cast<uint32_t>(imm) & 0xFFF) << 20);
}

static uint32_t rv_u_type(uint8_t opcode, uint8_t rd, int32_t imm) {
    return opcode | (rd << 7) | (static_cast<uint32_t>(imm) & 0xFFFFF000);
}

static uint32_t rv_ADDI(uint8_t rd, uint8_t rs1, int32_t imm) {
    return rv_i_type(OP_IMM, rd, ALU_ADDI, rs1, imm);
}
static uint32_t rv_LUI(uint8_t rd, int32_t imm) {
    return rv_u_type(OP_LUI, rd, imm);
}
static uint32_t rv_ECALL() {
    return rv_i_type(OP_SYSTEM, 0, 0, 0, 0);
}

// R-type encoding for register-register ALU ops.
//
static uint32_t rv_r_type(uint8_t opcode, uint8_t rd, uint8_t funct3,
                           uint8_t rs1, uint8_t rs2, uint8_t funct7) {
    return opcode | (rd << 7) | (funct3 << 12) | (rs1 << 15)
         | (rs2 << 20) | (static_cast<uint32_t>(funct7) << 25);
}
static uint32_t rv_ADD(uint8_t rd, uint8_t rs1, uint8_t rs2) {
    return rv_r_type(OP_REG, rd, ALU_ADD, rs1, rs2, 0x00);
}
static uint32_t rv_SUB(uint8_t rd, uint8_t rs1, uint8_t rs2) {
    return rv_r_type(OP_REG, rd, ALU_ADD, rs1, rs2, 0x20);
}

// B-type encoding (branches).
//
static uint32_t rv_b_type(uint8_t funct3, uint8_t rs1, uint8_t rs2,
                           int32_t imm) {
    uint32_t u = static_cast<uint32_t>(imm);
    return OP_BRANCH
         | (((u >> 11) & 1) << 7)
         | (((u >> 1) & 0xF) << 8)
         | (static_cast<uint32_t>(funct3) << 12)
         | (static_cast<uint32_t>(rs1) << 15)
         | (static_cast<uint32_t>(rs2) << 20)
         | (((u >> 5) & 0x3F) << 25)
         | (((u >> 12) & 1) << 31);
}
static uint32_t rv_BEQ(uint8_t rs1, uint8_t rs2, int32_t off) {
    return rv_b_type(BR_BEQ, rs1, rs2, off);
}
static uint32_t rv_BNE(uint8_t rs1, uint8_t rs2, int32_t off) {
    return rv_b_type(BR_BNE, rs1, rs2, off);
}
static uint32_t rv_BGE(uint8_t rs1, uint8_t rs2, int32_t off) {
    return rv_b_type(BR_BGE, rs1, rs2, off);
}
static uint32_t rv_BGEU(uint8_t rs1, uint8_t rs2, int32_t off) {
    return rv_b_type(BR_BGEU, rs1, rs2, off);
}

// S-type encoding (stores).
//
static uint32_t rv_SB(uint8_t base, uint8_t src, int32_t off) {
    uint32_t u = static_cast<uint32_t>(off);
    return OP_STORE
         | ((u & 0x1F) << 7)
         | (static_cast<uint32_t>(ST_SB) << 12)
         | (static_cast<uint32_t>(base) << 15)
         | (static_cast<uint32_t>(src) << 20)
         | (((u >> 5) & 0x7F) << 25);
}

// Load byte unsigned.
//
static uint32_t rv_LBU(uint8_t rd, uint8_t base, int32_t off) {
    return rv_i_type(OP_LOAD, rd, LD_LBU, base, off);
}

// Store doubleword.
//
static uint32_t rv_SD(uint8_t base, uint8_t src, int32_t off) {
    uint32_t u = static_cast<uint32_t>(off);
    return OP_STORE
         | ((u & 0x1F) << 7)
         | (static_cast<uint32_t>(ST_SD) << 12)
         | (static_cast<uint32_t>(base) << 15)
         | (static_cast<uint32_t>(src) << 20)
         | (((u >> 5) & 0x7F) << 25);
}

// Load doubleword.
//
static uint32_t rv_LD(uint8_t rd, uint8_t base, int32_t off) {
    return rv_i_type(OP_LOAD, rd, LD_LD, base, off);
}

// J-type encoding (JAL).
//
static uint32_t rv_JAL(uint8_t rd, int32_t imm) {
    uint32_t u = static_cast<uint32_t>(imm);
    return OP_JAL
         | (static_cast<uint32_t>(rd) << 7)
         | (((u >> 12) & 0xFF) << 12)
         | (((u >> 11) & 1) << 20)
         | (((u >> 1) & 0x3FF) << 21)
         | (((u >> 20) & 1) << 31);
}

// Inline string copy: copy NUL-terminated string from src_reg to dest_reg.
// Clobbers t0 (x5).  5 instructions (byte-by-byte loop).
//
static void rv_emit_strcpy(std::vector<uint32_t> &code,
                            uint8_t dest_reg, uint8_t src_reg) {
    constexpr uint8_t t0 = 5;
    // loop:
    size_t loop = code.size();
    code.push_back(rv_LBU(t0, src_reg, 0));             // LBU t0, 0(src)
    code.push_back(rv_SB(dest_reg, t0, 0));              // SB t0, 0(dest)
    code.push_back(rv_ADDI(src_reg, src_reg, 1));        // src++
    code.push_back(rv_ADDI(dest_reg, dest_reg, 1));      // dest++
    int32_t off = -static_cast<int32_t>((code.size() - loop) * 4);
    code.push_back(rv_BNE(t0, 0, off));                  // BNE t0, x0, loop
}

// M extension: MUL, DIV, REM.
//
static uint32_t rv_MUL(uint8_t rd, uint8_t rs1, uint8_t rs2) {
    return rv_r_type(OP_REG, rd, 0, rs1, rs2, 0x01);
}
static uint32_t rv_DIV(uint8_t rd, uint8_t rs1, uint8_t rs2) {
    return rv_r_type(OP_REG, rd, 4, rs1, rs2, 0x01);
}
static uint32_t rv_REM(uint8_t rd, uint8_t rs1, uint8_t rs2) {
    return rv_r_type(OP_REG, rd, 6, rs1, rs2, 0x01);
}

// ---------------------------------------------------------------
// Inline RISC-V atoi: parse decimal string → signed integer.
//
// Input:  addr_reg = guest address of NUL-terminated string
// Output: out_reg  = signed 64-bit integer
// Clobbers: t0(x5), t1(x6), t2(x7), t3(x28), t4(x29), addr_reg
// 19 instructions.
// ---------------------------------------------------------------

static void rv_emit_atoi(std::vector<uint32_t> &code,
                          uint8_t addr_reg, uint8_t out_reg) {
    constexpr uint8_t t0=5, t1=6, t2=7, t3=28, t4=29;

    code.push_back(rv_ADDI(t1, 0, 0));                 //  0: acc = 0
    code.push_back(rv_ADDI(t2, 0, 0));                 //  1: sign = 0
    code.push_back(rv_LBU(t0, addr_reg, 0));           //  2: load byte
    code.push_back(rv_ADDI(t3, 0, 45));                //  3: t3 = '-'
    size_t bne_sign = code.size();
    code.push_back(0);                                  //  4: BNE → skip_sign (patch)
    code.push_back(rv_ADDI(t2, 0, 1));                 //  5: sign = 1
    code.push_back(rv_ADDI(addr_reg, addr_reg, 1));    //  6: advance past '-'
    // skip_sign:
    size_t skip_sign = code.size();
    code[bne_sign] = rv_BNE(t0, t3,
        static_cast<int32_t>((skip_sign - bne_sign) * 4));

    code.push_back(rv_LBU(t0, addr_reg, 0));           //  7: (re)load byte
    // digit_loop:
    size_t digit_loop = code.size();
    code.push_back(rv_ADDI(t4, t0, -48));              //  8: digit = byte - '0'
    code.push_back(rv_ADDI(t3, 0, 10));                //  9: t3 = 10
    size_t bgeu_done = code.size();
    code.push_back(0);                                  // 10: BGEU → done (patch)
    code.push_back(rv_MUL(t1, t1, t3));                // 11: acc *= 10
    code.push_back(rv_ADD(t1, t1, t4));                 // 12: acc += digit
    code.push_back(rv_ADDI(addr_reg, addr_reg, 1));    // 13: advance
    code.push_back(rv_LBU(t0, addr_reg, 0));           // 14: load next byte
    size_t bk = code.size();
    code.push_back(rv_BEQ(0, 0,                        // 15: j digit_loop
        static_cast<int32_t>((digit_loop - bk) * 4)));
    // done:
    size_t done = code.size();
    code[bgeu_done] = rv_BGEU(t4, t3,
        static_cast<int32_t>((done - bgeu_done) * 4));

    size_t beq_pos = code.size();
    code.push_back(0);                                  // 16: BEQ → skip_neg (patch)
    code.push_back(rv_SUB(t1, 0, t1));                 // 17: negate
    size_t skip_neg = code.size();
    code[beq_pos] = rv_BEQ(t2, 0,
        static_cast<int32_t>((skip_neg - beq_pos) * 4));

    code.push_back(rv_ADDI(out_reg, t1, 0));           // 18: mv out, acc
}

// ---------------------------------------------------------------
// Inline RISC-V itoa: signed integer → decimal string.
//
// Input:  val_reg = signed 64-bit integer
//         buf_reg = guest address of output buffer (≥21 bytes)
// Output: NUL-terminated string at buf_reg
// Clobbers: t0(x5), t1(x6), t2(x7), t3(x28), t4(x29),
//           t5(x30), t6(x31), buf_reg
// 30 instructions.
// ---------------------------------------------------------------

static void rv_emit_itoa(std::vector<uint32_t> &code,
                          uint8_t val_reg, uint8_t buf_reg) {
    constexpr uint8_t t0=5, t1=6, t2=7, t3=28, t4=29, t5=30, t6=31;

    code.push_back(rv_ADDI(t0, buf_reg, 0));           //  0: wr = buf
    code.push_back(rv_ADDI(t1, val_reg, 0));            //  1: t1 = val
    size_t bge_pos = code.size();
    code.push_back(0);                                  //  2: BGE → skip_neg (patch)
    code.push_back(rv_ADDI(t4, 0, 45));                //  3: t4 = '-'
    code.push_back(rv_SB(t0, t4, 0));                  //  4: write '-'
    code.push_back(rv_ADDI(t0, t0, 1));                //  5: advance wr
    code.push_back(rv_SUB(t1, 0, t1));                 //  6: negate
    // skip_neg:
    size_t skip_neg = code.size();
    code[bge_pos] = rv_BGE(t1, 0,
        static_cast<int32_t>((skip_neg - bge_pos) * 4));

    code.push_back(rv_ADDI(t5, t0, 0));                //  7: digit_start = wr
    size_t bne_nz = code.size();
    code.push_back(0);                                  //  8: BNE → digit_loop (patch)
    code.push_back(rv_ADDI(t4, 0, 48));                //  9: '0'
    code.push_back(rv_SB(t0, t4, 0));                  // 10: write '0'
    code.push_back(rv_ADDI(t0, t0, 1));                // 11: advance
    size_t beq_nul = code.size();
    code.push_back(0);                                  // 12: BEQ → nul_term (patch)

    // digit_loop:
    size_t digit_loop = code.size();
    code[bne_nz] = rv_BNE(t1, 0,
        static_cast<int32_t>((digit_loop - bne_nz) * 4));
    code.push_back(rv_ADDI(t3, 0, 10));                // 13: t3 = 10
    code.push_back(rv_REM(t2, t1, t3));                // 14: t2 = val % 10
    code.push_back(rv_DIV(t1, t1, t3));                // 15: t1 = val / 10
    code.push_back(rv_ADDI(t2, t2, 48));               // 16: '0' + digit
    code.push_back(rv_SB(t0, t2, 0));                  // 17: write digit
    code.push_back(rv_ADDI(t0, t0, 1));                // 18: advance
    code.push_back(rv_BNE(t1, 0,                       // 19: loop if more
        static_cast<int32_t>((digit_loop - (code.size())) * 4)));

    // nul_term:
    size_t nul_term = code.size();
    code[beq_nul] = rv_BEQ(0, 0,
        static_cast<int32_t>((nul_term - beq_nul) * 4));
    code.push_back(rv_SB(t0, 0, 0));                   // 20: write '\0'
    code.push_back(rv_ADDI(t6, t0, -1));               // 21: end = wr - 1

    // reverse_loop:
    size_t rev_loop = code.size();
    size_t bge_rev = code.size();
    code.push_back(0);                                  // 22: BGE → done (patch)
    code.push_back(rv_LBU(t3, t5, 0));                 // 23: t3 = *start
    code.push_back(rv_LBU(t4, t6, 0));                 // 24: t4 = *end
    code.push_back(rv_SB(t5, t4, 0));                  // 25: *start = t4
    code.push_back(rv_SB(t6, t3, 0));                  // 26: *end = t3
    code.push_back(rv_ADDI(t5, t5, 1));                // 27: start++
    code.push_back(rv_ADDI(t6, t6, -1));               // 28: end--
    code.push_back(rv_BEQ(0, 0,                        // 29: j reverse_loop
        static_cast<int32_t>((rev_loop - (code.size())) * 4)));

    // done:
    size_t done = code.size();
    code[bge_rev] = rv_BGE(t5, t6,
        static_cast<int32_t>((done - bge_rev) * 4));
}

// Load a signed 64-bit value into a register.
//
static void rv_load_i64(std::vector<uint32_t> &code, uint8_t rd, int64_t val) {
    if (val >= -2048 && val <= 2047) {
        code.push_back(rv_ADDI(rd, 0, static_cast<int32_t>(val)));
        return;
    }
    if (val >= -2147483648LL && val <= 2147483647LL) {
        uint32_t uval = static_cast<uint32_t>(static_cast<int32_t>(val));
        uint32_t hi = uval & 0xFFFFF000;
        int32_t lo = static_cast<int32_t>(uval & 0xFFF);
        if (lo & 0x800) {
            hi += 0x1000;
            lo -= 0x1000;
        }
        code.push_back(rv_LUI(rd, static_cast<int32_t>(hi)));
        if (lo) code.push_back(rv_ADDI(rd, rd, lo));
        return;
    }
    // Values beyond 32-bit: load zero (shouldn't happen for typical softcode).
    code.push_back(rv_ADDI(rd, 0, 0));
}

// Load a value into a register using LUI + ADDI.
//
static void rv_load_val(std::vector<uint32_t> &code, uint8_t rd,
                         uint64_t val) {
    if (val == 0) {
        code.push_back(rv_ADDI(rd, 0, 0));
        return;
    }

    int32_t sval = static_cast<int32_t>(val);
    if (sval >= -2048 && sval <= 2047 && val == static_cast<uint64_t>(static_cast<uint32_t>(sval))) {
        code.push_back(rv_ADDI(rd, 0, sval));
        return;
    }

    uint32_t hi = static_cast<uint32_t>(val) & 0xFFFFF000;
    int32_t lo = static_cast<int32_t>(val & 0xFFF);
    if (lo & 0x800) {
        hi += 0x1000;
        lo = lo - 0x1000;
    }
    code.push_back(rv_LUI(rd, hi));
    if (lo) code.push_back(rv_ADDI(rd, rd, lo));
}

// Emit ECALL to call a function.
//
// If func_idx > 0, uses indexed dispatch (ECALL_CALL_INDEX, a0 = index).
// Otherwise, uses string dispatch (ECALL_CALL_FUNC, a0 = name_addr).
//
static void rv_emit_call(std::vector<uint32_t> &code,
                          uint64_t name_addr, uint64_t fargs_addr,
                          int nfargs, uint64_t out_addr, int out_size,
                          int func_idx = 0) {
    if (func_idx > 0) {
        // Indexed dispatch — no string lookup at runtime.
        code.push_back(rv_ADDI(17, 0, 0x101));    // a7 = ECALL_CALL_INDEX
        rv_load_val(code, 10, func_idx);            // a0 = function index
    } else {
        // String-based dispatch (fallback).
        code.push_back(rv_ADDI(17, 0, 0x100));    // a7 = ECALL_CALL_FUNC
        rv_load_val(code, 10, name_addr);           // a0 = name
    }
    rv_load_val(code, 11, fargs_addr);             // a1 = fargs
    code.push_back(rv_ADDI(12, 0, nfargs));        // a2 = nfargs
    rv_load_val(code, 13, out_addr);               // a3 = output
    rv_load_val(code, 14, out_size);               // a4 = outsize
    code.push_back(rv_ECALL());
}

static void rv_emit_exit(std::vector<uint32_t> &code) {
    code.push_back(rv_ADDI(17, 0, ECALL_EXIT));
    code.push_back(rv_ADDI(10, 0, 0));
    code.push_back(rv_ECALL());
}

// Emit a Tier 2 call: JAL to pre-compiled blob function.
// Calling convention: a0=output, a1=fargs, a2=nfargs.
// Return value in a0 (pointer to output buffer).
//
static void rv_emit_tier2_call(std::vector<uint32_t> &code,
                                uint64_t fargs_addr, int nfargs,
                                uint64_t out_addr, uint64_t func_guest_addr) {
    rv_load_val(code, 10, out_addr);                  // a0 = output
    rv_load_val(code, 11, fargs_addr);                // a1 = fargs
    code.push_back(rv_ADDI(12, 0, nfargs));           // a2 = nfargs

    // JAL ra, target — offset relative to current PC.
    uint64_t current_pc = code.size() * 4;  // guest PC of the JAL
    int32_t offset = static_cast<int32_t>(func_guest_addr - current_pc);
    code.push_back(rv_JAL(1, offset));                // JAL ra, blob_func
}

// ---------------------------------------------------------------
// Constant folding: evaluate known functions at compile time.
//
// Uses the same libmux functions (is_integer, mux_atof, fval, etc.)
// that the real engine functions use, so results are identical.
// ---------------------------------------------------------------

// Format a double result the same way fval() does: use a temporary
// buffer and call fval, then extract the string.
//
static std::string format_double(double val) {
    UTF8 buf[LBUF_SIZE];
    UTF8 *bufc = buf;
    fval(buf, &bufc, val);
    *bufc = '\0';
    return std::string(reinterpret_cast<const char *>(buf));
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

// Helper: cast std::string arg to const UTF8 *.
//
static inline const UTF8 *u8(const std::string &s) {
    return reinterpret_cast<const UTF8 *>(s.c_str());
}

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

    if (upper == "MID" && nargs == 3) {
        const std::string &s = args[0];
        long start = mux_atol(u8(args[1]));
        long len = mux_atol(u8(args[2]));
        if (start < 0 || len < 0 || static_cast<size_t>(start) >= s.size()) {
            result = "";
        } else {
            result = s.substr(static_cast<size_t>(start), static_cast<size_t>(len));
        }
        return true;
    }

    // --- FIRST(s) / REST(s) / WORDS(s) — space-delimited ---
    if (upper == "FIRST" && nargs >= 1) {
        const std::string &s = args[0];
        size_t pos = s.find(' ');
        result = (pos == std::string::npos) ? s : s.substr(0, pos);
        return true;
    }
    if (upper == "REST" && nargs >= 1) {
        const std::string &s = args[0];
        size_t pos = s.find(' ');
        if (pos == std::string::npos) { result = ""; }
        else {
            // Skip leading spaces after first word.
            size_t start = s.find_first_not_of(' ', pos);
            result = (start == std::string::npos) ? "" : s.substr(start);
        }
        return true;
    }
    if (upper == "WORDS" && nargs >= 1) {
        const std::string &s = args[0];
        if (s.empty()) { result = "0"; return true; }
        long count = 0;
        bool in_word = false;
        for (char c : s) {
            if (c == ' ') { in_word = false; }
            else if (!in_word) { in_word = true; count++; }
        }
        result = format_long(count);
        return true;
    }

    // --- POS(pattern, string) ---
    if (upper == "POS" && nargs == 2) {
        size_t pos = args[1].find(args[0]);
        result = (pos == std::string::npos) ? "0"
                 : format_long(static_cast<long>(pos + 1));
        return true;
    }

    // --- STRMATCH(s, pattern) — wildcard match ---
    // Only fold exact equality (no wildcards in constant patterns).
    if (upper == "STRMATCH" && nargs == 2) {
        // If pattern has no wildcards, it's just string comparison.
        if (args[1].find('*') == std::string::npos
            && args[1].find('?') == std::string::npos) {
            result = (args[0] == args[1]) ? "1" : "0";
            return true;
        }
        // Don't fold wildcards — too complex for compile time.
        return false;
    }

    return false;
}

// ---------------------------------------------------------------
// Type tracking for native integer arithmetic
// ---------------------------------------------------------------

// Functions known to always return integer strings.
//
static bool returns_int(const std::string &upper) {
    return upper == "RAND" || upper == "STRLEN" || upper == "WORDS"
        || upper == "POS" || upper == "EQ" || upper == "NEQ"
        || upper == "GT" || upper == "GTE" || upper == "LT" || upper == "LTE"
        || upper == "NOT" || upper == "T" || upper == "COMP"
        || upper == "INC" || upper == "DEC" || upper == "SIGN"
        || upper == "MOD" || upper == "ABS" || upper == "MAX"
        || upper == "MIN" || upper == "BOUND" || upper == "IDIV"
        || upper == "STRMATCH" || upper == "MEMBER";
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

static void qreg_init() {
    for (int i = 0; i < HIR_NUM_QREGS; i++) qreg[i] = -1;
    qreg_used = false;
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

static int hir_lower_node(hir_program &h, rv_compiler &rc,
                           const ASTNode *node);

// Lower a sequence node (string concatenation).
//
static int hir_lower_sequence(hir_program &h, rv_compiler &rc,
                               const ASTNode *node) {
    if (node->children.size() == 1) {
        return hir_lower_node(h, rc, node->children[0].get());
    }

    // Lower each child.
    std::vector<int> children;
    for (auto &child : node->children) {
        children.push_back(hir_lower_node(h, rc, child.get()));
    }

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

    // Mixed: emit STRCAT.  Convert any ints to strings first.
    for (auto &ci : children) {
        if (h.ty[ci] == TY_INT) {
            ci = h.emit(HIR_ITOA, TY_STRING, ci);
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

static int hir_lower_node(hir_program &h, rv_compiler &rc,
                           const ASTNode *node);

// Lower a NOEVAL child, stripping leading/trailing spaces.
//
// MUX NOEVAL functions (switch/case/if/iter) evaluate their arguments
// with EV_STRIP_CURLY, which strips leading/trailing whitespace.
// In the AST, the space after a comma becomes an AST_SPACE node at
// the start of the argument's sequence.  This helper skips those.
//
static int hir_lower_trimmed(hir_program &h, rv_compiler &rc,
                              const ASTNode *child) {
    if (child->type == AST_SEQUENCE && !child->children.empty()) {
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
        // Lower only the trimmed children.
        std::vector<int> parts;
        for (size_t i = first; i < last; i++) {
            parts.push_back(hir_lower_node(h, rc, child->children[i].get()));
        }
        if (parts.size() == 1) return parts[0];
        // Concatenate: ensure all parts are strings.
        for (auto &p : parts) {
            if (h.ty[p] == TY_INT) {
                p = h.emit(HIR_ITOA, TY_STRING, p);
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
                int val = hir_lower_node(h, rc, node->children[1].get());
                qreg[rn] = val;
                qreg_used = true;
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
                int val = hir_lower_node(h, rc, node->children[1].get());
                qreg[rn] = val;
                qreg_used = true;
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
            } else if (h.known_int[cond]) {
                cond = h.emit(HIR_ATOI, TY_INT, cond);
            } else {
                // Non-integer condition (string truth = non-empty).
                // Fall through to ECALL for safety.
                goto general_lowering;
            }
        }

        // Constant condition: fold — only lower the selected branch.
        if (h.kind[cond] == HIR_ICONST) {
            if (h.val[cond] != 0) {
                return hir_lower_node(h, rc, node->children[1].get());
            } else if (has_else) {
                return hir_lower_node(h, rc, node->children[2].get());
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

        // Lower true branch.
        h.cur_block = true_block;
        int true_val = hir_lower_node(h, rc, node->children[1].get());
        int true_exit = h.cur_block;  // might change with nested ifelse
        h.emit(HIR_BR, TY_VOID, -1, -1, merge_block);
        h.add_edge(true_exit, merge_block);

        // Lower false branch.
        h.cur_block = false_block;
        int false_val;
        if (has_else) {
            false_val = hir_lower_node(h, rc, node->children[2].get());
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
        }

        // Evaluate delimiters (child[2] = input, child[3] = output).
        int delim_val;
        if (nfargs >= 3) {
            delim_val = hir_lower_trimmed(h, rc, node->children[2].get());
            if (h.ty[delim_val] == TY_INT) {
                delim_val = h.emit(HIR_ITOA, TY_STRING, delim_val);
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
    // General function call lowering.
    // ---------------------------------------------------------------
general_lowering:

    // Lower arguments.
    std::vector<int> args;
    for (auto &child : node->children) {
        args.push_back(hir_lower_node(h, rc, child.get()));
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
    // Fall through to ECALL.
    // ---------------------------------------------------------------

    // Convert any TY_INT args to strings for the ECALL convention.
    for (auto &ai : args) {
        if (h.ty[ai] == TY_INT) {
            ai = h.emit(HIR_ITOA, TY_STRING, ai);
        }
    }

    int fidx = engine_api_lookup(upper.c_str());

    // Check Tier 2 blob before falling through to ECALL.
    uint64_t t2addr = tier2_lookup(upper);

    // ECALL/Tier2 results are always strings in guest memory.  If the
    // function is known to return integers (strlen, eq, etc.),
    // mark known_int so downstream ops can ATOI and use natively.
    int i = h.emit_call(TY_STRING, fidx,
                         args.data(), nargs);
    if (t2addr) {
        h.tier2_addr[i] = t2addr;
        h.tier2_calls++;
    } else {
        h.ecalls++;
    }
    if (returns_int(upper)) {
        h.known_int[i] = true;
    }
    h.needs_jit = true;
    return i;
}

static int hir_lower_node(hir_program &h, rv_compiler &rc,
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
            return hir_lower_node(h, rc, node->children[0].get());
        }
        {
            uint64_t addr = rc.pool_str("");
            return h.emit_sconst(addr, "");
        }

    case AST_SUBST:
        // ## (itext), #@ (inum), #$ (switch token).
        // Resolved from iter/switch context set during lowering.
        if (node->text.size() >= 2 && node->text[0] == '#') {
            if (node->text[1] == '#' && iter_itext_val >= 0) {
                return iter_itext_val;
            }
            if (node->text[1] == '@' && iter_inum1_val >= 0) {
                return iter_inum1_val;
            }
        }
        // Unresolvable substitution (no enclosing iter).
        {
            uint64_t addr = rc.pool_str("");
            return h.emit_sconst(addr, "");
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
// Walks the HIR instruction array and emits RV64 instructions.
// Each HIR instruction gets a "location" — either a guest memory
// address (TY_STRING) or an RV64 register (TY_INT).
// ===============================================================

struct hir_loc {
    uint64_t addr;       // guest memory address (for strings)
    uint8_t  reg;        // RV64 register (for integers)
    bool     in_reg;     // true if value is in a register
    int      spill_slot; // -1 = not spilled, >=0 = stack slot index
};

// Branch patch record for backpatching.
struct branch_patch {
    int code_idx;       // index into rc.code
    int target_blk;     // target block number
};

// ---------------------------------------------------------------
// Register allocation: linear scan over SSA live ranges
//
// Poletto-Sarkar algorithm.  Computes live intervals for all
// integer-typed SSA values, then assigns the 11 saved registers
// (s1-s11).  When register pressure exceeds 11, the interval
// ending furthest in the future is spilled to the RV64 stack.
// ---------------------------------------------------------------

// Allocatable integer registers: s1-s11 (x9, x18-x27).
static constexpr int RA_NUM_REGS = 11;
static constexpr uint8_t RA_REGS[RA_NUM_REGS] = {
    9, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27
};

// Scratch register for spill/reload (s0 = x8, callee-saved).
static constexpr uint8_t RA_SCRATCH = 8;

// Second scratch for two-operand instructions (t3 = x28).
// Safe because arithmetic ops don't call atoi/itoa.
static constexpr uint8_t RA_SCRATCH2 = 28;

struct live_interval {
    int     value;      // HIR instruction index (SSA value number)
    int     start;      // program point of definition
    int     end;        // program point of last use (inclusive)
};

struct reg_alloc_result {
    uint8_t reg[HIR_MAX_INSNS];        // assigned register (0 = spilled/none)
    int     spill_slot[HIR_MAX_INSNS]; // -1 = not spilled
    int     n_spill_slots;             // total spill slots used
};

// Returns true if HIR instruction i produces an integer that needs
// a register.
//
static bool needs_int_reg(hir_program &h, int i) {
    switch (h.kind[i]) {
    case HIR_ICONST:
    case HIR_ATOI:
    case HIR_ADD: case HIR_SUB: case HIR_MUL: case HIR_DIV: case HIR_REM:
    case HIR_NEG: case HIR_ABS: case HIR_SIGN:
    case HIR_MAX: case HIR_MIN:
    case HIR_EQ:  case HIR_NE:  case HIR_GT:  case HIR_LT:
    case HIR_GE:  case HIR_LE:
    case HIR_NOT: case HIR_BOOL:
    case HIR_INC: case HIR_DEC:
        return true;
    case HIR_PHI:
        return h.ty[i] == TY_INT;
    case HIR_COPY:
        return h.ty[i] == TY_INT;
    default:
        return false;
    }
}

// Compute live intervals for all integer-typed SSA values.
//
static void compute_live_ranges(hir_program &h,
                                 std::vector<live_interval> &intervals) {
    // Assign program points in codegen order (blocks in layout order,
    // instructions within each block in order).
    int prog_point[HIR_MAX_INSNS];
    int block_end_pp[HIR_MAX_BLOCKS];
    memset(prog_point, -1, sizeof(int) * h.n_insns);

    int pp = 0;
    for (int b = 0; b < h.n_blocks; b++) {
        if (h.block_first[b] <= h.block_last[b]) {
            for (int i = h.block_first[b]; i <= h.block_last[b]; i++) {
                if (h.blk[i] == b) {
                    prog_point[i] = pp++;
                }
            }
        }
        block_end_pp[b] = pp++;  // virtual point at end of block
    }
    int max_pp = pp;

    // Find last use program point for each value.
    int last_use[HIR_MAX_INSNS];
    memset(last_use, -1, sizeof(int) * h.n_insns);

    for (int i = 0; i < h.n_insns; i++) {
        if (prog_point[i] < 0) continue;
        int pp_i = prog_point[i];

        // src1 is always a value reference.
        if (h.src1[i] >= 0 && h.src1[i] < h.n_insns) {
            if (pp_i > last_use[h.src1[i]])
                last_use[h.src1[i]] = pp_i;
        }

        // src2 is a value reference EXCEPT for BRC (where it's a block).
        if (h.kind[i] != HIR_BRC && h.src2[i] >= 0 && h.src2[i] < h.n_insns) {
            if (pp_i > last_use[h.src2[i]])
                last_use[h.src2[i]] = pp_i;
        }

        // Call/strcat arguments.
        if (h.kind[i] == HIR_CALL || h.kind[i] == HIR_STRCAT) {
            for (int j = 0; j < h.cnargs[i]; j++) {
                int arg = h.carg[h.cbase[i] + j];
                if (arg >= 0 && arg < h.n_insns) {
                    if (pp_i > last_use[arg])
                        last_use[arg] = pp_i;
                }
            }
        }

        // PHI arguments: value is used at end of predecessor block.
        if (h.kind[i] == HIR_PHI) {
            for (int j = 0; j < h.pnargs[i]; j++) {
                int val = h.pval[h.pbase[i] + j];
                int pred_blk = h.pblk[h.pbase[i] + j];
                if (val >= 0 && val < h.n_insns &&
                    pred_blk >= 0 && pred_blk < h.n_blocks) {
                    int end_pp = block_end_pp[pred_blk];
                    if (end_pp > last_use[val])
                        last_use[val] = end_pp;
                }
            }
        }
    }

    // Loop-aware liveness extension.
    //
    // A back-edge is (latch → header) where rpo_pos[header] <= rpo_pos[latch].
    // Any value used inside the loop body must be live through the entire
    // loop — its last_use must extend to at least the latch's block_end_pp.
    // Without this, values used at the header (e.g., nwords for the loop
    // condition) get their registers reused inside the body, corrupting
    // the value on the next iteration.
    //
    if (h.n_rpo > 0) {
        for (int b = 0; b < h.n_blocks; b++) {
            for (int s = 0; s < h.block_nsucc[b]; s++) {
                int tgt = h.block_succ[b][s];
                if (tgt < 0) continue;
                // Back-edge: successor has <= RPO position.
                if (h.rpo_pos[tgt] <= h.rpo_pos[b]) {
                    int latch_end = block_end_pp[b];
                    // Extend every value used inside the loop
                    // (any block with RPO position between header and latch).
                    for (int i = 0; i < h.n_insns; i++) {
                        if (prog_point[i] < 0) continue;
                        int ib = h.blk[i];
                        if (h.rpo_pos[ib] < h.rpo_pos[tgt] ||
                            h.rpo_pos[ib] > h.rpo_pos[b]) continue;
                        // This instruction is inside the loop.
                        // Extend any operand defined outside the loop.
                        auto extend = [&](int v) {
                            if (v < 0 || v >= h.n_insns) return;
                            if (prog_point[v] < 0) return;
                            int vb = h.blk[v];
                            // Value defined outside loop (or at header).
                            if (h.rpo_pos[vb] <= h.rpo_pos[tgt]) {
                                if (latch_end > last_use[v])
                                    last_use[v] = latch_end;
                            }
                        };
                        extend(h.src1[i]);
                        if (h.kind[i] != HIR_BRC)
                            extend(h.src2[i]);
                        if (h.kind[i] == HIR_CALL || h.kind[i] == HIR_STRCAT) {
                            for (int j = 0; j < h.cnargs[i]; j++)
                                extend(h.carg[h.cbase[i] + j]);
                        }
                    }
                }
            }
        }
    }

    // The final result must survive to the end of the program.
    if (h.result >= 0 && h.result < h.n_insns && needs_int_reg(h, h.result)) {
        last_use[h.result] = max_pp - 1;
    }

    // Build intervals for integer-typed values.
    //
    // For PHI nodes, the live range must start at the earliest
    // block_end_pp of the PHI's predecessor blocks (where the PHI
    // copy writes to the PHI's register), not at the PHI instruction's
    // program point.  Without this, the allocator may assign a PHI the
    // same register as a value that is still live across the block
    // boundary, causing the PHI copy to clobber it.
    //
    intervals.clear();
    for (int i = 0; i < h.n_insns; i++) {
        if (!needs_int_reg(h, i)) continue;
        if (prog_point[i] < 0) continue;  // unreachable

        int def = prog_point[i];

        // PHI: start at earliest predecessor's block end point.
        if (h.kind[i] == HIR_PHI && h.pnargs[i] > 0) {
            for (int j = 0; j < h.pnargs[i]; j++) {
                int pred_blk = h.pblk[h.pbase[i] + j];
                if (pred_blk >= 0 && pred_blk < h.n_blocks) {
                    int ep = block_end_pp[pred_blk];
                    if (ep < def) def = ep;
                }
            }
        }

        int end = (last_use[i] >= def) ? last_use[i] : def;

        intervals.push_back({i, def, end});
    }
}

// Poletto-Sarkar linear scan register allocation.
//
static reg_alloc_result linear_scan(std::vector<live_interval> &intervals) {
    reg_alloc_result result;
    memset(result.reg, 0, sizeof(result.reg));
    memset(result.spill_slot, -1, sizeof(result.spill_slot));
    result.n_spill_slots = 0;

    if (intervals.empty()) return result;

    // Sort intervals by start point.
    std::sort(intervals.begin(), intervals.end(),
              [](const live_interval &a, const live_interval &b) {
                  return a.start < b.start;
              });

    // Free register pool (stack-based for fast alloc/free).
    uint8_t free_regs[RA_NUM_REGS];
    int n_free = RA_NUM_REGS;
    for (int i = 0; i < RA_NUM_REGS; i++) {
        free_regs[i] = RA_REGS[RA_NUM_REGS - 1 - i];  // s11 at bottom
    }

    // Active intervals, sorted by end point ascending.
    // Small-N (max 11 entries), so linear insertion is fine.
    struct active_entry {
        int end;
        int value;
        uint8_t reg;
    };
    std::vector<active_entry> active;

    for (auto &iv : intervals) {
        // ExpireOldIntervals: remove intervals that ended before iv.start.
        size_t j = 0;
        while (j < active.size()) {
            if (active[j].end >= iv.start) break;  // sorted: rest are live
            // Return register to free pool.
            free_regs[n_free++] = active[j].reg;
            active.erase(active.begin() + j);
            // Don't increment j — next element shifted down.
        }

        if (n_free > 0) {
            // Assign a register.
            uint8_t reg = free_regs[--n_free];
            result.reg[iv.value] = reg;

            // Insert into active, maintaining sort by end.
            active_entry ae = {iv.end, iv.value, reg};
            auto pos = std::lower_bound(active.begin(), active.end(), ae,
                [](const active_entry &a, const active_entry &b) {
                    return a.end < b.end;
                });
            active.insert(pos, ae);
        } else {
            // Spill: evict the interval ending furthest in the future.
            auto &spill = active.back();  // largest end
            if (spill.end > iv.end) {
                // Spill the active interval, give its register to iv.
                result.reg[iv.value] = spill.reg;
                result.reg[spill.value] = 0;
                result.spill_slot[spill.value] = result.n_spill_slots++;

                // Remove spilled interval from active.
                active.pop_back();

                // Insert iv into active.
                active_entry ae = {iv.end, iv.value, result.reg[iv.value]};
                auto pos = std::lower_bound(active.begin(), active.end(), ae,
                    [](const active_entry &a, const active_entry &b) {
                        return a.end < b.end;
                    });
                active.insert(pos, ae);
            } else {
                // Spill the new interval (it ends later than everything).
                result.spill_slot[iv.value] = result.n_spill_slots++;
            }
        }
    }

    return result;
}

// Spill slot stack offset: -8*(slot+1) from SP.
static int32_t spill_offset(int slot) {
    return -8 * (slot + 1);
}

// Emit SD reg, off(sp) — store integer register to spill slot.
static void emit_spill_store(std::vector<uint32_t> &code, uint8_t reg, int slot) {
    code.push_back(rv_SD(2, reg, spill_offset(slot)));
}

// Emit LD rd, off(sp) — reload integer register from spill slot.
static void emit_spill_load(std::vector<uint32_t> &code, uint8_t rd, int slot) {
    code.push_back(rv_LD(rd, 2, spill_offset(slot)));
}

// Get the register holding integer value v, reloading from spill
// slot if necessary.  scratch = register to reload into if spilled.
//
static uint8_t ra_get_reg(rv_compiler &rc, hir_loc *loc, int v,
                           uint8_t scratch) {
    if (v < 0) return 0;
    if (loc[v].spill_slot >= 0 && !loc[v].in_reg) {
        emit_spill_load(rc.code, scratch, loc[v].spill_slot);
        return scratch;
    }
    return loc[v].reg;
}

// Set loc[i] from allocation result and optionally emit spill.
// dest = the register the value was computed into.
// Returns the destination register.
//
static void ra_set_loc(rv_compiler &rc, hir_loc *loc,
                        reg_alloc_result &alloc, int i, uint8_t computed_in) {
    uint8_t assigned = alloc.reg[i];
    int slot = alloc.spill_slot[i];

    if (assigned != 0) {
        // Value lives in a register.
        loc[i].reg = assigned;
        loc[i].in_reg = true;
        loc[i].spill_slot = -1;
    } else if (slot >= 0) {
        // Value is spilled — emit store.
        emit_spill_store(rc.code, computed_in, slot);
        loc[i].reg = 0;
        loc[i].in_reg = false;
        loc[i].spill_slot = slot;
    }
}

// Emit PHI copies: when branching from from_blk to to_blk,
// emit moves for any PHI nodes at the target block.
//
static void emit_phi_copies(hir_program &h, rv_compiler &rc,
                             hir_loc *loc, int from_blk, int to_blk) {
    if (h.block_first[to_blk] > h.block_last[to_blk]) return;
    for (int i = h.block_first[to_blk]; i <= h.block_last[to_blk]; i++) {
        if (h.blk[i] != to_blk || h.kind[i] != HIR_PHI) continue;

        // Find the PHI argument for from_blk.
        int base = h.pbase[i];
        for (int j = 0; j < h.pnargs[i]; j++) {
            if (h.pblk[base + j] != from_blk) continue;
            int val = h.pval[base + j];
            if (val < 0) break;

            bool phi_is_int = (loc[i].in_reg || loc[i].spill_slot >= 0);
            if (phi_is_int) {
                // Integer PHI (registered or spilled).
                uint8_t phi_dest = loc[i].in_reg ? loc[i].reg : RA_SCRATCH;
                uint8_t val_reg;
                if (loc[val].in_reg) {
                    val_reg = loc[val].reg;
                } else if (loc[val].spill_slot >= 0) {
                    // Spilled integer operand: reload.
                    emit_spill_load(rc.code, RA_SCRATCH2, loc[val].spill_slot);
                    val_reg = RA_SCRATCH2;
                } else {
                    // String value used as int PHI — load addr and atoi.
                    rv_load_val(rc.code, 10, loc[val].addr);
                    rv_emit_atoi(rc.code, 10, phi_dest);
                    if (loc[i].spill_slot >= 0 && !loc[i].in_reg) {
                        emit_spill_store(rc.code, phi_dest, loc[i].spill_slot);
                    }
                    break;
                }
                rc.code.push_back(rv_ADD(phi_dest, val_reg, 0));
                if (loc[i].spill_slot >= 0 && !loc[i].in_reg) {
                    emit_spill_store(rc.code, phi_dest, loc[i].spill_slot);
                }
            } else {
                // String PHI: copy string to PHI's output buffer.
                if (loc[val].in_reg) {
                    // Integer val → ITOA to PHI buffer.
                    rv_load_val(rc.code, 10, loc[i].addr);
                    rv_emit_itoa(rc.code, loc[val].reg, 10);
                } else if (loc[val].spill_slot >= 0) {
                    // Spilled integer val → reload, then ITOA.
                    emit_spill_load(rc.code, RA_SCRATCH, loc[val].spill_slot);
                    rv_load_val(rc.code, 10, loc[i].addr);
                    rv_emit_itoa(rc.code, RA_SCRATCH, 10);
                } else {
                    // String → string: byte copy.
                    rv_load_val(rc.code, 7, loc[i].addr);    // t2 = dest
                    rv_load_val(rc.code, 6, loc[val].addr);  // t1 = src
                    rv_emit_strcpy(rc.code, 7, 6);
                }
            }
            break;
        }
    }
}

static void hir_codegen(hir_program &h, rv_compiler &rc) {
    // Location map: where each instruction's result lives.
    hir_loc loc[HIR_MAX_INSNS];
    memset(loc, 0, sizeof(loc));
    for (int i = 0; i < h.n_insns; i++) loc[i].spill_slot = -1;

    // Block code offsets for branch backpatching.
    int block_offset[HIR_MAX_BLOCKS];
    memset(block_offset, 0, sizeof(block_offset));
    std::vector<branch_patch> patches;

    // Run linear scan register allocation.
    std::vector<live_interval> intervals;
    compute_live_ranges(h, intervals);
    reg_alloc_result alloc = linear_scan(intervals);

    // Pre-allocate PHI locations before codegen starts.
    for (int i = 0; i < h.n_insns; i++) {
        if (h.kind[i] == HIR_PHI) {
            if (h.ty[i] == TY_INT) {
                uint8_t reg = alloc.reg[i];
                if (reg) {
                    loc[i].reg = reg;
                    loc[i].in_reg = true;
                    loc[i].spill_slot = -1;
                } else if (alloc.spill_slot[i] >= 0) {
                    loc[i].reg = 0;
                    loc[i].in_reg = false;
                    loc[i].spill_slot = alloc.spill_slot[i];
                }
            } else {
                uint64_t out = rc.alloc_output();
                loc[i].addr = out;
                loc[i].in_reg = false;
                loc[i].spill_slot = -1;
            }
        }
    }

    // Process blocks in layout order.
    for (int b = 0; b < h.n_blocks; b++) {
        block_offset[b] = static_cast<int>(rc.code.size());
        if (h.block_first[b] > h.block_last[b]) continue;

        for (int i = h.block_first[b]; i <= h.block_last[b]; i++) {
            if (h.blk[i] != b) continue;

            switch (h.kind[i]) {
            case HIR_SCONST:
                loc[i].addr = static_cast<uint64_t>(h.val[i]);
                loc[i].in_reg = false;
                break;

            case HIR_ICONST: {
                uint8_t reg = alloc.reg[i];
                bool spilled = (reg == 0 && alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                rv_load_i64(rc.code, dest, h.val[i]);
                ra_set_loc(rc, loc, alloc, i, dest);
                break;
            }

            case HIR_ATOI: {
                int s1 = h.src1[i];
                uint8_t reg = alloc.reg[i];
                bool spilled = (reg == 0 && alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                if (h.kind[s1] == HIR_SCONST) {
                    int64_t v = static_cast<int64_t>(
                        mux_atol(u8(h.sval[s1])));
                    rv_load_i64(rc.code, dest, v);
                } else {
                    rv_load_val(rc.code, 10, loc[s1].addr);
                    rv_emit_atoi(rc.code, 10, dest);
                }
                ra_set_loc(rc, loc, alloc, i, dest);
                break;
            }

            case HIR_ITOA: {
                int s1 = h.src1[i];
                uint8_t s1r = ra_get_reg(rc, loc, s1, RA_SCRATCH);
                uint64_t out_addr = rc.alloc_output();
                rv_load_val(rc.code, 10, out_addr);
                rv_emit_itoa(rc.code, s1r, 10);
                loc[i].addr = out_addr;
                loc[i].in_reg = false;
                break;
            }

            case HIR_ADD: {
                int s1 = h.src1[i], s2 = h.src2[i];
                uint8_t r1 = ra_get_reg(rc, loc, s1, RA_SCRATCH);
                uint8_t r2 = ra_get_reg(rc, loc, s2, RA_SCRATCH2);
                uint8_t reg = alloc.reg[i];
                bool spilled = (reg == 0 && alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                rc.code.push_back(rv_ADD(dest, r1, r2));
                ra_set_loc(rc, loc, alloc, i, dest);
                break;
            }
            case HIR_SUB: {
                int s1 = h.src1[i], s2 = h.src2[i];
                uint8_t r1 = ra_get_reg(rc, loc, s1, RA_SCRATCH);
                uint8_t r2 = ra_get_reg(rc, loc, s2, RA_SCRATCH2);
                uint8_t reg = alloc.reg[i];
                bool spilled = (reg == 0 && alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                rc.code.push_back(rv_SUB(dest, r1, r2));
                ra_set_loc(rc, loc, alloc, i, dest);
                break;
            }
            case HIR_MUL: {
                int s1 = h.src1[i], s2 = h.src2[i];
                uint8_t r1 = ra_get_reg(rc, loc, s1, RA_SCRATCH);
                uint8_t r2 = ra_get_reg(rc, loc, s2, RA_SCRATCH2);
                uint8_t reg = alloc.reg[i];
                bool spilled = (reg == 0 && alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                rc.code.push_back(rv_MUL(dest, r1, r2));
                ra_set_loc(rc, loc, alloc, i, dest);
                break;
            }
            case HIR_REM: {
                int s1 = h.src1[i], s2 = h.src2[i];
                uint8_t r1 = ra_get_reg(rc, loc, s1, RA_SCRATCH);
                uint8_t r2 = ra_get_reg(rc, loc, s2, RA_SCRATCH2);
                uint8_t reg = alloc.reg[i];
                bool spilled = (reg == 0 && alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                rc.code.push_back(rv_REM(dest, r1, r2));
                ra_set_loc(rc, loc, alloc, i, dest);
                break;
            }
            case HIR_DIV: {
                int s1 = h.src1[i], s2 = h.src2[i];
                uint8_t r1 = ra_get_reg(rc, loc, s1, RA_SCRATCH);
                uint8_t r2 = ra_get_reg(rc, loc, s2, RA_SCRATCH2);
                uint8_t reg = alloc.reg[i];
                bool spilled = (reg == 0 && alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                rc.code.push_back(rv_DIV(dest, r1, r2));
                ra_set_loc(rc, loc, alloc, i, dest);
                break;
            }

            // ABS: branchless absolute value.
            // SRA tmp, rs, 63 (sign mask: all 1s if negative, all 0s if positive)
            // XOR dest, rs, tmp
            // SUB dest, dest, tmp
            case HIR_ABS: {
                int s1 = h.src1[i];
                uint8_t r1 = ra_get_reg(rc, loc, s1, RA_SCRATCH);
                uint8_t reg = alloc.reg[i];
                bool spilled = (reg == 0 && alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                constexpr uint8_t t0 = 5;  // scratch for sign mask
                // SRAI t0, r1, 63 — arithmetic shift right by 63
                rc.code.push_back(rv_i_type(OP_IMM, t0, ALU_SRLI, r1, 63)
                                  | (0x10u << 26));  // set funct6 high bit for SRAI
                // XOR dest, r1, t0
                rc.code.push_back(rv_r_type(OP_REG, dest, ALU_XOR, r1, t0, 0));
                // SUB dest, dest, t0
                rc.code.push_back(rv_SUB(dest, dest, t0));
                ra_set_loc(rc, loc, alloc, i, dest);
                break;
            }

            // SIGN: returns -1, 0, or 1.
            // SLT t0, rs, x0   (t0 = 1 if rs < 0)
            // SLT dest, x0, rs (dest = 1 if rs > 0, i.e., 0 < rs)
            // SUB dest, dest, t0
            case HIR_SIGN: {
                int s1 = h.src1[i];
                uint8_t r1 = ra_get_reg(rc, loc, s1, RA_SCRATCH);
                uint8_t reg = alloc.reg[i];
                bool spilled = (reg == 0 && alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                constexpr uint8_t t0 = 5;
                rc.code.push_back(rv_r_type(OP_REG, t0, ALU_SLT, r1, 0, 0));
                rc.code.push_back(rv_r_type(OP_REG, dest, ALU_SLT, 0, r1, 0));
                rc.code.push_back(rv_SUB(dest, dest, t0));
                ra_set_loc(rc, loc, alloc, i, dest);
                break;
            }

            // MAX: max(a, b) — branchless via SLT + conditional select.
            // SLT t0, r1, r2   (t0 = 1 if r1 < r2)
            // BEQ t0, x0, +8   (skip if r1 >= r2, i.e., r1 is already max)
            // MV dest, r2       (r2 is larger)
            // Otherwise dest = r1.
            // Actually simpler: compute both, select.
            // SUB t0, r1, r2
            // SRA t0, t0, 63   (sign mask: all 1s if r1 < r2)
            // AND t0, t0, SUB → use the mask to select
            // Better: just branch.
            // BLT r1, r2, +12; MV dest, r1; JAL x0, +8; MV dest, r2
            case HIR_MAX: {
                int s1 = h.src1[i], s2 = h.src2[i];
                uint8_t r1 = ra_get_reg(rc, loc, s1, RA_SCRATCH);
                uint8_t r2 = ra_get_reg(rc, loc, s2, RA_SCRATCH2);
                uint8_t reg = alloc.reg[i];
                bool spilled = (reg == 0 && alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                // BGE r1, r2, +12 (skip to dest=r1 case when r1 >= r2)
                rc.code.push_back(rv_BGE(r1, r2, 12));
                // r1 < r2: dest = r2
                rc.code.push_back(rv_ADD(dest, r2, 0));  // MV dest, r2
                rc.code.push_back(rv_JAL(0, 8));          // skip next
                // r1 >= r2: dest = r1
                rc.code.push_back(rv_ADD(dest, r1, 0));  // MV dest, r1
                ra_set_loc(rc, loc, alloc, i, dest);
                break;
            }

            // MIN: min(a, b) — mirror of MAX.
            case HIR_MIN: {
                int s1 = h.src1[i], s2 = h.src2[i];
                uint8_t r1 = ra_get_reg(rc, loc, s1, RA_SCRATCH);
                uint8_t r2 = ra_get_reg(rc, loc, s2, RA_SCRATCH2);
                uint8_t reg = alloc.reg[i];
                bool spilled = (reg == 0 && alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                // BLT r1, r2, +12 (skip to dest=r1 case when r1 < r2)
                rc.code.push_back(rv_b_type(BR_BLT, r1, r2, 12));
                // r1 >= r2: dest = r2
                rc.code.push_back(rv_ADD(dest, r2, 0));  // MV dest, r2
                rc.code.push_back(rv_JAL(0, 8));          // skip next
                // r1 < r2: dest = r1
                rc.code.push_back(rv_ADD(dest, r1, 0));  // MV dest, r1
                ra_set_loc(rc, loc, alloc, i, dest);
                break;
            }

            case HIR_EQ: {
                int s1 = h.src1[i], s2 = h.src2[i];
                uint8_t r1 = ra_get_reg(rc, loc, s1, RA_SCRATCH);
                uint8_t r2 = ra_get_reg(rc, loc, s2, RA_SCRATCH2);
                uint8_t reg = alloc.reg[i];
                bool spilled = (reg == 0 && alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                rc.code.push_back(rv_SUB(5, r1, r2));
                rc.code.push_back(rv_i_type(OP_IMM, dest, ALU_SLTIU, 5, 1));
                ra_set_loc(rc, loc, alloc, i, dest);
                break;
            }
            case HIR_NE: {
                int s1 = h.src1[i], s2 = h.src2[i];
                uint8_t r1 = ra_get_reg(rc, loc, s1, RA_SCRATCH);
                uint8_t r2 = ra_get_reg(rc, loc, s2, RA_SCRATCH2);
                uint8_t reg = alloc.reg[i];
                bool spilled = (reg == 0 && alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                rc.code.push_back(rv_SUB(5, r1, r2));
                rc.code.push_back(rv_r_type(OP_REG, dest, ALU_SLTU, 0, 5, 0));
                ra_set_loc(rc, loc, alloc, i, dest);
                break;
            }
            case HIR_GT: {
                int s1 = h.src1[i], s2 = h.src2[i];
                uint8_t r1 = ra_get_reg(rc, loc, s1, RA_SCRATCH);
                uint8_t r2 = ra_get_reg(rc, loc, s2, RA_SCRATCH2);
                uint8_t reg = alloc.reg[i];
                bool spilled = (reg == 0 && alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                rc.code.push_back(rv_r_type(OP_REG, dest, ALU_SLT, r2, r1, 0));
                ra_set_loc(rc, loc, alloc, i, dest);
                break;
            }
            case HIR_LT: {
                int s1 = h.src1[i], s2 = h.src2[i];
                uint8_t r1 = ra_get_reg(rc, loc, s1, RA_SCRATCH);
                uint8_t r2 = ra_get_reg(rc, loc, s2, RA_SCRATCH2);
                uint8_t reg = alloc.reg[i];
                bool spilled = (reg == 0 && alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                rc.code.push_back(rv_r_type(OP_REG, dest, ALU_SLT, r1, r2, 0));
                ra_set_loc(rc, loc, alloc, i, dest);
                break;
            }
            case HIR_GE: {
                int s1 = h.src1[i], s2 = h.src2[i];
                uint8_t r1 = ra_get_reg(rc, loc, s1, RA_SCRATCH);
                uint8_t r2 = ra_get_reg(rc, loc, s2, RA_SCRATCH2);
                uint8_t reg = alloc.reg[i];
                bool spilled = (reg == 0 && alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                rc.code.push_back(rv_r_type(OP_REG, dest, ALU_SLT, r1, r2, 0));
                rc.code.push_back(rv_i_type(OP_IMM, dest, ALU_XORI, dest, 1));
                ra_set_loc(rc, loc, alloc, i, dest);
                break;
            }
            case HIR_LE: {
                int s1 = h.src1[i], s2 = h.src2[i];
                uint8_t r1 = ra_get_reg(rc, loc, s1, RA_SCRATCH);
                uint8_t r2 = ra_get_reg(rc, loc, s2, RA_SCRATCH2);
                uint8_t reg = alloc.reg[i];
                bool spilled = (reg == 0 && alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                rc.code.push_back(rv_r_type(OP_REG, dest, ALU_SLT, r2, r1, 0));
                rc.code.push_back(rv_i_type(OP_IMM, dest, ALU_XORI, dest, 1));
                ra_set_loc(rc, loc, alloc, i, dest);
                break;
            }

            case HIR_NOT: {
                int s1 = h.src1[i];
                uint8_t r1 = ra_get_reg(rc, loc, s1, RA_SCRATCH);
                uint8_t reg = alloc.reg[i];
                bool spilled = (reg == 0 && alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                rc.code.push_back(rv_i_type(OP_IMM, dest, ALU_SLTIU, r1, 1));
                ra_set_loc(rc, loc, alloc, i, dest);
                break;
            }

            // BOOL (t function): SNEZ — set if not equal to zero.
            // SLTU dest, x0, r1 → dest = (0 < r1) unsigned = (r1 != 0)
            case HIR_BOOL: {
                int s1 = h.src1[i];
                uint8_t r1 = ra_get_reg(rc, loc, s1, RA_SCRATCH);
                uint8_t reg = alloc.reg[i];
                bool spilled = (reg == 0 && alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                rc.code.push_back(rv_r_type(OP_REG, dest, ALU_SLTU, 0, r1, 0));
                ra_set_loc(rc, loc, alloc, i, dest);
                break;
            }

            case HIR_INC: {
                int s1 = h.src1[i];
                uint8_t r1 = ra_get_reg(rc, loc, s1, RA_SCRATCH);
                uint8_t reg = alloc.reg[i];
                bool spilled = (reg == 0 && alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                rc.code.push_back(rv_ADDI(dest, r1, 1));
                ra_set_loc(rc, loc, alloc, i, dest);
                break;
            }
            case HIR_DEC: {
                int s1 = h.src1[i];
                uint8_t r1 = ra_get_reg(rc, loc, s1, RA_SCRATCH);
                uint8_t reg = alloc.reg[i];
                bool spilled = (reg == 0 && alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                rc.code.push_back(rv_ADDI(dest, r1, -1));
                ra_set_loc(rc, loc, alloc, i, dest);
                break;
            }

            case HIR_CALL: {
                uint64_t out_addr = rc.alloc_output();
                int na = h.cnargs[i];
                int base = h.cbase[i];
                std::vector<uint64_t> farg_addrs;
                for (int j = 0; j < na; j++) {
                    int ai = h.carg[base + j];
                    farg_addrs.push_back(loc[ai].addr);
                }
                uint64_t fargs_addr = rc.alloc_fargs(farg_addrs);

                if (h.tier2_addr[i]) {
                    // Tier 2: JAL to pre-compiled blob function.
                    rv_emit_tier2_call(rc.code, fargs_addr, na,
                                        out_addr, h.tier2_addr[i]);
                } else {
                    // ECALL to engine function.
                    int fidx = h.func_idx[i];
                    rv_emit_call(rc.code, 0, fargs_addr, na,
                                  out_addr, rv_compiler::OUT_SLOT, fidx);
                }
                loc[i].addr = out_addr;
                loc[i].in_reg = false;
                break;
            }

            case HIR_STRCAT: {
                uint64_t out_addr = rc.alloc_output();
                int na = h.cnargs[i];
                int base = h.cbase[i];
                std::vector<uint64_t> farg_addrs;
                for (int j = 0; j < na; j++) {
                    int ai = h.carg[base + j];
                    farg_addrs.push_back(loc[ai].addr);
                }
                uint64_t fargs_addr = rc.alloc_fargs(farg_addrs);
                uint64_t t2addr = tier2_lookup("STRCAT");
                if (t2addr) {
                    rv_emit_tier2_call(rc.code, fargs_addr, na,
                                        out_addr, t2addr);
                } else {
                    int fidx = h.func_idx[i];
                    uint64_t name_addr = fidx ? 0 : rc.pool_str("strcat");
                    rv_emit_call(rc.code, name_addr, fargs_addr, na,
                                  out_addr, rv_compiler::OUT_SLOT, fidx);
                }
                loc[i].addr = out_addr;
                loc[i].in_reg = false;
                break;
            }

            case HIR_COPY: {
                int s1 = h.src1[i];
                if (s1 < 0) break;
                if (needs_int_reg(h, i)) {
                    uint8_t r1 = ra_get_reg(rc, loc, s1, RA_SCRATCH2);
                    uint8_t reg = alloc.reg[i];
                    bool spilled = (reg == 0 && alloc.spill_slot[i] >= 0);
                    uint8_t dest = spilled ? RA_SCRATCH : reg;
                    if (!dest) { loc[i] = loc[s1]; break; }
                    rc.code.push_back(rv_ADD(dest, r1, 0));
                    ra_set_loc(rc, loc, alloc, i, dest);
                } else {
                    loc[i] = loc[s1];
                }
                break;
            }

            case HIR_PHI:
                // Location already allocated above.
                break;

            case HIR_BRC: {
                // Conditional branch: if cond != 0, go to true_blk.
                int cond_insn = h.src1[i];
                int true_blk = static_cast<int>(h.val[i]);
                int false_blk = h.src2[i];

                uint8_t cond_reg = ra_get_reg(rc, loc, cond_insn, RA_SCRATCH);

                // Emit PHI copies for true path, then BNE.
                emit_phi_copies(h, rc, loc, b, true_blk);
                int bne_idx = static_cast<int>(rc.code.size());
                rc.code.push_back(rv_BNE(cond_reg, 0, 0));
                patches.push_back({bne_idx, true_blk});

                // Emit PHI copies for false path.
                emit_phi_copies(h, rc, loc, b, false_blk);

                // If false block is not the next in layout, emit JAL.
                if (false_blk != b + 1) {
                    int jal_idx = static_cast<int>(rc.code.size());
                    rc.code.push_back(rv_JAL(0, 0));
                    patches.push_back({jal_idx, false_blk});
                }
                break;
            }

            case HIR_BR: {
                int target = static_cast<int>(h.val[i]);

                // Emit PHI copies for target.
                emit_phi_copies(h, rc, loc, b, target);

                // If target is not the next block, emit JAL.
                if (target != b + 1) {
                    int jal_idx = static_cast<int>(rc.code.size());
                    rc.code.push_back(rv_JAL(0, 0));
                    patches.push_back({jal_idx, target});
                }
                break;
            }

            case HIR_RET:
                rv_emit_exit(rc.code);
                break;

            case HIR_NOP:
            case HIR_STORE_Q:  // consumed by SSA construction
            case HIR_LOAD_Q:   // should be COPY after SSA; harmless NOP
                break;

            default:
                break;
            }
        }
    }

    // Backpatch branch offsets.
    for (auto &p : patches) {
        int target_off = block_offset[p.target_blk];
        int branch_off = p.code_idx;
        int32_t rel = static_cast<int32_t>((target_off - branch_off) * 4);
        uint32_t insn = rc.code[branch_off];
        uint8_t opcode = insn & 0x7F;
        if (opcode == OP_BRANCH) {
            // B-type: re-encode with correct offset.
            uint8_t funct3 = (insn >> 12) & 7;
            uint8_t rs1 = (insn >> 15) & 0x1F;
            uint8_t rs2 = (insn >> 20) & 0x1F;
            rc.code[branch_off] = rv_b_type(funct3, rs1, rs2, rel);
        } else if (opcode == OP_JAL) {
            // J-type: re-encode with correct offset.
            uint8_t rd = (insn >> 7) & 0x1F;
            rc.code[branch_off] = rv_JAL(rd, rel);
        }
    }

    // Set the result location in the rv_compiler.
    int ri = h.result;
    if (ri >= 0) {
        if (loc[ri].in_reg) {
            // Final result is in a register — need ITOA.
            uint64_t out_addr = rc.alloc_output();
            rv_load_val(rc.code, 10, out_addr);
            rv_emit_itoa(rc.code, loc[ri].reg, 10);
            rc.final_out = out_addr;
        } else if (loc[ri].spill_slot >= 0) {
            // Final result is spilled — reload and ITOA.
            uint64_t out_addr = rc.alloc_output();
            emit_spill_load(rc.code, RA_SCRATCH, loc[ri].spill_slot);
            rv_load_val(rc.code, 10, out_addr);
            rv_emit_itoa(rc.code, RA_SCRATCH, 10);
            rc.final_out = out_addr;
        } else {
            rc.final_out = loc[ri].addr;
        }
    }

    // Emit exit.
    rv_emit_exit(rc.code);
}

// ---------------------------------------------------------------
// Public: compile an expression string to a runnable RV64 program.
// ---------------------------------------------------------------

struct compiled_program {
    std::vector<uint8_t> memory;
    size_t memory_size;
    uint64_t out_addr;      // where the final result lives
    uint64_t out_used;      // bytes of output region actually allocated
    bool ok;
    int folds;
    int ecalls;
    int tier2_calls;
    int native_ops;
    bool needs_jit;         // true if JIT execution required
};

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
            s_blob_version = compute_blob_version();
            return;
        }
    }
    // No blob found — Tier 2 disabled, ECALL fallback for everything.
    s_blob_version = "none";
}

static compiled_program compile_expression(const UTF8 *expr, size_t nLen) {
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
        return prog;
    }

    // --- HIR pipeline ---

    // Phase 1: Lower AST → HIR.
    rv_compiler rc;

    // Install Tier 2 blob into guest memory (if loaded).
    tier2_install(rc.memory, rv_compiler::BLOB_BASE);

    hir_program h;
    h.init();
    qreg_init();
    h.result = hir_lower_node(h, rc, ast.get());

    // Phase 2: SSA construction (for multi-block programs, M4+).
    // For single-block programs this is a no-op but builds the CFG.
    hir_build_cfg(h);
    if (h.n_blocks > 1) {
        hir_ssa_construct(h);
    }

    // Phase 3: SSA optimization (constant fold, copy prop, DCE).
    hir_optimize(h);

    // Phase 4: Codegen HIR → RV64.
    hir_codegen(h, rc);

    // Copy code to guest memory.
    for (size_t i = 0; i < rc.code.size() && i * 4 < rv_compiler::CODE_LIMIT; i++) {
        memcpy(rc.memory.data() + i * 4, &rc.code[i], 4);
    }

    prog.memory = std::move(rc.memory);
    prog.memory_size = rv_compiler::MEM_SIZE;
    prog.out_addr = rc.final_out;
    prog.out_used = rc.out_pool - rv_compiler::OUT_BASE;
    prog.ok = true;
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
struct eval_ctx {
    uint8_t *memory;
    size_t   memory_size;
    dbref    executor;
    dbref    caller;
    dbref    enactor;
};

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

// Persist a compiled_program to the SQLite code cache.
//
static void store_to_sqlite_cache(const UTF8 *expr, size_t nLen,
                                   const compiled_program &prog) {
    if (!g_pSQLiteBackend) return;
    CSQLiteDB &db = g_pSQLiteBackend->GetDB();
    std::string src_key(reinterpret_cast<const char *>(expr), nLen);
    int persist_len = static_cast<int>(rv_compiler::FARGS_LIMIT);
    db.CodeCachePut(src_key.c_str(), s_blob_version.c_str(),
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
static compiled_program *compile_cached(const UTF8 *expr, size_t nLen) {
    std::string key(reinterpret_cast<const char *>(expr), nLen);

    auto it = s_compile_cache.find(key);
    if (it != s_compile_cache.end()) {
        // Memory cache hit — move to front of LRU.
        s_compile_lru.splice(s_compile_lru.begin(), s_compile_lru,
                             it->second.lru_it);
        return &it->second.prog;
    }

    // Memory cache miss — check SQLite persistent cache.
    compiled_program prog;
    bool from_sqlite = false;

    if (g_pSQLiteBackend && nLen >= COMPILE_CACHE_MIN_LEN) {
        CSQLiteDB &db = g_pSQLiteBackend->GetDB();
        CSQLiteDB::CodeCacheRecord rec;
        if (db.CodeCacheGet(key.c_str(), s_blob_version.c_str(), rec)) {
            // rec.memory_blob points into SQLite statement memory.
            // reconstruct_from_cache copies it; then we must release
            // the statement to avoid holding a read lock.
            prog = reconstruct_from_cache(rec);
            db.CodeCacheReset();
            from_sqlite = true;
        }
    }

    if (!from_sqlite) {
        // Full cache miss — compile from scratch.
        prog = compile_expression(expr, nLen);
        if (!prog.ok) return nullptr;

        // Persist to SQLite for future restarts.
        if (nLen >= COMPILE_CACHE_MIN_LEN) {
            store_to_sqlite_cache(expr, nLen, prog);
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
static bool run_cached_program(compiled_program *prog,
                                dbref executor, dbref caller_db,
                                dbref enactor,
                                UTF8 *out, size_t out_size) {
    if (!prog->needs_jit) {
        const char *r = reinterpret_cast<const char *>(
            prog->memory.data() + prog->out_addr);
        size_t n = strlen(r);
        if (n >= out_size) n = out_size - 1;
        memcpy(out, r, n);
        out[n] = '\0';
        return true;
    }

    // Clear output region for clean re-run.
    memset(prog->memory.data() + rv_compiler::OUT_BASE, 0,
           rv_compiler::OUT_LIMIT - rv_compiler::OUT_BASE);

    eval_ctx ec;
    ec.memory = prog->memory.data();
    ec.memory_size = prog->memory_size;
    ec.executor = executor;
    ec.caller = caller_db;
    ec.enactor = enactor;

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
            0, fargs, nfargs, nullptr, 0);

    *bufc = '\0';
    size_t result_len = static_cast<size_t>(bufc - buff);
    if (result_len >= out_size) result_len = out_size - 1;
    memcpy(ec->memory + out_addr, buff, result_len);
    ec->memory[out_addr + result_len] = '\0';

    free_lbuf(buff);

    ctx->x[10] = static_cast<uint64_t>(result_len);
    return -1;
}

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

    default:
        ctx->x[10] = 0;
        return -1;
    }
}

FUNCTION(fun_rveval)
{
    UNUSED_PARAMETER(fp);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (nfargs < 1) {
        safe_str(T("#-1 TOO FEW ARGUMENTS"), buff, bufc);
        return;
    }

    const UTF8 *expr = fargs[0];
    size_t nLen = strlen(reinterpret_cast<const char *>(expr));

    // Look up in compile cache (or compile on miss).
    compiled_program *prog = compile_cached(expr, nLen);
    if (!prog) {
        safe_str(T("#-1 COMPILATION FAILED"), buff, bufc);
        return;
    }

    // If everything was constant-folded, the result is already
    // in the string pool — no need to run the JIT at all.
    if (!prog->needs_jit) {
        const UTF8 *result = prog->memory.data() + prog->out_addr;
        safe_str(result, buff, bufc);
        return;
    }

    // Run through the JIT with compile+block cache.
    UTF8 result[LBUF_SIZE];
    if (!run_cached_program(prog, executor, caller, enactor,
                             result, sizeof(result))) {
        safe_str(T("#-1 DBT EXECUTION ERROR"), buff, bufc);
        return;
    }

    safe_str(result, buff, bufc);
}

// ---------------------------------------------------------------
// fun_rvbench: benchmark rveval vs native mux_exec.
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
        std::string key(reinterpret_cast<const char *>(expr), nLen);
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
        compiled_program *cp = compile_cached(expr, nLen);
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
