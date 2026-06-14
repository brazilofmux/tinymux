# Survey: function/expression parser (eval.cpp + ast.cpp)

Audit of the third named parser — the function/`[...]` expression parser. In
modern TinyMUX it spans two files: `eval.cpp` holds the line-splitting helpers
(`parse_to`, `parse_arglist`), and `ast.cpp` holds the actual tokenizer/parser
(`ast_lex`/`ast_parse`) and evaluator (`mux_exec`/`ast_eval_node`). Methodology
matches the boolexp/wild/JIT campaign.

## eval.cpp — CLEAN

- **`parse_to`** (split a line at a delimiter, respecting `()`/`[]`/`{}` nesting
  and `%`/`\` escapes): **iterative**, not recursive — it tracks nesting in a
  fixed `UTF8 stack[32]` and bounds every push with `if (sp < stacklim)`, so deep
  nesting can neither overflow the C stack nor the array (excess nesting just
  stops being tracked — a parsing-semantics edge, not a safety bug). The
  `isSpecial(L3, …)` global-table mutation is restored on every return path and
  the scan never calls out, so no re-entrancy corruption.
- **`parse_arglist`**: bounded by `iArg < nfargs` (callers pass `MAX_ARG`), never
  writes past `fargs[]`; args are `alloc_lbuf` (heap, LBUF) and filled via
  `mux_exec`/`mux_strncpy` (LBUF-bounded).

## ast.cpp — one defense-in-depth gap (FIXED, #840)

- **Evaluator** (`ast_eval_node`): already hard-capped — `AST_EVAL_MAX_DEPTH=400`,
  RAII `AstEvalDepthGuard`, sets `bStackLimitReached` on overflow. Plus
  `func_nest_lim` (function call depth) and `nStackLimit` (bracket nesting,
  default 10000). No fixed stack buffers. Deep nesting is safely bounded —
  verified `[[[[…]]]]` / `add(add(…))` at max LBUF nesting (16380) don't crash and
  `u()`-indirected deep attrs are bounded.
- **Parser** (`parseSequence`→`parseEvalBracket`/`parseBraceGroup`/
  `parseFunctionCall`): recursive-descent. `m_bracketDepth`/`m_braceDepth` are
  *semantic* counters, **not** a recursion bound — the parser had no hard cap,
  bounded only by LBUF input (~16380 nesting) and small frames. On the 8 MiB
  main-thread stack that's empirically safe (16380-deep survives), but it's the
  same bug *class* as boolexp #839, and the evaluator's own cap comment cites
  "platforms with smaller stack defaults." **#840 (57bc4066b):** added
  `AstParseDepthGuard` (`AST_PARSE_MAX_DEPTH=1000`) at `parseSequence`, mirroring
  the evaluator's cap. `thread_local`, so it also bounds the NOEVAL
  structural-arg re-parse. Over the cap, `parseSequence` returns its empty node
  without recursing; the enclosing bracket already consumed its opening token, so
  `m_pos` advances and parsing terminates. Smoke 1255/1255; normal exprs
  unchanged; 16380-deep survives.

## Buffers examined — bounded

`parser_lookup_builtin_noeval`'s `TempFun` (LBUF, clamped), `parse_arglist`
buffers (LBUF), `parse_to` `stack[32]` (bounded push). No fixed stack arrays in
ast.cpp. Bracket/brace/paren matching is depth-counter or RAII-guard bounded.

## Three-parser sweep complete

locks (`boolexp.cpp`) — #839 parser recursion DoS fixed; commands
(`command.cpp`) — clean; functions (`eval.cpp`/`ast.cpp`) — eval.cpp clean,
ast.cpp parser hard-capped (#840). See docs/survey-boolexp.md,
docs/survey-command-parser.md.
