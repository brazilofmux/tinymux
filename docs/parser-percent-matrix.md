# Parser Percent-Substitution Semantic Matrix

This document enumerates every `%` substitution form across TinyMUX 2.13,
TinyMUX 2.14 (AST), and PennMUSH.  Each row classifies the divergence
type so that production compatibility work can target the right layer.

## Legend

- **Token**: divergence is in the scanner / tokenizer
- **Eval**: same tokens, different evaluation semantics
- **Stream**: behavior depends on multi-pass streaming interaction
- **=**: engines agree
- `(lit)`: unknown-fallback — outputs the character after `%` literally

## 1. Percent Substitution Forms

| Form | 2.13 | 2.14 AST | PennMUSH | Divergence | Type |
|------|------|----------|----------|------------|------|
| `%0`–`%9` | arg N | arg N | arg N | = | — |
| `%%` | `%` (iCode 11) | `%` (explicit) | `%` (explicit) | = | — |
| `%b` / `%B` | space (iCode 7) | space | space | = | — |
| `%r` / `%R` | `\r\n` (iCode 5) | `\r\n` | `\n` | Penn uses LF only | Eval |
| `%t` / `%T` | tab (iCode 8) | tab | tab | = | — |
| `%#` | enactor dbref (iCode 3) | enactor dbref | enactor dbref | = | — |
| `%!` | executor dbref (iCode 4) | executor dbref | executor dbref | = | — |
| `%@` | caller dbref (iCode 20) | caller dbref | caller dbref | = | — |
| `%n` / `%N` | enactor name (iCode 12) | enactor name | enactor name | = | — |
| `%s` / `%S` | subj pronoun (iCode 14) | subj pronoun | subj pronoun | = | — |
| `%p` / `%P` | poss pronoun (iCode 15) | poss pronoun | poss pronoun | = | — |
| `%o` / `%O` | obj pronoun (iCode 16) | obj pronoun | obj pronoun | = | — |
| `%a` / `%A` | abs poss pronoun (iCode 17) | abs poss pronoun | abs poss pronoun | = | — |
| `%k` / `%K` | moniker (iCode 23) | moniker | moniker | = | — |
| `%l` / `%L` | enactor location (iCode 9) | enactor location | enactor location | = | — |
| `%m` / `%M` | last command (iCode 19) | last command | — | Penn has `%c`/`%u` instead | Eval |
| `%q0`–`%qz` | register (iCode 2) | register | register | = | — |
| `%q<name>` | named register | named register | named register | = | — |
| `%va`–`%vz` | var attr (iCode 10) | var attr | var attr | = | — |
| `%cx`/`%xx` | fg color (iCode 6) | fg color | — | Penn uses `%x`/`%X` for attrs | Token |
| `%Cx`/`%Xx` | bg color (iCode 6+0x40) | bg color | — | Penn uses `%X` for attrs | Token |
| `%c<rgb>`/`%x<rgb>` | extended color | extended color | — | Not in Penn | Token |
| `%i0`–`%i9` | loop itext (iCode 24) | loop itext | loop itext | = | — |
| `%=` | `=` literal (iCode 21) | `=` literal | attr name | Penn: current attr name | Eval |
| `%=<attr>` | attr value (iCode 21) | attr value | — | Not in Penn | Token |
| `%=<N>` | extended arg (iCode 21) | extended arg | — | Not in Penn | Token |
| `%+` | arg count (iCode 22) | arg count | arg count | = | — |
| `%:` | enactor objid (iCode 25) | enactor objid | enactor unique id | = | — |
| `%\|` | piped output (iCode 13) | piped output | — | Not in Penn | Eval |
| `% ` (pct-space) | (lit) → space | (lit) → space | `% ` (two chars) | **Key divergence** | Token+Eval |
| `%c` / `%C` | color dispatch | color dispatch | raw command line | **Collision** | Token+Eval |
| `%u` / `%U` | — | — | evaled command | Penn-only | Eval |
| `%~` | — | — | accented name | Penn-only | Eval |
| `%?` | — | — | invoc/recur limits | Penn-only | Eval |
| `%$0`–`%$9` | — | — | stack var | Penn-only | Eval |
| `%$L` | — | — | stack current | Penn-only | Eval |
| `%iL` / `%IL` | — | — | iter current level | Penn-only | Eval |
| `%wa`–`%wz` | — | — | W-attr on executor | Penn-only | Token |
| `%xa`–`%xz` | — | — | X-attr on executor | Penn overlaps MUX color | Token |
| unknown `%X` | (lit) → `X` | (lit) → `X` | (lit) → `X` | = | — |

### Uppercase Case-Modification

All three engines apply first-character uppercasing when the substitution
letter is uppercase.  The set of letters with this behavior:

