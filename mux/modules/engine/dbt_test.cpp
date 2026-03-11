/*! \file dbt_test.cpp
 * \brief Standalone test harness for the RV64IMD interpreter.
 *
 * Compile:
 *   g++ -std=c++17 -O2 -I../../include -o dbt_test dbt_test.cpp dbt_interp.cpp dbt_elf64.cpp
 *
 * Run:
 *   ./dbt_test                          # hand-assembled tests only
 *   ./dbt_test dbt_rt/test_rv64.elf     # also run cross-compiled ELF
 *
 * Tests hand-assembled RV64IMD instruction sequences against expected
 * results.  Each test allocates a small memory buffer, writes machine
 * code, runs the interpreter, and checks register values.
 */

#include "dbt_interp.h"
#include "dbt_decoder.h"
#include "dbt_elf64.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <cmath>
#include <vector>

// ---------------------------------------------------------------
// Test infrastructure
// ---------------------------------------------------------------

static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define CHECK_EQ(desc, actual, expected) do { \
    g_tests_run++; \
    if ((actual) == (expected)) { \
        g_tests_passed++; \
    } else { \
        g_tests_failed++; \
        fprintf(stderr, "  FAIL: %s: got 0x%llX, expected 0x%llX\n", \
                (desc), \
                (unsigned long long)(actual), \
                (unsigned long long)(expected)); \
    } \
} while (0)

#define CHECK_FEQ(desc, actual, expected) do { \
    g_tests_run++; \
    double a_ = (actual), e_ = (expected); \
    if (a_ == e_ || (std::isnan(a_) && std::isnan(e_))) { \
        g_tests_passed++; \
    } else { \
        g_tests_failed++; \
        fprintf(stderr, "  FAIL: %s: got %g, expected %g\n", \
                (desc), a_, e_); \
    } \
} while (0)

// ECALL handler for tests: ECALL 93 = exit(a0).
//
static int test_ecall(rv64_state_t *state, void *) {
    uint64_t syscall_num = state->x[17]; // a7
    if (syscall_num == 93) {
        return static_cast<int>(state->x[10]); // a0 = exit code
    }
    fprintf(stderr, "  test_ecall: unhandled ecall %llu\n",
            (unsigned long long)syscall_num);
    return -1; // continue
}

// Helper: encode RV64 instructions.
//
// R-type: opcode | rd<<7 | funct3<<12 | rs1<<15 | rs2<<20 | funct7<<25
//
static uint32_t r_type(uint8_t opcode, uint8_t rd, uint8_t funct3,
                        uint8_t rs1, uint8_t rs2, uint8_t funct7) {
    return opcode | (rd << 7) | (funct3 << 12) | (rs1 << 15)
         | (rs2 << 20) | (funct7 << 25);
}

// I-type: opcode | rd<<7 | funct3<<12 | rs1<<15 | imm[11:0]<<20
//
static uint32_t i_type(uint8_t opcode, uint8_t rd, uint8_t funct3,
                        uint8_t rs1, int32_t imm) {
    return opcode | (rd << 7) | (funct3 << 12) | (rs1 << 15)
         | ((static_cast<uint32_t>(imm) & 0xFFF) << 20);
}

// S-type: opcode | imm[4:0]<<7 | funct3<<12 | rs1<<15 | rs2<<20 | imm[11:5]<<25
//
static uint32_t s_type(uint8_t opcode, uint8_t funct3,
                        uint8_t rs1, uint8_t rs2, int32_t imm) {
    return opcode | ((static_cast<uint32_t>(imm) & 0x1F) << 7)
         | (funct3 << 12) | (rs1 << 15) | (rs2 << 20)
         | (((static_cast<uint32_t>(imm) >> 5) & 0x7F) << 25);
}

// U-type: opcode | rd<<7 | imm[31:12]
//
static uint32_t u_type(uint8_t opcode, uint8_t rd, int32_t imm) {
    return opcode | (rd << 7) | (static_cast<uint32_t>(imm) & 0xFFFFF000);
}

