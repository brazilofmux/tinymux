# Server-Side Lua Integration — Design Document

## 1. Goals

Embed Lua 5.4 into the TinyMUX server as a first-class scripting layer
alongside softcode. Lua scripts live on objects (in attributes), execute with
the same security model as softcode, and can optionally be JIT-compiled
through the existing RV64→x86-64 pipeline.

**Non-goals for Phase 1:** client-side Lua, Lua-to-Lua module loading, custom
Lua C libraries, replacing softcode.

## 2. Architecture Overview

```
Softcode                 Lua
   |                      |
   v                      v
 lua() function      @lua command
   |                      |
   +------+-------+-------+
          |
    Lua VM (lua_State)
          |
    +-----+------+
    |            |
  Stock VM   JIT path (Phase 2)
    |            |
    |       Lua bytecode
    |            |
    |       HIR lowering
    |            |
    |       RV64 codegen
    |            |
    |       DBT x86-64
    |            |
    +-----+------+
          |
     Result string
```

## 3. Module Structure

Follow the established COM module pattern (comsys_mod, mail_mod).

### 3.1 Files

```
mux/modules/lua/
    lua_mod.h           — CID/IID constants, CLuaMod class declaration
    lua_mod.cpp         — Module implementation
    lua_sandbox.cpp     — Sandboxed Lua state factory
    Makefile.in         — Build rules, links -llua
```

The module compiles to `lua_mod.so` and loads via `module lua_mod` in the
config file.

### 3.2 COM Interfaces

**New interface — mux_ILuaControl:**

```cpp
const MUX_CID CID_LuaMod       = UINT64_C(0x00000002E1A3B5C7);
const MUX_IID IID_ILuaControl   = UINT64_C(0x00000002F2B4C6D8);

interface mux_ILuaControl : public mux_IUnknown
{
    // Execute a Lua chunk with the given source text.  Result written
    // to pResult (up to nResultMax bytes).
    //
    virtual MUX_RESULT Eval(dbref executor, dbref caller, dbref enactor,
        const UTF8 *pSource, size_t nSource,
        UTF8 *pResult, size_t nResultMax, size_t *pnResultLen) = 0;

    // Execute a named Lua function stored on an object attribute.
    //
    virtual MUX_RESULT CallAttr(dbref executor, dbref caller, dbref enactor,
        dbref obj, int attrnum,
        const UTF8 *pArgs[], int nArgs,
        UTF8 *pResult, size_t nResultMax, size_t *pnResultLen) = 0;

    // Load/reload a Lua script from an attribute.  Compiles and caches
    // the bytecode.
    //
    virtual MUX_RESULT LoadScript(dbref obj, int attrnum) = 0;

    // Resource statistics for @list lua.
    //
    virtual MUX_RESULT GetStats(size_t *pnStates, size_t *pnBytecodeBytes,
        size_t *pnMemUsed) = 0;
};
```

**Acquired interfaces** (same pattern as comsys_mod):
- `mux_ILog` — logging
- `mux_IServerEventsControl` — lifecycle hooks
- `mux_INotify` — player notification
- `mux_IObjectInfo` — object queries, permission checks, flag access
- `mux_IAttributeAccess` — read/write attributes
- `mux_IEvaluator` — call back into softcode from Lua
- `mux_IPermissions` — IsWizard, HasControl, HasCommAll

**Lifecycle** (mux_IServerEventsSink):
- `startup()` — create the global Lua state, register builtins
- `presync_database()` — flush any cached Lua bytecode to SQLite
- `shutdown()` — close all Lua states, release COM interfaces

### 3.3 mudstate Integration

```cpp
// In mudconf.h
mux_ILuaControl *pILuaControl;   // Set during module discovery
```

Set in the same block where `pIComsysControl` and `pIMailControl` are
discovered (conf.cpp / driver.cpp after LoadGame).

## 4. Where Lua Scripts Live

### 4.1 On Objects (Primary)

Lua scripts are stored as **regular attributes** on objects, exactly like
softcode. The attribute value is Lua source text. A naming convention
distinguishes Lua from softcode:

