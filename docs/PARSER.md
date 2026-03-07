# TinyMUX Parser Architecture

## Status: Reference Document (Study Complete)

This document records the study of `mux_exec`'s stream-transformer
architecture. The findings informed the AST evaluator design in
[PARSER_REPLACE.md](PARSER_REPLACE.md). All stages through Phase 2
are now implemented on the `brazil` branch.

## Overview

TinyMUX has three parsers (as noted in the source):

1. **Expression evaluator** (`eval.cpp`) — `mux_exec()`, `parse_to()`, `parse_to_lite()`
2. **Command parser** (`command.cpp`) — command dispatch, switch decoding
3. **Lock evaluator** (`boolexp.cpp`) — boolean expression parser for locks

The goal of this project is to study the expression evaluator with an eye toward
producing an AST (Abstract Syntax Tree) from MUX softcode. If feasible, we would
write a Ragel -G2 scanner/lexer with a recursive-descent parser that behaves
identically to the existing evaluator.

## Current Parser Architecture

### Three Layers

```
Input string (attribute value)
    |
    v
+-- command.cpp ----------------------------------------+
|  Split on ';' (command separator)                     |
|  Split on '=' (command/argument separator)            |
|  Identify command name, switches                      |
|  Route to handler                                     |
+-------------------------------------------------------+
    |
    v
+-- eval.cpp -------------------------------------------+
|  mux_exec(): The expression evaluator                 |
|    - %-substitutions (%0-%9, %q0-%qz, %q<name>, etc.) |
|    - []-evaluation brackets (recursive mux_exec)      |
|    - function(args) recognition and dispatch          |
|    - {}-grouping (deferred evaluation)                |
|    - Space compression                                |
|    - Escape handling (%, \)                            |
|  parse_to(): Split at delimiter, respecting nesting   |
|  parse_to_lite(): Non-destructive variant             |
+-------------------------------------------------------+
    |
    v
+-- boolexp.cpp ----------------------------------------+
|  Lock expressions: TYPE^THING|+DARK/!#1234            |
|  Separate grammar, not relevant to this project       |
+-------------------------------------------------------+
```

### Key Data Structures

#### isSpecial Tables

Four lookup tables (`L1` through `L4`) classify characters by role:

