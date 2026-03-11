/*! \file dbt_spike.cpp
 * \brief Spike: add(1,2) end-to-end through the JIT.
 *
 * Proves that compiled RISC-V code can call a real MUX function
 * (fun_add) through ECALL dispatch and get the correct result.
 *
 * The flow:
 *   1. Hand-assemble RV64 code that:
 *      - Writes "1\0" and "2\0" to guest memory
 *      - Sets up an fargs[] array pointing to those strings
 *      - Puts function index + arg pointers in registers
 *      - Executes ECALL to request "call function"
 *      - The ECALL handler (running native x86-64) calls fun_add
 *      - Result is written to a guest memory buffer
 *   2. Run through the DBT (JIT to x86-64)
 *   3. Verify the output buffer contains "3"
 *
 * Compile:
 *   g++ -std=c++17 -O2 -I../../include -o dbt_spike \
 *       dbt_spike.cpp dbt.cpp dbt_interp.cpp dbt_elf64.cpp -lm
 */

#include "dbt.h"
#include "dbt_interp.h"
#include "dbt_decoder.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <cstdint>

// ---------------------------------------------------------------
// Minimal function handler matching the FUN signature.
//
// We don't link against the full engine — instead we provide
// a self-contained implementation of add() that works the same
// way: parse integer arguments, sum them, write result to buffer.
// ---------------------------------------------------------------

using UTF8 = unsigned char;
using dbref = int;

// safe_ltoa: write a long to a buffer (simplified).
//
static void spike_safe_ltoa(long val, UTF8 *buff, UTF8 **bufc) {
    char tmp[32];
    int n = snprintf(tmp, sizeof(tmp), "%ld", val);
    size_t avail = (buff + 8192) - *bufc;
    if (static_cast<size_t>(n) > avail) n = static_cast<int>(avail);
    memcpy(*bufc, tmp, n);
    *bufc += n;
}

// Minimal add(): sum integer arguments, write result string.
//
static void spike_fun_add(UTF8 *buff, UTF8 **bufc,
                           UTF8 *fargs[], int nfargs) {
    long sum = 0;
    for (int i = 0; i < nfargs; i++) {
        sum += atol(reinterpret_cast<const char *>(fargs[i]));
    }
    spike_safe_ltoa(sum, buff, bufc);
}

// ---------------------------------------------------------------
// ECALL convention for function calls:
//
//   a7 (x17) = 0x100  — "call softcode function"
//   a0 (x10) = function index (0 = add for this spike)
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

        // Bounds check
        if (out_addr + out_size > sc->memory_size) {
            ctx->x[10] = 0;
            return -1;
        }

        // Build host fargs[] by reading guest pointers
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

        // Set up output buffer
        UTF8 *buff = sc->memory + out_addr;
        UTF8 *bufc = buff;

        switch (func_id) {
        case 0: // add
            spike_fun_add(buff, &bufc, fargs, nfargs);
            break;
        default:
            break;
        }

        // Null-terminate and return length
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
// RV64 instruction encoding helpers (same as dbt_test.cpp)
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
static uint32_t ADD(uint8_t rd, uint8_t rs1, uint8_t rs2) {
    return r_type(OP_REG, rd, ALU_ADD, rs1, rs2, 0x00);
}
static uint32_t LUI(uint8_t rd, int32_t imm) {
    return u_type(OP_LUI, rd, imm);
}
static uint32_t SD(uint8_t rs1, uint8_t rs2, int32_t imm) {
    return s_type(OP_STORE, 3, rs1, rs2, imm);
}
static uint32_t ECALL() {
    return i_type(OP_SYSTEM, 0, 0, 0, 0);
}
// SB: store byte
static uint32_t SB(uint8_t rs1, uint8_t rs2, int32_t imm) {
    return s_type(OP_STORE, 0, rs1, rs2, imm);
}

// ---------------------------------------------------------------
// The spike: add(1,2) → "3"
// ---------------------------------------------------------------

