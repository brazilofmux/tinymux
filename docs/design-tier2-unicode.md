# Tier 2 RV64 Softlib: Unicode and Color Requirements

## Status: Study / Design

Branch: `brazil`

## Context

The JIT compiler's Tier 2 path replaces C++ `fun_*` implementations with
RV64 functions compiled into a binary blob (`softlib.rv64`). These
functions run inside the DBT sandbox with no access to engine internals.

The original Tier 2 functions were implemented as pure byte-level
operations. A correctness audit (2026-03-12) revealed that several
functions diverge from native behavior when inputs contain:

 - **ANSI color escape sequences** (`\x1B[...m`)
 - **Multi-byte UTF-8 characters** (2-4 bytes per code point)
 - **Grapheme clusters** (one user-perceived character = multiple code
   points, e.g., base + combining marks, emoji ZWJ sequences)

This document captures what we learned and lays out the requirements
for making Tier 2 functions exact behavioral matches.

## The Goal

The endgame is:

 1. Every softcode function is either compiler intelligence (Tier 1
    constant folding / native ops) or a Tier 2 RV64 implementation.
 2. The server switches to always use the compiler path.
 3. The native C++ `fun_*` implementations become dead code and
    are deleted.

This requires Tier 2 functions to be **exact** behavioral matches.
Not "close enough"—exact. 592+ smoke tests, every edge case, color
and Unicode byte-perfect.

## What We Learned

### Three layers of text complexity

The native `fun_*` implementations deal with three layers that the
byte-level RV64 functions ignored:

**Layer 1: Raw bytes (UTF-8 encoding)**

Operations that treat the string as a bag of bytes. Concatenation
(`cat`, `strcat`) is in this category—joining two NFC strings
produces an NFC string, and ANSI escape sequences pass through
unchanged. No special handling needed.

**Layer 2: ANSI color escapes**

MUX text can contain ANSI terminal color codes: `\x1B[31m` (red),
`\x1B[0m` (reset), etc. These are byte sequences interleaved with
the text content. They are NOT Unicode code points, NOT grapheme
clusters, and NOT countable characters. They are invisible metadata.

The native engine handles color via `mux_string`, which strips ANSI
on import (storing color as side-channel metadata in `m_vcs`) and
re-injects it on export. Functions that need to count, index, or
search characters operate on the stripped text.

For Tier 2 purposes, color requires:

 - **Skipping** escape sequences when scanning for delimiters or
   counting characters
 - **Preserving** them in output (bytes pass through unchanged)
 - **Not counting** them as characters

Color escapes have a simple, fixed grammar: `\x1B[` followed by
zero or more digits/semicolons, terminated by a letter. This is
trivially recognizable without Unicode tables.

**Layer 3: Unicode grapheme clusters**

A "character" in user-facing operations is a grapheme cluster per
UAX #29. This affects any operation that counts, indexes, or scans
for delimiters based on character boundaries.

