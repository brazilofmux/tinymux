/*! \file dbt_spike.cpp
 * \brief Deep spike: real engine fun_add/fun_sub/fun_mul through ECALL.
 *
 * Proves that compiled RISC-V code can call the REAL MUX functions
 * from funmath.eo — the exact same object code linked into engine.so.
 * These functions use real libmux utilities (mux_atol, safe_ltoa,
 * mux_atof, fval, is_integer, AddDoubles) through the real FUN
 * calling convention.
 *
 * This is the "risk #1" proof: can the ECALL handler call real
 * engine functions that were compiled against the full engine
 * header chain (externs.h → mudstate/mudconf)?  Answer: yes,
 * because fun_add/sub/mul only touch libmux utilities at runtime.
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
#include "core.h"
#include "functions.h"

#include "dbt.h"
#include "dbt_interp.h"
#include "dbt_decoder.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <cstdint>

// ---------------------------------------------------------------
// Extern declarations for the REAL engine functions in funmath.eo.
// These are the exact same compiled functions linked into engine.so.
// ---------------------------------------------------------------

extern FUNCTION(fun_add);
extern FUNCTION(fun_sub);
extern FUNCTION(fun_mul);

// ---------------------------------------------------------------
// Dispatch table: maps function index → real engine handler.
// ---------------------------------------------------------------

typedef void (*mux_fn_t)(FUN *fp, UTF8 *buff, UTF8 **bufc,
                          dbref executor, dbref caller, dbref enactor,
                          int eval, UTF8 *fargs[], int nfargs,
                          const UTF8 *cargs[], int ncargs);

static struct {
    const char *name;
    mux_fn_t handler;
} dispatch_table[] = {
    { "add",    fun_add    },   // 0 — real engine fun_add
    { "mul",    fun_mul    },   // 1 — real engine fun_mul
    { "sub",    fun_sub    },   // 2 — real engine fun_sub
};

static constexpr int NUM_DISPATCH = sizeof(dispatch_table) / sizeof(dispatch_table[0]);

// ---------------------------------------------------------------
// ECALL convention for function calls:
//
//   a7 (x17) = 0x100  — "call softcode function"
//   a0 (x10) = function index in dispatch table
//   a1 (x11) = pointer to fargs[] array in guest memory
//   a2 (x12) = nfargs
//   a3 (x13) = pointer to output buffer in guest memory
//   a4 (x14) = output buffer size
//
// After ECALL:
//   a0 (x10) = number of bytes written to output buffer
//
// ECALL 93 = exit(a0) as before.
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
        uint64_t func_id = ctx->x[10];
        uint64_t fargs_addr = ctx->x[11];
        int nfargs = static_cast<int>(ctx->x[12]);
        uint64_t out_addr = ctx->x[13];
        uint64_t out_size = ctx->x[14];

        // Validate function index.
        if (func_id >= static_cast<uint64_t>(NUM_DISPATCH)) {
            fprintf(stderr, "spike_ecall: bad func_id %llu\n",
                    (unsigned long long)func_id);
            ctx->x[10] = 0;
            return -1;
        }

        // Bounds check output buffer.
        if (out_addr + out_size > sc->memory_size) {
            ctx->x[10] = 0;
            return -1;
        }

        // Build host fargs[] by reading guest pointers.
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

        // Set up output buffer with MUX buff/bufc convention.
        UTF8 *buff = sc->memory + out_addr;
        UTF8 *bufc = buff;

        // Call the REAL engine function with full MUX signature.
        dispatch_table[func_id].handler(
            nullptr,    // fp (unused by math functions)
            buff, &bufc,
            1, 1, 1,    // executor, caller, enactor = GOD
            0,           // eval
            fargs, nfargs,
            nullptr, 0   // cargs, ncargs
        );

        // Null-terminate and return length.
        *bufc = '\0';
        ctx->x[10] = static_cast<uint64_t>(bufc - buff);
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
    const char *name;
    int func_id;
    const char *args[16];
    int nargs;
    const char *expected;
};

static uint64_t write_guest_string(uint8_t *mem, uint64_t &pool,
                                    const char *s) {
    uint64_t addr = pool;
    size_t len = strlen(s) + 1;
    memcpy(mem + addr, s, len);
    pool += (len + 7) & ~7ULL;
    return addr;
}

static std::vector<uint32_t> build_call_code(int func_id,
                                              uint64_t fargs_addr,
                                              int nfargs,
                                              uint64_t out_addr,
                                              int out_size) {
    std::vector<uint32_t> code;
    code.push_back(ADDI(17, 0, 0x100));
    code.push_back(ADDI(10, 0, func_id));

    uint32_t hi = fargs_addr & 0xFFFFF000;
    int32_t lo = static_cast<int32_t>(fargs_addr & 0xFFF);
    code.push_back(LUI(11, hi));
    code.push_back(ADDI(11, 11, lo));

    code.push_back(ADDI(12, 0, nfargs));

    hi = out_addr & 0xFFFFF000;
    lo = static_cast<int32_t>(out_addr & 0xFFF);
    code.push_back(LUI(13, hi));
    code.push_back(ADDI(13, 13, lo));

    code.push_back(ADDI(14, 0, out_size));
    code.push_back(ECALL());

    code.push_back(ADDI(17, 0, 93));
    code.push_back(ADDI(10, 0, 0));
    code.push_back(ECALL());
    return code;
}

static bool run_test(const test_case &tc, bool use_dbt) {
    const size_t MEM_SIZE = 64 * 1024;
    std::vector<uint8_t> memory(MEM_SIZE, 0);

    uint64_t pool = 0x1000;
    uint64_t arg_addrs[16];
    for (int i = 0; i < tc.nargs; i++) {
        arg_addrs[i] = write_guest_string(memory.data(), pool, tc.args[i]);
    }

    uint64_t fargs_addr = (pool + 7) & ~7ULL;
    for (int i = 0; i < tc.nargs; i++) {
        memcpy(memory.data() + fargs_addr + i * 8, &arg_addrs[i], 8);
    }

    uint64_t out_addr = fargs_addr + tc.nargs * 8 + 16;
    out_addr = (out_addr + 7) & ~7ULL;
    int out_size = 192;

    auto code = build_call_code(tc.func_id, fargs_addr, tc.nargs,
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
           dispatch_table[tc.func_id].name, tc.args[0]);
    for (int i = 1; i < tc.nargs; i++) printf(",%s", tc.args[i]);
    printf(") = \"%s\"  (expect \"%s\")  %s\n",
           result, tc.expected, pass ? "PASS" : "FAIL");

    return pass;
}

// ---------------------------------------------------------------
// Main
// ---------------------------------------------------------------

int main() {
    printf("=== Deep Spike: REAL engine functions via ECALL ===\n");
    printf("    (fun_add, fun_sub, fun_mul from funmath.eo)\n\n");

    test_case tests[] = {
        // fun_add: integer fast path
        { "int_add",       0, {"1", "2"},              2, "3"  },
        // fun_add: float path (mux_atof + AddDoubles + fval)
        { "float_add",     0, {"1.5", "2.5"},          2, "4"  },
        // fun_add: multi-arg
        { "multi_add",     0, {"1","2","3","4","5"},    5, "15" },
        // fun_add: large numbers crossing int→float boundary
        { "big_add",       0, {"999999999","1"},        2, "1000000000" },
        // fun_mul: real engine multiply
        { "mul",           1, {"6", "7"},               2, "42" },
        // fun_mul: float multiply
        { "float_mul",     1, {"3.14", "2"},            2, "6.28" },
        // fun_sub: real engine subtract
        { "sub",           2, {"100", "58"},            2, "42" },
        // fun_sub: negative result
        { "neg_sub",       2, {"10", "100"},            2, "-90" },
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
