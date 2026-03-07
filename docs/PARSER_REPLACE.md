# Replacing mux_exec with an AST Evaluator

## Status: Phase 2 Complete — Shadow Validation Active

Branch: `brazil`

## Design Constraints

Two language restrictions that make static compilation feasible:

1. **Deprecate `##`, `#@`, `#$`** — Replace with `%i0`-`%i9`, `inum()`,
   and register-based patterns. `##` is a known security risk (code
   injection via unescaped values). `safer_iter` config already disables
   `##`/`#@` in `iter()`. This change makes it universal and permanent.

2. **No dynamic function calls** — `%q0(args)` where the function name
   is computed at runtime is not supported. This is extremely rare in
   practice and can be replaced with `switch()` or `u()` patterns.
   Tools can scan a game database to identify and convert these sites.

With these two constraints, every function call is statically known at
parse time, and every substitution is a typed AST node. No text-level
replacement-and-reparse is needed.

**Practical compromise:** The current implementation still supports
`##`/`#@`/`#$` via `replace_tokens()` for backward compatibility.
Native NOEVAL handlers use `ast_has_hash_tokens()` to detect these
and fall back to the serialize-replace-reparse path only when needed.
When no hash tokens are present (the common case with `%i0`), the
subtree is evaluated directly without serialization.

## Architecture

### Current: Stream Transformer

```
source text --> mux_exec() --> output buffer
                 ^ recursive calls for [...], function args, FN_NOEVAL bodies
```

`mux_exec` interleaves scanning, evaluation, and output in a single pass.
104 call sites across 19 source files. The function signature:

```cpp
void mux_exec(const UTF8 *pStr, size_t nStr,
              UTF8 *buff, UTF8 **bufc,
              dbref executor, dbref caller, dbref enactor,
              int eval,
              const UTF8 *cargs[], int ncargs);
```

### New: Parse --> AST --> Evaluate

```
source text --> parse() --> ASTNode tree --> eval() --> output string
                                 |
                            LRU cache (1024 entries)
```

Two phases, cleanly separated:

1. **Parse** — Tokenize and build AST. No evaluation, no database access,
   no side effects. Cached by expression text in an LRU cache.

2. **Evaluate** — Walk the AST with an evaluation context. Function
   dispatch, substitution resolution, database access all happen here.

### Function signatures (implemented)

```cpp
// Tokenize: text --> token stream
std::vector<ASTToken> ast_tokenize(const UTF8 *input, size_t nLen);

// Parse: token stream --> AST
std::unique_ptr<ASTNode> ast_parse(const std::vector<ASTToken> &tokens);

// Combined parse: text --> AST (convenience)
std::unique_ptr<ASTNode> ast_parse_string(const UTF8 *input, size_t nLen);

// Reconstruct raw text from AST (for NOEVAL serialization)
std::string ast_raw_text(const ASTNode *node);

// Drop-in replacement for mux_exec (parse + cache + evaluate)
void mux_exec2(const UTF8 *pStr, size_t nStr,
               UTF8 *buff, UTF8 **bufc,
               dbref executor, dbref caller, dbref enactor,
               int eval, const UTF8 *cargs[], int ncargs);
```

## AST Node Types (implemented)

```cpp
enum ASTNodeType {
    AST_SEQUENCE,       // Ordered list of children
    AST_LITERAL,        // Plain text
    AST_SPACE,          // Whitespace run
    AST_SUBST,          // %-substitution (%0, %q<name>, %i0, %r, etc.)
    AST_ESCAPE,         // \-escape
    AST_FUNCCALL,       // function(args...) -- name is static string
    AST_EVALBRACKET,    // [expression]
    AST_BRACEGROUP,     // {deferred expression}
    AST_SEMICOLON,      // ; command separator
};
```

No NODE_DYNCALL — eliminated by design constraint.

```cpp
struct ASTNode {
    ASTNodeType type;
    std::string text;
    std::vector<std::unique_ptr<ASTNode>> children;
};
```

## Token Types (implemented)

```cpp
enum ASTTokenType {
    ASTTOK_LIT,       // Literal text run
    ASTTOK_FUNC,      // Identifier preceding '(' -- promoted from ASTTOK_LIT
    ASTTOK_LPAREN,    // (
    ASTTOK_RPAREN,    // )
    ASTTOK_LBRACK,    // [
    ASTTOK_RBRACK,    // ]
    ASTTOK_LBRACE,    // {
    ASTTOK_RBRACE,    // }
    ASTTOK_COMMA,     // ,
    ASTTOK_SEMI,      // ;
    ASTTOK_PCT,       // %-substitution sequence (gathered by gather_pct)
    ASTTOK_ESC,       // \-escape
    ASTTOK_SPACE,     // Whitespace run
    ASTTOK_EOF        // End of input
};
```