| Letter | 2.13 (0x80 flag) | 2.14 | Penn |
|--------|-------------------|------|------|
| `A` | yes | yes | yes |
| `K` | yes | yes | yes |
| `M` | yes | yes | — |
| `N` | yes | yes | yes |
| `O` | yes | yes | yes |
| `P` | yes | yes | yes |
| `Q` | yes | yes | yes |
| `S` | yes | yes | yes |
| `V` | yes | yes | — |

### Hash Forms

| Form | 2.13 | 2.14 AST | PennMUSH | Notes |
|------|------|----------|----------|-------|
| `##` | loop bound var | loop bound var | — | Same as `%i0` |
| `#@` | loop position | loop position | — | Same as `inum()` |
| `#$` | switch match | switch match | — | `mudstate.switch_token` |

## 2. Backslash Escape Behavior

| Sequence | 2.13 (streaming) | 2.14 (AST) | PennMUSH | Divergence | Type |
|----------|-------------------|-------------|----------|------------|------|
| `\X` | consume `\`, emit `X` | ESC node → `X` | consume `\`, emit `X` | = | — |
| `\%` | consume `\`, emit `%` | ESC node → `%` | consume `\`, emit `%` | = | — |
| `\\` | consume `\`, emit `\` | ESC node → `\` | consume `\`, emit `\` | = | — |
| `\` (at EOL) | back up, no consume | ESC node → `\` | exit sequence | Minor | Eval |
| `\\%X` | `\` then `%X` substituted | ESC(`\\`)→`\` + SUBST(`%X`)→*expanded* | `\` then `%X` substituted | = | — |

### The `\\%X` Case: Where Engines Agree

For `\\%X` where `%X` is a recognized form:

| Step | 2.13 streaming | 2.14 AST | PennMUSH |
|------|----------------|----------|----------|
| 1 | `\` escapes `\` → `\` | ESC(`\\`) → `\` | `\` escapes `\` → `\` |
| 2 | `%X` → substitution | SUBST(`%X`) → *result* | `%X` → substitution |
| Result | `\` + *subst* | `\` + *subst* | `\` + *subst* |

All three agree when `%X` is recognized (like `%b`, `%#`, `%n`).

### The `\%X` Case: Single Backslash Before Percent

For `\%b` (backslash-percent-b):

| Step | 2.13 streaming | 2.14 AST | PennMUSH |
|------|----------------|----------|----------|
| 1 | `\` escapes `%` → `%` | ESC(`\%`) → `%` | `\` escapes `%` → `%` |
| 2 | `b` is literal | `b` is literal | `b` is literal |
| Result | `%b` | `%b` | `%b` |

All three agree on `\%b` → `%b`.

### The Real-World Break: Confirmed via Debugger

Test cases run against TinyMUX 2.13.0.12 BETA:

```
> think \\% capacity
\ capacity

> think [switch(1,1,{\\% capacity})]
% capacity
```

The divergence requires **brace groups inside FN_NOEVAL functions**.
Plain `\\%` outside braces gives `\ capacity` on all engines.

### Root Cause: Unconditional Backslash Consumption

In 2.13's `mux_exec()`, the backslash handler (eval.cpp line 2439) has
**no `EV_EVAL` guard**.  It runs on every pass — eval or noeval:

```c
else  // if (pStr[iStr] == '\\')
{
    at_space = 0;
    iStr++;
    if (pStr[iStr])
    {
        *(*bufc)++ = pStr[iStr];  // unconditional
    }
}
```

Compare with the percent handler (line 1686), which IS guarded:

```c
if (!(eval & EV_EVAL))
{
    // Copy % and following char literally
}
```

This asymmetry means **NOEVAL does not mean "don't touch the text."**
It means "don't substitute percents."  Backslash escaping still runs.

### Two-Pass Trace Through `switch(1,1,{\\% capacity})`

**Pass 1 — FN_NOEVAL argument collection** (EV_EVAL stripped):

`switch` is FN_NOEVAL.  `mux_exec()` extracts arguments with
`feval = eval & ~(EV_EVAL|EV_TOP|EV_FMAND|EV_STRIP_CURLY)`.

For arg 2 (`{\\% capacity}`), the brace contents go through
`mux_exec` without EV_EVAL.  The backslash handler fires anyway:

| Input | Handler | Guard? | Output |
|-------|---------|--------|--------|
| `\\` (first `\`) | backslash (2439) | **none** | consume `\`, emit `\` |
| `% ` | percent (1686) | `EV_EVAL` off | copy `%` and ` ` literally |

Result in `fargs[2]`: `{\% capacity}` — **one backslash, not two**.

**Pass 2 — `fun_switch` evaluates winning branch** (EV_EVAL set):

`fun_switch()` calls `mux_exec(fargs[2], ..., EV_STRIP_CURLY|EV_EVAL)`.
Braces are stripped, contents `\% capacity` are evaluated:

| Input | Handler | Guard? | Output |
|-------|---------|--------|--------|
| `\%` (`\`) | backslash (2439) | **none** | consume `\`, emit `%` |
| ` capacity` | literal | — | copy |

Result: **`% capacity`**

### Why 2.14 AST Gets This Wrong

The Ragel scanner tokenizes the original text once, greedily:

- `\\` → `ASTTOK_ESC("\\")` (backslash + any)
- `% ` → `ASTTOK_PCT("% ")` (percent + any)

These become independent AST nodes.  There is no first pass to strip
a layer of backslash.  When the winning branch is evaluated:

- ESC(`\\`) → `\`
- SUBST(`% `) → ` ` (unknown fallback)

Result: **`\ capacity`** — missing the backslash-consumption pass.

### The Fix Specification

For brace-group arguments to FN_NOEVAL functions, the 2.14 AST
evaluator must replicate 2.13's unconditional backslash consumption.
Specifically, ESC nodes inside brace groups that are processed in a
noeval context need one layer of backslash stripping before the
branch is evaluated.

This is not a tokenizer fix.  The tokenizer is correct to produce
independent ESC and SUBST nodes.  The fix belongs in the evaluator's
handling of brace-group children of FN_NOEVAL function calls.

## 3. Divergence Classification

### Tokenization-Time Divergences

These can be resolved by profile-aware tokenizer rules:

| Issue | Description |
|-------|-------------|
| `% ` recognition | Penn: atomic `% ` token. MUX: `%` + unknown → literal. |
| `%c`/`%x` collision | MUX: color codes. Penn: `%c` = raw command, `%x` = X-attr. |
| `%w` forms | Penn-only: W-attribute substitution. |
| Hash forms `##`/`#@`/`#$` | MUX-only. Not in Penn. |

