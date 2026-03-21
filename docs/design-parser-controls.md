# Parser Control Options

## Purpose

This document starts the next phase after the parser investigation work.

The investigation answered:

- what TinyMUX 2.13 does
- what the 2.14 AST path currently does
- what PennMUSH does
- where the confirmed divergences are

This note is about something different:

- what controls are possible
- which controls are genuinely independent
- where those controls can live in production code
- which combinations make sense as product policy

Use this document for design discussion and review of production changes.
Keep the raw evidence in:

- [docs/parser-escape-oracle.md](/home/sdennis/tinymux/docs/parser-escape-oracle.md)
- [docs/command-escape-oracle.md](/home/sdennis/tinymux/docs/command-escape-oracle.md)
- [docs/design-parser-compatibility.md](/home/sdennis/tinymux/docs/design-parser-compatibility.md)

## The Decision Is Not One-Dimensional

There is not one parser-compatibility switch here.

At minimum, there are two independent control dimensions:

1. compatibility target
2. control scope

In practice, there are at least four:

1. compatibility target
2. command layer vs evaluator layer
3. tokenizer grammar vs evaluator semantics
4. default behavior vs optional behavior

Those need to be separated or the implementation will become muddled.

## Dimension 1: Compatibility Target

This is the most important dimension.

### Option A: TinyMUX 2.13 Compatibility

Meaning:

- existing softcode should keep working the way 2.13 worked
- odd legacy behavior is preserved on purpose
- the bboard case is a regression to fix, not a behavior to redesign

Implications:

- noeval branch/body handling must match 2.13
- `\\%` interactions in noeval contexts must be allowed to delete `%`
  when 2.13 did
- command-visible weirdness is acceptable if it is legacy-correct

### Option B: Penn-Inspired Cleanup

Meaning:

- preserve `%` more often
- reduce backslash surprises where practical
- allow a more explicit substitution grammar

Implications:

- this is not 2.13 compatibility
- it must be opt-in or treated as an intentional compatibility break
- documentation must state where it differs from 2.13

### Option C: Hybrid Target

Meaning:

- keep 2.13 behavior for known compatibility-sensitive paths
- add Penn-like improvements in selected contexts

Risk:

- easy to create a system that is neither fully 2.13-compatible nor
  internally coherent
- requires very explicit boundaries

This option is viable only if the boundaries are narrow and testable.

## Dimension 2: Control Scope

Even after choosing the target, there is a separate question:
where does the control apply?

### Scope A: Evaluator Only

Meaning:

- direct expression evaluation changes
- token stream or AST handling changes
- command preprocessing is left alone

This is the smallest production scope and is the natural first target for
the current regression.

### Scope B: Command Layer Only

Meaning:

- command parsing and quoting normalize expressions differently
- evaluator semantics are left alone

This is relevant only when command-layer traces differ from parser-level
behavior for reasons outside the evaluator.

### Scope C: Both Layers

Meaning:

- evaluator compatibility plus command-path normalization rules

This may eventually be necessary, but it is too broad for the first
production fix unless the oracles prove the regression cannot be solved
in the evaluator alone.

## Dimension 3: Tokenizer Grammar vs Evaluator Semantics

The confirmed divergences do not all belong at the same layer.

### Tokenizer-Controlled Differences

Examples:

- Penn `% ` as an atomic substitution
- Penn `%iL`
- Penn `%w*`
- MUX `%c`/`%x` color forms

These are grammar decisions. They belong in the scanner/tokenizer rules.

### Evaluator-Controlled Differences

Examples:

- `%r` newline semantics
- unknown `%` fallback behavior
- noeval branch/body replay policy
- 2.13-style unconditional backslash consumption during noeval passes

These belong in evaluation policy, not token recognition.

### Mixed Cases

Examples:

- `\\%` plus noeval branch selection
- command-layer quoting that changes the effective expression before eval

These need explicit care because they span layers.

## Dimension 4: Default vs Optional Behavior

This is product policy, not just parser design.

### Default Behavior

Questions:

- Should stock 2.14 aim to behave like 2.13 by default?
- Or should stock 2.14 preserve the current AST behavior where known?

