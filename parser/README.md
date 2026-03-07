# MUX Parser Study

Standalone tools for studying the TinyMUX expression parser.
See `../docs/PARSER.md` for the full architecture analysis.

## Contents

- `tokenize.cpp` — CLI tool that tokenizes MUX expressions
- `Makefile` — Build rules

## Usage

```
make
echo 'add(1,mul(2,3))' | ./tokenize
echo '[setq(0,hello)]%q0 world' | ./tokenize
```

## Purpose

This is an exploration tool, not production code. The goal is to
understand the existing parser well enough to determine whether a
Ragel scanner + recursive-descent parser can reproduce its behavior.
