/*! \file dbt_spike.cpp
 * \brief Deep spike: real MUX function dispatch through ECALL.
 *
 * Proves that compiled RISC-V code can call real MUX functions
 * through ECALL dispatch using the actual FUN calling convention,
 * real MUX types (UTF8, dbref), and real libmux utilities
 * (mux_atol, safe_ltoa, mux_atof, fval).
 *
 * The ECALL handler maintains a dispatch table of function pointers
 * matching the full MUX FUNCTION() signature.  Guest RISC-V code
 * passes a function index, and the handler calls the real function.
 *
 * Tests:
 *   1. add(1,2)         → "3"       (integer fast path)
 *   2. add(1.5,2.5)     → "4"       (float path via mux_atof)
 *   3. mul(6,7)          → "42"      (second function in table)
 *   4. add(1,2,3,4,5)   → "15"      (multi-arg)
 *   5. sub(100,58)       → "42"      (subtraction)
 *   6. strlen(hello)     → "5"       (string function)
 *
 * Compile:
 *   g++ -std=c++17 -O2 -I../../include -o dbt_spike \
 *       dbt_spike.cpp dbt.cpp dbt_interp.cpp dbt_elf64.cpp \
 *       -L../../lib -lmux -Wl,-rpath,'$ORIGIN/../../lib' -lm
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
// Real MUX functions using the full FUNCTION() signature.
//
// These use real libmux utilities (mux_atol, safe_ltoa, mux_atof,
// fval, AddDoubles) and follow the real buff/bufc convention.
// ---------------------------------------------------------------

// Exact copy of fun_add from funmath.cpp — uses real MUX types.
//
static const long nMaximums[10] =
{
    0, 9, 99, 999, 9999, 99999, 999999, 9999999, 99999999, 999999999
};

static double g_aDoubles[MAX_WORDS];

static FUNCTION(spike_fun_add)
{
    UNUSED_PARAMETER(fp);
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    int nArgs = nfargs;
    if (MAX_WORDS < nArgs)
    {
        nArgs = MAX_WORDS;
    }

    int i;
    for (i = 0; i < nArgs; i++)
    {
        int nDigits;
        long nMaxValue = 0;
        if (  !is_integer(fargs[i], &nDigits)
           || nDigits > 9
           || (nMaxValue += nMaximums[nDigits]) > 999999999L)
        {
            // Do it the slow way.
            //
            for (int j = 0; j < nArgs; j++)
            {
                g_aDoubles[j] = mux_atof(fargs[j]);
            }

            fval(buff, bufc, AddDoubles(nArgs, g_aDoubles));
            return;
        }
    }

    // We can do it the fast way.
    //
    long sum = 0;
    for (i = 0; i < nArgs; i++)
    {
        sum += mux_atol(fargs[i]);
    }
    safe_ltoa(sum, buff, bufc);
}

// fun_mul: multiply numeric arguments.
//
static FUNCTION(spike_fun_mul)
{
    UNUSED_PARAMETER(fp);
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    double product = 1.0;
    for (int i = 0; i < nfargs; i++)
    {
        product *= mux_atof(fargs[i]);
    }
    fval(buff, bufc, product);
}

// fun_sub: subtract second arg from first.
//
static FUNCTION(spike_fun_sub)
{
    UNUSED_PARAMETER(fp);
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (nfargs != 2) return;
    fval(buff, bufc, mux_atof(fargs[0]) - mux_atof(fargs[1]));
}

// fun_strlen: return string length.
//
static FUNCTION(spike_fun_strlen)
{
    UNUSED_PARAMETER(fp);
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (nfargs < 1) return;
    size_t len = strlen(reinterpret_cast<const char *>(fargs[0]));
    safe_ltoa(static_cast<long>(len), buff, bufc);
}

// ---------------------------------------------------------------
// Dispatch table: maps function index → real handler.
// ---------------------------------------------------------------

typedef void (*mux_fn_t)(FUN *fp, UTF8 *buff, UTF8 **bufc,
                          dbref executor, dbref caller, dbref enactor,
                          int eval, UTF8 *fargs[], int nfargs,
                          const UTF8 *cargs[], int ncargs);

static struct {
    const char *name;
    mux_fn_t handler;
} dispatch_table[] = {
    { "add",    spike_fun_add    },   // 0
    { "mul",    spike_fun_mul    },   // 1
    { "sub",    spike_fun_sub    },   // 2
    { "strlen", spike_fun_strlen },   // 3
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

        // Call the real function with full MUX signature.
        // executor=1 (GOD), caller=1, enactor=1, eval=0
        dispatch_table[func_id].handler(
            nullptr,    // fp (unused by our functions)
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

static uint32_t s_type(uint8_t opcode, uint8_t funct3,
                        uint8_t rs1, uint8_t rs2, int32_t imm) {
    return opcode | ((static_cast<uint32_t>(imm) & 0x1F) << 7)
         | (funct3 << 12) | (rs1 << 15) | (rs2 << 20)
         | (((static_cast<uint32_t>(imm) >> 5) & 0x7F) << 25);
}

static uint32_t u_type(uint8_t opcode, uint8_t rd, int32_t imm) {
    return opcode | (rd << 7) | (static_cast<uint32_t>(imm) & 0xFFFFF000);
}

static uint32_t r_type(uint8_t opcode, uint8_t rd, uint8_t funct3,
                        uint8_t rs1, uint8_t rs2, uint8_t funct7) {
    return opcode | (rd << 7) | (funct3 << 12) | (rs1 << 15)
         | (rs2 << 20) | (funct7 << 25);
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
// Helper: set up guest memory for a function call test.
//
// Memory layout (within a 64KB region):
//   0x0000 .. 0x03FF: code
//   0x1000+:          string pool (args written sequentially)
//   fargs_base:       fargs[] array (uint64_t pointers)
//   out_base:         output buffer (192 bytes)
// ---------------------------------------------------------------

struct test_case {
    const char *name;
    int func_id;
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
    pool += (len + 7) & ~7ULL; // align to 8
    return addr;
}

// Build RV64 code to call a function and exit.
//
static std::vector<uint32_t> build_call_code(int func_id,
                                              uint64_t fargs_addr,
                                              int nfargs,
                                              uint64_t out_addr,
                                              int out_size) {
    std::vector<uint32_t> code;

    // a7 = 0x100 (ECALL_CALL_FUNC)
    code.push_back(ADDI(17, 0, 0x100));

    // a0 = func_id
    code.push_back(ADDI(10, 0, func_id));

    // a1 = fargs_addr (use LUI + ADDI for addresses > 2047)
    uint32_t hi = fargs_addr & 0xFFFFF000;
    int32_t lo = static_cast<int32_t>(fargs_addr & 0xFFF);
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

    // ECALL
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

    // Write argument strings to guest memory.
    uint64_t pool = 0x1000;
    uint64_t arg_addrs[16];
    for (int i = 0; i < tc.nargs; i++) {
        arg_addrs[i] = write_guest_string(memory.data(), pool, tc.args[i]);
    }

    // Write fargs[] array (aligned after string pool).
    uint64_t fargs_addr = (pool + 7) & ~7ULL;
    for (int i = 0; i < tc.nargs; i++) {
        memcpy(memory.data() + fargs_addr + i * 8, &arg_addrs[i], 8);
    }

    // Output buffer after fargs array.
    uint64_t out_addr = fargs_addr + tc.nargs * 8 + 16;
    out_addr = (out_addr + 7) & ~7ULL;
    int out_size = 192;

    // Assemble code.
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

    printf("  %-20s %s(\"%s\"", use_dbt ? "[JIT]" : "[interp]",
           dispatch_table[tc.func_id].name, tc.args[0]);
    for (int i = 1; i < tc.nargs; i++) printf(",\"%s\"", tc.args[i]);
    printf(") = \"%s\"  (expect \"%s\")  %s\n",
           result, tc.expected, pass ? "PASS" : "FAIL");

    return pass;
}

// ---------------------------------------------------------------
// Main
// ---------------------------------------------------------------

int main() {
    printf("=== Deep Spike: real MUX function dispatch via ECALL ===\n\n");

    test_case tests[] = {
        { "int_add",       0, {"1", "2"},            2, "3"  },
        { "float_add",     0, {"1.5", "2.5"},        2, "4"  },
        { "multi_add",     0, {"1","2","3","4","5"},  5, "15" },
        { "mul",           1, {"6", "7"},             2, "42" },
        { "sub",           2, {"100", "58"},          2, "42" },
        { "strlen",        3, {"hello"},              1, "5"  },
        { "strlen_empty",  3, {""},                   1, "0"  },
        { "big_add",       0, {"999999999","1"},      2, "1000000000" },
    };
    int ntests = sizeof(tests) / sizeof(tests[0]);
    int pass = 0, fail = 0;

    // Run each test through both interpreter and JIT.
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