// B-type: opcode | imm[11]<<7 | imm[4:1]<<8 | funct3<<12 | rs1<<15 | rs2<<20 | imm[10:5]<<25 | imm[12]<<31
//
static uint32_t b_type(uint8_t opcode, uint8_t funct3,
                        uint8_t rs1, uint8_t rs2, int32_t imm) {
    uint32_t i = static_cast<uint32_t>(imm);
    return opcode
         | (((i >> 11) & 1) << 7)
         | (((i >> 1) & 0xF) << 8)
         | (funct3 << 12)
         | (rs1 << 15)
         | (rs2 << 20)
         | (((i >> 5) & 0x3F) << 25)
         | (((i >> 12) & 1) << 31);
}

// R4-type (FMA): opcode | rd<<7 | funct3<<12 | rs1<<15 | rs2<<20 | fmt<<25 | rs3<<27
//
static uint32_t r4_type(uint8_t opcode, uint8_t rd, uint8_t funct3,
                         uint8_t rs1, uint8_t rs2, uint8_t rs3, uint8_t fmt) {
    return opcode | (rd << 7) | (funct3 << 12) | (rs1 << 15)
         | (rs2 << 20) | (fmt << 25) | (rs3 << 27);
}

// Convenience: ADDI rd, rs1, imm
static uint32_t ADDI(uint8_t rd, uint8_t rs1, int32_t imm) {
    return i_type(OP_IMM, rd, ALU_ADDI, rs1, imm);
}

// Convenience: ADDIW rd, rs1, imm
static uint32_t ADDIW(uint8_t rd, uint8_t rs1, int32_t imm) {
    return i_type(OP_IMM32, rd, 0, rs1, imm);
}

// Convenience: ADD rd, rs1, rs2
static uint32_t ADD(uint8_t rd, uint8_t rs1, uint8_t rs2) {
    return r_type(OP_REG, rd, ALU_ADD, rs1, rs2, 0x00);
}

// Convenience: SUB rd, rs1, rs2
static uint32_t SUB(uint8_t rd, uint8_t rs1, uint8_t rs2) {
    return r_type(OP_REG, rd, ALU_ADD, rs1, rs2, 0x20);
}

// Convenience: MUL rd, rs1, rs2
static uint32_t MUL(uint8_t rd, uint8_t rs1, uint8_t rs2) {
    return r_type(OP_REG, rd, 0, rs1, rs2, 0x01);
}

// Convenience: DIV rd, rs1, rs2
static uint32_t DIV(uint8_t rd, uint8_t rs1, uint8_t rs2) {
    return r_type(OP_REG, rd, 4, rs1, rs2, 0x01);
}

// Convenience: REM rd, rs1, rs2
static uint32_t REM(uint8_t rd, uint8_t rs1, uint8_t rs2) {
    return r_type(OP_REG, rd, 6, rs1, rs2, 0x01);
}

// Convenience: SLLI rd, rs1, shamt (64-bit)
static uint32_t SLLI(uint8_t rd, uint8_t rs1, int shamt) {
    return i_type(OP_IMM, rd, ALU_SLLI, rs1, shamt & 0x3F);
}

// Convenience: SRLI rd, rs1, shamt (64-bit)
static uint32_t SRLI(uint8_t rd, uint8_t rs1, int shamt) {
    return i_type(OP_IMM, rd, ALU_SRLI, rs1, shamt & 0x3F);
}

// Convenience: SRAI rd, rs1, shamt (64-bit)
static uint32_t SRAI(uint8_t rd, uint8_t rs1, int shamt) {
    return i_type(OP_IMM, rd, ALU_SRLI, rs1, (shamt & 0x3F) | 0x400);
}

// Convenience: LUI rd, imm (upper 20 bits)
static uint32_t LUI(uint8_t rd, int32_t imm) {
    return u_type(OP_LUI, rd, imm);
}

// Convenience: SD rs2, offset(rs1)
static uint32_t SD(uint8_t rs1, uint8_t rs2, int32_t offset) {
    return s_type(OP_STORE, ST_SD, rs1, rs2, offset);
}

// Convenience: LD rd, offset(rs1)
static uint32_t LD(uint8_t rd, uint8_t rs1, int32_t offset) {
    return i_type(OP_LOAD, rd, LD_LD, rs1, offset);
}

// Convenience: BEQ rs1, rs2, offset
static uint32_t BEQ(uint8_t rs1, uint8_t rs2, int32_t offset) {
    return b_type(OP_BRANCH, BR_BEQ, rs1, rs2, offset);
}