- **L1** (mux_exec main loop): NUL, SP, %, (, [, \, {
- **L2** (%-substitution dispatch): 0-9, Q, R, S, B, T, L, N, C, etc.
- **L3** (parse_to nesting): tracks delimiters, brackets, braces, escapes
- **L4** (brace-interior): %, \, {, }

The tables are **mutable at runtime** — `parse_to` temporarily overwrites
entries to mark client-specific delimiters (code 8), then restores them.
This means the parser is **not reentrant** without saving/restoring state,
which `mux_exec` already does (lines 1329-1338).

#### Nesting via parse_to

`parse_to()` maintains an explicit stack (depth 32) tracking `[`/`]` and
`(`/`)` nesting. Curly braces `{}` are handled with a separate counter
(`bracketlev`) because they represent deferred evaluation blocks, not
expression nesting.

### mux_exec Flow

```
mux_exec(pStr, nStr, buff, bufc, executor, caller, enactor, eval, cargs, ncargs)
    |
    +-- mundane characters: bulk copy
    |
    +-- '(' : function call
    |     +-- look backwards in output buffer for function name
    |     +-- lookup in builtin_functions / ufunc_htab
    |     +-- parse_arglist_lite() to get arguments
    |     +-- recursive mux_exec for each argument (unless FN_NOEVAL)
    |     +-- call function handler
    |
    +-- '%' : substitution
    |     +-- L2 table dispatch on next character
    |     +-- %0-%9: command arguments
    |     +-- %q0-%qz, %q<name>: registers
    |     +-- %r, %t, %b: special characters
    |     +-- %s, %o, %p, %a: pronouns
    |     +-- %#, %!, %@, %l, %c, %n: executor/enactor/etc.
    |     +-- %%: literal %
    |
    +-- '[' : evaluation bracket
    |     +-- parse_to_lite() to find matching ']'
    |     +-- recursive mux_exec on contents
    |
    +-- '{' : deferred evaluation
    |     +-- count brace nesting
    |     +-- copy contents without evaluating (if EV_STRIP_CURLY, strip braces)
    |
    +-- ' ' : space compression (if enabled)
    |
    +-- '\' : escape next character
    |
    +-- '\0' : end of string
```

### Key Observations

1. **Not a traditional parser.** There is no separate lexer phase. `mux_exec`
   interleaves scanning and evaluation in a single pass, writing output
   directly to a buffer. It's a **stream transformer**, not a parser that
   builds a tree.

2. **Function names are recognized backwards.** When `(` is encountered,
   `mux_exec` looks backwards in the *output buffer* to find the function
   name. This means function name recognition depends on what has already
   been evaluated and written. Example: `[setq(0,add)]%q0(1,2)` — the
   function name is determined at runtime.

3. **Context-dependent grammar.** The `eval` flags change what characters
   are special. `EV_FCHECK` enables `(` recognition. `EV_NOFCHECK` disables
   `[`. `EV_STRIP_CURLY` changes `{}` handling. `EV_EVAL` gates `%`
   substitutions. The "grammar" shifts mid-parse.

4. **Destructive parsing.** `parse_to()` modifies the input string in place
   (inserting NULs). `parse_to_lite()` was added later as a non-destructive
   alternative used by `parse_arglist_lite()`.

5. **Global mutable state.** The `isSpecial_L3` table is mutated during
   parsing to handle delimiter overrides. Parser state is saved/restored
   on reentry.

6. **Output-buffer-is-the-state.** There's no intermediate representation.
   The output buffer *is* the accumulator. Function args are allocated,
   evaluated into separate buffers, passed to handlers, and freed.

### Action List Processing (cque.cpp)

Before `mux_exec` or `process_command` sees anything, the command queue
processor in `cque.cpp` splits action lists on `;` using `parse_to()`:

```
"@pemit %#=Hello;think Done"
    |
    parse_to(&command, ';', EV_STRIP_AROUND)
    |
    +-- "@pemit %#=Hello"  --> process_command()
    +-- "think Done"       --> process_command()
```

The pipe operator `|` is also handled here — if the next command after `;`
starts with `|`, the output of the current command is piped via `%|`.

### process_command Flow (command.cpp)

```
process_command(executor, caller, enactor, eval, interactive, command, args, nargs)
    |
    +-- strip leading whitespace
    +-- space compress (if configured)
    +-- check single-char prefix commands (", :, ;, etc.)
    +-- check HOME, GOTO
    +-- lowercase the command, look up in command hash table
    +-- split on first unescaped '=' for command arg / right-side arg
    +-- decode /switches
    +-- process_cmdent() to execute the command handler
    +-- if no match: try exits, $-commands, global $-commands
```

### Complete %-Substitution Reference (L2 Table)

The L2 table encodes both the substitution type (low 6 bits) and
a capitalization flag (bit 7, 0x80). When bit 7 is set, the first
character of the result is uppercased.

```
Code  Char  Meaning
----  ----  --------------------------------------------------
 0    (other) Literal copy of the escaped character
 1    0-9   Command argument %0 through %9
 2    Q/q   Register: %q0-%qz or %q<name>
 3    #     Enactor dbref (%#)
 4    !     Executor dbref (%!)
 5    R/r   Carriage return (%r → \r\n)
 6    C/c   Color code (%cn, %ch, %c<rgb>)
      X/x   Color code (alias for %c)
 7    B/b   Blank/space (%b)
 8    T/t   Tab (%t)
 9    L/l   Enactor location (%l)
10    V/v   Variable attribute (%va-%vz)
11    %     Literal percent (%%)
12    N/n   Enactor name (%n/%N)
13    |     Pipe output (%|)
14    S/s   Subjective pronoun (%s/%S → he/she/it/they)
15    P/p   Possessive pronoun (%p/%P → his/her/its/their)
16    O/o   Objective pronoun (%o/%O → him/her/it/them)
17    A/a   Absolute possessive (%a/%A → his/hers/its/theirs)
18    \0    End of string (stop parsing)
19    M/m   Last command (%m)
20    @     Caller dbref (%@)
21    =     Attribute shorthand (%=<attr>, like v(attr))
              Also extended args: %=<0> through %=<nnn>
22    +     Argument count (%+)
23    K/k   Moniker (%k — colored name)
24    I/i   Iterator text (%i0, %i1, etc.)
25    :     Enactor objid (%: → #dbref:ctime)
```

Uppercase variants (A vs a, N vs n, S vs s, etc.) have bit 7 set
(0x80) in L2, which triggers `mux_toupper_first()` on the result.
This means `%N` gives "Stephen" while `%n` gives "stephen".

### Eval Flags Reference

```
EV_EVAL         0x0004   Evaluate %-substitutions
EV_STRIP_CURLY  0x0008   Strip outer {} from args
EV_FCHECK       0x0010   Check for function calls on (
EV_FMAND        0x0020   Require valid function name (else #-1 error)
EV_NOFCHECK     0x0040   Suppress [ evaluation
EV_NO_COMPRESS  0x0080   Don't compress spaces
EV_NO_LOCATION  0x0100   Don't allow %l
EV_NOTRACE      0x0200   Don't trace
EV_TRACE        0x0400   Force tracing
EV_TOP          0x0800   Top-level evaluation
EV_STRIP_LS     0x1000   Strip leading spaces
EV_STRIP_TS     0x2000   Strip trailing spaces
EV_STRIP_AROUND 0x4000   Strip surrounding {} if present
```

Key combinations:
- Inside `[]`: `eval | EV_FCHECK | EV_FMAND` — functions mandatory
- Inside `{}` with EV_EVAL: `eval & ~(EV_STRIP_CURLY|EV_FCHECK|EV_FMAND)` — no function check
- Inside `{}` without EV_EVAL: `eval & ~EV_FMAND` — just copy
- Function arguments: `eval & ~(EV_TOP|EV_FMAND)` or without EV_EVAL for FN_NOEVAL

## Challenges for AST Generation

### The Backwards Function Name Problem

The biggest obstacle to traditional parsing. In:
```
think add(1,2)
```
A traditional parser sees `add` as a token, then `(` as the start of an
arglist. But in:
```
think [setq(0,add)]%q0(1,2)
```
The function name `add` doesn't exist in the source text — it's constructed
at runtime from a register. A static AST cannot represent this without a
"dynamic function call" node.

### The Eval Flags Problem

The same source text can parse differently depending on `eval` flags passed
to `mux_exec`. For example, `{foo}` might be stripped to `foo` or preserved
as `{foo}` depending on `EV_STRIP_CURLY`. An AST would need to capture
these as annotations or generate different trees per context.

### The Space Compression Problem

When `space_compress` is enabled, multiple spaces are collapsed to one, and
leading/trailing spaces are stripped. This affects the *output* but not the
parse structure. An AST could ignore this (handle it at emission time).

## Possible Approaches

### Approach A: Static AST with Dynamic Nodes

Parse what we can statically, and use "dynamic call" nodes for cases where
the function name is computed at runtime.

```
AST for "think add(1,2)":
  Command("think")
    FunctionCall("add")
      Literal("1")
      Literal("2")

AST for "think [setq(0,add)]%q0(1,2)":
  Command("think")
    EvalBracket
      FunctionCall("setq")
        Literal("0")
        Literal("add")
    DynamicCall(Register("q0"))
      Literal("1")
      Literal("2")
```

### Approach B: Tokenize Only, Interpret the Token Stream

Don't try to build a full AST. Instead, produce a token stream that can
be interpreted faster than re-scanning the source text each time.

```
Tokens: LITERAL("think ") FUNC("add") ARGSTART LITERAL("1") ARGSEP LITERAL("2") ARGEND
```

This is closer to what the current parser does, just with the scanning
factored out.

### Approach C: Ragel Scanner + Recursive Descent

Use Ragel to generate a fast DFA-based scanner that identifies:
- Literal runs
- %-substitution sequences
- Function names followed by `(`
- `[` and `]` eval brackets
- `{` and `}` grouping
- Delimiters (`,`, `;`, `=`)

Feed the token stream to a recursive-descent parser that builds AST nodes.
The scanner handles the character-level complexity; the parser handles
nesting and structure.

This is the most promising approach for eventually compiling to RISC-V.

## Staged Plan

### Stage 0: Study and Document (this document)
- Map the current parser behavior completely
- Document edge cases and quirks
- Build a test corpus of interesting expressions

### Stage 1: Standalone Scanner Prototype
- Work in `./parser/`
- Ragel -G2 scanner for MUX expressions
- CLI tool that reads expressions and emits tokens
- Verify against the existing parser using smoke test expressions

### Stage 2: Recursive-Descent Parser
- Build AST nodes from the token stream
- Handle static function calls, %-substitutions, eval brackets, grouping
- "Dynamic call" nodes for computed function names

### Stage 3: AST Interpreter
- Walk the AST and evaluate it
- Should produce identical output to `mux_exec` for all test cases
- Performance comparison

### Stage 4: Integration Study
- Can the AST interpreter replace `mux_exec`?
- What's the interface boundary?
- How do function handlers interact with the AST?

### Stage 5: Compilation Target (future, possibly 2.14+)
- AST to RISC-V IR
- Run under sandboxed DBT (~/riscv or ~/slow-32)
- JIT evaluation of softcode

## Test Corpus

Interesting expressions to test against:
```
think hello world
think add(1,2)
think [add(1,mul(2,3))]
think %0 %1 %q0 %q<foo>
think {literal [not evaluated]}
think [setq(0,add)]%q0(1,2)
@if eq(1,1)={think yes},{think no}
@dolist a b c={think ##-#@}
think [iter(a b c,[strlen(##)])]
think [switch(%0,a,alpha,b,beta,*,other)]
```
