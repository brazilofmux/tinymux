/*! \file hir_lower_lua.h
 * \brief Lua bytecode → HIR lowering.
 */

#ifndef HIR_LOWER_LUA_H
#define HIR_LOWER_LUA_H

struct hir_program;
struct rv_compiler;
struct lua_bc_proto;

// Rejection reasons for lua_bc_eligible().
//
enum lua_bc_reject {
    LUA_BC_ELIGIBLE     = 0,   // Proto is eligible for JIT
    LUA_BC_EMPTY,              // No code
    LUA_BC_TOO_LARGE,          // Code or stack exceeds limits
    LUA_BC_TOO_MANY_PARAMS,    // numparams > 8
    LUA_BC_HAS_CLOSURE,        // Contains OP_CLOSURE
    LUA_BC_HAS_VARARG,         // Contains OP_VARARG (actual vararg access)
    LUA_BC_HAS_TBC,            // Contains OP_TBC (to-be-closed)
    LUA_BC_HAS_TAILCALL,       // Contains OP_TAILCALL
    LUA_BC_HAS_NESTED_PROTOS,  // Contains nested function definitions
    LUA_BC_UNSUPPORTED_OP,     // Contains an opcode not in the supported set
};

// Return a human-readable name for a rejection reason.
//
const char *lua_bc_reject_name(lua_bc_reject reason);

// Fast eligibility check.  Scans the proto's header fields and
// instruction stream without allocating memory or building HIR.
// Returns LUA_BC_ELIGIBLE if the proto can be JIT-compiled, or
// a specific rejection reason otherwise.
//
lua_bc_reject lua_bc_eligible(const lua_bc_proto *proto);

// Lower a Lua 5.4 function prototype to HIR instructions.
// Returns the HIR instruction index of the result, or -1 on failure
// (unsupported opcode → caller falls back to Lua VM).
//
// Caller should call lua_bc_eligible() first for a fast pre-check.
//
int hir_lower_lua_proto(hir_program &h, rv_compiler &rc,
                        const lua_bc_proto *proto);

#endif // HIR_LOWER_LUA_H
