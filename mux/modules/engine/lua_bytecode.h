/*! \file lua_bytecode.h
 * \brief Lua 5.4 bytecode structures — standalone deserializer.
 *
 * Our own Lua 5.4 bytecode structures.  No Lua headers needed.
 * engine.so reads the output of lua_dump() from lua_mod.so and
 * lowers it through the HIR/RV64/x86-64 pipeline.
 */

#ifndef LUA_BYTECODE_H
#define LUA_BYTECODE_H

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

// ---------------------------------------------------------------
// Lua 5.4 opcodes (all 83)
// ---------------------------------------------------------------

enum lua_bc_opcode {
    OP_LUA_MOVE,        //  0
    OP_LUA_LOADI,       //  1
    OP_LUA_LOADF,       //  2
    OP_LUA_LOADK,       //  3
    OP_LUA_LOADKX,      //  4
    OP_LUA_LOADFALSE,   //  5
    OP_LUA_LFALSESKIP,  //  6
    OP_LUA_LOADTRUE,    //  7
    OP_LUA_LOADNIL,     //  8
    OP_LUA_GETUPVAL,    //  9
    OP_LUA_SETUPVAL,    // 10
    OP_LUA_GETTABUP,    // 11
    OP_LUA_GETTABLE,    // 12
    OP_LUA_GETTABI,     // 13
    OP_LUA_GETFIELD,    // 14
    OP_LUA_SETTABUP,    // 15
    OP_LUA_SETTABLE,    // 16
    OP_LUA_SETTABI,     // 17
    OP_LUA_SETFIELD,    // 18
    OP_LUA_NEWTABLE,    // 19
    OP_LUA_SELF,        // 20
    OP_LUA_ADDI,        // 21
    OP_LUA_ADDK,        // 22
    OP_LUA_SUBK,        // 23
    OP_LUA_MULK,        // 24
    OP_LUA_MODK,        // 25
    OP_LUA_POWK,        // 26
    OP_LUA_DIVK,        // 27
    OP_LUA_IDIVK,       // 28
    OP_LUA_BANDK,       // 29
    OP_LUA_BORK,        // 30
    OP_LUA_BXORK,       // 31
    OP_LUA_SHRI,        // 32
    OP_LUA_SHLI,        // 33
    OP_LUA_ADD,         // 34
    OP_LUA_SUB,         // 35
    OP_LUA_MUL,         // 36
    OP_LUA_MOD,         // 37
    OP_LUA_POW,         // 38
    OP_LUA_DIV,         // 39
    OP_LUA_IDIV,        // 40
    OP_LUA_BAND,        // 41
    OP_LUA_BOR,         // 42
    OP_LUA_BXOR,        // 43
    OP_LUA_SHL,         // 44
    OP_LUA_SHR,         // 45
    OP_LUA_MMBIN,       // 46
    OP_LUA_MMBINI,      // 47
    OP_LUA_MMBINK,      // 48
    OP_LUA_UNM,         // 49
    OP_LUA_BNOT,        // 50
    OP_LUA_NOT,         // 51
    OP_LUA_LEN,         // 52
    OP_LUA_CONCAT,      // 53
    OP_LUA_CLOSE,       // 54
    OP_LUA_TBC,         // 55
    OP_LUA_JMP,         // 56
    OP_LUA_EQ,          // 57
    OP_LUA_LT,          // 58
    OP_LUA_LE,          // 59
    OP_LUA_EQK,         // 60
    OP_LUA_EQI,         // 61
    OP_LUA_LTI,         // 62
    OP_LUA_LEI,         // 63
    OP_LUA_GTI,         // 64
    OP_LUA_GEI,         // 65
    OP_LUA_TEST,        // 66
    OP_LUA_TESTSET,     // 67
    OP_LUA_CALL,        // 68
    OP_LUA_TAILCALL,    // 69
    OP_LUA_RETURN,      // 70
    OP_LUA_RETURN0,     // 71
    OP_LUA_RETURN1,     // 72
    OP_LUA_FORLOOP,     // 73
    OP_LUA_FORPREP,     // 74
    OP_LUA_TFORPREP,    // 75
    OP_LUA_TFORCALL,    // 76
    OP_LUA_TFORLOOP,    // 77
    OP_LUA_SETLIST,     // 78
    OP_LUA_CLOSURE,     // 79
    OP_LUA_VARARG,      // 80
    OP_LUA_VARARGPREP,  // 81
    OP_LUA_EXTRAARG,    // 82
    OP_LUA_NUM_OPCODES
};

