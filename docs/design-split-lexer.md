# Split Lexer Design: Deferred Backslash/Percent Semantics

Status: superseded as the primary implementation direction.

The production tree now follows the parser-controlled lexer-mode design
in [docs/design-parser-controlled-lexer.md](/home/sdennis/tinymux/docs/design-parser-controlled-lexer.md).
This note remains useful as a record of the rejected alternative and as
an explanation of why pure up-front tokenization was insufficient.

## Problem

The 2.14 AST parser tokenizes backslash escapes (`\\`) and percent
substitutions (`%X`) as compound tokens during a single up-front
scanning pass.  The scanner knows nothing about evaluation context
(EVAL vs NOEVAL).

In 2.13 (and PennMUSH), the scanner and evaluator are the same
character-at-a-time loop.  The NOEVAL flag is live during scanning
and affects how characters are consumed:

- **Backslash handler**: runs unconditionally (no EV_EVAL guard).
  `\\` consumes one `\`, outputs `\`.
- **Percent handler**: guarded by EV_EVAL.  In NOEVAL, `%X` passes
  through literally.  In EVAL, `%X` is dispatched as a substitution.

This means that in a NOEVAL context, `\\% capacity`:

1. `\\` strips to `\` (backslash handler fires)
2. `% ` passes through as `% ` (percent handler skipped)
3. Result after NOEVAL pass: `\% capacity`
4. On re-evaluation: `\%` is a single escape unit, outputs `%`
5. Final result: `% capacity`

The 2.14 AST scanner produces `ESC("\\")` and `SUBST("% ")` as
independent tokens before any evaluation state is known.  No amount
of tree-walking can reunite them into the `ESC("\%")` that the
re-evaluation pass needs, because the semantic decision about what
constitutes an escape sequence was already made during scanning.

This breaks real-world softcode.  Myrddin's BBS (installed on
essentially every MU* game) uses `\\%` inside a `switch()` NOEVAL
branch to produce a literal `%` character.

## Why Tree Rewriting Alone Cannot Fix This

The `ast_noeval_pass()` function (ast.cpp) attempts to serialize
the AST back to a string with one layer of backslash stripping,
then re-tokenize and re-evaluate.  This is the right idea, but
it operates on pre-formed tokens:

- `ESC("\\")` can emit `\` correctly.
- `SUBST("% ")` can pass through as `% ` correctly.
- The serialized result `\% capacity` is correct.

But when this string is fed back to the scanner for re-tokenization,
the scanner splits it into `ESC("\%")` + `LIT(" capacity")` and
evaluation produces `% capacity`.  In principle this should work.

In practice, the re-tokenization is a second scan that must exactly
reconstruct what the 2.13 character-at-a-time evaluator would have
produced.  Edge cases involving nested braces, eval brackets, and
multi-level escaping can cause the round-trip to diverge.

More fundamentally, the problem is that the scanner makes semantic
decisions (what constitutes an escape sequence, what constitutes a
percent substitution) without access to evaluation context.  Patching
this after the fact is inherently fragile.

## Proposed Design: Split Lexer

Defer the semantic decisions about backslash and percent to the
tree evaluation phase, where EVAL/NOEVAL context is known.

### Phase 1: Scanner Changes (Ragel)

The scanner currently forms compound tokens:

```
'\\' any  => { ASTTOK_ESC, text = "\X" }
'%' any   => { ASTTOK_PCT, text = "%X" }
```

Change to emit atomic single-character tokens:

```
'\\'  => { ASTTOK_BACKSLASH }
'%'   => { ASTTOK_PERCENT }
```

These tokens carry no following character.  They are markers that
say "a backslash/percent appeared here" without deciding what it
means.  All other scanner rules remain unchanged: braces, brackets,
commas, parens, function names, literals.

The parser builds `AST_BACKSLASH` and `AST_PERCENT` nodes in the
tree.  These are leaf nodes with no text payload beyond the single
character.

### Phase 2: Context-Sensitive Combination (Tree Walk)

The sequence evaluator (`AST_SEQUENCE` case) gains lookahead logic
for `AST_BACKSLASH` and `AST_PERCENT` nodes.  This is the "second
lexical pass" — it does character-level combination during tree
evaluation, which is architecturally ugly but necessary.

```
for each child in sequence:
    if child is AST_BACKSLASH:
        // Backslash handler: NOT guarded by EV_EVAL.
        // Consumes the first character of the next sibling.
        // Exactly replicates 2.13 behavior.
        //
        char ch = consume_first_char(next_sibling)
        safe_chr(ch, buff, bufc)

    else if child is AST_PERCENT:
        if (eval & EV_EVAL):
            // Percent handler: guarded by EV_EVAL.
            // Consumes following character(s) and dispatches
            // as a substitution (same as current AST_SUBST).
            //
            dispatch_percent_subst(next_sibling, ...)
        else:
            // NOEVAL: output literal '%', do not consume.
            //
            safe_chr('%', buff, bufc)

    else:
        ast_eval_node(child, ...)
