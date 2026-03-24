# ulambda() Investigation Notes

## The Problem

Three smoke tests fail (pre-existing, not JIT regressions):

```
TC004: u with lambda. Failed (DA39A3EE5E6B4B0D3255BFEF95601890AFD80709)
TC008: sortby with lambda. Failed (1E580FB4C72ADC55D1281A4B738E3C5F568ACA95)
TC001: while stops on condition. Failed (4526D62236CEE7BE109803D49B6CD2AEADEAB6D2)
```

DA39A3EE... is SHA1 of the empty string. These fail identically with
and without `--enable-jit`.

## Root Cause: Lexer splits `#lambda/body(args)`

The AST lexer (`ast_scan.rl`) treats `#` as a standalone token — it is
NOT part of `lit_plain` (line 51: `lit_plain = [^[\]{}(),;%\\ \0#]`).
The standalone `#` rule (line 185) emits it as ASTTOK_LIT.

For the expression `u(#lambda/mul(\%0,\%1), 6, 7)`, the token stream is:

```
u        → ASTTOK_LIT → promoted to ASTTOK_FUNC by (
(        → ASTTOK_LPAREN
#        → ASTTOK_LIT (standalone rule)
lambda/mul → ASTTOK_LIT → promoted to ASTTOK_FUNC by the next (
(        → ASTTOK_LPAREN
\%0      → ASTTOK_ESC
,        → ASTTOK_COMMA (inside lambda/mul's parens)
\%1      → ASTTOK_ESC
)        → ASTTOK_RPAREN (closes lambda/mul)
,        → ASTTOK_COMMA (at u's level)
 6       → ASTTOK_LIT
,        → ASTTOK_COMMA
 7       → ASTTOK_LIT
)        → ASTTOK_RPAREN (closes u)
```

The parser creates TWO nested function calls:
- Outer: `u(args...)`
- Inner: `lambda/mul(\%0, \%1)` as AST_FUNCCALL

At evaluation time:
1. `lambda/mul` is not a known function
2. In EV_FMAND context: `#-1 FUNCTION (LAMBDA/MUL) NOT FOUND`
3. Combined with the standalone `#` literal: `"##-1 FUNCTION (LAMBDA/MUL) NOT FOUND"`
4. `u()` receives this as fargs[0] — not a valid attribute reference
5. `parse_and_get_attrib` fails → returns false → empty output

## Verification

- TC001-TC003 (map/filter/fold with #lambda) PASS because those
  functions call `parse_and_get_attrib` internally AFTER receiving the
  evaluated first argument. The lambda body with `(` in these cases
  uses simpler functions that happen to evaluate to something
  `parse_and_get_attrib` can still handle.

  Actually, on closer inspection: TC002 `filter(#lambda/gt(\%0,3), ...)`
  has the same tokenization problem — `gt(\%0,3)` is parsed as a function
  call. But `gt(\%0,3)` evaluates: `\%0` → `%0` (literal), `3` → `3`.
  `gt("%0", "3")` → `gt(0, 3)` → `0`. So filter gets `"#0"` as the
  callback reference. `#0` is object 0 (Limbo). `filter` calls
  `parse_and_get_attrib` on `"#0"` which tries to resolve an attribute
  on Limbo... and this somehow produces the right result? This needs
  further investigation — it may be succeeding for wrong reasons or
  the hash may have been determined empirically.

- fun_u was confirmed NEVER called during the entire smoke test run
  (debug logging at the function entry point showed zero hits). This
  confirms the expression evaluation never reaches the function dispatch.

## PennMUSH's Solution

PennMUSH provides `ulambda()`:

- In PennMUSH, `ulambda` and `u` share `fun_ufun` implementation
- `ulambda` sets `UFUN_LAMBDA` flag → `fetch_ufun_attrib` recognizes
  `#lambda/` prefix
- **Critical**: PennMUSH's classic parser (process_expression) does NOT
  promote `word(` to a function call in the same aggressive way. Their
  scanner-evaluator hybrid recognizes function calls contextually.
- PennMUSH docs say: "The code will normally be parsed twice, so special
  characters should be escaped where needed." (`\%0` → `%0` on first
  eval, `%0` substituted on second eval)
- The list of functions that support `#lambda` does NOT include `u()` —
  only callback functions: filter, fold, map, sortby, sortkey, etc.

Source: `/tmp/pennmush/game/txt/hlp/penntop.hlp` lines 282-341

## MUX Implementation Approach

Since MUX's AST tokenizer always promotes `word(` to ASTTOK_FUNC, the
body inside `#lambda/body(args)` will always be parsed as a function
call. We need `ulambda()` with NOEVAL semantics for the first argument.