// Convenience: BNE rs1, rs2, offset
static uint32_t BNE(uint8_t rs1, uint8_t rs2, int32_t offset) {
    return b_type(OP_BRANCH, BR_BNE, rs1, rs2, offset);
}

// Convenience: ECALL
static uint32_t ECALL() {
    return i_type(OP_SYSTEM, 0, 0, 0, 0);
}

// Convenience: ADDW rd, rs1, rs2
static uint32_t ADDW(uint8_t rd, uint8_t rs1, uint8_t rs2) {
    return r_type(OP_REG32, rd, ALU_ADD, rs1, rs2, 0x00);
}

// Convenience: SUBW rd, rs1, rs2
static uint32_t SUBW(uint8_t rd, uint8_t rs1, uint8_t rs2) {
    return r_type(OP_REG32, rd, ALU_ADD, rs1, rs2, 0x20);
}

// Convenience: MULW rd, rs1, rs2
static uint32_t MULW(uint8_t rd, uint8_t rs1, uint8_t rs2) {
    return r_type(OP_REG32, rd, 0, rs1, rs2, 0x01);
}

// Run a code sequence, return exit code.
//
struct TestResult {
    int exit_code;
    rv64_state_t state;
};

static TestResult run_code(const std::vector<uint32_t>& code,
                           rv64_state_t *init_state = nullptr) {
    // Memory layout: code at offset 0, stack at end.
    //
    const size_t MEM_SIZE = 64 * 1024; // 64K
    std::vector<uint8_t> memory(MEM_SIZE, 0);

    // Write code to memory at offset 0.
    //
    for (size_t i = 0; i < code.size(); i++) {
        memcpy(memory.data() + i * 4, &code[i], 4);
    }

    rv64_memory_t mem = { memory.data(), MEM_SIZE };

    rv64_state_t state = {};
    if (init_state) {
        state = *init_state;
    }
    state.pc = 0;
    state.x[0] = 0;
    // Set stack pointer to near end of memory.
    state.x[2] = MEM_SIZE - 16;

    int rc = rv64_interp_run(&state, &mem, test_ecall, nullptr);

    TestResult result;
    result.exit_code = rc;
    result.state = state;
    return result;
}

// ---------------------------------------------------------------
// Tests
// ---------------------------------------------------------------

static void test_addi() {
    printf("test_addi...\n");

    // ADDI x1, x0, 42; ADDI x17, x0, 93; ADDI x10, x1, 0; ECALL
    auto r = run_code({
        ADDI(1, 0, 42),     // x1 = 42
        ADDI(17, 0, 93),    // a7 = 93 (exit)
        ADDI(10, 1, 0),     // a0 = x1
        ECALL()
    });

    CHECK_EQ("exit code", r.exit_code, 42);
    CHECK_EQ("x1", r.state.x[1], 42);
}

static void test_addi_negative() {
    printf("test_addi_negative...\n");

    auto r = run_code({
        ADDI(1, 0, -10),    // x1 = -10
        ADDI(17, 0, 93),
        ADDI(10, 0, 0),
        ECALL()
    });

    CHECK_EQ("x1 = -10", r.state.x[1], static_cast<uint64_t>(-10LL));
}

static void test_add_sub() {
    printf("test_add_sub...\n");

    auto r = run_code({
        ADDI(1, 0, 100),    // x1 = 100
        ADDI(2, 0, 58),     // x2 = 58
        ADD(3, 1, 2),       // x3 = 158
        SUB(4, 1, 2),       // x4 = 42
        ADDI(17, 0, 93),
        ADDI(10, 4, 0),     // a0 = x4
        ECALL()
    });

    CHECK_EQ("x3 = 158", r.state.x[3], 158);
    CHECK_EQ("x4 = 42", r.state.x[4], 42);
    CHECK_EQ("exit code", r.exit_code, 42);
}

static void test_lui_addi() {
    printf("test_lui_addi...\n");

    // Load 0x12345000 + 0x678 = 0x12345678
    auto r = run_code({
        LUI(1, 0x12345000),      // x1 = 0x12345000
        ADDI(1, 1, 0x678),       // x1 = 0x12345678
        ADDI(17, 0, 93),
        ADDI(10, 0, 0),
        ECALL()
    });

    CHECK_EQ("x1 = 0x12345678", r.state.x[1], 0x12345678ULL);
}