// ---------------------------------------------------------------
// Instruction field accessors
// ---------------------------------------------------------------

// Lua 5.4 instruction format: 32-bit word
//   iABC:  C:8 | B:8 | k:1 | A:8 | Op:7
//   iABx:  Bx:17               | A:8 | Op:7  (unsigned)
//   iAsBx: sBx:17              | A:8 | Op:7  (signed = Bx - offset)
//   iAx:   Ax:25                     | Op:7
//   isJ:   sJ:25                     | Op:7

struct lua_bc_instruction {
    uint32_t raw;

    int opcode() const { return raw & 0x7F; }
    int A()      const { return (raw >> 7) & 0xFF; }
    int k()      const { return (raw >> 15) & 0x1; }
    int B()      const { return (raw >> 16) & 0xFF; }
    int sB()     const { return B() - 128; }       // signed B field
    int C()      const { return (raw >> 24) & 0xFF; }
    int sC()     const { return C() - 128; }       // signed C field
    int Bx()     const { return (raw >> 15) & 0x1FFFF; }  // bits 15-31
    int sBx()    const { return Bx() - 65535; }     // offset = (2^17 - 1) / 2
    int Ax()     const { return (raw >> 7) & 0x1FFFFFF; }
    int sJ()     const { return static_cast<int>((raw >> 7) & 0x1FFFFFF) - (1 << 24); }
};

// ---------------------------------------------------------------
// Constant types
// ---------------------------------------------------------------

enum lua_bc_const_type {
    LUA_BC_TNIL     = 0,
    LUA_BC_TFALSE   = 1,
    LUA_BC_TTRUE    = 17,
    LUA_BC_TINT     = 3,
    LUA_BC_TFLOAT   = 19,
    LUA_BC_TSHRSTR  = 4,
    LUA_BC_TLNGSTR  = 20,
};

struct lua_bc_constant {
    lua_bc_const_type type;
    int64_t  ival;
    double   fval;
    std::string sval;
};

// ---------------------------------------------------------------
// Upvalue descriptor
// ---------------------------------------------------------------

struct lua_bc_upvalue {
    uint8_t instack;
    uint8_t idx;
    uint8_t kind;
};

// ---------------------------------------------------------------
// Function prototype
// ---------------------------------------------------------------

struct lua_bc_proto {
    // Source info.
    std::string source;
    int linedefined;
    int lastlinedefined;
    uint8_t numparams;
    uint8_t is_vararg;
    uint8_t maxstacksize;

    // Code.
    std::vector<lua_bc_instruction> code;

    // Constants.
    std::vector<lua_bc_constant> constants;

    // Upvalues.
    std::vector<lua_bc_upvalue> upvalues;

    // Nested protos.
    std::vector<lua_bc_proto> protos;

    // Debug info (line numbers, local names) — we skip these.
};

// ---------------------------------------------------------------
// Top-level chunk
// ---------------------------------------------------------------

struct lua_bc_chunk {
    // Header fields.
    uint8_t version;
    uint8_t format;
    uint8_t int_size;
    uint8_t size_t_size;
    uint8_t insn_size;
    uint8_t lua_int_size;
    uint8_t lua_num_size;

    // Main function prototype.
    lua_bc_proto main;

    // Number of upvalues in main proto (from header).
    uint8_t num_upvalues;
};

// ---------------------------------------------------------------
// Deserializer
// ---------------------------------------------------------------

// Load a Lua 5.4 bytecode dump into `out`.
// Returns true on success, false on malformed input.
//
bool lua_bc_load(const uint8_t *data, size_t len, lua_bc_chunk *out);

#endif // LUA_BYTECODE_H