- `LUA_<name>` — Lua source attribute (e.g., `LUA_GREET`, `LUA_ONTICK`)
- Stored via `atr_add_raw_LEN()` — gets NFC normalization, SQLite
  write-through, attribute cache, all for free

No new attribute flags needed. Attribute ownership and `AF_WIZARD` /
`AF_MDARK` / `AF_LOCK` permissions apply unchanged.

### 4.2 Bytecode Cache (SQLite)

Compiled Lua bytecode is cached in a new table:

```sql
CREATE TABLE lua_cache (
    object      INTEGER NOT NULL,
    attrnum     INTEGER NOT NULL,
    source_hash TEXT NOT NULL,
    bytecode    BLOB NOT NULL,
    compile_time INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (object, attrnum)
) WITHOUT ROWID;
```

Cache invalidation: compare `source_hash` (SHA-1 of attribute value) on
load. If stale, recompile. The module owns this table via its own SQLite
connection (same pattern as comsys_mod/mail_mod — WAL mode,
busy_timeout=5000).

### 4.3 Metadata

```sql
INSERT OR REPLACE INTO metadata(key, value)
    VALUES('lua_cache_version', 1);
```

## 5. Execution Model

### 5.1 Lua State Management

**One global lua_State per server**, with per-execution sandboxed
environments. This is the standard embedding pattern:

```
Global lua_State
  |
  +-- Sandbox env table (per execution)
  |     +-- mux.notify(), mux.get(), mux.set(), ...
  |     +-- math, string, table (whitelisted stdlib)
  |     +-- NO os, io, debug, package, load, dofile
  |
  +-- Bytecode cache (precompiled chunks keyed by obj:attr)
  +-- Registry (interned strings, type metatables)
```

**Why one state:** Lua states are ~30KB each. One per object would exhaust
memory on a game with 50,000 objects. One global state with sandboxed
environments is standard practice (OpenResty, Prosody, WoW addons).

**Per-execution sandbox:** Each `lua()` or `@lua` call creates a temporary
environment table that restricts global access. The sandbox is discarded
after execution. This prevents scripts from polluting each other's state.

### 5.2 Softcode Integration — lua() Function

Register `lua()` as a builtin softcode function:

```cpp
// In builtin_function_list:
{"LUA", fun_lua, MAX_ARG, 2, MAX_ARG, FN_NOEVAL, CA_PUBLIC, nullptr}
```

**Signature:** `lua(<object>/<attr>, <arg1>, <arg2>, ...)`

**Semantics:**
1. Resolve `<object>/<attr>` using standard attribute lookup (respects
   `see_attr`, `AF_VISUAL`, ownership, `AF_WIZARD`)
2. Read attribute value — must be Lua source
3. Check bytecode cache; compile if miss
4. Create sandbox environment with `mux.args = {arg1, arg2, ...}`
5. Execute in sandbox with resource limits active
6. Return value is `tostring()` of the Lua return value
7. On error, return `#-1 LUA ERROR: <message>`

**FN_NOEVAL:** Arguments are not pre-evaluated. The Lua script receives raw
argument strings and can choose whether to evaluate them (via
`mux.eval()`). This matches the pattern used by `if()`, `switch()`, etc.

### 5.3 @lua Command

For interactive use and debugging:

```
@lua <object>/<attr>          — Execute a Lua script
@lua/inline <code>            — Execute inline Lua (wizard-only)
@lua/load <object>/<attr>     — Pre-compile and cache
@lua/stats                    — Show Lua resource usage
```

### 5.4 Lua-to-Server Bridge (mux.* API)

The `mux` table is the Lua API surface into the server. All calls go through
COM interfaces — the Lua module never touches `db[]` or engine internals
directly.