static void test_lui_sign_extend() {
    printf("test_lui_sign_extend...\n");

    // LUI with bit 31 set: should sign-extend to 64 bits.
    // LUI x1, 0x80000000 -> x1 = 0xFFFFFFFF80000000
    auto r = run_code({
        LUI(1, static_cast<int32_t>(0x80000000)),
        ADDI(17, 0, 93),
        ADDI(10, 0, 0),
        ECALL()
    });

    CHECK_EQ("LUI sign-extend", r.state.x[1], 0xFFFFFFFF80000000ULL);
}

static void test_shifts_64bit() {
    printf("test_shifts_64bit...\n");

    auto r = run_code({
        ADDI(1, 0, 1),          // x1 = 1
        SLLI(2, 1, 32),         // x2 = 1 << 32 = 0x100000000
        SLLI(3, 1, 63),         // x3 = 1 << 63 = 0x8000000000000000
        SRLI(4, 3, 63),         // x4 = 0x8000000000000000 >> 63 = 1
        SRAI(5, 3, 63),         // x5 = (int64_t)0x8000000000000000 >> 63 = -1
        ADDI(17, 0, 93),
        ADDI(10, 0, 0),
        ECALL()
    });

    CHECK_EQ("SLLI 32", r.state.x[2], 0x100000000ULL);
    CHECK_EQ("SLLI 63", r.state.x[3], 0x8000000000000000ULL);
    CHECK_EQ("SRLI 63", r.state.x[4], 1);
    CHECK_EQ("SRAI 63", r.state.x[5], static_cast<uint64_t>(-1LL));
}

static void test_mul_div_rem() {
    printf("test_mul_div_rem...\n");

    auto r = run_code({
        ADDI(1, 0, 7),          // x1 = 7
        ADDI(2, 0, 6),          // x2 = 6
        MUL(3, 1, 2),           // x3 = 42
        DIV(4, 3, 1),           // x4 = 42 / 7 = 6
        REM(5, 3, 2),           // x5 = 42 % 6 = 0
        ADDI(6, 0, 5),
        REM(7, 3, 6),           // x7 = 42 % 5 = 2
        ADDI(17, 0, 93),
        ADDI(10, 0, 0),
        ECALL()
    });

    CHECK_EQ("MUL 7*6", r.state.x[3], 42);
    CHECK_EQ("DIV 42/7", r.state.x[4], 6);
    CHECK_EQ("REM 42%6", r.state.x[5], 0);
    CHECK_EQ("REM 42%5", r.state.x[7], 2);
}

static void test_div_by_zero() {
    printf("test_div_by_zero...\n");

    auto r = run_code({
        ADDI(1, 0, 42),
        DIV(2, 1, 0),           // x2 = 42 / 0 = -1 (all ones)
        ADDI(17, 0, 93),
        ADDI(10, 0, 0),
        ECALL()
    });

    CHECK_EQ("DIV by zero", r.state.x[2], UINT64_MAX);
}

static void test_branch_beq() {
    printf("test_branch_beq...\n");

    auto r = run_code({
        ADDI(1, 0, 5),          // x1 = 5
        ADDI(2, 0, 5),          // x2 = 5
        BEQ(1, 2, 8),           // if x1 == x2, skip next
        ADDI(3, 0, 1),          // x3 = 1 (skipped)
        ADDI(3, 0, 2),          // x3 = 2 (taken)
        ADDI(17, 0, 93),
        ADDI(10, 3, 0),
        ECALL()
    });

    CHECK_EQ("BEQ taken", r.state.x[3], 2);
    CHECK_EQ("exit code", r.exit_code, 2);
}

static void test_branch_bne() {
    printf("test_branch_bne...\n");

    auto r = run_code({
        ADDI(1, 0, 5),
        ADDI(2, 0, 6),
        BNE(1, 2, 8),           // if x1 != x2, skip next
        ADDI(3, 0, 1),          // skipped
        ADDI(3, 0, 2),          // taken
        ADDI(17, 0, 93),
        ADDI(10, 3, 0),
        ECALL()
    });

    CHECK_EQ("BNE taken", r.state.x[3], 2);
}

