# Parser Compatibility Modes

## Purpose

This note documents the current understanding of the parser compatibility
problem across three relevant behaviors:

- TinyMUX 2.13 streaming evaluator
- TinyMUX 2.14 AST parser/evaluator
- PennMUSH's recursive `process_expression()` parser

The immediate trigger is real-world bboard softcode that depends on
legacy handling of backslash-plus-percent sequences near expression
boundaries, such as `]\\\\% capacity`.

The study tool now has a verified minimal reproduction of that case:

- `mux214`: `[switch(1,1,{\\% capacity})]` -> `\ capacity`
- `mux213`: `[switch(1,1,{\\% capacity})]` -> `% capacity`
- `penn`: `[switch(1,1,{\\% capacity})]` -> `\% capacity`

## Key Finding

The primary fault line is not "recursive descent" versus "non-recursive
descent". The real distinction is:

- streaming parse/evaluate semantics, where escapes and substitutions can
  interact before the expression is split into independent units
- token-first AST semantics, where escapes and substitutions become
  separate nodes and are then evaluated independently

PennMUSH demonstrates that a recursive parser can still be compatible
with legacy softcode when its substitution grammar is sufficiently rich.

## Observed Behaviors

### TinyMUX 2.13

`parse_to()` treats `%` and `\\` as special characters and copies them
with the following byte as a unit during streaming parse. See
[eval.cpp](/home/sdennis/tinymux/mux2.13_12/src/eval.cpp#L291).

Later, the `%` substitution dispatcher falls back to copying only the
following character for unknown sequences. See
[eval.cpp](/home/sdennis/tinymux/mux2.13_12/src/eval.cpp#L1797).

This means 2.13 behavior is shaped by the interaction of:

- escape copying during streaming parse
- later `%` dispatch on the surviving character stream
- possible multi-pass evaluation around brackets and function args

The current study prototype models one important part of that last item:
selected brace-group arguments to `FN_NOEVAL` functions (`if`, `switch`,
`case`) get a noeval pass that strips one layer of backslashes before
the result is parsed and evaluated again.

### TinyMUX 2.14 AST

The AST scanner tokenizes `%` and `\\` separately. See
[ast_scan.rl](/home/sdennis/tinymux/mux/modules/engine/ast_scan.rl#L102)
and [ast_scan.rl](/home/sdennis/tinymux/mux/modules/engine/ast_scan.rl#L117).

The AST evaluator then evaluates:

- `AST_SUBST` independently at
  [ast.cpp](/home/sdennis/tinymux/mux/modules/engine/ast.cpp#L1802)
- `AST_ESCAPE` independently at
  [ast.cpp](/home/sdennis/tinymux/mux/modules/engine/ast.cpp#L1816)

Unknown `%` forms fall back to the following character only at
[ast.cpp](/home/sdennis/tinymux/mux/modules/engine/ast.cpp#L932).

This separation is exactly what breaks legacy couplings such as
`Esc("\\\\") + Sub("% ")`.

In the verified bboard-shaped case, 2.14-style behavior is:

1. the brace-group argument is selected by a `FN_NOEVAL` function
2. braces are stripped
3. the remaining `Esc("\\\\") + Sub("% ")` are evaluated independently
4. the result is `\ capacity`

### PennMUSH

PennMUSH's parser is recursive, but still streaming. Its `\\` handling
strips the backslash and emits the next character directly. See
[parse.c](/tmp/pennmush/src/parse.c#L3106).

It also explicitly recognizes `% ` as a valid substitution that expands
to literal `% `. See [parse.c](/tmp/pennmush/src/parse.c#L2419).

PennMUSH's changelog calls this out directly:

- `% ` became a literal percent-space in 1.8.1p2
- `%+`, `% `, and `%i0-%i9` were later documented as substitutions

See [CHANGES.181](/tmp/pennmush/CHANGES.181#L332) and
[CHANGES.182](/tmp/pennmush/CHANGES.182#L195).

For the same bboard-shaped case, Penn behavior differs from both MUX
profiles because `% ` is atomic. After brace stripping, `\\% ` becomes
`Esc("\\\\") + Sub("% ")`, which evaluates to `\% `.

## Working Matrix

The study tool currently documents and tests the following focused
differences:

| Case | `mux214` | `mux213` | `penn` |
| --- | --- | --- | --- |
| `\\% capacity` | `\ capacity` | `\ capacity` | `\% capacity` |
| `[switch(1,1,{\\% capacity})]` | `\ capacity` | `% capacity` | `\% capacity` |
| `%wa` | `wa` | `wa` | Penn W-attribute |
| `%iL` | `iL` | `iL` | current iterator level |
| `% ` | unknown-subst fallback to space | unknown-subst fallback to space | literal `% ` |

This matrix matters because it separates three different causes:

- tokenizer grammar differences, such as Penn-only `%wa` and `%iL`
- substitution-table differences, especially Penn `% `
- multi-pass noeval behavior, which is the current best explanation for
  the 2.13 bboard result

## Compatibility Axes

Any production parser-profile design will need to decide at least:

1. Which `%` forms are atomic substitutions.
2. Whether `% ` is recognized explicitly.
3. Whether `\\` may absorb or literalize a following `%` form.
4. Whether compatibility is implemented:
   - during tokenization
   - during AST sequence evaluation
   - or by a selective fallback to streaming evaluation

## Prototype in `parser/`

The study tools now expose:

- `--profile mux214`
- `--profile mux213`
- `--profile penn`

Current prototype scope:

- `mux214` is the baseline token-first AST model.
- `penn` adds Penn-only atomic `%` forms such as `% `, `%wa`, and `%iL`.
- `mux213` currently models one legacy behavior that matters for real
  softcode: brace-group arguments selected by `FN_NOEVAL` functions get
  a noeval pass that strips one layer of backslashes before reparse.

This prototype is intended to answer "where can the mode live?" rather
than "is the production fix already done?"

## Recommended Next Step

Build a semantic matrix before touching production parsing:

1. Enumerate all `%` forms in 2.13, 2.14, and Penn.
2. Enumerate backslash rules in each engine, including noeval paths.
3. Classify each divergence as tokenization-time, evaluation-time, or
   multi-pass streaming behavior.
4. Decide whether production needs:
   - one default profile plus a compatibility mode, or
   - explicit parser profiles such as `mux213`, `mux214`, and `penn`.

Production-control candidates now look like:

- scanner/tokenizer profile flags for Penn-only `%` forms
- evaluator profile flags for `%` dispatch semantics
- noeval-argument policy for brace-group reparsing

The bboard case suggests the third category is the first one that must
be controlled for TinyMUX 2.13 compatibility.
