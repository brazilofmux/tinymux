# Replacing mux_exec with an AST Evaluator

## Status: Study / 2.14 Experiment Branch

## Prerequisites

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

## Architecture

### Current: Stream Transformer

```
source text → mux_exec() → output buffer
               ↑ recursive calls for [...], function args, FN_NOEVAL bodies
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

### New: Parse → AST → Evaluate

```
source text → parse() → ASTNode tree → eval() → output string
```

Two phases, cleanly separated:

1. **Parse** — Tokenize and build AST. No evaluation, no database access,
   no side effects. Can be cached (attribute text → AST).

2. **Evaluate** — Walk the AST with an evaluation context. Function
   dispatch, substitution resolution, database access all happen here.

### New function signature

```cpp
// Parse: text → AST (cacheable, no side effects)
ASTNode *mux_parse(const UTF8 *pStr, size_t nStr);

// Evaluate: AST → output string
void mux_eval(const ASTNode *ast,
              UTF8 *buff, UTF8 **bufc,
              dbref executor, dbref caller, dbref enactor,
              int eval,
              const UTF8 *cargs[], int ncargs);

// Combined (drop-in replacement during transition)
void mux_exec2(const UTF8 *pStr, size_t nStr,
               UTF8 *buff, UTF8 **bufc,
               dbref executor, dbref caller, dbref enactor,
               int eval,
               const UTF8 *cargs[], int ncargs);
```

`mux_exec2` is a drop-in replacement: parse then evaluate. During
transition, both `mux_exec` and `mux_exec2` coexist. Call sites can
be migrated one at a time.

## AST Node Types

```cpp
enum NodeType {
    NODE_SEQUENCE,      // Ordered list of children
    NODE_LITERAL,       // Plain text
    NODE_SPACE,         // Whitespace (for space compression)
    NODE_SUBST,         // %-substitution (%0, %q<name>, %i0, %r, etc.)
    NODE_ESCAPE,        // \-escape
    NODE_FUNCCALL,      // function(args...) — name is static string
    NODE_EVALBRACKET,   // [expression]
    NODE_BRACEGROUP,    // {deferred expression}
    NODE_SEMICOLON,     // ; command separator
};
```

No NODE_DYNCALL — eliminated by design constraint.

## FN_NOEVAL Handling

23 functions use FN_NOEVAL. They fall into three categories:

### 1. Conditional evaluation (evaluate selected branches)

- `if(cond, true_branch, false_branch)`
- `ifelse(cond, true_branch, false_branch)`
- `switch(val, pat1, result1, ..., default)`
- `case(val, pat1, result1, ..., default)`
- `cand(expr1, expr2, ...)`  — short-circuit AND
- `cor(expr1, expr2, ...)`   — short-circuit OR

**AST approach:** The evaluator checks function flags before evaluating
children. For NOEVAL functions, children are passed as ASTNode* and the
handler calls `mux_eval()` selectively:

```cpp
// Pseudo-code for if()
std::string result = mux_eval(children[0]);  // evaluate condition
if (xlate(result)) {
    return mux_eval(children[1]);            // evaluate true branch
} else if (children.size() > 2) {
    return mux_eval(children[2]);            // evaluate false branch
}
```

### 2. Iterative evaluation (evaluate body per item)

- `iter(list, body, osep, isep)`
- `list(list, body, osep, isep)`
- `filter(obj/attr, list, isep, osep)`
- `step(obj/attr, list, step, isep, osep)`

**AST approach:** Push iterator state, evaluate body subtree per item:

```cpp
// Pseudo-code for iter()
std::string list_val = mux_eval(children[0]);
auto items = split(list_val, sep);
for (int i = 0; i < items.size(); i++) {
    push_itext(items[i], i);
    result += mux_eval(children[1]);  // body references %i0
    pop_itext();
}
```

No `replace_tokens` needed — `##` is gone, `%i0` resolves through
the iterator stack during normal evaluation.

### 3. Literal pass-through

- `@@(comment)` — discard
- `lit(text)` — return unevaluated
- `parenmatch(text)` — return unevaluated
- `fcount()` — no eval needed