static void test_load_store_64() {
    printf("test_load_store_64...\n");

    // Store a 64-bit value to stack, load it back.
    auto r = run_code({
        ADDI(1, 0, 1),          // x1 = 1
        SLLI(1, 1, 40),         // x1 = 1 << 40 = 0x10000000000
        ADDI(1, 1, 42),         // x1 = 0x1000000002A
        SD(2, 1, -8),           // mem[sp-8] = x1
        LD(3, 2, -8),           // x3 = mem[sp-8]
        ADDI(17, 0, 93),
        ADDI(10, 0, 0),
        ECALL()
    });

    CHECK_EQ("SD/LD roundtrip", r.state.x[3], 0x1000000002AULL);
}

static void test_load_store_32_sign_ext() {
    printf("test_load_store_32_sign_ext...\n");

    // Store 0xFFFFFFFF to memory, load with LW (sign-extend) and LWU (zero-extend).
    auto r = run_code({
        ADDI(1, 0, -1),                                         // x1 = 0xFFFFFFFFFFFFFFFF
        s_type(OP_STORE, ST_SW, 2, 1, -8),                     // SW x1, -8(sp)
        i_type(OP_LOAD, 3, LD_LW, 2, -8),                      // LW x3, -8(sp) — sign-extend
        i_type(OP_LOAD, 4, LD_LWU, 2, -8),                     // LWU x4, -8(sp) — zero-extend
        ADDI(17, 0, 93),
        ADDI(10, 0, 0),
        ECALL()
    });

    CHECK_EQ("LW sign-ext", r.state.x[3], 0xFFFFFFFFFFFFFFFFULL);
    CHECK_EQ("LWU zero-ext", r.state.x[4], 0x00000000FFFFFFFFULL);
}

static void test_w_suffix_ops() {
    printf("test_w_suffix_ops...\n");

    auto r = run_code({
        // ADDIW: operate on lower 32, sign-extend.
        // Start with 0x7FFF, add 1 via ADDIW: 0x8000 fits in 32 bits, positive.
        // Better test: use LUI to get 0x7FFFF000, ADDIW 0x7FF+1 overflows.
        // Simplest: load a value where ADDIW wraps bit 31.
        LUI(1, static_cast<int32_t>(0x80000000)), // x1 = 0xFFFFFFFF80000000
        ADDIW(2, 1, -1),        // ADDIW: lower32(0x80000000) + (-1) = 0x7FFFFFFF -> sext = 0x7FFFFFFF
        // ADDW
        ADDI(3, 0, 100),
        ADDI(4, 0, 200),
        ADDW(5, 3, 4),          // x5 = sext32(300)
        // SUBW
        SUBW(6, 3, 4),          // x6 = sext32(-100)
        // MULW
        ADDI(7, 0, -7),
        MULW(8, 4, 7),          // x8 = sext32(200 * -7 = -1400)
        ADDI(17, 0, 93),
        ADDI(10, 0, 0),
        ECALL()
    });

    CHECK_EQ("ADDIW wrap", r.state.x[2], 0x000000007FFFFFFFULL);
    CHECK_EQ("ADDW 100+200", r.state.x[5], 300);
    CHECK_EQ("SUBW 100-200", r.state.x[6], static_cast<uint64_t>(static_cast<int64_t>(-100)));
    CHECK_EQ("MULW 200*-7", r.state.x[8], static_cast<uint64_t>(static_cast<int64_t>(-1400)));
}

static void test_loop() {
    printf("test_loop...\n");

    // Sum 1..10 using a loop.
    // x1 = counter (starts at 10), x2 = accumulator
    auto r = run_code({
        ADDI(1, 0, 10),         //  0: x1 = 10
        ADDI(2, 0, 0),          //  4: x2 = 0
        ADD(2, 2, 1),           //  8: x2 += x1  (loop body)
        ADDI(1, 1, -1),         // 12: x1--
        BNE(1, 0, -8),          // 16: if x1 != 0, goto offset -8 (back to +8)
        ADDI(17, 0, 93),        // 20:
        ADDI(10, 2, 0),         // 24:
        ECALL()                  // 28:
    });

    CHECK_EQ("loop sum 1..10", r.state.x[2], 55);
    CHECK_EQ("exit code", r.exit_code, 55);
}

