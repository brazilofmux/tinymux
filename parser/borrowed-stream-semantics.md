# Borrowed Stream Semantics

This note extracts the smallest useful parts of the legacy streaming
parsers for study.

The goal is not to port TinyMUX 2.13 or PennMUSH wholesale. The goal is
to keep only the mechanisms that explain why `%` and `\` behave
correctly there and incorrectly in the current AST pipeline.

## Scope

Only these mechanisms matter for the current compatibility problem:

1. How function arguments are collected for deferred-eval functions.
2. What `%` does when evaluation is off.
3. What `\` does when evaluation is off.
4. How a deferred argument is evaluated again later.

Everything else is noise for this specific study.

## TinyMUX 2.13: Minimal Mechanism

Source anchors:

- `mux2.13_12/src/eval.cpp:1528`
- `mux2.13_12/src/eval.cpp:1680`
- `mux2.13_12/src/eval.cpp:2438`

### 1. FN_NOEVAL argument collection changes flags

For `FN_NOEVAL` functions, 2.13 clears `EV_EVAL`, `EV_TOP`,
`EV_FMAND`, and `EV_STRIP_CURLY` before parsing the argument list:

```c
if (fp && (fp->flags & FN_NOEVAL)) {
    feval = eval & ~(EV_EVAL|EV_TOP|EV_FMAND|EV_STRIP_CURLY);
} else {
    feval = eval & ~(EV_TOP|EV_FMAND);
}

tstr = parse_arglist_lite(..., feval, fargs, ...);
```

That means the argument text is still scanned by `mux_exec()`, but with
percent evaluation disabled.

### 2. Percent handling is gated by `EV_EVAL`

In the `%` branch:

```c
if (!(eval & EV_EVAL)) {
    *(*bufc)++ = '%';
    iStr++;
    *(*bufc)++ = pStr[iStr];
} else {
    // full substitution dispatch
}
```

So in noeval collection, `%` is copied literally.

### 3. Backslash handling is not gated by `EV_EVAL`

In the `\` branch:

```c
iStr++;
if (pStr[iStr]) {
    *(*bufc)++ = pStr[iStr];
} else {
    iStr--;
}
```

There is no `EV_EVAL` check here. Every pass through `mux_exec()`
consumes one backslash layer.

### 4. Deferred branch is evaluated again later

The stored argument text is later passed back into `mux_exec()` with
`EV_EVAL` restored by the function implementation. That creates the
critical multi-pass behavior:

- pass 1: `\\% capacity` -> `\% capacity`
- pass 2: `\% capacity` -> `% capacity`

## PennMUSH: Matching Minimal Mechanism

Source anchors:

- `pennmush/src/parse.c:2355`
- `pennmush/src/parse.c:2848`
- `pennmush/src/parse.c:3106`

PennMUSH preserves the same structural asymmetry.

### 1. Deferred parsing still calls the same streaming parser

Function arguments are collected by recursively calling
`process_expression()` with adjusted flags. The parser is still live
while argument text is being collected.

### 2. Percent handling is gated by `PE_EVALUATE`

```c
if (!(eflags & PE_EVALUATE)) {
    safe_chr('%', buff, bp);
    (*str)++;
    savec = **str;
    safe_chr(savec, buff, bp);
    (*str)++;
    ...
}
```

So `%` passes through literally in noeval collection.

### 3. Backslash handling is not gated by `PE_EVALUATE`

```c
if (eflags & PE_LITERAL) {
    safe_chr('\\', buff, bp);
    (*str)++;
    break;
}
if (!(eflags & PE_EVALUATE))
    safe_chr('\\', buff, bp);
(*str)++;
if (!**str)
    goto exit_sequence;
/* FALL THROUGH */
default:
    safe_chr(**str, buff, bp);
    (*str)++;
```

Penn differs in exact literal-mode behavior and `% ` grammar, but it
still keeps scan-time evaluation state live and still couples
backslash-consumption to the character stream rather than to a prior
tokenization pass.

## What To Borrow

Borrow these ideas, not these codebases:

- Deferred-eval argument collection is still a scan pass.
- `%` semantics depend on eval state at the moment characters are read.
- `\` stripping happens per pass, not per final output.
- A later re-eval pass must be able to see adjacency that was preserved
  through the earlier pass.

## What Not To Borrow

Do not pull in:

- the full substitution tables
- permission checks
- function dispatch
- recursion/accounting machinery
- buffer management details
- command parser behavior outside expression scanning

Those are engine concerns, not the minimal parser-semantics study.

## Immediate Design Pressure On 2.14 AST

The current AST/token model commits too early:

- `\\` becomes one escape token
- `% ` or `%x` becomes one substitution token

After that, later passes no longer have the original stream adjacency
that 2.13 and Penn relied on during deferred evaluation.

That is the specific behavior any compatibility design must restore,
whether by:

- a narrow deferred-pass streaming layer, or
- a broader split-lexer design

This note is the reduced reference for that work.