```lua
-- Object queries (via mux_IObjectInfo)
mux.name(dbref)              -- Object name
mux.owner(dbref)             -- Owner dbref
mux.location(dbref)          -- Location dbref
mux.type(dbref)              -- "PLAYER", "ROOM", "THING", "EXIT"
mux.flags(dbref)             -- Flag string
mux.isplayer(dbref)          -- Boolean
mux.isconnected(dbref)       -- Boolean
mux.pennies(dbref)           -- Integer

-- Attribute access (via mux_IAttributeAccess)
mux.get(dbref, attrname)     -- Read attribute value
mux.set(dbref, attrname, v)  -- Write attribute (permission-checked)

-- Communication (via mux_INotify)
mux.notify(dbref, message)   -- Send text to player
mux.pemit(dbref, message)    -- Alias for notify

-- Softcode bridge (via mux_IEvaluator)
mux.eval(expression)         -- Evaluate softcode, return result

-- Permission checks (via mux_IPermissions)
mux.iswizard(dbref)          -- Boolean
mux.controls(who, what)      -- Boolean

-- Execution context (injected per call)
mux.executor                 -- dbref of executor
mux.caller                   -- dbref of caller
mux.enactor                  -- dbref of enactor
mux.args                     -- {arg1, arg2, ...} table
```

**Security:** Every `mux.set()` and `mux.get()` call goes through the same
permission checks as softcode `set()` and `get()`. The executor is always
the player who triggered the Lua execution, not the object owner (unless
`FN_PRIV` semantics apply).

## 6. Security & Sandboxing

### 6.1 Lua Library Whitelist

| Library  | Status  | Rationale                                    |
|----------|---------|----------------------------------------------|
| base     | Partial | `print` remapped to `mux.notify(executor)`.  |
|          |         | `load`, `dofile`, `loadfile` removed.        |
|          |         | `require` removed.                           |
| string   | Full    | Pure computation, no side effects             |
| table    | Full    | Pure computation                              |
| math     | Full    | Pure computation                              |
| utf8     | Full    | Unicode-aware string ops                      |
| os       | None    | Filesystem/process access                     |
| io       | None    | File I/O                                      |
| debug    | None    | Sandbox escape vector                         |
| package  | None    | Module loading, filesystem access             |
| coroutine| Full    | Useful for state machines, safe in sandbox    |

### 6.2 Resource Limits

**Instruction count:** Use `lua_sethook(L, hook, LUA_MASKCOUNT, limit)` to
interrupt execution after N VM instructions. Default: 100,000 instructions
per call. Configurable:

```
lua_instruction_limit 100000
```

**Memory limit:** Custom allocator that tracks bytes. Hard cap per
execution. Default: 1MB.

```
lua_memory_limit 1048576
```

**Recursion depth:** Lua's own stack limit (`LUAI_MAXSTACK`) plus the
existing `mudconf.func_nest_lim` for `mux.eval()` callbacks.

**Output size:** Results are bounded by `LBUF_SIZE` (32KB), same as
softcode.

### 6.3 Attribute Permission Model

Lua scripts inherit the full attribute permission model:

- To **read** `LUA_FOO` on object X, you must pass `see_attr()` checks
- To **write** `LUA_FOO`, you must pass `set_attr_internal()` checks
- `AF_WIZARD` attributes can only be read/written by wizards
- `AF_LOCK` prevents modification by non-owners
- Object ownership and zone checks apply

No new permission flags needed. Wizards can set `@set obj/LUA_FOO=wizard`
to restrict a script.

## 7. Configuration

New config options (registered in conftable via cf_int/cf_bool):

```
lua_instruction_limit   100000    # Max VM instructions per lua() call
lua_memory_limit        1048576   # Max bytes per execution (1MB)
lua_cache_size          256       # Max bytecode cache entries
lua_enabled             yes       # Master enable/disable
```

**@list lua** — Shows:
- Number of cached bytecode entries
- Total bytecode bytes
- Total Lua memory in use
- Execution statistics (calls, errors, instruction limit hits)

## 8. JIT Integration (Phase 2)

The existing JIT pipeline compiles softcode: AST → HIR → RV64 → x86-64.
Lua bytecode is a well-defined, stable input format that can enter this
pipeline at the HIR level.

### 8.1 Why Not LuaJIT?

LuaJIT is Lua 5.1 only, maintained by one person, and brings its own x86-64
backend that would duplicate the existing DBT. Using stock Lua 5.4 with our
own JIT gives:

- Lua 5.4 features (integers, generational GC, utf8 library)
- Single JIT backend for both softcode and Lua
- Full control over the sandbox boundary
- No external dependency beyond liblua

### 8.2 Compilation Path

```
Lua source
    |
    v
luac_compile()          — Stock Lua compiler → bytecode
    |
    v
Lua bytecode (Proto)    — Lua's internal representation
    |
    v
lua_to_hir()            — New: walk Proto, emit HIR instructions
    |
    v
HIR program             — Same IR as softcode JIT
    |                      Reuse: SSA, const fold, copy prop, DCE, LICM
    v
hir_codegen()           — Existing: HIR → RV64
    |
    v
RV64 instructions       — Guest code in 1MB memory space
    |
    v
DBT                     — Existing: RV64 → x86-64 translation
    |
    v
Native execution
```

### 8.3 Lua Bytecode → HIR Mapping

Lua 5.4 has ~80 opcodes. The HIR already handles the fundamental operations:

| Lua opcode       | HIR instruction          | Notes                      |
|------------------|--------------------------|----------------------------|
| OP_ADD/SUB/MUL   | HIR_ADD/SUB/MUL          | Native integer arithmetic  |
| OP_ADDI          | HIR_ADD + HIR_ICONST     | Immediate add              |
| OP_EQ/LT/LE      | HIR_EQ/LT/LE            | Comparison                 |
| OP_MOVE          | HIR_COPY                 | Register move (SSA rename) |
| OP_LOADK         | HIR_ICONST/HIR_SCONST    | Constant load              |
| OP_GETTABUP      | HIR_CALL (ECALL)         | Table lookup → bridge call |
| OP_CALL          | HIR_CALL                 | Function call              |
| OP_RETURN        | (block terminator)       | Exit                       |
| OP_FORLOOP       | HIR_BR/HIR_BRC           | Loop (M2 multi-block)      |
| OP_CONCAT        | HIR_STRCAT               | String concatenation       |

Opcodes not mappable to HIR (e.g., `OP_CLOSURE`, `OP_VARARG`, metatables)
fall back to ECALL into the Lua VM. The JIT handles the hot numeric/string
paths; complex Lua features execute in the interpreter.

### 8.4 Guest Memory Layout Extension

The existing 1MB guest memory layout has room for Lua:

```
Existing layout:
  0x0000 - 0x1000    CODE_BASE (RV64 instructions)
  0x1000 - 0x4000    STR_BASE (string pool)
  0x4000 - 0x8000    FARGS_BASE
  0x8000 - 0x68000   OUT_BASE + slots
  0x68000 - 0x71000  CARGS/SUBST
  0x80000 - 0x84000  DMA descriptors

New for Lua:
  0x84000 - 0x90000  LUA_STACK (48KB — Lua register file)
  0x90000 - 0xA0000  LUA_UPVAL (64KB — upvalue storage)
  0xA0000 - 0xFFFFF  LUA_HEAP  (384KB — Lua tables, closures)
```

### 8.5 ECALL Extensions

New ECALL numbers for Lua VM operations:

```cpp
#define ECALL_LUA_GETTABLE  0x300   // Table read
#define ECALL_LUA_SETTABLE  0x301   // Table write
#define ECALL_LUA_NEWCLOSURE 0x302  // Create closure
#define ECALL_LUA_CONCAT    0x303   // String concat (multi-arg)
#define ECALL_LUA_LEN       0x304   // # operator
#define ECALL_LUA_CALL      0x310   // Generic Lua function call
```

### 8.6 Tier 2 Blob for Lua

Standard Lua library functions (string.find, string.sub, table.concat,
table.sort, math.floor, etc.) can be pre-compiled as Tier 2 RV64 blobs,
just like the existing co_* functions for softcode. This avoids ECALL
overhead for hot library calls.

### 8.7 Cache Integration

Compiled Lua programs use the same `code_cache` table, keyed by
`lua:<object>:<attrnum>:<source_hash>`. The `blob_hash` field tracks Tier 2
blob version for invalidation.

## 9. Implementation Phases