static void test_fp_add() {
    printf("test_fp_add...\n");

    // Load 3.14 and 2.72 into FP regs via integer move, add them.
    double a = 3.14, b = 2.72;
    uint64_t a_bits, b_bits;
    memcpy(&a_bits, &a, 8);
    memcpy(&b_bits, &b, 8);

    // We need to load 64-bit immediates into integer regs.
    // Strategy: store the doubles in memory (at stack area), load via FLD.
    //
    // Code: write a_bits to mem[sp-16], b_bits to mem[sp-8],
    //       FLD f1, -16(sp); FLD f2, -8(sp); FADD.D f3, f1, f2
    //       FMV.X.D x3, f3; exit.
    //
    // But we can't assemble 64-bit immediates easily in RV64.
    // Instead, pre-fill the memory.

    const size_t MEM_SIZE = 64 * 1024;
    std::vector<uint8_t> memory(MEM_SIZE, 0);

    // Write doubles at known addresses (0x1000).
    memcpy(memory.data() + 0x1000, &a_bits, 8);
    memcpy(memory.data() + 0x1008, &b_bits, 8);

    // Code at offset 0.
    std::vector<uint32_t> code = {
        LUI(1, 0x1000),                                     // x1 = 0x1000
        i_type(OP_FP_LOAD, 1, 3, 1, 0),                     // FLD f1, 0(x1)
        i_type(OP_FP_LOAD, 2, 3, 1, 8),                     // FLD f2, 8(x1)
        r_type(OP_FP, 3, 0, 1, 2, (FP_FADD << 2) | FP_FMT_D), // FADD.D f3, f1, f2
        r_type(OP_FP, 3, 0, 3, 0, (FP_FCLASS << 2) | FP_FMT_D), // FMV.X.D x3, f3
        ADDI(17, 0, 93),
        ADDI(10, 0, 0),
        ECALL()
    };

    for (size_t i = 0; i < code.size(); i++) {
        memcpy(memory.data() + i * 4, &code[i], 4);
    }

    rv64_memory_t mem = { memory.data(), MEM_SIZE };
    rv64_state_t state = {};
    state.pc = 0;
    state.x[2] = MEM_SIZE - 16;

    rv64_interp_run(&state, &mem, test_ecall, nullptr);

    // x3 should have the bits of 3.14 + 2.72 = 5.86
    double result;
    memcpy(&result, &state.x[3], 8);
    CHECK_FEQ("FADD.D 3.14+2.72", result, 3.14 + 2.72);
}

static void test_fp_mul_div() {
    printf("test_fp_mul_div...\n");

    double a = 6.0, b = 7.0;
    uint64_t a_bits, b_bits;
    memcpy(&a_bits, &a, 8);
    memcpy(&b_bits, &b, 8);

    const size_t MEM_SIZE = 64 * 1024;
    std::vector<uint8_t> memory(MEM_SIZE, 0);
    memcpy(memory.data() + 0x1000, &a_bits, 8);
    memcpy(memory.data() + 0x1008, &b_bits, 8);

    std::vector<uint32_t> code = {
        LUI(1, 0x1000),
        i_type(OP_FP_LOAD, 1, 3, 1, 0),                         // FLD f1, 0(x1)
        i_type(OP_FP_LOAD, 2, 3, 1, 8),                         // FLD f2, 8(x1)
        r_type(OP_FP, 3, 0, 1, 2, (FP_FMUL << 2) | FP_FMT_D),  // FMUL.D f3, f1, f2
        r_type(OP_FP, 4, 0, 3, 2, (FP_FDIV << 2) | FP_FMT_D),  // FDIV.D f4, f3, f2
        r_type(OP_FP, 3, 0, 3, 0, (FP_FCLASS << 2) | FP_FMT_D), // FMV.X.D x3, f3
        r_type(OP_FP, 4, 0, 4, 0, (FP_FCLASS << 2) | FP_FMT_D), // FMV.X.D x4, f4
        ADDI(17, 0, 93),
        ADDI(10, 0, 0),
        ECALL()
    };

    for (size_t i = 0; i < code.size(); i++) {
        memcpy(memory.data() + i * 4, &code[i], 4);
    }

    rv64_memory_t mem = { memory.data(), MEM_SIZE };
    rv64_state_t state = {};
    state.pc = 0;
    state.x[2] = MEM_SIZE - 16;

    rv64_interp_run(&state, &mem, test_ecall, nullptr);

    double mul_result, div_result;
    memcpy(&mul_result, &state.x[3], 8);
    memcpy(&div_result, &state.x[4], 8);
    CHECK_FEQ("FMUL.D 6*7", mul_result, 42.0);
    CHECK_FEQ("FDIV.D 42/7", div_result, 6.0);
}