static bool run_spike_interp() {
    printf("=== Spike: add(1,2) via interpreter ===\n");

    // Memory layout:
    //   0x0000 .. 0x03FF: code
    //   0x1000 .. 0x100F: string "1\0"
    //   0x1010 .. 0x101F: string "2\0"
    //   0x1020 .. 0x102F: fargs[] array (2 x uint64_t pointers)
    //   0x1040 .. 0x10FF: output buffer
    //   0xF000 .. 0xFFFF: stack
    //
    const size_t MEM_SIZE = 64 * 1024;
    std::vector<uint8_t> memory(MEM_SIZE, 0);

    // Pre-fill strings
    memory[0x1000] = '1';
    memory[0x1001] = '\0';
    memory[0x1010] = '2';
    memory[0x1011] = '\0';

    // Pre-fill fargs[] array: two 64-bit pointers
    uint64_t ptr0 = 0x1000, ptr1 = 0x1010;
    memcpy(memory.data() + 0x1020, &ptr0, 8);
    memcpy(memory.data() + 0x1028, &ptr1, 8);

    // Assemble code:
    //   a7 = 0x100 (ECALL_CALL_FUNC)
    //   a0 = 0 (func_id = add)
    //   a1 = 0x1020 (fargs pointer)
    //   a2 = 2 (nfargs)
    //   a3 = 0x1040 (output buffer)
    //   a4 = 192 (output size)
    //   ecall
    //   # Result length is now in a0
    //   a7 = 93 (exit)
    //   a0 = 0 (success)
    //   ecall
    //
    std::vector<uint32_t> code = {
        ADDI(17, 0, 0x100),     // a7 = 0x100
        ADDI(10, 0, 0),         // a0 = 0 (add)
        LUI(11, 0x1000),        // a1 = 0x1000
        ADDI(11, 11, 0x020),    // a1 = 0x1020
        ADDI(12, 0, 2),         // a2 = 2
        LUI(13, 0x1000),        // a3 = 0x1000
        ADDI(13, 13, 0x040),    // a3 = 0x1040
        ADDI(14, 0, 192),       // a4 = 192
        ECALL(),
        // a0 now has result length — save it
        ADDI(5, 10, 0),         // t0 = a0 (result length)
        ADDI(17, 0, 93),        // a7 = 93 (exit)
        ADDI(10, 0, 0),         // a0 = 0 (success)
        ECALL()
    };

    for (size_t i = 0; i < code.size(); i++) {
        memcpy(memory.data() + i * 4, &code[i], 4);
    }

    // Run via interpreter
    spike_ctx sc = { memory.data(), MEM_SIZE };

    rv64_state_t state = {};
    state.pc = 0;
    state.x[2] = MEM_SIZE - 16; // SP

    rv64_memory_t mem = { memory.data(), MEM_SIZE };

    // Wrap the ecall for the interpreter's signature
    struct interp_wrap {
        static int ecall(rv64_state_t *s, void *ud) {
            spike_ctx *sc = static_cast<spike_ctx *>(ud);
            // Convert rv64_state_t to rv64_ctx_t for spike_ecall
            rv64_ctx_t ctx = {};
            for (int i = 0; i < 32; i++) ctx.x[i] = s->x[i];
            int rc = spike_ecall(&ctx, ud);
            for (int i = 0; i < 32; i++) s->x[i] = ctx.x[i];
            return rc;
        }
    };

    int rc = rv64_interp_run(&state, &mem, interp_wrap::ecall, &sc);

    // Check result
    const char *result = reinterpret_cast<const char *>(memory.data() + 0x1040);
    printf("  Exit code: %d\n", rc);
    printf("  Output buffer: \"%s\"\n", result);
    printf("  Result length (t0): %llu\n", (unsigned long long)state.x[5]);

    bool pass = (rc == 0 && strcmp(result, "3") == 0);
    printf("  %s\n\n", pass ? "PASS" : "FAIL");
    return pass;
}

static bool run_spike_dbt() {
    printf("=== Spike: add(1,2) via DBT/JIT ===\n");

    const size_t MEM_SIZE = 64 * 1024;
    std::vector<uint8_t> memory(MEM_SIZE, 0);

    // Pre-fill strings
    memory[0x1000] = '1';
    memory[0x1001] = '\0';
    memory[0x1010] = '2';
    memory[0x1011] = '\0';

    // Pre-fill fargs[] array
    uint64_t ptr0 = 0x1000, ptr1 = 0x1010;
    memcpy(memory.data() + 0x1020, &ptr0, 8);
    memcpy(memory.data() + 0x1028, &ptr1, 8);

    // Same code as interpreter version
    std::vector<uint32_t> code = {
        ADDI(17, 0, 0x100),
        ADDI(10, 0, 0),
        LUI(11, 0x1000),
        ADDI(11, 11, 0x020),
        ADDI(12, 0, 2),
        LUI(13, 0x1000),
        ADDI(13, 13, 0x040),
        ADDI(14, 0, 192),
        ECALL(),
        ADDI(5, 10, 0),
        ADDI(17, 0, 93),
        ADDI(10, 0, 0),
        ECALL()
    };

    for (size_t i = 0; i < code.size(); i++) {
        memcpy(memory.data() + i * 4, &code[i], 4);
    }

    spike_ctx sc = { memory.data(), MEM_SIZE };

    dbt_state_t dbt;
    if (dbt_init(&dbt, memory.data(), MEM_SIZE, spike_ecall, &sc) != 0) {
        fprintf(stderr, "Failed to init DBT\n");
        return false;
    }
    int rc = dbt_run(&dbt, 0, MEM_SIZE - 16);

    const char *result = reinterpret_cast<const char *>(memory.data() + 0x1040);
    printf("  Exit code: %d\n", rc);
    printf("  Output buffer: \"%s\"\n", result);
    printf("  %s\n\n", (rc == 0 && strcmp(result, "3") == 0) ? "PASS" : "FAIL");

    bool pass = (rc == 0 && strcmp(result, "3") == 0);

    dbt_cleanup(&dbt);
    return pass;
}

// ---------------------------------------------------------------
// Main
// ---------------------------------------------------------------

int main() {
    bool p1 = run_spike_interp();
    bool p2 = run_spike_dbt();

    printf("Spike result: %s\n",
           (p1 && p2) ? "ALL PASS" : "SOME FAILED");
    return (p1 && p2) ? 0 : 1;
}