## FN_NOEVAL Handling (implemented)

23 functions use FN_NOEVAL. The AST evaluator handles them in three tiers:

### 1. Native handlers (pure AST, no serialization)

These evaluate AST subtrees directly without converting back to text:

- `cand(expr1, expr2, ...)` / `candbool(...)` — short-circuit AND
- `cor(expr1, expr2, ...)` / `corbool(...)` — short-circuit OR
- `if(cond, true, false)` / `ifelse(cond, true, false)` — conditional
- `switch(val, pat1, res1, ..., default)` — wildcard first-match
- `case(val, pat1, res1, ..., default)` — exact first-match
- `switchall(val, pat1, res1, ..., default)` — wildcard all-match
- `caseall(val, pat1, res1, ..., default)` — exact all-match
- `iter(list, body, isep, osep)` — list iteration

The `ast_eval_branch()` helper checks `ast_has_hash_tokens()` to
decide between direct subtree evaluation (fast path) and the
serialize-replace-reparse fallback (slow path for `##`/`#@`/`#$`).

### 2. Generic NOEVAL (serialize to raw text)

Functions not in the native handler list receive raw text via
`ast_raw_text()`. The existing FUN handler calls `mux_exec`
internally on the args it chooses to evaluate:

- `@@(comment)`, `lit(text)`, `parenmatch(text)`, `fcount()`
- `objeval()`, `filter()`, `step()`, `list()`, `parse()`
- `while()`, `until()`, `fold()`, `munge()`, `sortby()`

### 3. User-defined functions (UFUN)

Evaluated arguments are passed to the fetched attribute text,
which is evaluated via `mux_exec`. The `FN_PRES` flag triggers
register save/restore around the call.

## Eval Flags (implemented)

The `eval` parameter changes evaluation behavior. A single cached AST
works for all eval flag combinations — the evaluator respects flags
at walk time:

| Flag | Evaluator Behavior |
|------|-------------------|
| EV_EVAL | Resolve AST_SUBST nodes (else pass through literally) |
| EV_FCHECK | Dispatch AST_FUNCCALL nodes (else output as literal) |
| EV_FMAND | Error on unknown function name |
| EV_STRIP_CURLY | Unwrap AST_BRACEGROUP (else preserve braces) |
| EV_NOFCHECK | Don't recurse into AST_EVALBRACKET (pass through) |
| EV_NO_LOCATION | Suppress %l resolution |

## AST Parse Cache (implemented)

```cpp
struct ASTCacheEntry {
    std::shared_ptr<ASTNode> ast;
    std::list<std::string>::iterator lru_it;
};

static std::unordered_map<std::string, ASTCacheEntry> s_astCache;
static std::list<std::string> s_astLru;
static const size_t AST_CACHE_MAX = 1024;
static const size_t AST_CACHE_MIN_LEN = 16;
```

- LRU eviction: oldest entries dropped when cache reaches 1024
- Minimum length: expressions shorter than 16 bytes parsed without caching
- Key: raw expression text (no hashing — std::string handles it)
- `shared_ptr<ASTNode>` enables safe sharing from cache

## Shadow Comparison Mode (implemented)

Config option `shadow_eval yes` (GOD-only, default off) runs both
evaluators in parallel and logs mismatches:

```cpp
// In mux_exec, after producing output:
if (mudconf.shadow_eval && !mudstate.bShadowActive && !alarm_clock.alarmed)
{
    mudstate.bShadowActive = true;
    // Save registers + counters
    // Run mux_exec2 into separate buffer
    // Restore registers + counters
    // Compare outputs, log mismatches via LOG_BUGS "EVAL" "SHADOW"
    mudstate.bShadowActive = false;
}
```