For legacy TinyMUX compatibility, the defensible answer is:

- default should move toward 2.13 compatibility

because that is the regression-fix target.

### Optional Behavior

If a saner backslash mode exists, it should be:

- explicitly named
- opt-in
- documented as non-2.13
- tested separately from the compatibility path

It should not be hidden inside the same control as the 2.13 fix.

## Concrete Control Surfaces

The main implementation choices appear to be:

### Surface A: One Hardcoded 2.13-Compatible Evaluator Policy

Meaning:

- change `ast.cpp` so AST evaluation simply matches 2.13 behavior in the
  relevant noeval paths
- do not add a user-visible mode yet

Pros:

- smallest surface area
- directly fixes the regression
- easiest to reason about

Cons:

- does not create room for optional cleanup behavior
- some current 2.14 AST behavior would intentionally change

### Surface B: Internal Evaluator Policy Flags

Meaning:

- add internal control bits for behaviors such as:
  - noeval backslash replay
  - Penn-style percent grammar
  - percent-space handling

Pros:

- cleaner internal factoring
- makes later modes easier to implement

Cons:

- easy to over-engineer before the production need is clear
- adds abstraction before the first production fix lands

### Surface C: User-Visible Parser/Profile Modes

Meaning:

- explicit production profiles such as:
  - `mux213_compat`
  - `mux214_ast`
  - `penn_like`

Pros:

- explicit semantics
- easier to document long term

Cons:

- larger product/design decision
- configuration and migration burden
- likely too large for the immediate fix

### Surface D: Per-Function Noeval Compatibility Policy

Meaning:

- only `if`/`switch`/`case`/`iter` selected-body handling is made
  2.13-compatible

Pros:

- matches the currently confirmed regression zone
- smallest blast radius

Cons:

- can become ad hoc if more divergences appear elsewhere
- must be documented as a deliberate scope choice

## Recommended Staging

The safest path looks like this:

### Stage 1: Fix the Regression Narrowly

Goal:

- make AST evaluator noeval branch/body behavior match 2.13 for the
  confirmed rows in the parser oracle

Scope:

- evaluator only
- no new user-visible mode yet
- focus on `if`/`switch`/`case`/`iter`

This is a regression fix.

Acceptance criterion:

- once `ast.cpp` is patched, the same confirmed rows from the parser
  oracle should be verifiable against a running 2.14 server, not only in
  the study tool
- if production behavior and the oracle disagree, either the production
  patch is wrong or the oracle needs to be refined explicitly

### Stage 2: Refactor Into Named Internal Controls

Goal:

- after the regression fix works, isolate the behavior behind coherent
  internal policy boundaries

This is where it becomes reasonable to separate:

- tokenizer grammar policy
- evaluator substitution policy
- noeval replay policy

### Stage 3: Decide on Optional Non-2.13 Behavior

Goal:

- if desired, define an explicit opt-in mode for saner backslash behavior

This must be treated as a product feature, not as part of the regression
fix.

## Current Recommendation

The current recommendation is:

1. treat 2.13 compatibility as the immediate and unconditional default target
2. fix it in the evaluator first, not the command layer
3. scope the first production patch narrowly to confirmed noeval divergences
4. add internal policy abstractions only after the regression fix lands
5. keep Penn-inspired cleanup separate, explicitly optional, and future-facing

That gives the reviewer and future implementation work a clear bar:

- do not solve the regression by quietly changing the target semantics
- do not bundle optional cleanup into the compatibility fix
- do not blur command preprocessing evidence with parser semantics

## Open Questions

These are the design questions still worth arguing about:

1. Does any confirmed production divergence outside selected noeval branch/body evaluation belong in the first patch, or should Stage 1 remain strictly limited to the pinned oracle rows?
2. What is the cleanest way to verify production `ast.cpp` against the parser oracle rows on a running 2.14 server?
3. When Stage 2 begins, which internal control boundaries are worth naming first: tokenizer grammar, evaluator substitution policy, or noeval replay policy?
4. If a non-2.13 cleanup mode ever exists, what exact semantics would it promise beyond "more Penn-like"?
5. If an optional mode ever exists, can it remain a single global setting and avoid any finer-grained scope?