```

The `consume_first_char()` operation is the messy part.  It must
handle several sibling node types:

- **AST_LITERAL**: consume first byte, shorten the literal.  If the
  literal becomes empty, skip the node.
- **AST_BACKSLASH**: consume the `\` character itself.  The backslash
  node is fully consumed and skipped.
- **AST_PERCENT**: consume the `%` character itself.  The percent
  node is fully consumed and skipped.
- **AST_SPACE**: consume the space.  Node is skipped.
- **End of sequence**: trailing backslash with nothing following.
  Output nothing (or `\` — match 2.13 behavior).

The `dispatch_percent_subst()` operation similarly consumes one or
more characters from the following sibling(s) to form the
substitution key (`%b`, `%0`, `%q<name>`, `%c<rgb>`, etc.).

### Phase 3: NOEVAL Branch Evaluation

With the split lexer, NOEVAL evaluation of `\\% capacity` proceeds:

1. BACKSLASH: consume first char of next sibling (BACKSLASH) → `\`
2. PERCENT: EV_EVAL is off → output `%` literally
3. LIT(" capacity"): output ` capacity`
4. Result: `\% capacity`

The re-evaluation pass sees `\% capacity` and (now with EV_EVAL on):

1. BACKSLASH: consume first char of next sibling (PERCENT) → `%`
2. LIT(" capacity"): output ` capacity`
3. Result: `% capacity`

This matches 2.13 exactly because the combination decisions are made
with the same EVAL/NOEVAL awareness that 2.13's entangled
scanner/evaluator had.

## Tradeoffs

### Costs

- **More tokens, more AST nodes.**  Every `\` and `%` in the source
  becomes its own node instead of being folded into a compound token.
  The LRU parse cache mitigates re-scanning cost.

- **Lookahead in sequence evaluation.**  The sequence evaluator becomes
  a mini state machine that reaches across sibling boundaries.  This
  is lexer-level work happening during tree evaluation — exactly the
  entanglement we tried to eliminate with the AST architecture.

- **Node mutation during evaluation.**  `consume_first_char()` modifies
  or skips sibling nodes, which means the AST is not purely read-only
  during evaluation.  (Alternative: use an index-advancement scheme
  instead of mutation.)

- **Percent dispatch complexity.**  Multi-character percent forms
  (`%q<name>`, `%c<rgb>`) require consuming variable numbers of
  characters from potentially multiple sibling nodes.

### Benefits

- **Exact 2.13 NOEVAL semantics.**  The split lexer reproduces the
  character-at-a-time interleaving of scanning and evaluation that
  makes `\\%` work in NOEVAL contexts.

- **No serialization round-trip.**  The tree walk operates directly
  on nodes.  No serialize-to-string-and-re-scan step that could
  introduce divergences.

- **Contained change.**  The scanner change is mechanical (remove
  compound rules, add atomic rules).  The evaluator change is
  localized to the `AST_SEQUENCE` case.  The rest of the AST
  infrastructure (caching, JIT, function dispatch) is unaffected.

- **Testable.**  The existing oracle corpus in
  `parser/escape_oracle_cases.txt` and the Myrddin BBS case provide
  concrete pass/fail criteria.

## Scope and Risk

This is a significant change to the scanner and the hottest path
in the evaluator.  It should not be attempted without:

1. A comprehensive test matrix covering `\\`, `\%`, `\\%`, `%%%`,
   and multi-level nesting in both EVAL and NOEVAL contexts.
2. Benchmarking to measure the token-count and evaluation overhead.
3. Careful review of all percent substitution forms to ensure the
   tree-walk dispatcher handles variable-length consumption correctly.

The change is confined to:

- `ast_scan.rl` / `ast_scan.cpp` (scanner)
- `ast.cpp` (AST_SEQUENCE evaluator, percent dispatch)
- `ast.h` (new node types)

No changes to the JIT compiler, HIR lowering, or SSA pipeline are
required, though the JIT would need to be taught about the new node
types if it encounters them.

## Prior Art

No compiler textbook covers this because no designed language works
this way.  MU* softcode evolved organically over 30 years with the
scanner and evaluator entangled by accident, and real-world softcode
depends on the resulting behavior.

PennMUSH has the same entanglement — `process_expression()` is both
scanner and evaluator, with `PE_EVALUATE` checked during character
scanning.  See `parse.c` line 3113.

TinyMUX 2.13's `mux_exec()` has the same structure with `EV_EVAL`
checked during the character loop.

The split lexer is TinyMUX 2.14's way of reintroducing the minimum
necessary entanglement into an otherwise clean AST architecture.

## Alternative

There is now a second design direction in
[docs/design-parser-controlled-lexer.md](/home/sdennis/tinymux/docs/design-parser-controlled-lexer.md).

That approach keeps parser-owned structural boundaries and lets the
scanner run under parser-supplied `EVAL`/`NOEVAL` modes for deferred
regions. It is still intentionally impure, but it may preserve the AST
architecture with less evaluator-side AST mangling than the full split
lexer described here.

## Status

Design proposal. Not implemented.
