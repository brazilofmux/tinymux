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

- `penn` adds atomic `% ` support.
- `mux213` adds a sequence-level fold for `Esc("\\\\") + Sub("%...")`,
  emitting the raw `%...` text literally. This is a targeted experiment,
  not a claim of full 2.13 emulation.

This prototype is intended to answer "where can the mode live?" rather
than "is the production fix already done?"

## Recommended Next Step

Build a semantic matrix before touching production parsing:

1. Enumerate all `%` forms in 2.13, 2.14, and Penn.
2. Enumerate escape-folding rules in each engine.
3. Classify each divergence as tokenization-time, evaluation-time, or
   multi-pass streaming behavior.
4. Decide whether production needs:
   - one default profile plus a compatibility mode, or
   - explicit parser profiles such as `mux213`, `mux214`, and `penn`.
