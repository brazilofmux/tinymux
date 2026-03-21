# Command Escape Oracle

## Purpose

This document records observed end-to-end command behavior from traced
server runs, separate from the parser-level oracle in
[docs/parser-escape-oracle.md](/home/sdennis/tinymux/docs/parser-escape-oracle.md).

This distinction matters because command execution can normalize or
rewrite the effective expression before it reaches the evaluator. The
user-visible result is still real and important, but it is not always a
byte-for-byte match for direct parser input.

The sanitized command-level corpus lives in
[docs/command-escape-oracle.txt](/home/sdennis/tinymux/docs/command-escape-oracle.txt).

## What Belongs Here

Only traced end-to-end observations from real commands, for example:

- `@pemit`
- `think`
- other command paths that pass through command parsing, quoting, or
  attribute expansion before expression evaluation

Each row should describe what was observed, not what the parser harness
is expected to do in isolation.

## Why This Is Separate

The parser oracle answers:

- given an effective expression string, how should the evaluator behave?

The command oracle answers:

- what does a real player see after command parsing, quoting, and eval?

Those are related questions, but they are not the same question.

Example: the traced `@pemit me='\\\%b'` runs on both 2.13 and Penn
collapsed to an observed effective expression of `\%b`, yielding `%b`.
That is valid command-level evidence, but it does not mean the direct
parser-level row for `\\\%b` should also evaluate to `%b`.

## Current Status

The currently recorded rows come from sanitized traced runs supplied by
the user from TinyMUX 2.13 and PennMUSH.

Player names, dbrefs, and other identifying command-trace details are
intentionally not preserved here.

## How To Use This

When a production AST change is proposed:

1. check the parser-level oracle for the intended effective-expression behavior
2. check the command-level oracle for the user-visible traced behavior
3. if they disagree, decide explicitly whether the fix belongs in:
   - command preprocessing
   - expression evaluation
   - or both

This prevents command-layer traces from being copied mechanically into a
parser-only test corpus.
