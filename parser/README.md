# MUX Parser Study

Standalone tools for studying the TinyMUX expression parser.
See `../docs/PARSER.md` for the full architecture analysis.

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
```

## Stages

1. **Tokenizer** — Character-level scanning that mirrors eval.cpp's
   isSpecial tables. Identifies literals, function names, brackets,
   braces, %-substitutions, escapes, and structural characters.

2. **Parser** — Recursive-descent parser that builds an AST from
   the token stream. Handles static function calls (FuncCall),
   dynamic/computed calls (DynCall), eval brackets, brace groups,
   and nested argument lists.

3. **Interpreter** — (future) Walk the AST and evaluate it, producing
   identical output to mux_exec.

## Purpose

This is an exploration tool, not production code. The goal is to
understand the existing parser well enough to determine whether a
Ragel scanner + recursive-descent parser can reproduce its behavior.