static void test_fp_convert() {
    printf("test_fp_convert...\n");

    // FCVT.D.L: convert int64 42 to double, then FCVT.L.D back.
    auto r = run_code({
        ADDI(1, 0, 42),
        // FCVT.D.L f1, x1 (rs2=2 means L, fmt=D=1)
        r_type(OP_FP, 1, 0, 1, 2, (FP_FCVTDW << 2) | FP_FMT_D),
        // FCVT.L.D x2, f1 (rs2=2 means L, fmt=D=1)
        r_type(OP_FP, 2, 0, 1, 2, (FP_FCVTW << 2) | FP_FMT_D),
        ADDI(17, 0, 93),
        ADDI(10, 2, 0),
        ECALL()
    });

    CHECK_EQ("FCVT roundtrip", r.state.x[2], 42);
    CHECK_EQ("exit code", r.exit_code, 42);
}

static void test_fp_compare() {
    printf("test_fp_compare...\n");

    double a = 3.0, b = 5.0;
    uint64_t a_bits, b_bits;
    memcpy(&a_bits, &a, 8);
    memcpy(&b_bits, &b, 8);

    const size_t MEM_SIZE = 64 * 1024;
    std::vector<uint8_t> memory(MEM_SIZE, 0);
    memcpy(memory.data() + 0x1000, &a_bits, 8);
    memcpy(memory.data() + 0x1008, &b_bits, 8);

    std::vector<uint32_t> code = {
        LUI(1, 0x1000),
        i_type(OP_FP_LOAD, 1, 3, 1, 0),                         // FLD f1, 0(x1)  = 3.0
        i_type(OP_FP_LOAD, 2, 3, 1, 8),                         // FLD f2, 8(x1)  = 5.0
        r_type(OP_FP, 3, 1, 1, 2, (FP_FCMP << 2) | FP_FMT_D),  // FLT.D x3, f1, f2 (3<5 = 1)
        r_type(OP_FP, 4, 1, 2, 1, (FP_FCMP << 2) | FP_FMT_D),  // FLT.D x4, f2, f1 (5<3 = 0)
        r_type(OP_FP, 5, 2, 1, 1, (FP_FCMP << 2) | FP_FMT_D),  // FEQ.D x5, f1, f1 (3==3 = 1)
        ADDI(17, 0, 93),
        ADDI(10, 0, 0),
        ECALL()
    };

    for (size_t i = 0; i < code.size(); i++) {
        memcpy(memory.data() + i * 4, &code[i], 4);
    }

    rv64_memory_t mem = { memory.data(), MEM_SIZE };
    rv64_state_t state = {};
    state.pc = 0;
    state.x[2] = MEM_SIZE - 16;

    rv64_interp_run(&state, &mem, test_ecall, nullptr);

    CHECK_EQ("FLT 3<5", state.x[3], 1);
    CHECK_EQ("FLT 5<3", state.x[4], 0);
    CHECK_EQ("FEQ 3==3", state.x[5], 1);
}

static void test_mulh() {
    printf("test_mulh...\n");

    // MULH: signed high multiply.
    // 0x7FFFFFFFFFFFFFFF * 2 = 0xFFFFFFFFFFFFFFFE (low), 0x0000000000000000 (high)
    // Wait, let's use a case that gives a non-zero high part.
    // 0x100000000 * 0x100000000 = 0x10000000000000000 -> high = 1
    auto r = run_code({
        ADDI(1, 0, 1),
        SLLI(1, 1, 32),             // x1 = 0x100000000
        ADDI(2, 0, 1),
        SLLI(2, 2, 32),             // x2 = 0x100000000
        r_type(OP_REG, 3, 3, 1, 2, 0x01), // MULHU x3, x1, x2 (funct3=3)
        ADDI(17, 0, 93),
        ADDI(10, 0, 0),
        ECALL()
    });

    CHECK_EQ("MULHU high", r.state.x[3], 1);
}

