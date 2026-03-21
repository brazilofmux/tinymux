# MUX Parser Study

Standalone tools for studying the TinyMUX expression parser.
See `../docs/design-parser-compatibility.md` for the compatibility analysis.

## Contents

- `tokenize.cpp` — Stage 1: tokenizer (flat token stream)
- `parse.cpp` — Stage 2: recursive-descent parser (AST)
- `test_corpus.txt` — Test expressions
- `Makefile` — Build rules

## Usage

```
make
echo 'add(1,mul(2,3))' | ./tokenize
echo '[setq(0,hello)]%q0 world' | ./parse
./parse < test_corpus.txt
echo '\\% happy' | ./eval --profile mux214
echo '\\\\% happy' | ./eval --profile mux213
echo '% happy' | ./eval --profile penn
```

## Stages

1. **Tokenizer** — Character-level scanning that mirrors eval.cpp's
   isSpecial tables. Identifies literals, function names, brackets,
   braces, %-substitutions, escapes, and structural characters.

2. **Parser** — Recursive-descent parser that builds an AST from
   the token stream. Handles static function calls (FuncCall),
   dynamic/computed calls (DynCall), eval brackets, brace groups,
   and nested argument lists.

3. **Evaluator** — Tree-walking evaluator for pure expressions.
   Supports ~50 builtin functions (arithmetic, string, list, logic,
   comparison, registers). Demonstrates that pure-expression MUX
   softcode CAN be evaluated from an AST.

   ```
   echo '[add(1,mul(2,3))]' | ./eval
   echo '[if(eq(%0,hello),yes,no)]' | ./eval
   echo '[sort(3 1 4 1 5)]' | ./eval
   ./eval --ast  # show AST alongside output
   ```

   **Key finding:** FN_NOEVAL functions (iter, switch, case, if) need
   deferred argument evaluation. The evaluator must pass unevaluated
   AST subtrees to these handlers rather than pre-evaluated strings.
   This is solvable but requires a different dispatch mechanism.

## Purpose

This is an exploration tool, not production code. The goal is to
understand the existing parser well enough to determine whether a
Ragel scanner + recursive-descent parser can reproduce its behavior.

## Compatibility Profiles

The study tools now support `--profile mux214|mux213|penn`:

- `mux214` models the current AST tokenizer/evaluator shape.
- `mux213` prototypes one important legacy compatibility rule: a `\\`
  token followed by a `%` token is emitted as a literal raw `%...`
  sequence instead of being evaluated independently.
- `penn` recognizes `% ` as an atomic substitution that expands to
  literal `% `, matching PennMUSH's long-standing behavior.

These profiles are intentionally incomplete. They are meant to help
identify where parser behavior differs, not to claim full engine
compatibility.
