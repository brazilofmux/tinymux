# MUX Parser Study

Standalone tools for studying the TinyMUX expression parser.
See `../docs/design-parser-compatibility.md` for the compatibility analysis
and `../docs/parser-escape-oracle.md` for the focused escape oracle.
For end-to-end traced command behavior, see
`../docs/command-escape-oracle.md`.

## Contents

- `tokenize.cpp` — Stage 1: tokenizer (flat token stream)
- `parse.cpp` — Stage 2: recursive-descent parser (AST)
- `test_corpus.txt` — Test expressions
- `escape_oracle_cases.txt` — focused cross-profile escape corpus
- `run_escape_oracle.sh` — runner for the focused escape corpus
- `borrowed-stream-semantics.md` — reduced 2.13/Penn parser mechanisms
- `stream_passes.cpp` — tiny pass-by-pass stream vs frozen-token model
- `hammer_refrozen.sh` — prints core deferred-boundary cases across models
- `Makefile` — Build rules

## Usage

```
make
echo 'add(1,mul(2,3))' | ./tokenize
echo '[setq(0,hello)]%q0 world' | ./parse
./parse < test_corpus.txt
echo '\\\\% capacity' | ./eval --profile mux214
echo '[switch(1,1,{\\\\% capacity})]' | ./eval --profile mux213
echo '%xg' | ./eval --profile penn
echo '\\\\% capacity' | ./stream_passes --profile mux213 --model stream --passes noeval,eval
echo '\\\\% capacity' | ./stream_passes --profile mux213 --model frozen --passes noeval,eval
./hammer_refrozen.sh
./run_escape_oracle.sh
RUN_EXPLORATORY_MUX213=1 ./run_escape_oracle.sh
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
- `mux213` is a study profile for 2.13-like behavior, but some older
  deferred-noeval rows were wrong and have now been removed from the
  focused oracle. Treat live-engine checks and the focused oracle as the
  spec, not the study profile.
- `penn` recognizes Penn-only `%` behavior where we have real-engine
  data, including `% `, `%wa`, `%iL`, `%xg`, `%cg`, and `%=`.

Focused examples:

```
echo '\\\\% capacity' | ./eval --profile mux214
# -> % capacity

echo '[switch(1,1,{\\\\% capacity})]' | ./eval --profile mux213
# study profile output may still lag the corrected oracle; use the
# focused oracle and live-engine checks as the spec here

echo '%xg' | ./eval --profile penn
# -> thing
```

These profiles are intentionally incomplete. They are meant to help
identify where parser behavior differs, not to claim full engine
compatibility.

## Escape Oracle

The general evaluator tests in `test_eval.sh` are still useful, but they
mix parser behavior with ordinary function coverage. For the compatibility
work, use the focused oracle:

```
./run_escape_oracle.sh
```

That runner:

- checks the narrow escape/percent corpus across `mux214`, `mux213`, and
  `penn`
- validates `mux214` and `penn` by default
- treats `mux213` as exploratory unless `RUN_EXPLORATORY_MUX213=1` is set
- prints which cases still need live-server verification
- is allowed to disagree with the exploratory `mux213` study profile if
  the profile has drifted from the corrected oracle
- keeps the current effort centered on parser semantics instead of the
  whole evaluator surface area

The intended workflow is:

1. keep adding only parser-semantic cases to `escape_oracle_cases.txt`
2. remove or downgrade rows when the live basis is unclear instead of
   keeping stale guesses in the oracle
3. mark cases `confirmed` only after a live engine check
4. use the resulting matrix to drive AST-engine changes in `mux/`
