# Ragel -G2 Color-Aware String Primitives—Master Plan

## The thesis

`mux_string` exists because dealing with Unicode and color simultaneously is too
hard to hold in your head. It solves this by stripping PUA color on import into a
parallel `m_vcs[]` vector, operating on clean text, then re-injecting color on
export. This works but it's:

1. **Slow** — import/export cycle on every operation
2. **C++ only** — RV64 softlib can't use it
3. **Complex** — 2500+ lines of class machinery

Ragel -G2 eliminates all three. The `color_scan` machine discriminates color from
visible in the DFA itself—no strip/re-inject cycle. Pure C. Goto-driven. The
fastest thing on the planet.

## Stages

### Stage 0—Core primitives (COMPLETE)

30 tests. Foundational `color_scan` Ragel machine with `alphtype unsigned char`.

- `co_visible_length` — count visible code points (skipping color PUA)
- `co_skip_color` — advance past color PUA code points at current position
- `co_visible_advance` — advance past exactly n visible code points
- `co_copy_visible` — copy up to n visible code points, preserving color
- `co_find_delim` — find first single-byte delimiter, skipping color

Key lesson: `alphtype unsigned char` is mandatory—without it, Ragel generates
signed comparisons and bytes >= 0x80 never match.

### Stage 1—Word and substring operations (COMPLETE)

33 tests. Compose on Stage 0 Ragel primitives.

- `co_strip_color` — copy string with all color PUA removed
- `co_words_count` — count words separated by delimiter, skipping color
- `co_first` — extract first word before delimiter, preserving color
- `co_rest` — everything after first word, preserving color
- `co_last` — extract last word after final delimiter
- `co_extract` — extract words iFirst..iLast (1-based), preserving color
- `co_left` — first n visible code points, preserving color
- `co_right` — last n visible code points, preserving color
- `co_trim` — trim leading/trailing characters, preserving color
- `co_search` — find first occurrence of visible substring, skipping color

Covers: `words()`, `first()`, `rest()`, `last()`, `extract()`, `left()`,
`right()`, `trim()`, search/position.

### Stage 2—Transforms (COMPLETE)

25 tests.

- `co_toupper` — convert visible code points to uppercase, preserving color
- `co_tolower` — convert visible code points to lowercase, preserving color
- `co_totitle` — capitalize first visible code point, preserving color
- `co_reverse` — reverse visible code points, preserving color attachment
- `co_edit` — find-and-replace, color-aware
- `co_transform` — character mapping (tr/translate), color-aware
- `co_compress` — collapse runs of a character, preserving color

Key lesson: `$action` (per-byte) on shared-prefix branches causes misrouting.
When `color_bmp` (EF 94-9F xx) and `vis_3_ef` (EF 80-93|A0-BF xx) share the
0xEF prefix byte, a `$` action fires before disambiguation. Fix: use `>mark`
(entering) to save start position, then `@emit` (finishing/last-byte) to copy
the accumulated bytes—the finishing action only fires once the branch is fully
resolved.

### Stage 3—Position, substring, padding

- `co_mid(out, p, len, start, count)` — substring by visible position (like `mid()`)
- `co_pos(haystack, needle)` — visible index (returns size_t, not pointer)
- `co_lpos(haystack, needle)` — last visible index
- `co_member(p, len, char_set)` — visible index of first char in set
- `co_center(out, p, len, width, fill)` — center-pad to width
- `co_ljust(out, p, len, width, fill)` — left-justify (right-pad)
- `co_rjust(out, p, len, width, fill)` — right-justify (left-pad)
- `co_repeat(out, p, len, count)` — repeat string N times

These are all compositions on Stage 0/1 primitives. Straightforward.

### Stage 4—Delete, splice, insert, sort, set operations (COMPLETE)

28 tests.

- `co_delete` — delete visible code points by 0-based position, color-aware
- `co_splice` — word-level: replace matching words from parallel list
- `co_insert_word` — insert word at 1-based position in word list
- `co_sort_words` — sort word list (ASCII, case-insensitive, numeric, dbref)
- `co_setunion` — sorted set union of two word lists
- `co_setdiff` — sorted set difference (list1 minus list2)
- `co_setinter` — sorted set intersection

