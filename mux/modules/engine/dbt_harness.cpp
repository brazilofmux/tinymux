/*! \file dbt_harness.cpp
 * \brief Test harness: rvcall() softcode function.
 *
 * Temporary scaffolding.  Provides rvcall(<func>, <arg1>, ..., <argN>)
 * which calls <func> by routing through the RISC-V DBT pipeline:
 *
 *   1. Writes function name and args to guest memory
 *   2. Assembles RV64 code: set up ECALL registers, ECALL, exit
 *   3. Runs through the JIT (or interpreter)
 *   4. ECALL handler looks up <func> in mudstate.builtin_functions,
 *      allocates a real LBUF, calls the real function, copies result
 *      to guest memory
 *   5. Returns the result to softcode
 *
 * This proves the full integration: softcode → RV64 → JIT → ECALL
 * → real engine function → result back to softcode.
 *
 * Usage from softcode:
 *   think rvcall(add, 1, 2)       → 3
 *   think rvcall(mul, 6, 7)       → 42
 *   think rvcall(sub, 100, 58)    → 42
 *   think rvcall(strlen, hello)   → 5
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "dbt.h"
#include "dbt_decoder.h"

// ---------------------------------------------------------------
// ECALL handler for the harness.
//
// Uses the real mudstate.builtin_functions and real LBUFs.
// ---------------------------------------------------------------

static constexpr uint64_t ECALL_CALL_FUNC = 0x100;

struct harness_ctx {
    uint8_t *memory;
    size_t   memory_size;
    dbref    executor;
    dbref    caller;
    dbref    enactor;
};

static int harness_ecall(rv64_ctx_t *ctx, void *user_data) {
    harness_ctx *hc = static_cast<harness_ctx *>(user_data);
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

        // Bounds checks.
        if (name_addr >= hc->memory_size ||
            out_addr + out_size > hc->memory_size) {
            ctx->x[10] = 0;
            return -1;
        }

        // Look up function by name.
        const UTF8 *func_name = hc->memory + name_addr;
        size_t nCased;
        UTF8 *pCased = mux_strupr(func_name, nCased);
        std::vector<UTF8> key(pCased, pCased + nCased);
        auto it = mudstate.builtin_functions.find(key);
        if (it == mudstate.builtin_functions.end()) {
            // Function not found — write error to output.
            const char *err = "#-1 FUNCTION NOT FOUND";
            size_t elen = strlen(err);
            if (elen >= out_size) elen = out_size - 1;
            memcpy(hc->memory + out_addr, err, elen);
            hc->memory[out_addr + elen] = '\0';
            ctx->x[10] = static_cast<uint64_t>(elen);
            return -1;
        }

        FUN *fp = it->second;

        // Build host fargs[].
        UTF8 *fargs[MAX_ARG];
        if (nfargs > MAX_ARG) nfargs = MAX_ARG;
        for (int i = 0; i < nfargs; i++) {
            uint64_t ptr;
            memcpy(&ptr, hc->memory + fargs_addr + i * 8, 8);
            if (ptr >= hc->memory_size) {
                ctx->x[10] = 0;
                return -1;
            }
            fargs[i] = hc->memory + ptr;
        }

        // Allocate real LBUF for output.
        UTF8 *buff = alloc_lbuf("harness_ecall");
        UTF8 *bufc = buff;

        // Call the real engine function.
        fp->fun(fp, buff, &bufc, hc->executor, hc->caller, hc->enactor,
                0, fargs, nfargs, nullptr, 0);

        // Copy result to guest memory.
        *bufc = '\0';
        size_t result_len = static_cast<size_t>(bufc - buff);
        if (result_len >= out_size) result_len = out_size - 1;
        memcpy(hc->memory + out_addr, buff, result_len);
        hc->memory[out_addr + result_len] = '\0';

        free_lbuf(buff);

        ctx->x[10] = static_cast<uint64_t>(result_len);
        return -1; // continue
    }

    default:
        ctx->x[10] = 0;
        return -1;
    }
}

// ---------------------------------------------------------------
// RV64 code generation for a single function call + exit.
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

// Load a 32-bit address into a register using LUI + ADDI.
//
static void rv_load_addr(std::vector<uint32_t> &code, uint8_t rd,
                          uint64_t addr) {
    uint32_t hi = addr & 0xFFFFF000;
    int32_t lo = static_cast<int32_t>(addr & 0xFFF);
    // If lo bit 11 is set, LUI needs adjustment (sign extension).
    if (lo & 0x800) {
        hi += 0x1000;
        lo = lo - 0x1000; // sign-extend
    }
    code.push_back(rv_LUI(rd, hi));
    if (lo) code.push_back(rv_ADDI(rd, rd, lo));
}

// ---------------------------------------------------------------
// fun_rvcall: the softcode function.
//
// rvcall(<func>, <arg1>, ..., <argN>)
//
// Calls <func> by routing through the RISC-V DBT pipeline.
// ---------------------------------------------------------------

FUNCTION(fun_rvcall)
{
    UNUSED_PARAMETER(fp);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (nfargs < 1) {
        safe_str(T("#-1 TOO FEW ARGUMENTS"), buff, bufc);
        return;
    }

    // Guest memory layout:
    //   0x0000..0x03FF  code (256 instructions max)
    //   0x1000+         data pool (strings, fargs array, output)
    //
    const size_t MEM_SIZE = 64 * 1024;
    std::vector<uint8_t> memory(MEM_SIZE, 0);

    uint64_t pool = 0x1000;

    // Write function name to guest memory.
    auto write_str = [&](const char *s) -> uint64_t {
        uint64_t addr = pool;
        size_t len = strlen(s) + 1;
        if (addr + len >= MEM_SIZE) return 0;
        memcpy(memory.data() + addr, s, len);
        pool += (len + 7) & ~7ULL;
        return addr;
    };

    uint64_t name_addr = write_str(
        reinterpret_cast<const char *>(fargs[0]));

    // Write each argument string.
    int inner_nfargs = nfargs - 1;
    if (inner_nfargs > MAX_ARG) inner_nfargs = MAX_ARG;

    uint64_t arg_addrs[MAX_ARG];
    for (int i = 0; i < inner_nfargs; i++) {
        arg_addrs[i] = write_str(
            reinterpret_cast<const char *>(fargs[i + 1]));
    }

    // Write fargs[] pointer array.
    uint64_t fargs_addr = (pool + 7) & ~7ULL;
    for (int i = 0; i < inner_nfargs; i++) {
        memcpy(memory.data() + fargs_addr + i * 8, &arg_addrs[i], 8);
    }
    pool = fargs_addr + inner_nfargs * 8 + 8;

    // Output buffer.
    uint64_t out_addr = (pool + 7) & ~7ULL;
    int out_size = LBUF_SIZE;
    if (out_addr + out_size > MEM_SIZE) {
        out_size = static_cast<int>(MEM_SIZE - out_addr);
    }

    // Assemble RV64 code.
    std::vector<uint32_t> code;

    // a7 = 0x100 (ECALL_CALL_FUNC)
    code.push_back(rv_ADDI(17, 0, 0x100));

    // a0 = name_addr
    rv_load_addr(code, 10, name_addr);

    // a1 = fargs_addr
    rv_load_addr(code, 11, fargs_addr);

    // a2 = nfargs
    code.push_back(rv_ADDI(12, 0, inner_nfargs));

    // a3 = out_addr
    rv_load_addr(code, 13, out_addr);

    // a4 = out_size
    // out_size might be > 2047, use LUI+ADDI
    if (out_size <= 2047) {
        code.push_back(rv_ADDI(14, 0, out_size));
    } else {
        rv_load_addr(code, 14, out_size);
    }

    // ECALL
    code.push_back(rv_ECALL());

    // Exit: a7=93, a0=0
    code.push_back(rv_ADDI(17, 0, 93));
    code.push_back(rv_ADDI(10, 0, 0));
    code.push_back(rv_ECALL());

    // Copy code to guest memory.
    for (size_t i = 0; i < code.size() && i * 4 < 0x1000; i++) {
        memcpy(memory.data() + i * 4, &code[i], 4);
    }

    // Set up context and run.
    harness_ctx hc;
    hc.memory = memory.data();
    hc.memory_size = MEM_SIZE;
    hc.executor = executor;
    hc.caller = caller;
    hc.enactor = enactor;

    dbt_state_t dbt;
    if (dbt_init(&dbt, memory.data(), MEM_SIZE, harness_ecall, &hc) != 0) {
        safe_str(T("#-1 DBT INIT FAILED"), buff, bufc);
        return;
    }

    int rc = dbt_run(&dbt, 0, MEM_SIZE - 16);
    dbt_cleanup(&dbt);

    if (rc != 0) {
        safe_str(T("#-1 DBT EXECUTION ERROR"), buff, bufc);
        return;
    }

    // Copy result from guest output buffer to softcode output.
    const UTF8 *result = memory.data() + out_addr;
    safe_str(result, buff, bufc);
}
