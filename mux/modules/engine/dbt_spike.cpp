/*! \file dbt_spike.cpp
 * \brief Deep spike: name-based dispatch + real LBUFs via ECALL.
 *
 * Proves risk #2 (buffer management) and #3 (name-based dispatch):
 * the ECALL handler looks up functions by name in
 * mudstate.builtin_functions, allocates a real LBUF for output,
 * calls the function, copies the result to guest memory, and
 * frees the LBUF.
 *
 * This links the real funmath.eo (same code as engine.so) and
 * provides minimal globals (mudstate, mudconf) for the dispatch
 * infrastructure.  No changes to engine.so exports or visibility.
 *
 * Compile:
 *   g++ -std=c++17 -O2 -fPIC -I../../include -o dbt_spike \
 *       dbt_spike.cpp dbt.cpp dbt_interp.cpp dbt_elf64.cpp funmath.eo \
 *       -L../../lib -lmux -Wl,-rpath,'$ORIGIN/../../lib' \
 *       -lssl -lcrypto -lm \
 *       -Wl,--unresolved-symbols=ignore-in-object-files
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "dbt.h"
#include "dbt_interp.h"
#include "dbt_decoder.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <cstdint>

// ---------------------------------------------------------------
// Provide the two engine globals.  Zero-initialization is fine
// for the spike — we only use mudstate.builtin_functions (an
// unordered_map that default-constructs empty).
// ---------------------------------------------------------------

STATEDATA mudstate;
CONFDATA  mudconf;

// ---------------------------------------------------------------
// Stubs for engine symbols referenced by other functions in
// funmath.eo (not used by fun_add/sub/mul at runtime).
// ---------------------------------------------------------------

const UTF8 *OUT_OF_RANGE = T("#-1 OUT OF RANGE");

mux_subnets::mux_subnets() : msnRoot(nullptr) {}
mux_subnets::~mux_subnets() {}

bool delim_check(UTF8 *buff, UTF8 **bufc,
                 dbref executor, dbref caller, dbref enactor,
                 int eval,
                 UTF8 *fargs[], int nfargs,
                 const UTF8 *cargs[], int ncargs,
                 int sep_arg, SEP *sep, int dflags) {
    return false;
}

UTF8 *trim_space_sep(UTF8 *str, const SEP &sep) {
    return str;
}

UTF8 *split_token(UTF8 **sp, const SEP &sep) {
    return nullptr;
}

int list2arr(UTF8 *arr[], int maxlen, UTF8 *list, const SEP &sep) {
    return 0;
}

bool xlate(UTF8 *arg) {
    return false;
}

void mux_exec(const UTF8 *pdstr, size_t nStr, UTF8 *buff, UTF8 **bufc,
              dbref executor, dbref caller, dbref enactor, int eval,
              const UTF8 *cargs[], int ncargs) {
}

// ---------------------------------------------------------------
// Extern declarations for real engine functions in funmath.eo.
// ---------------------------------------------------------------

extern FUNCTION(fun_add);
extern FUNCTION(fun_sub);
extern FUNCTION(fun_mul);

// ---------------------------------------------------------------
// Register functions into mudstate.builtin_functions the same way
// init_functab() does, but for just the functions we need.
// ---------------------------------------------------------------

static FUN spike_funtab[] = {
    { T("ADD"), fun_add, MAX_ARG, 1, MAX_ARG, 0, CA_PUBLIC, nullptr },
    { T("MUL"), fun_mul, MAX_ARG, 1, MAX_ARG, 0, CA_PUBLIC, nullptr },
    { T("SUB"), fun_sub, MAX_ARG, 2,       2, 0, CA_PUBLIC, nullptr },
    { nullptr,  nullptr,       0, 0,       0, 0,         0, nullptr },
};

static void spike_init_functab() {
    for (FUN *fp = spike_funtab; fp->name; fp++) {
        size_t nCased;
        UTF8 *pCased = mux_strupr(fp->name, nCased);
        std::vector<UTF8> name(pCased, pCased + nCased);
        mudstate.builtin_functions.insert(std::make_pair(name, fp));
    }
}

// Look up a function by name (case-insensitive via mux_strupr).
//
static FUN *spike_lookup_function(const UTF8 *name) {
    size_t nCased;
    UTF8 *pCased = mux_strupr(name, nCased);
    std::vector<UTF8> key(pCased, pCased + nCased);
    auto it = mudstate.builtin_functions.find(key);
    if (it != mudstate.builtin_functions.end()) {
        return it->second;
    }
    return nullptr;
}

// ---------------------------------------------------------------
// ECALL convention (evolved):
//
//   a7 (x17) = 0x100  — "call softcode function"
//   a0 (x10) = guest pointer to function name (null-terminated)
//   a1 (x11) = pointer to fargs[] array in guest memory
//   a2 (x12) = nfargs
//   a3 (x13) = pointer to output buffer in guest memory
//   a4 (x14) = output buffer size
//
// After ECALL:
//   a0 (x10) = number of bytes written to output buffer
//              (0 if function not found)
//
// The handler allocates a real LBUF, calls the function with the
// standard buff/bufc convention, copies the result to guest memory,
// and frees the LBUF.
//
// ECALL 93 = exit(a0).
// ---------------------------------------------------------------

static constexpr uint64_t ECALL_CALL_FUNC = 0x100;

struct spike_ctx {
    uint8_t *memory;
    size_t   memory_size;
};

static int spike_ecall(rv64_ctx_t *ctx, void *user_data) {
    spike_ctx *sc = static_cast<spike_ctx *>(user_data);
    uint64_t syscall_num = ctx->x[17];

    switch (syscall_num) {
    case 93: // exit
        return static_cast<int>(ctx->x[10]);

    case ECALL_CALL_FUNC: {
        uint64_t name_addr = ctx->x[10];
        uint64_t fargs_addr = ctx->x[11];
        int nfargs = static_cast<int>(ctx->x[12]);
        uint64_t out_addr = ctx->x[13];
        uint64_t out_size = ctx->x[14];

        // Bounds check.
        if (name_addr >= sc->memory_size ||
            out_addr + out_size > sc->memory_size) {
            ctx->x[10] = 0;
            return -1;
        }

        // Look up function by name.
        const UTF8 *func_name = sc->memory + name_addr;
        FUN *fp = spike_lookup_function(func_name);
        if (!fp) {
            fprintf(stderr, "spike_ecall: function '%s' not found\n",
                    reinterpret_cast<const char *>(func_name));
            ctx->x[10] = 0;
            return -1;
        }

        // Build host fargs[].
        UTF8 *fargs[16];
        for (int i = 0; i < nfargs && i < 16; i++) {
            uint64_t ptr;
            memcpy(&ptr, sc->memory + fargs_addr + i * 8, 8);
            if (ptr >= sc->memory_size) {
                ctx->x[10] = 0;
                return -1;
            }
            fargs[i] = sc->memory + ptr;
        }

        // Allocate a real LBUF for output.
        UTF8 *buff = alloc_lbuf("spike_ecall");
        UTF8 *bufc = buff;

        // Call the real engine function.
        fp->fun(fp, buff, &bufc, 1, 1, 1, 0,
                fargs, nfargs, nullptr, 0);

        // Copy result to guest memory.
        *bufc = '\0';
        size_t result_len = static_cast<size_t>(bufc - buff);
        if (result_len >= out_size) {
            result_len = out_size - 1;
        }
        memcpy(sc->memory + out_addr, buff, result_len);
        sc->memory[out_addr + result_len] = '\0';

        // Free the LBUF.
        free_lbuf(buff);

        ctx->x[10] = static_cast<uint64_t>(result_len);
        return -1; // continue
    }

    default:
        fprintf(stderr, "spike_ecall: unhandled ecall %llu\n",
                (unsigned long long)syscall_num);
        return -1;
    }
}

// ---------------------------------------------------------------
// RV64 instruction encoding helpers
// ---------------------------------------------------------------

static uint32_t i_type(uint8_t opcode, uint8_t rd, uint8_t funct3,
                        uint8_t rs1, int32_t imm) {
    return opcode | (rd << 7) | (funct3 << 12) | (rs1 << 15)
         | ((static_cast<uint32_t>(imm) & 0xFFF) << 20);
}

static uint32_t u_type(uint8_t opcode, uint8_t rd, int32_t imm) {
    return opcode | (rd << 7) | (static_cast<uint32_t>(imm) & 0xFFFFF000);
}

static uint32_t ADDI(uint8_t rd, uint8_t rs1, int32_t imm) {
    return i_type(OP_IMM, rd, ALU_ADDI, rs1, imm);
}
static uint32_t LUI(uint8_t rd, int32_t imm) {
    return u_type(OP_LUI, rd, imm);
}
static uint32_t ECALL() {
    return i_type(OP_SYSTEM, 0, 0, 0, 0);
}

// ---------------------------------------------------------------
// Test infrastructure
// ---------------------------------------------------------------

struct test_case {
    const char *label;
    const char *func_name;
    const char *args[16];
    int nargs;
    const char *expected;
};

// Write a string to guest memory, return guest address.
//
static uint64_t write_guest_string(uint8_t *mem, uint64_t &pool,
                                    const char *s) {
    uint64_t addr = pool;
    size_t len = strlen(s) + 1;
    memcpy(mem + addr, s, len);
    pool += (len + 7) & ~7ULL;
    return addr;
}

// Build RV64 code for name-based function call.
//
// a0 = pointer to function name string (guest addr)
// a1 = fargs array pointer
// a2 = nfargs
// a3 = output buffer pointer
// a4 = output buffer size
//
static std::vector<uint32_t> build_call_code(uint64_t name_addr,
                                              uint64_t fargs_addr,
                                              int nfargs,
                                              uint64_t out_addr,
                                              int out_size) {
    std::vector<uint32_t> code;

    // a7 = 0x100
    code.push_back(ADDI(17, 0, 0x100));

    // a0 = name_addr
    uint32_t hi = name_addr & 0xFFFFF000;
    int32_t lo = static_cast<int32_t>(name_addr & 0xFFF);
    code.push_back(LUI(10, hi));
    code.push_back(ADDI(10, 10, lo));

    // a1 = fargs_addr
    hi = fargs_addr & 0xFFFFF000;
    lo = static_cast<int32_t>(fargs_addr & 0xFFF);
    code.push_back(LUI(11, hi));
    code.push_back(ADDI(11, 11, lo));

    // a2 = nfargs
    code.push_back(ADDI(12, 0, nfargs));

    // a3 = out_addr
    hi = out_addr & 0xFFFFF000;
    lo = static_cast<int32_t>(out_addr & 0xFFF);
    code.push_back(LUI(13, hi));
    code.push_back(ADDI(13, 13, lo));

    // a4 = out_size
    code.push_back(ADDI(14, 0, out_size));

    code.push_back(ECALL());

    // Exit: a7=93, a0=0
    code.push_back(ADDI(17, 0, 93));
    code.push_back(ADDI(10, 0, 0));
    code.push_back(ECALL());

    return code;
}

static bool run_test(const test_case &tc, bool use_dbt) {
    const size_t MEM_SIZE = 64 * 1024;
    std::vector<uint8_t> memory(MEM_SIZE, 0);

    // Write function name to guest memory.
    uint64_t pool = 0x1000;
    uint64_t name_addr = write_guest_string(memory.data(), pool,
                                             tc.func_name);

    // Write argument strings.
    uint64_t arg_addrs[16];
    for (int i = 0; i < tc.nargs; i++) {
        arg_addrs[i] = write_guest_string(memory.data(), pool, tc.args[i]);
    }

    // Write fargs[] array.
    uint64_t fargs_addr = (pool + 7) & ~7ULL;
    for (int i = 0; i < tc.nargs; i++) {
        memcpy(memory.data() + fargs_addr + i * 8, &arg_addrs[i], 8);
    }

    // Output buffer.
    uint64_t out_addr = fargs_addr + tc.nargs * 8 + 16;
    out_addr = (out_addr + 7) & ~7ULL;
    int out_size = 192;

    // Assemble code.
    auto code = build_call_code(name_addr, fargs_addr, tc.nargs,
                                out_addr, out_size);
    for (size_t i = 0; i < code.size(); i++) {
        memcpy(memory.data() + i * 4, &code[i], 4);
    }

    spike_ctx sc = { memory.data(), MEM_SIZE };
    int rc;

    if (use_dbt) {
        dbt_state_t dbt;
        if (dbt_init(&dbt, memory.data(), MEM_SIZE, spike_ecall, &sc) != 0) {
            fprintf(stderr, "  Failed to init DBT\n");
            return false;
        }
        rc = dbt_run(&dbt, 0, MEM_SIZE - 16);
        dbt_cleanup(&dbt);
    } else {
        rv64_state_t state = {};
        state.pc = 0;
        state.x[2] = MEM_SIZE - 16;
        rv64_memory_t mem = { memory.data(), MEM_SIZE };

        struct interp_wrap {
            static int ecall(rv64_state_t *s, void *ud) {
                rv64_ctx_t ctx = {};
                for (int i = 0; i < 32; i++) ctx.x[i] = s->x[i];
                int r = spike_ecall(&ctx, ud);
                for (int i = 0; i < 32; i++) s->x[i] = ctx.x[i];
                return r;
            }
        };

        rc = rv64_interp_run(&state, &mem, interp_wrap::ecall, &sc);
    }

    const char *result = reinterpret_cast<const char *>(
        memory.data() + out_addr);
    bool pass = (rc == 0 && strcmp(result, tc.expected) == 0);

    printf("  %-8s %s(%s", use_dbt ? "[JIT]" : "[interp]",
           tc.func_name, tc.args[0]);
    for (int i = 1; i < tc.nargs; i++) printf(",%s", tc.args[i]);
    printf(") = \"%s\"  (expect \"%s\")  %s\n",
           result, tc.expected, pass ? "PASS" : "FAIL");

    return pass;
}

// ---------------------------------------------------------------
// Main
// ---------------------------------------------------------------

int main() {
    printf("=== Deep Spike: name-based dispatch + real LBUFs ===\n");
    printf("    (mudstate.builtin_functions + alloc_lbuf/free_lbuf)\n\n");

    // Initialize buffer pools (LBUF is the one we need).
    pool_init(POOL_LBUF, LBUF_SIZE);
    pool_init(POOL_MBUF, MBUF_SIZE);
    pool_init(POOL_SBUF, SBUF_SIZE);

    // Register functions into mudstate.builtin_functions.
    spike_init_functab();
    printf("  Registered %zu functions in mudstate.builtin_functions\n\n",
           mudstate.builtin_functions.size());

    test_case tests[] = {
        // Name-based lookup (case-insensitive)
        { "add_lower",   "add",   {"1", "2"},            2, "3"   },
        { "add_upper",   "ADD",   {"10", "20"},          2, "30"  },
        { "add_mixed",   "Add",   {"7", "8"},            2, "15"  },
        // Float path
        { "add_float",   "add",   {"1.5", "2.5"},        2, "4"   },
        // Multi-arg
        { "add_multi",   "add",   {"1","2","3","4","5"},  5, "15"  },
        // Big numbers (int→float crossover)
        { "add_big",     "add",   {"999999999","1"},      2, "1000000000" },
        // Multiply
        { "mul",         "mul",   {"6", "7"},             2, "42"  },
        { "mul_float",   "mul",   {"3.14", "2"},          2, "6.28"},
        // Subtract
        { "sub",         "sub",   {"100", "58"},          2, "42"  },
        { "sub_neg",     "sub",   {"10", "100"},          2, "-90" },
    };
    int ntests = sizeof(tests) / sizeof(tests[0]);
    int pass = 0, fail = 0;

    printf("--- Interpreter ---\n");
    for (int i = 0; i < ntests; i++) {
        if (run_test(tests[i], false)) pass++; else fail++;
    }

    printf("\n--- JIT/DBT ---\n");
    for (int i = 0; i < ntests; i++) {
        if (run_test(tests[i], true)) pass++; else fail++;
    }

    printf("\n%d/%d passed, %d failed\n",
           pass, pass + fail, fail);
    return fail ? 1 : 0;
}