In NFC form (which all internal strings are), many characters are
single code points (precomposed). But:

 - Some valid NFC sequences have combining marks after a base
   (multiple accents, diacritical marks that can't be precomposed)
 - Emoji ZWJ sequences: 👨‍👩‍👧‍👦 = 7 code points, 1 cluster
 - Regional indicators: 🇺🇸 = 2 code points, 1 cluster
 - Variation selectors, skin tone modifiers

Grapheme clusters have **no upper bound** on length. A base
character can be followed by any number of combining marks (ccc > 0),
ZWJ continuations, or extending characters. In practice, NFC limits
the common cases, but "zalgo text" (dozens of stacked combining marks)
is valid NFC and constitutes a single cluster.

### Function classification

The audit classified every Tier 2 function by which layers it needs:

**Layer 1 only (byte-safe, currently in Tier 2):**

| Function | Why safe |
|----------|---------|
| `cat` | Concatenation with literal space—no scanning |
| `strcat` | Pure concatenation—no scanning |

These are safe because concatenating NFC strings preserves NFC, and
ANSI escape sequences are just bytes that pass through.

**Layer 2 needed (ANSI awareness):**

| Function | Issue |
|----------|-------|
| `words` | Native strips color before counting tokens |
| `strlen` | Native strips color + counts clusters |

Even a function that just counts space-delimited tokens needs to
skip ANSI escapes to avoid false delimiter matches (though in
practice `\x1B[...m` doesn't contain spaces, so `words` with
default space delimiter is accidentally safe).

**Layer 3 needed (grapheme cluster boundaries):**

| Function | Issue |
|----------|-------|
| `strlen` | Must count clusters, not bytes or code points |
| `extract` | Delimiter scan must not split a cluster |
| `words` | Same—token boundaries must respect clusters |
| `first` | Delimiter scan |
| `rest` | Delimiter scan |
| `last` | Delimiter scan |
| `member` | Token comparison + delimiter scan |
| `trim` | Trim character could be multi-byte |
| `before` | Pattern search within text |
| `after` | Pattern search within text |
| `split_token` | Delimiter scan (used by iter() loop) |
| `ldelete` | Delimiter scan + multi-position |
| `replace` | Delimiter scan + multi-position |
| `insert` | Delimiter scan + multi-position |

**The critical insight:** any function that scans for a user-supplied
delimiter in user-supplied text needs grapheme cluster boundary
awareness. A single-byte delimiter like `|` seems safe, but if the
text contains a code point with the same byte value as part of a
multi-byte sequence, or if the delimiter is a base character that
appears in a cluster with combining marks, the byte-level scan
produces wrong results.

### The delimiter problem in detail

Consider `extract(café·latte, 2, 1, é)` where `é` is the delimiter:

 - In NFC, `é` is U+00E9 (one code point, two UTF-8 bytes: `0xC3 0xA9`)
 - The text `café` contains `0xC3 0xA9` as the last two bytes
 - A byte-level scan looking for the two-byte delimiter would match
   correctly here (finding `é` in `café`)
 - But if the text were `cafe\u0301` (NFD decomposed), the `e` is a
   separate code point from the combining accent—the delimiter `é`
   wouldn't match at the byte level even though visually it should

NFC normalization protects against the decomposed case. But it
doesn't protect against:

 - `extract(Ä·B, 2, 1, A)` where `Ä` is U+0041 U+0308 (A + combining
   diaeresis, valid NFC when the precomposed form doesn't exist for
   this combination). A byte-level scan for `A` would match the base
   of the cluster, splitting it incorrectly.

 - Any case where the delimiter character appears as a base with
   combining marks following it. The RV64 byte scan would match the
   base and leave the orphaned combining marks in the wrong token.

This is not theoretical—MUX players are creative and adversarial
with string functions. If a byte-level delimiter split can orphan
combining characters, someone will find it and exploit it.

### Functions removed from Tier 2 (2026-03-12)

Based on the audit:

 - **`strlen`** — removed (byte count ≠ cluster count)
 - **`ldelete`** — removed (also missing multi-position/negative index)
 - **`replace`** — removed (same)
 - **`insert`** — removed (same)
 - **`before`** — bug fixed (not-found now returns whole string)

Functions remaining in Tier 2 as of this writing:

    cat, strcat, extract, words, first, rest, last,
    member, repeat, trim, before, after, split_token

Of these, only `cat` and `strcat` are fully correct. The rest are
technically vulnerable to the delimiter/cluster issue, but safe in
practice for the current rveval use case (ASCII text, space
delimiters).

## Requirements for Correct Tier 2

### Requirement 1: ANSI escape skipping

RV64 functions need a helper that recognizes and skips ANSI escape
sequences in the byte stream. This does NOT require Unicode tables —
ANSI escapes have a fixed grammar:

```
ESC [ <digits/semicolons> <letter>
0x1B 0x5B [0x30-0x3B]* [0x40-0x7E]
```

A simple state machine (3 states) suffices:

```c
/* Skip past an ANSI escape sequence starting at p.
 * Returns pointer to first byte after the sequence.
 * If p does not point to ESC, returns p unchanged.
 */
static const char *skip_ansi(const char *p) {
    if (*p != '\x1B') return p;
    p++;
    if (*p != '[') return p;  /* Not CSI — skip ESC only */
    p++;
    while (*p >= 0x30 && *p <= 0x3B) p++;  /* parameters */
    if (*p >= 0x40 && *p <= 0x7E) p++;     /* final byte */
    return p;
}
```

This would live in the blob as a helper function, called by every
delimiter-scanning function before examining the current byte.

Cost: minimal. One branch per byte to check for `0x1B`.

### Requirement 2: Grapheme cluster boundary detection

This is the hard one. To scan for a delimiter without splitting
clusters, the RV64 code needs to know where cluster boundaries are.

**What we need:**

A function `next_cluster(p)` that advances a pointer past one
complete grapheme cluster. This requires:

 1. Decode the lead byte to determine how many bytes the current
    code point occupies (1-4)
 2. Classify the code point's Grapheme_Cluster_Break property
 3. Apply the UAX #29 break rules to determine if the next code
    point continues the current cluster or starts a new one
 4. Repeat until a break is found

**What we do NOT need:**

 - Knowledge of what the character IS (letter, digit, symbol)
 - Case mapping, normalization, display width
 - The full `mux_string` machinery
 - Color re-injection (bytes pass through)

We only need boundary detection.

**The DFA approach:**

The `./utf` pipeline already produces compressed DFA tables from
Unicode property data. A Grapheme_Cluster_Break DFA would:

 - Input: UTF-8 byte stream
 - Output: the GCB property of each code point (one of ~15 values)
 - Table size: comparable to existing DFAs (likely 100-300 states,
   a few KB of table data)

The break rules themselves (GB1-GB999 from UAX #29) are a small
state machine on top of the property values. There are about
15 rules, most of which are "do not break between X and Y."

**Integration into the blob:**

The GCB DFA table would be compiled into the blob's `.rodata`
section, alongside the function code. The `next_cluster()` helper
would read the table to classify code points and apply break rules.

The blob format already supports a `.rodata` section (currently
2 bytes). The GCB table would increase it by a few KB.

### Requirement 3: Delimiter matching at cluster boundaries

With `skip_ansi()` and `next_cluster()`, the delimiter scanning
pattern changes from:

```c
/* WRONG: byte-level scan */
while (*p && *p != delim) p++;
```

To:

```c
/* CORRECT: cluster-aware scan, skipping ANSI */
while (*p) {
    if (*p == '\x1B') { p = skip_ansi(p); continue; }
    /* Check if current cluster matches delimiter */
    const char *cluster_start = p;
    p = next_cluster(p);
    if (cluster_is_delim(cluster_start, p, delim)) break;
}
```

For single-byte ASCII delimiters (the common case), `cluster_is_delim`
reduces to checking that the cluster is exactly one byte long and
matches. Multi-byte delimiters require comparing the full cluster.

This is more expensive per byte than the current `*p != delim`, but
the branch for `0x1B` (ANSI) is almost never taken (most text has no
ANSI escapes), and the cluster advance for ASCII text (bytes < 0x80)
degenerates to `p++` since each ASCII byte is its own cluster.

### Requirement 4: strlen as cluster count

`strlen()` needs to count grapheme clusters, not bytes. With the
above primitives:

```c
int count = 0;
const char *p = str;
while (*p) {
    if (*p == '\x1B') { p = skip_ansi(p); continue; }
    p = next_cluster(p);
    count++;
}
```

This is what `strip_color()` + `utf8_cluster_count()` does in the
native implementation, but without the intermediate allocation.

## Narrowing the Unicode surface

The full Unicode grapheme cluster segmentation (UAX #29) handles:

 - Hangul jamo composition (L, V, T, LV, LVT rules)
 - Regional indicator pairs (flag emoji)
 - ZWJ emoji sequences (Extended_Pictographic + ZWJ chains)
 - Prepend characters (rare—some Indic scripts)
 - SpacingMark (combines visually but is not a combining mark)

**For MUX's purposes, how much of this do we actually need?**

The MUX input boundary (`CL_PRINT` DFA) currently rejects combining
characters entirely. The design-unicode-evolution.md doc plans to
accept them (Layer 2+3), but today, all internal strings are ASCII

- precomposed NFC characters + ANSI escapes.

If we scope Tier 2 to the **current** server state:

 - Every code point is a complete cluster (no combining marks)
 - `next_cluster()` = advance past one UTF-8 code point (1-4 bytes)
 - No need for the full GCB property table—just UTF-8 lead byte
   decoding: `if (byte < 0x80) 1; else if (byte < 0xE0) 2;
   else if (byte < 0xF0) 3; else 4;`

This dramatically simplifies the implementation and is correct for
the current server, at the cost of needing to revisit when Layer 3
of the Unicode evolution lands.

**Recommended approach:**

1. **Phase 1 (now):** Implement `skip_ansi()` + code-point-level
   `next_codepoint()` in the blob. This handles ANSI and multi-byte
   UTF-8 correctly for the current server where every code point is
   its own cluster. Small, testable, no new tables needed.

2. **Phase 2 (with Unicode evolution Layer 3):** Replace
   `next_codepoint()` with a full `next_cluster()` that uses the
   GCB DFA table. The function signature is the same—callers
   don't change, only the implementation.

This gives us correctness today without over-engineering for a
Unicode capability the server doesn't yet have.

## ANSI Color: What the blob needs to know

The blob does NOT need to understand what colors are, parse color
attributes, or maintain a color state vector. It only needs to:

 1. **Recognize** that `\x1B[` starts an escape sequence
 2. **Skip past** the sequence (to the terminating letter)
 3. **Not count** the escape bytes as characters
 4. **Not match** delimiter bytes that fall inside escape sequences
 5. **Preserve** escape bytes in output (copy them through)

This is a 3-state FSM with no table dependencies:

```
State 0 (normal):    byte == 0x1B → State 1; else process byte
State 1 (after ESC): byte == '[' → State 2; else → State 0
State 2 (in CSI):    byte in 0x30-0x3B → stay; byte in 0x40-0x7E → State 0
```

The only subtlety: MUX uses xterm-256 and 24-bit color extensions
(`\x1B[38;5;Nm` and `\x1B[38;2;R;G;Bm`), which have longer parameter
sections but the same grammar. The FSM above handles them correctly
because the parameter bytes (digits and semicolons) are all in the
0x30-0x3B range.

## Grapheme clusters: How long can they get?

UAX #29 defines no upper bound on cluster length. A cluster is:

```
base_character (Extend | ZWJ Extend* Extended_Pictographic)* SpacingMark*
```

Where `Extend` includes:

 - Combining diacritical marks (U+0300-U+036F)
 - Combining marks in other blocks (~1500 code points total)
 - Variation selectors (U+FE00-U+FE0F, U+E0100-U+E01EF)
 - Zero Width Joiner (U+200D)

**Practical limits:**

 - NFC precomposes most single-accent combinations. Sequences of
   2+ combining marks after a base (valid NFC when the composition
   pair doesn't exist) are uncommon but legal.

 - Emoji ZWJ sequences: the longest standard ones are ~25 bytes
   (family emoji with 4 members + 3 ZWJs). Custom/extended ZWJ
   sequences are theoretically unbounded but renderers cap them.

 - "Zalgo text": `a` + 100 combining marks = valid NFC, 1 cluster,
   ~300 bytes. This is the adversarial case.

**For the blob, cluster length doesn't matter for correctness** —
`next_cluster()` just keeps advancing until it finds a break. But
it matters for buffer management: a function like `extract()` that
copies clusters needs to handle the case where one "character" is
hundreds of bytes. The LBUF_SIZE (8000 byte) limit provides a
natural cap.

## Implementation cost estimate

### Phase 1: ANSI skip + code-point advance (current server)

 - `skip_ansi()`: ~15 lines of C, 3-state FSM
 - `next_codepoint()`: ~10 lines of C, UTF-8 lead byte decode
 - Update 10 delimiter-scanning functions to use both
 - No new tables, no blob size increase beyond code
 - Estimated: ~200 lines of C changes to softlib.c

### Phase 2: Full grapheme cluster support (future)

 - GCB DFA table from `./utf` pipeline: ~2-5 KB in blob rodata
 - `next_cluster()` with GCB property lookup and break rules:
   ~50-80 lines of C
 - Replace `next_codepoint()` calls (same signature)
 - New `./utf` build target for GCB table generation
 - Estimated: ~300 lines of C + pipeline changes

### Phase 3: strlen / cluster counting

 - `rv64_strlen` returns to Tier 2 with proper cluster counting
 - Uses `skip_ansi()` + `next_cluster()` (or `next_codepoint()`)
 - ~20 lines of C

### Phase 4: ldelete/replace/insert with full semantics

 - Multi-position support (parse comma-separated position list)
 - Negative index support (count from end)
 - Uses cluster-aware delimiter scanning
 - ~100 lines of C per function

## Relationship to other design docs

 - **design-unicode-evolution.md** Layer 3 (Grapheme Cluster
   Segmentation) defines the server-side grapheme cluster support.
   Phase 2 of this document depends on that work for the GCB
   property tables.

 - **design-jit-compiler.md** defines the Tier 2 compilation
   pipeline. This document specifies correctness requirements
   for the Tier 2 function implementations.

 - **design-unicode-evolution.md** Layer 2 (NFC at input boundary)
   is already in place. The Phase 1 simplification (every code
   point = one cluster) depends on NFC enforcement.

## Open questions

 1. **Should the blob carry its own GCB table, or should the
    DBT map the server's table into guest memory?**  The blob
    currently has no access to server data. Embedding the table
    in the blob is simpler but means updating the blob when
    Unicode versions change. Mapping it would require a new
    DBT mechanism for shared read-only data.

 2. **Should ANSI stripping happen at the rveval boundary
    (before compilation) rather than inside each function?**
    If all rveval inputs are pre-stripped, the blob functions
    don't need `skip_ansi()` at all. But this loses the ability
    to process colored text and re-emit it with color intact,
    which limits future utility.

 3. **Is Phase 1 (code-point-level) sufficient for shipping?**
    Given that the server currently rejects combining characters
    at the network boundary, all internal strings are
    cluster-per-code-point. Phase 1 is exact for the current
    server. The risk is that someone enables combining characters
    (via Layer 3 of the Unicode evolution) before Phase 2 is done.
    A compile-time or runtime guard could prevent this.

 4. **Performance impact of cluster-aware scanning.**  For ASCII
    text (bytes < 0x80), `next_codepoint()` degenerates to `p++`
    and `skip_ansi()` is a single branch on `0x1B`. The overhead
    is negligible. For multi-byte text, the per-code-point
    classification adds ~2-3 table lookups per character. This
    needs measurement but is expected to be within the ECALL
    overhead that Tier 2 eliminates.