### Phase 1: Stock Lua VM (no JIT)

1. Create `mux/modules/lua/` directory structure
2. Implement CLuaMod COM class (mux_Register, mux_GetClassObject, factory)
3. Implement mux_IServerEventsSink (startup/shutdown lifecycle)
4. Create sandboxed lua_State with library whitelist
5. Implement `mux.*` bridge functions via COM interfaces
6. Implement `lua()` softcode function in functions.cpp
7. Implement `@lua` command in command.cpp
8. Add `lua_cache` SQLite table
9. Add config options (lua_instruction_limit, lua_memory_limit, etc.)
10. Add `@list lua` display
11. Write smoke tests

**Deliverable:** `lua()` works, scripts live on attributes, sandbox enforced,
no JIT. This is the validation phase — proves the API surface before
investing in JIT work.

### Phase 2: Lua Bytecode → HIR Lowering

1. Write `lua_to_hir.cpp` — walk Lua Proto, emit HIR
2. Handle integer arithmetic, comparisons, string ops natively
3. ECALL fallback for table ops, closures, metatables
4. Integrate with compile cache (lua: prefix keys)
5. Benchmark against stock VM

**Deliverable:** Hot numeric and string-heavy Lua scripts run through the
JIT. Complex scripts fall back gracefully.

### Phase 3: Tier 2 Blobs for Lua Stdlib

1. Write RV64 implementations of hot Lua stdlib functions
2. Add to `softlib.rv64` blob
3. Register intrinsics in DBT for native emission

**Deliverable:** Lua string/table/math library calls run at native speed.

### Phase 4: Persistent Lua State (Optional)

Allow objects to maintain Lua tables across executions:

- `mux.persist(key, value)` — store in object attribute
- `mux.restore(key)` — retrieve
- Serialization via MessagePack or custom format
- Storage in a dedicated `lua_state` SQLite table

This is optional and depends on user demand.

## 10. Testing Strategy

**Smoke tests** (`testcases/lua_fn.mux`):

```
# Basic execution
&tr.tc001 test_lua=@pemit %#=[lua(obj/LUA_ADD, 2, 3)]
&tr.tc001.expect test_lua=5

# Sandbox enforcement — os.execute blocked
&tr.tc002 test_lua=@pemit %#=[lua(obj/LUA_EVIL)]
&tr.tc002.expect test_lua=#-1 LUA ERROR:*

# mux.get() bridge
&tr.tc003 test_lua=@pemit %#=[lua(obj/LUA_GETNAME)]
&tr.tc003.expect test_lua=TestObject

# Instruction limit
&tr.tc004 test_lua=@pemit %#=[lua(obj/LUA_INFINITE)]
&tr.tc004.expect test_lua=#-1 LUA ERROR: instruction limit*

# Permission check — can't read wizard-locked attr
&tr.tc005 test_lua=@pemit %#=[lua(obj/LUA_READWIZ)]
&tr.tc005.expect test_lua=#-1 LUA ERROR: permission denied*
```

**Unit tests** for the HIR lowering (Phase 2) can use the existing
`compiled_program` test harness.

## 11. Open Questions

1. **`FN_NOEVAL` vs `FN_EVAL` for lua():** NOEVAL gives scripts control over
   argument evaluation. But most Lua scripts probably want pre-evaluated
   strings. Should there be both `lua()` (eval) and `luaraw()` (noeval)?

2. **Coroutine persistence:** Should coroutines survive across calls? This
   would require per-object Lua threads, significantly increasing memory.
   Defer to Phase 4 at earliest.

3. **Error propagation:** Should Lua errors halt the containing softcode
   expression (like `#-1`), or should there be a `luacheck()` function that
   tests for errors?

4. **@trigger integration:** Should `@trigger obj/LUA_FOO` work? This would
   require the trigger system to detect the `LUA_` prefix and route through
   the Lua module instead of mux_exec().

5. **IDLE event hooks:** Should Lua scripts be triggerable on a timer (like
   `@daily`)? This would need a `LUA_DAILY` or `LUA_IDLE` attribute
   convention plus integration with the timer system.