Infrastructure: `split_words` helper, `sort_elem_t` with stripped-content
comparison, four comparators (ASCII, case-insensitive, numeric, dbref),
`emit_sorted` output builder.

Note: MUX's `splice()` and `insert()` are word-level operations (not
character-level). `delete()` is character-level (0-based cluster index).
`unique()` does not exist in MUX. `munge()` requires attribute evaluation
(server-dependent, not implemented here).

### Stage 5—Color collapse

`co_collapse_color(out, p, len)` — eliminate redundant/conflicting PUA sequences.

This is `mux_string::import()` in reverse, done inline. Track `ColorState` as we scan:

- `>flush` entering action on visible: emit minimal PUA delta from previous state
- `@update` finishing action on color: merge into running `ColorState`
- Consecutive FG colors—keep last. Conflicting attrs—resolve. RGB deltas accumulate.

This is what `mux_string` does that we don't yet—and it's the key to matching
it completely.

Second function: `co_apply_color(out, plain, len, color_state)` — inject a
ColorState at start. Enables: `ansi()` function (apply named color to string).

### Stage 6—Full Unicode case mapping (COMPLETE)

10 tests. Replaced ASCII-only toupper/tolower/totitle with full Unicode via the
existing DFA tables from `utf8tables.cpp`.

- Extracted DFA tables into `unicode_tables.c` / `unicode_tables.h` (pure C)
- `co_dfa_toupper` / `co_dfa_tolower` / `co_dfa_totitle` — inline C functions
  executing the compressed RUN/COPY DFA, returning `co_string_desc*` + XOR flag
- Ragel `@emit_upper` / `@emit_lower` actions call the DFA per visible code point
- Handles variable-length output: ẞ (3 bytes) — ß (2 bytes), µ — Μ (cross-script)
- XOR optimization: same-length transforms applied as byte-wise XOR (e.g., é ↔ É)
- Literal replacement: different-length transforms copy replacement bytes directly
- Color PUA code points passed through unchanged (Ragel `color_scan` discriminates)

Coverage: ~1460 tolower, ~1477 toupper, ~1481 totitle code point mappings.
All Simple_Uppercase/Lowercase/Titlecase_Mapping from UnicodeData.txt.

### Stage 7—Integration proving ground

- Pick 3-5 `fun_*` functions from `functions.cpp` and write them using `co_*` calls
- Benchmark against the `mux_string` versions
- If the numbers hold up: start replacing in the server
- Simultaneously: expose `co_*` functions as RV64 ECALLs in the softlib

## The endgame

Once Stages 0-6 are complete, every operation `mux_string` performs has a `co_*`
equivalent that:

- Runs in a single pass (no import/export cycle)
- Is pure C (compiles for both x86-64 and RV64)
- Is goto-driven Ragel -G2 (fastest possible generated code)
- Preserves color byte-for-byte (or collapses it, your choice)

At that point, `mux_string` becomes optional scaffolding. The compiler can emit
`co_*` calls directly. The RV64 softlib gets the same primitives natively. The
Gordian Knot is cut.

## Color encoding reference

TinyMUX encodes color as Unicode Private Use Area code points inline in UTF-8:

**BMP PUA (3-byte UTF-8):**

- U+F500: reset
- U+F501: intense
- U+F504: underline
- U+F505: blink
- U+F507: inverse
- U+F600-F6FF: 256 foreground XTERM indexed colors
- U+F700-F7FF: 256 background XTERM indexed colors

**Supplementary PUA (4-byte UTF-8, Plane 15):**

- U+F0000-F00FF: red FG delta
- U+F0100-F01FF: green FG delta
- U+F0200-F02FF: blue FG delta
- U+F0300-F03FF: red BG delta
- U+F0400-F04FF: green BG delta
- U+F0500-F05FF: blue BG delta

A color annotation is 1-4 code points: a base (3 bytes BMP PUA), optionally
followed by up to 3 RGB delta code points (4 bytes SMP PUA). 24-bit color =
base + up to 3 deltas = up to 15 bytes.

**UTF-8 byte patterns:**

- BMP color: `EF (94-9F) (80-BF)` — 3 bytes
- SMP color: `F3 B0 (80-97) (80-BF)` — 4 bytes