- `bShadowActive` flag prevents recursion (mux_exec2's handlers call mux_exec)
- State save/restore: global registers, func_invk_ctr, func_nest_lev
- Smoke tests run with `shadow_eval yes` — 358/360 pass, zero mismatches

## Native %-Substitution Handler (implemented)

All L2 dispatch table entries handled natively in `ast_eval_subst()`:

| Code | Chars | Substitution |
|------|-------|-------------|
| %0-%9 | digits | Command arguments |
| %q | Q/q | Registers (%q0-%qz, %q\<name\>) |
| %# | # | Enactor dbref |
| %! | ! | Executor dbref |
| %@ | @ | Caller dbref |
| %% | % | Literal percent |
| %r | R/r | Carriage return |
| %b | B/b | Space |
| %t | T/t | Tab |
| %n | N/n | Enactor name |
| %l | L/l | Enactor location |
| %s | S/s | Subjective pronoun |
| %p | P/p | Possessive pronoun |
| %o | O/o | Objective pronoun |
| %a | A/a | Absolute possessive |
| %m | M/m | Last command |
| %k | K/k | Moniker |
| %\| | \| | Pipe output |
| %+ | + | Argument count |
| %: | : | Enactor objid |
| %v | V/v | Variable attribute (%va-%vz) |
| %i | I/i | Iterator text (%i0-%i9) |
| %= | = | Attr shorthand / extended args |
| %c/%x | C/X | Color codes (simple + RGB/24-bit) |

Uppercase variants (A, M, N, O, P, Q, S, V) trigger `mux_toupper_first()`
on the result, matching the L2 table's 0x80 flag.

## Integration Path

### Phase 1: Infrastructure (COMPLETE)

Standalone tokenizer, parser, and evaluator prototyped in `parser/`
directory. CLI tools for testing. Merged into production as
`mux/src/ast.h` and `mux/src/ast.cpp`.

Commits:
- `666759ca` Add parser architecture study and tokenizer prototype
- `a5aa9efa` Add recursive-descent parser and enhanced tokenizer
- `60f668bf` Add AST evaluator with 50+ builtin functions
- `fe8321dd` Implement proper FN_NOEVAL deferred evaluation
- `f124a8cd` Factor shared tokenizer/parser/AST into mux_parse.h
- `a92cf12f` Add AST parser to production build (Phase 1 stub)

### Phase 2: mux_exec2 (COMPLETE)

`mux_exec2()` is a drop-in replacement. `eval2()` softcode function
exposes the AST evaluator for A/B testing. `shadow_eval` config runs
both evaluators in parallel and logs mismatches.

Commits:
- `990b7da6` Implement Phase 2 AST evaluator with eval2() test function
- `3d9ae901` Inline %-substitutions natively in AST evaluator
- `a29bf743` Inline color substitutions natively in AST evaluator
- `5bb4c8f7` Add native NOEVAL handlers for AST evaluator
- `8541e080` Add AST parse cache and direct subtree evaluation
- `e4351d79` Add shadow eval mode to compare mux_exec and mux_exec2

### Phase 3: Migration (NEXT)

Replace `mux_exec` call sites one at a time. Start with the simplest
callers (those that pass fixed eval flags). Leave complex callers
(function handlers that modify eval flags mid-call) for last.

With `shadow_eval` active, mismatches will be caught during migration
and can be diagnosed from the server log.

### Phase 4: Deprecation

Once all call sites use `mux_exec2`, remove the old `mux_exec`.
Remove `replace_tokens`, `isSpecial` tables, `parse_to`/`parse_to_lite`.

### Phase 5: Compilation (future)

With a stable AST, the evaluator can be replaced with a compiler:
- AST --> RISC-V IR --> sandboxed DBT execution
- Pure functions compile to straight-line code
- Database functions compile to call-outs
- Iterator functions compile to loops with %i0 in registers

## Files

| File | Purpose |
|------|---------|
| `parser/` | Stage 1 prototypes (tokenizer, parser, eval CLI tools) |
| `mux/src/ast.h` | AST node types, token types, public API (117 lines) |
| `mux/src/ast.cpp` | Tokenizer, parser, evaluator, cache, native handlers (2057 lines) |
| `mux/src/eval.cpp` | Shadow eval comparison block in mux_exec |
| `mux/src/externs.h` | mux_exec2 declaration |
| `mux/src/mudconf.h` | shadow_eval config, bShadowActive state |
| `mux/src/conf.cpp` | shadow_eval defaults and config registration |
| `testcases/eval2_fn.mux` | 12 eval2() smoke tests (338 lines) |
| `testcases/tools/Smoke` | shadow_eval yes in smoke config |

## Test Results

- 360 total smoke tests (176 suites)
- 358 pass, 2 pre-existing failures (locate_poss, lock_fn — unrelated)
- 12 eval2-specific tests covering: literals, function calls, nested
  functions, %-substitutions, eval brackets, brace groups, registers,
  iteration, switch, cand/cor, color codes, backslash escapes
- Shadow mode: zero mismatches across all 358 passing tests

## Risks and Mitigations

1. **Behavioral differences.** Mitigated by shadow_eval mode running
   in production. Zero mismatches observed across the smoke test suite.
   Additional fuzz testing recommended before Phase 3 migration.

2. **Performance.** Parse + eval is two passes vs one. AST allocation
   adds memory pressure. LRU cache (1024 entries) mitigates the parse
   cost for repeated evaluations. The eval pass avoids character-at-a-time
   scanning and L1/L2 table lookups.

3. **FN_NOEVAL edge cases.** 8 of 23 NOEVAL functions have native
   handlers. The remaining 15 use generic serialization via
   `ast_raw_text()`, which is functionally identical to the old path.

4. **Compatibility period.** Both code paths coexist. `shadow_eval`
   provides continuous validation. Call sites can be migrated
   incrementally with immediate regression detection.
