# Parser Escape Oracle

## Purpose

This is the narrow corpus for the current compatibility problem.

It is not trying to model the whole evaluator. It only tracks the cases
that distinguish:

- TinyMUX 2.13 compatibility
- current 2.14 AST behavior
- Penn-style percent grammar

The corpus lives in
[parser/escape_oracle_cases.txt](/home/sdennis/tinymux/parser/escape_oracle_cases.txt)
and is exercised by
[parser/run_escape_oracle.sh](/home/sdennis/tinymux/parser/run_escape_oracle.sh).

For traced real-command behavior after command parsing/quoting, see the
separate command-level oracle in
[docs/command-escape-oracle.md](/home/sdennis/tinymux/docs/command-escape-oracle.md).

## What Belongs Here

Only cases that answer one of these questions:

1. Does backslash stripping happen before or after percent dispatch?
2. Does a noeval path still consume backslashes?
3. Does brace-group selection change the answer?
4. Is the divergence caused by tokenizer grammar or evaluator behavior?

That means the oracle should stay focused on:

- `\%` vs `\\%` vs `\\\%`
- known `%` forms vs unknown `%` forms
- plain evaluation vs `if`/`switch`/`case`/`iter`
- brace vs non-brace noeval arguments
- Penn-only grammar collisions such as `% ` and `%iL`

## Status Meanings

- `confirmed`: checked against a running real engine and reflected in the corpus
- `needs_live`: local model is useful, but still needs a live-server checksum

The current 2.13 and Penn confirmations were captured from traced `@pemit`
runs and recorded only in sanitized form. Player names and dbrefs are
intentionally not preserved here.

Important: the oracle corpus is parser-level. If a traced command goes
through additional quoting or command parsing first, that trace is still
useful evidence, but it must be translated back to the effective eval
input before being copied into the corpus. If that translation is not
clear, record it in the command-level oracle instead.

## Current Spec Split

There are two separate targets. They should not be merged into one mode.

### Target A: TinyMUX 2.13 Compatibility

This is the regression-fix target for 2.14 AST work.

Requirements:

- match 2.13 ordinary `%` and `\` behavior
- match 2.13 FN_NOEVAL multi-pass behavior
- preserve 2.13 oddities even when they are awkward

This target is driven by live-engine checksums, not aesthetics.

### Target B: Optional Saner Backslash Mode

This is a future feature target, not a compatibility fix.

Requirements:

- explicit opt-in
- documented as intentionally non-2.13
- specified independently from the compatibility work

Penn can inform this mode, but Penn behavior should not be smuggled into
the 2.13-compatibility target.

## Immediate Next Step

Use the oracle corpus to close the smallest set of live unknowns before
touching more production code.

The focused oracle corpus is now fully pinned for the currently tracked
rows. Further live checks should only be added when a new divergence is
discovered, not as open-ended exploration.

With the current corpus pinned, the AST-engine work can proceed one
divergence at a time with a clear oracle behind it.