### Evaluation-Time Divergences

These can be resolved by profile switches in the evaluator:

| Issue | Description |
|-------|-------------|
| `%r` newline | MUX: `\r\n`. Penn: `\n` only. |
| `%m` / `%c`/`%u` | MUX: `%m` = last command. Penn: `%c` = raw cmd, `%u` = evaled cmd. |
| `%=` meaning | MUX: literal `=` or attr lookup. Penn: current attr name. |
| Penn-only forms | `%~`, `%?`, `%$`, `%iL`, `%u`, `%w` attrs. |

### Multi-Pass Backslash Divergence (CONFIRMED)

This is the critical divergence, confirmed via debugger trace:

| Issue | Description |
|-------|-------------|
| Unconditional `\` in noeval | 2.13's backslash handler runs with no `EV_EVAL` guard. Each pass through `mux_exec` strips one layer of backslash. Brace groups in FN_NOEVAL functions go through two passes, so `\\` becomes `\` then `%` is literalized. 2.14 AST tokenizes once, so `\\` is a single ESC node evaluated once. |

## 4. Production Strategy

Two distinct goals, in priority order:

### Goal 1: Correctness — Parse bboard code like 2.13 does

This is a **regression fix**, not a feature.  Root cause confirmed
via debugger trace of 2.13.0.12.

The fix is in the AST evaluator's handling of **brace-group arguments
to FN_NOEVAL functions**.  When the evaluator processes a brace group
in a noeval context, it must walk the AST and strip one layer of
backslash escaping from ESC nodes before storing the argument text.
This replicates 2.13's unconditional backslash handler.

Specifically:
- `ESC("\\")` (esc-backslash) → becomes `ESC("\")` or raw `\`
- `ESC("\X")` for any X → becomes raw `X`
- SUBST, LIT, SPACE nodes → copied literally (percent handler
  IS guarded by EV_EVAL, so no substitution in noeval)

The tokenizer is correct.  The scanner is correct.  The fix is
purely in evaluation semantics for noeval brace-group processing.

### Goal 2: Configurable backslash reduction

A separate, opt-in mode that reduces the backslash burden:

- Recognize `% ` as an atomic substitution (Penn-style), so softcode
  authors can write `50% capacity` without escaping.
- Potentially other Penn-inspired simplifications.
- **Default: off** — existing games keep current behavior.
- Games can opt in when they're ready to modernize their softcode.

This is independent of Goal 1.  Goal 1 is about matching 2.13
behavior exactly.  Goal 2 is about offering a better future.

### Implementation layers

| Layer | Goal 1 (correctness) | Goal 2 (backslash reduction) |
|-------|---------------------|------------------------------|
| Tokenizer | No change needed | `% ` recognition flag |
| Evaluator | Noeval brace-group: strip one backslash layer from ESC nodes | `% ` → `"% "` expansion |
| Config | None (always on — matches 2.13) | `parser_profile` config option |