### Implementation — WORKING (TC004 passes)

Files modified:
- `mux/include/ast.h`: Added `ASTNOEVAL_ULAMBDA` to enum
- `mux/modules/engine/ast.cpp`:
  - `ast_noeval_kind`: returns ASTNOEVAL_ULAMBDA for "ULAMBDA"
  - `ast_noeval_arg_is_deferred`: argIndex==0 is deferred for ULAMBDA
  - `ast_noeval_ulambda`: native handler that:
    1. Gets raw deferred text for arg 0 via `ast_call_raw_arg`
    2. Evaluates it through `mux_exec` WITHOUT `EV_FCHECK` (suppresses
       function dispatch, so `mul(...)` becomes literal text)
    3. Evaluates remaining args normally
    4. Calls `parse_and_get_attrib` on the reconstructed arg 0
    5. Evaluates the extracted body with `ast_exec` (NOT `mux_exec`)
       passing fargs[1..] as %0-%9 cargs
  - Dispatch table: ASTNOEVAL_ULAMBDA → ast_noeval_ulambda
- `mux/modules/engine/functions.cpp`:
  - `ULAMBDA` registered with `FN_NOEVAL` flag, using `fun_u` as stub

### Token Stream Correction

The investigation initially claimed `#` was tokenized separately from
`lambda/mul`. This was WRONG. Ragel's longest-match scanner combines
`#lambda/mul` into a single `lit_char+` token (because `lit_char`
includes `'#' lit_after_hash`). The `(` then promotes it to
`ASTTOK_FUNC`. The parser correctly creates nested FUNCCALL nodes
with `has_close_paren = true`. The AST structure was never the problem.

### Previous "dispatch not reached" Bug — Resolved

The earlier observation that `ast_eval_funccall` was "NEVER reached"
was from testing done before the current code was in place. With the
current implementation, instrumentation confirms:
- `ast_eval_funccall` IS called for `ulambda`
- `ast_try_native_noeval` dispatches to `ast_noeval_ulambda`
- Raw text capture works: `#lambda/mul(\%0,\%1)`
- Eval without EV_FCHECK works: produces `#lambda/mul(%0,%1)`
- `parse_and_get_attrib` succeeds: body=`mul(%0,%1)`, thing=#executor

### Root Cause of TC004 Failure: JIT Compiler

The final `mux_exec` call for the body `mul(%0,%1)` was silently
failing when JIT was enabled (`--enable-jit`). The JIT compiler
intercepts expressions >= 8 chars with `EV_EVAL` set. It compiled
`mul(%0,%1)` but used the **outer caller's cargs** (from the
enclosing attribute evaluation) instead of the **ulambda-provided
cargs** (`["6", "7"]`). The outer cargs were empty/null, producing
empty output.

**Fix**: Use `ast_exec()` instead of `mux_exec()` for the body
evaluation. `ast_exec` bypasses the JIT and evaluates via the AST
evaluator, which correctly uses the caller-provided cargs.

This is a known limitation: the JIT compiles with the cargs context
from the top-level `mux_exec` call, not from nested callers that
provide different cargs. A proper fix would require the JIT to
accept dynamically-scoped cargs, which is deferred to JIT Tier 3+.

### Verification

Instrumented run confirmed:
```
ast_noeval_ulambda rawText=#lambda/mul(\%0,\%1) len=20
ast_noeval_ulambda arg0 after eval: "#lambda/mul(%0,%1)"
ast_noeval_ulambda: body="mul(%0,%1)" thing=#109 fargs[1]="6"
ast_noeval_ulambda: mux_exec wrote 0 bytes: ""     ← JIT bug
ast_noeval_ulambda: ast_exec wrote 2 bytes: "42"   ← AST evaluator OK
```

## while() and sortby() failures

These are related but distinct:

- `while(me/fn_double, me/fn_gt10, list, 1)`: while() is NOT
  FN_NOEVAL. The args are plain strings ("me/fn_double" etc.) that
  should pass through evaluation unchanged. while() then calls
  `parse_and_get_attrib` on them internally. This should work via
  ECALL. Needs separate investigation — may be a while() implementation
  bug unrelated to #lambda.

- `sortby(#lambda/sub(\%1,\%0), 5 3 1 4 2)`: Same `#lambda` parsing
  issue as u(). The `sub(\%1,\%0)` becomes a nested function call.
  sortby uses `parse_and_get_attrib` on the callback reference, but
  the reference is mangled by evaluation.
