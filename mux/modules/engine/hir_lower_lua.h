/*! \file hir_lower_lua.h
 * \brief Lua bytecode → HIR lowering.
 */

#ifndef HIR_LOWER_LUA_H
#define HIR_LOWER_LUA_H

struct hir_program;
struct rv_compiler;
struct lua_bc_proto;

// Lower a Lua 5.4 function prototype to HIR instructions.
// Returns the HIR instruction index of the result, or -1 on failure
// (unsupported opcode → caller falls back to Lua VM).
//
int hir_lower_lua_proto(hir_program &h, rv_compiler &rc,
                        const lua_bc_proto *proto);

#endif // HIR_LOWER_LUA_H