**AST approach:** Return raw text reconstruction of the subtree, or
simply don't evaluate.

## Eval Flags

The `eval` parameter changes parsing/evaluation behavior. Key flags:

| Flag | Effect on Parser | Effect on Evaluator |
|------|-----------------|---------------------|
| EV_EVAL | Enable %-substitutions | Resolve %nodes |
| EV_FCHECK | Enable function calls on ( | Parse FUNC nodes |
| EV_FMAND | Require valid function name | Error on unknown func |
| EV_STRIP_CURLY | Strip outer {} | Unwrap BraceGroup |
| EV_NOFCHECK | Suppress [ evaluation | Don't recurse into EvalBracket |
| EV_NO_COMPRESS | Don't compress spaces | Pass through Space nodes |

**Design decision:** Parse with all features enabled (superset AST),
then let the evaluator respect flags. This means a single cached AST
works for all eval flag combinations. The evaluator checks:

- `EV_EVAL` → whether to resolve SUBST nodes
- `EV_FCHECK` → whether to dispatch FUNCCALL nodes
- `EV_STRIP_CURLY` → whether to unwrap BRACEGROUP nodes
- `EV_NOFCHECK` → whether to recurse into EVALBRACKET nodes

## AST Caching

Since parsing is pure (no side effects), ASTs can be cached:

```
attribute text → SHA1 hash → cached ASTNode tree
```

When an attribute is modified (`atr_add_raw_LEN`), invalidate its
cache entry. This means repeated evaluations of the same attribute
(common in loops, $-commands, etc.) skip parsing entirely.

The cache can be:
- Per-attribute (keyed by dbref + attrnum)
- Global LRU (keyed by text hash)
- Hybrid (per-attr with fallback to text hash for dynamic text)

## Integration Path

### Phase 1: Infrastructure (parser/ directory)

Build the production-quality tokenizer, parser, and evaluator as
standalone library code. Test against the existing smoke test corpus
by comparing `mux_exec` output vs `mux_eval` output for every test
expression.

### Phase 2: mux_exec2 (mux/src/)

Add `mux_exec2()` as a drop-in replacement. Initially, it's just
`mux_parse()` + `mux_eval()`. Add a config option to switch between
`mux_exec` and `mux_exec2` at runtime for A/B testing.

### Phase 3: Migration

Replace `mux_exec` call sites one at a time. Start with the simplest
callers (those that pass fixed eval flags). Leave complex callers
(function handlers that modify eval flags mid-call) for last.

### Phase 4: Deprecation

Once all call sites use `mux_exec2`, remove the old `mux_exec`.
Remove `replace_tokens`, `isSpecial` tables, `parse_to`/`parse_to_lite`.

### Phase 5: Compilation (future)

With a stable AST, the evaluator can be replaced with a compiler:
- AST → RISC-V IR → sandboxed DBT execution
- Pure functions compile to straight-line code
- Database functions compile to call-outs
- Iterator functions compile to loops with %i0 in registers

## Files

| File | Purpose |
|------|---------|
| `parser/tokenize.cpp` | Stage 1: standalone tokenizer |
| `parser/parse.cpp` | Stage 2: standalone AST parser |
| `parser/eval.cpp` | Stage 3: standalone AST evaluator |
| `mux/src/ast.h` | AST node definitions (production) |
| `mux/src/ast.cpp` | Parser + evaluator (production) |
| `mux/src/eval.cpp` | Modified: add mux_exec2, keep mux_exec |

## Risks

1. **Behavioral differences.** The new evaluator must produce byte-identical
   output to `mux_exec` for all inputs. The smoke test corpus (348 tests)
   is a good starting point but not exhaustive. Need additional fuzz testing.

2. **Performance.** Parse + eval is two passes vs one. AST allocation adds
   memory pressure. Caching mitigates the parse cost. The eval pass should
   be faster than mux_exec's character-at-a-time scanning.

3. **FN_NOEVAL edge cases.** Some functions do surprising things with their
   unevaluated arguments (e.g., `objeval` changes executor mid-evaluation).
   These need careful per-function analysis.

4. **Compatibility period.** Both code paths must coexist during transition.
   Config-switchable A/B testing is essential.
