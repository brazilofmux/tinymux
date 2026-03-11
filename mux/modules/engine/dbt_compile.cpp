/*! \file dbt_compile.cpp
 * \brief Compiler spike: softcode expression → RV64 via AST.
 *
 * Takes a MUX expression string, parses it via the existing AST
 * parser, and compiles it to RV64 instructions that call engine
 * functions through the ECALL convention.
 *
 * Supported AST node types (spike scope):
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
 *   1. Leaf nodes (literals) → write string to pool, return pool addr
 *   2. Function calls → compile each arg, build fargs array, emit ECALL
 *   3. Sequences → compile each child, concatenate results
 *   4. Each compiled sub-expression gets an output slot (guest addr)
 *
 * The final result is in the last output slot allocated.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"
#include "ast.h"

#include "dbt.h"
#include "dbt_decoder.h"

#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

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

    static constexpr size_t MEM_SIZE     = 64 * 1024;
    static constexpr uint64_t CODE_BASE  = 0x0000;
    static constexpr uint64_t CODE_LIMIT = 0x1000;
    static constexpr uint64_t STR_BASE   = 0x1000;
    static constexpr uint64_t STR_LIMIT  = 0x4000;
    static constexpr uint64_t FARGS_BASE = 0x4000;
    static constexpr uint64_t FARGS_LIMIT= 0x8000;
    static constexpr uint64_t OUT_BASE   = 0x8000;
    static constexpr uint64_t OUT_LIMIT  = 0xF000;
    static constexpr int      OUT_SLOT   = 256;
    static constexpr uint64_t STACK_TOP  = MEM_SIZE - 16;

    rv_compiler() : memory(MEM_SIZE, 0),
                    str_pool(STR_BASE),
                    fargs_pool(FARGS_BASE),
                    out_pool(OUT_BASE),
                    final_out(0) {}

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
//   name_addr: guest pointer to function name string
//   fargs_addr: guest pointer to fargs[] array
//   nfargs: number of arguments
//   out_addr: guest pointer to output buffer
//   out_size: output buffer size
//
static void rv_emit_call(std::vector<uint32_t> &code,
                          uint64_t name_addr, uint64_t fargs_addr,
                          int nfargs, uint64_t out_addr, int out_size) {
    code.push_back(rv_ADDI(17, 0, 0x100));   // a7 = ECALL_CALL_FUNC
    rv_load_val(code, 10, name_addr);          // a0 = name
    rv_load_val(code, 11, fargs_addr);         // a1 = fargs
    code.push_back(rv_ADDI(12, 0, nfargs));   // a2 = nfargs
    rv_load_val(code, 13, out_addr);           // a3 = output
    rv_load_val(code, 14, out_size);           // a4 = outsize
    code.push_back(rv_ECALL());
}

static void rv_emit_exit(std::vector<uint32_t> &code) {
    code.push_back(rv_ADDI(17, 0, 93));
    code.push_back(rv_ADDI(10, 0, 0));
    code.push_back(rv_ECALL());
}

// ---------------------------------------------------------------
// AST compilation (recursive, bottom-up)
//
// Returns the guest address where the result string lives.
// For literals, it's the string pool address.
// For function calls, it's the output buffer address.
// ---------------------------------------------------------------

// Forward declaration.
static uint64_t compile_node(rv_compiler &rc, const ASTNode *node);

// Compile a sequence: concatenate child results into one output slot.
//
static uint64_t compile_sequence(rv_compiler &rc, const ASTNode *node) {
    if (node->children.size() == 1) {
        return compile_node(rc, node->children[0].get());
    }

    // Compile each child, then concatenate via strcat ECALL.
    // For the spike, we concatenate by writing all literals
    // directly to one string pool entry when possible.

    // Check if all children are literals/spaces — can merge.
    bool all_literal = true;
    for (auto &child : node->children) {
        if (child->type != AST_LITERAL && child->type != AST_SPACE) {
            all_literal = false;
            break;
        }
    }

    if (all_literal) {
        std::string merged;
        for (auto &child : node->children) {
            merged += child->text;
        }
        return rc.pool_str(merged);
    }

    // Mixed sequence: compile each child, use strcat() to concatenate.
    // strcat(a, b, c, ...) joins without separators.
    std::vector<uint64_t> child_addrs;
    for (auto &child : node->children) {
        child_addrs.push_back(compile_node(rc, child.get()));
    }

    uint64_t name_addr = rc.pool_str("strcat");
    uint64_t fargs_addr = rc.alloc_fargs(child_addrs);
    uint64_t out_addr = rc.alloc_output();

    rv_emit_call(rc.code, name_addr, fargs_addr,
                 static_cast<int>(child_addrs.size()),
                 out_addr, rv_compiler::OUT_SLOT);

    return out_addr;
}

// Compile a function call: compile args, emit ECALL.
//
static uint64_t compile_funccall(rv_compiler &rc, const ASTNode *node) {
    // node->text is the function name.
    // node->children are the argument expressions.

    // Compile each argument.
    std::vector<uint64_t> arg_addrs;
    for (auto &child : node->children) {
        arg_addrs.push_back(compile_node(rc, child.get()));
    }

    uint64_t name_addr = rc.pool_str(node->text);
    uint64_t fargs_addr = rc.alloc_fargs(arg_addrs);
    uint64_t out_addr = rc.alloc_output();

    rv_emit_call(rc.code, name_addr, fargs_addr,
                 static_cast<int>(arg_addrs.size()),
                 out_addr, rv_compiler::OUT_SLOT);

    return out_addr;
}

static uint64_t compile_node(rv_compiler &rc, const ASTNode *node) {
    switch (node->type) {
    case AST_LITERAL:
    case AST_SPACE:
        return rc.pool_str(node->text);

    case AST_SEQUENCE:
        return compile_sequence(rc, node);

    case AST_FUNCCALL:
        return compile_funccall(rc, node);

    case AST_EVALBRACKET:
        // [expr] — compile the inner sequence.
        if (node->children.size() == 1) {
            return compile_node(rc, node->children[0].get());
        }
        // Shouldn't happen, but fall through.
        return rc.pool_str("");

    default:
        // Unsupported node type — return empty string.
        return rc.pool_str("");
    }
}

// ---------------------------------------------------------------
// Public: compile an expression string to a runnable RV64 program.
// ---------------------------------------------------------------

struct compiled_program {
    std::vector<uint8_t> memory;
    size_t memory_size;
    uint64_t out_addr;      // where the final result lives
    bool ok;
};

static compiled_program compile_expression(const UTF8 *expr, size_t nLen) {
    compiled_program prog;
    prog.ok = false;

    // Parse the expression.
    auto ast = ast_parse_string(expr, nLen);
    if (!ast) {
        return prog;
    }

    // Compile.
    rv_compiler rc;
    uint64_t result_addr = compile_node(rc, ast.get());

    // Emit exit.
    rv_emit_exit(rc.code);

    // Copy code to guest memory.
    for (size_t i = 0; i < rc.code.size() && i * 4 < rv_compiler::CODE_LIMIT; i++) {
        memcpy(rc.memory.data() + i * 4, &rc.code[i], 4);
    }

    prog.memory = std::move(rc.memory);
    prog.memory_size = rv_compiler::MEM_SIZE;
    prog.out_addr = result_addr;
    prog.ok = true;
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
//   think rveval(add(1,2))           → 3
//   think rveval(add(mul(3,4),5))    → 17
//   think rveval(strlen(hello))      → 5
// ---------------------------------------------------------------

// ECALL handler (same as dbt_harness.cpp).
//
static constexpr uint64_t ECALL_CALL_FUNC = 0x100;

struct eval_ctx {
    uint8_t *memory;
    size_t   memory_size;
    dbref    executor;
    dbref    caller;
    dbref    enactor;
};

static int eval_ecall(rv64_ctx_t *ctx, void *user_data) {
    eval_ctx *ec = static_cast<eval_ctx *>(user_data);
    uint64_t syscall_num = ctx->x[17];

    switch (syscall_num) {
    case 93:
        return static_cast<int>(ctx->x[10]);

    case ECALL_CALL_FUNC: {
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

        FUN *fp = it->second;

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

    // Compile the expression.
    const UTF8 *expr = fargs[0];
    size_t nLen = strlen(reinterpret_cast<const char *>(expr));

    compiled_program prog = compile_expression(expr, nLen);
    if (!prog.ok) {
        safe_str(T("#-1 COMPILATION FAILED"), buff, bufc);
        return;
    }

    // Run through the JIT.
    eval_ctx ec;
    ec.memory = prog.memory.data();
    ec.memory_size = prog.memory_size;
    ec.executor = executor;
    ec.caller = caller;
    ec.enactor = enactor;

    dbt_state_t dbt;
    if (dbt_init(&dbt, prog.memory.data(), prog.memory_size,
                 eval_ecall, &ec) != 0) {
        safe_str(T("#-1 DBT INIT FAILED"), buff, bufc);
        return;
    }

    int rc = dbt_run(&dbt, 0, rv_compiler::STACK_TOP);
    dbt_cleanup(&dbt);

    if (rc != 0) {
        safe_str(T("#-1 DBT EXECUTION ERROR"), buff, bufc);
        return;
    }

    // Copy result from guest output buffer.
    const UTF8 *result = prog.memory.data() + prog.out_addr;
    safe_str(result, buff, bufc);
}
