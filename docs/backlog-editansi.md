# Backlog: `editansi()` — recolor-by-color softcode function

**Status:** vetted candidate, not scheduled. Deferred while 2.14 is in its
tests/stress/hardening plateau; revisit when feature surface reopens.
**Prompted by:** a suggestion from Ashen-Shugar (RhostMUSH).
**Safety:** pure softcode function, `CA_PUBLIC`, no files, no privilege, no
trust boundary. Nothing here conflicts with the hardening posture except that
it is net-new surface.

## What it is, and what it is *not*

`editansi(<string>, <search-color>, <replace-color> [,<search>,<replace>]...)`
is a **color transform**, not a text editor. `<search>` and `<replace>` are
color specifications, not substrings. It finds every visible character
currently wearing `<search>` and repaints it `<replace>`; the letters never
change. Trailing pairs apply in sequence.

Uses: "repaint all red text green," "strip all hilite but keep the colors,"
"recolor everything that is *not* hilite" (negation matches).

**This is distinct from color-safe text replacement, which TinyMUX already
has.** Our `edit()` matches and replaces *visible text* while preserving the
surrounding color runs (via `co_edit`, `color_ops.c`) — that is the behavior
RhostMUSH provides as a separate `medit()` function. `editansi` is the other
axis: it keys on color and leaves text alone. We have the text axis; we do not
have the color axis as a distinct operation.

Verified today that our `edit()` already covers the text-axis case:

    edit(hello <red>world</> today, world, there)  ->  "there" comes out red
    edit(hello world, world, <yellow>there</>)     ->  explicit color honored

## Design if built

The transferable design from the prior art is a four-stage pipeline; only the
shape transfers, none of the code (theirs is welded to a per-glyph `ANSISPLIT`
array with a malloc-per-call pool). Ours would be expressed against
`color_ops`:

1. **Materialize** the current color state per visible character. Our layer is
   a Ragel machine over inline markup that passes color escapes through
   verbatim (see `co_toupper` et al. in `color_ops.c` for the pattern) — it
   does *not* currently expose a per-glyph color vector, so this is the part
   that is genuinely new: a pass that tracks running color state.
2. **Compile** each `<search>`/`<replace>` color argument into a comparable
   color descriptor. Reuse whatever `fun_ansi` already parses so the accepted
   color syntaxes match `ansi()` for free (16-color, xterm-256, 24-bit).
3. **Match and repaint.** The real product is the priority ladder: exact
   match, foreground-only, background-only, special-attribute-only (hilite/
   flash/underscore/inverse), and the negation forms ("not hilite," "any
   foreground"). This is where the design content is.
4. **Re-emit** minimal normalized markup, coalescing equal neighbors and
   inserting resets on color-decreasing transitions — which our layer already
   does for every other `co_*` function.

Estimated shape: one new Ragel machine (`co_recolor`) plus a color-descriptor
matcher, a `fun_editansi` wrapper, and smoke + jit-diff coverage. Bounded, but
not a trivial addition. Per `docs/generated-files.md`, the Ragel output is
generated — edit the `.rl` source and regenerate, never the `.c`.

## Why deferred rather than built

The color-safe text-edit family already covers the common need. Recolor-by-
color is niche (theming/reskinning display code), and 2.14 was declared
feature-complete with the direction set to hardening rather than feature
surface. It is recorded here — vetted, safe, with the design mapped — so the
decision is a scheduling choice later, not a re-investigation.

## Related

- `docs/assessment-object-transfer.md` — the `@snapshot` decline.
- `docs/survey-resource-defenses.md` — the connection-defense survey.
- Of four RhostMUSH pointers, one (SiteMon refusal notification) closed a real
  gap and shipped; the other three were already covered by existing
  mechanisms. `editansi` is the first that is both a genuine gap and net-new
  surface — hence backlogged rather than declined.
