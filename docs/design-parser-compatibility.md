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

- `mux214`: `[switch(1,1,{\\% capacity})]` -> `% capacity`
- `mux213`: `[switch(1,1,{\\% capacity})]` -> ` capacity`
- `penn`: `[switch(1,1,{\\% capacity})]` -> `% capacity`

## Key Finding

The primary fault line is not "recursive descent" versus "non-recursive
descent." The real distinction is:

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

The real-engine tests now show that 2.13 differs from 2.14/Penn most
clearly in noeval branch/body contexts such as `if`, `switch`, `case`,
and `iter`, where `%` can disappear from forms like `\\% capacity`.

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

In the verified cases collected so far, 2.14 behaves like this:

1. bare `\\% capacity` evaluates to `% capacity`
2. noeval branch/body uses such as `[switch(1,1,{\\% capacity})]`
   also evaluate to `% capacity`
3. `%iL` is partially recognized and yields `L` / `L L` in the
   tested contexts

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

Penn differs on two separate axes:

- `% ` is atomic, so bare `\% capacity` and `% capacity` both preserve
  the percent
- Penn has a richer `%` grammar, including behaviors like `%wa`, `%xg`,
  `%cg`, and iterator-sensitive `%iL`

## Working Matrix

The study tool currently documents and tests the following focused
differences:

| Case | `mux214` | `mux213` | `penn` |
| --- | --- | --- | --- |
| `\\% capacity` | `% capacity` | `% capacity` | `% capacity` |
| `\% capacity` | ` capacity` | ` capacity` | `% capacity` |
| `% capacity` | ` capacity` | ` capacity` | `% capacity` |
| `[switch(1,1,{\\% capacity})]` | `% capacity` | ` capacity` | `% capacity` |
| `[if(1,{\\% capacity})]` | `% capacity` | ` capacity` | `% capacity` |
| `[case(1,1,{\\% capacity})]` | `% capacity` | ` capacity` | `% capacity` |
| `[switch(1,1,\\% capacity)]` | `% capacity` | ` capacity` | `% capacity` |
| `[iter(a b,{\\% capacity})]` | `% capacity % capacity` | ` capacity  capacity` | `% capacity % capacity` |
| `%wa` | `wa` | `wa` | empty in tested context |
| `%iL` | `L` | `iL` | `#-1 ARGUMENT OUT OF RANGE` |
| `[iter(a b,%iL)]` | `L L` | `iL iL` | `a b` |
| `%=` | `=` | `=` | empty in tested context |
| `%xg` | MUX color form | MUX color form | `thing` |
| `%cg` | MUX color form | MUX color form | `@pemit me=%cgg` |
| `[switch(1,1,{\\%b})]` | `%b` | ` ` | `%b` |
| `[iter(a b,{\\%b})]` | `%b %b` | `   ` | `%b %b` |

This matrix matters because it separates three different causes:

- tokenizer grammar differences, such as Penn-only `%wa`, `%xg`, and
  `%cg`
- substitution-table differences, especially Penn `% `
- noeval branch/body behavior, where 2.13 diverges from both 2.14 and
  Penn on the tested `% capacity` cases

Important: the `mux214` column here reflects the parser-study oracle in
`parser/`, not a claim that production `mux/modules/engine/ast.cpp`
already matches those rows.

## Compatibility Axes

Any production parser-profile design will need to decide at least:

1. Which `%` forms are atomic substitutions.
2. Whether `% ` is recognized explicitly.
3. How `\%` and `\\%` interact with later substitution handling.
4. Whether noeval branch/body contexts change `%` behavior.
5. Whether compatibility is implemented:
   - during tokenization
   - during AST sequence evaluation
   - or by a selective fallback to streaming/noeval evaluation

## Prototype in `parser/`

The study tools now expose:

- `--profile mux214`
- `--profile mux213`
- `--profile penn`

Current prototype scope:

- `mux214` is the baseline token-first AST model.
- `penn` adds Penn `%` grammar where we have real-engine data, including
  `% `, `%wa`, `%iL`, `%xg`, `%cg`, and `%=`.
- `mux213` differs most visibly in the tested noeval branch/body cases,
  where `%` disappears from `\\%...` forms that 2.14 and Penn preserve.

This prototype is intended to answer "where can the mode live?" rather
than "is the production fix already done?"

## Evidence Layers

The project now has two distinct evidence sources:

- parser-level oracle:
  [parser/escape_oracle_cases.txt](/home/sdennis/tinymux/parser/escape_oracle_cases.txt)
  and [docs/parser-escape-oracle.md](/home/sdennis/tinymux/docs/parser-escape-oracle.md)
- command-level oracle:
  [docs/command-escape-oracle.txt](/home/sdennis/tinymux/docs/command-escape-oracle.txt)
  and [docs/command-escape-oracle.md](/home/sdennis/tinymux/docs/command-escape-oracle.md)

This split exists because traced command behavior can include quoting or
command parsing effects before expression evaluation begins.

## Recommended Next Step

Do not keep exploring this as an open-ended parser survey.

The next phase should be driven by a narrow oracle corpus in
[parser/escape_oracle_cases.txt](/home/sdennis/tinymux/parser/escape_oracle_cases.txt)
and documented in
[docs/parser-escape-oracle.md](/home/sdennis/tinymux/docs/parser-escape-oracle.md).

Real traced command observations should be recorded separately in
[docs/command-escape-oracle.txt](/home/sdennis/tinymux/docs/command-escape-oracle.txt)
so command-layer normalization is not confused with parser semantics.

That gives a tighter loop:

1. add only escape/percent cases that distinguish parser semantics
2. mark cases `confirmed` only after a live-engine checksum
3. patch production only for divergences that are pinned by the oracle

When the parser oracle and command oracle disagree, resolve that
explicitly instead of forcing one layer's evidence into the other.

Production-control candidates still look like:

- scanner/tokenizer profile flags for Penn-only `%` forms
- evaluator profile flags for `%` dispatch semantics
- noeval branch/body policy for `if`/`switch`/`case`/`iter`

The important design constraint is now clearer:

- TinyMUX 2.13 compatibility is one target
- optional Penn-inspired backslash cleanup is a separate target

They should not share a single undifferentiated compatibility mode.
