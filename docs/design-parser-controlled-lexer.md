# Parser-Controlled Lexer Modes

## Purpose

This note documents an alternative to the full "split lexer" design in
[docs/design-split-lexer.md](/home/sdennis/tinymux/docs/design-split-lexer.md).

The core idea is:

- keep the parser in control of structural boundaries
- make lexer behavior depend on parser-supplied `EVAL`/`NOEVAL` mode
- re-scan deferred regions later under a different mode

This is still an intentional violation of clean textbook lexer/parser
separation. It is less ugly than AST sibling-munging, but it is not a
pure front-end architecture.

## Problem Restated

The current 2.14 AST pipeline does this:

1. scan the entire expression once
2. form compound `%...` and `\X` tokens immediately
3. parse the frozen token stream into an AST
4. try to recover 2.13 `NOEVAL` behavior later

That loses information too early.

In 2.13 and PennMUSH, the semantic meaning of `%` depends on the eval
state *at the moment the character is read*, while backslash consumption
still happens even in `NOEVAL` paths.

The scanner therefore needs evaluation context. The parser is the part
that knows that context.

## Design Summary

Do not scan the whole expression once with a single global mode.

Instead:

1. parse structure with parser-controlled scan boundaries
2. scan content regions under a parser-supplied lexer mode
3. store deferred regions as raw source spans or raw text
4. when a deferred region is later selected, re-scan it under `EVAL`

This is extremely close to what PennMUSH already does, but made explicit
in the AST architecture.

## Modes

The scanner needs explicit modes:

- `ASTLEX_EVAL`
- `ASTLEX_NOEVAL`
- `ASTLEX_STRUCTURAL`

Meaning:

### `ASTLEX_EVAL`

- `%` is semantic
- `\` consumes the following character
- `%` forms may be grouped according to the active profile

### `ASTLEX_NOEVAL`

- `%` is not semantic
- `%` text passes through literally
- `\` still consumes the following character

This asymmetry is the important legacy rule.

### `ASTLEX_STRUCTURAL`

- recognizes brackets, braces, commas, parens, semicolons, spaces
- does only the minimum needed to let the parser find region boundaries
- does not commit `%` and `\` semantics more than necessary

This mode exists so the parser can discover the shape of function args
and deferred bodies without prematurely freezing all content semantics.

## Where The Context Comes From

The parser already knows when it is crossing semantically important
boundaries:

- entering a function call
- parsing comma-separated args
- seeing whether the callee is `FN_NOEVAL`
- entering brace groups and eval brackets
- later selecting a branch/body from a deferred arg

That means the parser can drive the scanner:

- normal function args: scan/parse in `ASTLEX_EVAL`
- `FN_NOEVAL` args: collect region boundaries structurally, but keep raw
  source for later
- selected deferred arg/body: re-scan in `ASTLEX_EVAL`

## Concrete Model In This Tree

### Current relevant files

- [mux/modules/engine/ast_scan.rl](/home/sdennis/tinymux/mux/modules/engine/ast_scan.rl)
- [mux/modules/engine/ast.cpp](/home/sdennis/tinymux/mux/modules/engine/ast.cpp)
- [mux/include/ast.h](/home/sdennis/tinymux/mux/include/ast.h)

### Current fault line

`ast_tokenize()` in `ast_scan.rl` is a one-shot whole-input scanner. It
forms:

- `ASTTOK_PCT`
- `ASTTOK_ESC`

before the parser knows whether a region is `EVAL` or `NOEVAL`.

That is the decision that must move.

## Phase Plan

### Phase 1: API and naming groundwork

Add explicit concepts to the AST interface:

- lexer mode enum
- source-span type for deferred regions
- region-parse entrypoint that accepts a mode

At this stage, the implementation may still be a wrapper around the
existing one-shot scanner. The purpose is to make the future design
visible in code and stop hard-coding the assumption that all parsing
starts in one global scanner mode.

### Phase 2: Region parse entrypoints

Add parser/scanner entrypoints that work on a substring or source span:

- tokenize region with a supplied mode
- parse region into a subtree

Still acceptable at this stage:

- `ASTLEX_EVAL` and `ASTLEX_NOEVAL` may initially share most code
- `FN_NOEVAL` may still store raw text rather than raw pointers

### Phase 3: Parser-aware function args

During function-call parsing:

- resolve the function name early
- if the callee is `FN_NOEVAL`, do not fully semantic-tokenize arg
  contents yet
- record raw arg regions for later

This is the key parser/lexer handshake.

### Phase 4: Replace legacy replay path

Replace the current noeval text-replay workaround with:

1. collect deferred region in `NOEVAL`
2. strip braces if required
3. parse selected region again in `EVAL`
4. evaluate resulting subtree

At that point, re-scanning is intentional and parser-directed rather
than an after-the-fact AST serialization trick.

### Phase 5: Optional full incremental scanner

Only if needed, replace the current batch tokenizer with a scanner
object that can:

- maintain position
- expose marks/spans
- switch modes mid-parse

This is not required to validate the design.

## Why This Is Better Than The Full Split Lexer

Compared to the design in
[docs/design-split-lexer.md](/home/sdennis/tinymux/docs/design-split-lexer.md),
this approach keeps the ugly part closer to the original disease.

Benefits:

- no AST sibling-mutation or sibling-consuming evaluation logic
- no global conversion of every `\` and `%` into atomic AST nodes
- deferred re-scan happens only at semantically real boundaries
- normal eval path can stay closer to the current architecture

Costs:

- parser and scanner are no longer cleanly separated
- function metadata influences parse behavior
- deferred args become region objects, not just already-parsed children
- some re-scan is now explicit design, not an accidental workaround

## Risk

This still is not "proper" in the usual compiler sense.

The risk is not conceptual novelty; the risk is getting the exact
boundaries wrong:

- which args are collected raw
- when braces are stripped
- whether nested brackets/braces are rescanned under the right mode
- whether the JIT/AST cache still sees stable parse units

This design should therefore be treated as:

- a compatibility mechanism
- limited to the smallest surface that must preserve 2.13 semantics

not as a general endorsement of context-sensitive lexing everywhere.

## Recommendation

Prefer this design over the full split-lexer tree-walk design if:

- the problem can be confined to deferred-eval boundaries
- parser-visible mode control is enough to reproduce the live-engine
  behavior
- ordinary top-level eval can stay in the normal path

If those assumptions fail, the broader split-lexer design remains the
fallback.