static void test_x0_always_zero() {
    printf("test_x0_always_zero...\n");

    // Attempt to write to x0 — should remain 0.
    auto r = run_code({
        ADDI(0, 0, 42),     // try to write 42 to x0
        ADDI(17, 0, 93),
        ADDI(10, 0, 0),     // a0 = x0 = should be 0
        ECALL()
    });

    CHECK_EQ("x0 = 0", r.state.x[0], 0);
    CHECK_EQ("exit code", r.exit_code, 0);
}

// ---------------------------------------------------------------
// Cross-compiled ELF test
// ---------------------------------------------------------------

// ECALL handler for ELF binaries: exit + write.
//
static int elf_ecall(rv64_state_t *state, void *user_data) {
    rv64_memory_t *mem = static_cast<rv64_memory_t *>(user_data);
    uint64_t syscall_num = state->x[17]; // a7

    switch (syscall_num) {
    case 93: // exit(code)
        return static_cast<int>(state->x[10]);

    case 64: { // write(fd, buf, len)
        uint64_t fd  = state->x[10];
        uint64_t buf = state->x[11];
        uint64_t len = state->x[12];
        if (buf + len > mem->size) {
            state->x[10] = static_cast<uint64_t>(-1LL);
            return -1;
        }
        ssize_t written = write(static_cast<int>(fd),
                                mem->data + buf, static_cast<size_t>(len));
        state->x[10] = static_cast<uint64_t>(written);
        return -1; // continue
    }

    default:
        fprintf(stderr, "elf_ecall: unhandled ecall %llu at PC=0x%llX\n",
                (unsigned long long)syscall_num,
                (unsigned long long)(state->pc - 4));
        return -1;
    }
}

static bool run_elf_test(const char *path) {
    printf("\nRunning ELF: %s\n", path);

    rv64_binary_t bin;
    if (rv64_load_elf(path, &bin) != 0) {
        fprintf(stderr, "Failed to load %s\n", path);
        return false;
    }

    rv64_state_t state = {};
    state.pc = bin.entry_point;
    state.x[2] = bin.stack_top; // sp

    rv64_memory_t mem = { bin.memory, bin.memory_size };

    int rc = rv64_interp_run(&state, &mem, elf_ecall, &mem);

    printf("ELF exited with code %d (%llu instructions)\n",
           rc, (unsigned long long)state.insn_count);

    rv64_free_binary(&bin);
    return rc == 0;
}

// ---------------------------------------------------------------
// Main
// ---------------------------------------------------------------

int main(int argc, char *argv[]) {
    printf("RV64IMD Interpreter Test Suite\n");
    printf("==============================\n\n");

    test_addi();
    test_addi_negative();
    test_add_sub();
    test_lui_addi();
    test_lui_sign_extend();
    test_shifts_64bit();
    test_mul_div_rem();
    test_div_by_zero();
    test_branch_beq();
    test_branch_bne();
    test_load_store_64();
    test_load_store_32_sign_ext();
    test_w_suffix_ops();
    test_loop();
    test_fp_add();
    test_fp_mul_div();
    test_fp_convert();
    test_fp_compare();
    test_mulh();
    test_x0_always_zero();

    printf("\n==============================\n");
    printf("Hand-assembled: %d run, %d passed, %d failed\n",
           g_tests_run, g_tests_passed, g_tests_failed);

    // Run ELF tests if provided on command line.
    //
    int elf_failures = 0;
    for (int i = 1; i < argc; i++) {
        if (!run_elf_test(argv[i])) {
            elf_failures++;
        }
    }

    if (argc > 1) {
        printf("\nELF tests: %d run, %d passed, %d failed\n",
               argc - 1, argc - 1 - elf_failures, elf_failures);
    }

    return (g_tests_failed > 0 || elf_failures > 0) ? 1 : 0;
}
