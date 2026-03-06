# Unicode Evolution Design

## Overview

Evolve TinyMUX's Unicode support from code-point-level operations to
grapheme-cluster-level operations, add NFC normalization at the input
boundary, upgrade Unicode data tables from 10.0 to 16.0, and update the
softcode interfaces (`chr()`, `ord()`, string functions) to work in terms of
user-perceived characters.

This is a multi-layer effort built incrementally on the existing DFA
state-machine infrastructure in `utf/` and `utf8tables.cpp`.

## Current State

TinyMUX has solid Unicode support at the **code point** level:

- Full UTF-8 encoding/decoding with byte-level validation (`utf8_FirstByte`)
- `mux_string` with dual cursors: `mux_cursor(m_byte, m_point)`
- State-machine-based case mapping (`mux_tolower`/`mux_toupper`) with
  context-sensitive rules and multi-codepoint expansion (e.g., ß — SS)
- East Asian Width tables for display width calculation (Unicode 10.0)
- Unicode-aware case-insensitive comparison (`mux_stricmp`)
- Character classification DFAs: printability, player/object/attribute
  name validity, script detection (Hangul, Hiragana, Kanji, Katakana)
- Charset conversion state machines (ASCII, CP437, Latin-1, Latin-2)
- All user-facing string functions (`mid()`, `left()`, `right()`,
  `strlen()`) count code points, not bytes
- Color state vector in `mux_string`: one color state per code point
- Network input validation: the `CL_PRINT` DFA rejects non-printable
  code points. Combining characters are currently rejected.

### What "character" means today

| Context | "Character" = |
|---------|---------------|
| `strlen()` | Code point count (after color stripping) |
| `mid(str, pos, len)` | Code point indexing |
| `mux_cursor` | `(byte_offset, codepoint_offset)` pair |
| `ord(str)` | Single code point — integer |
| `chr(int)` | Integer — single code point |
| Color vector | One entry per code point |
| Display width | Per-code-point via East Asian Width |
| Network input | Validated printable code points only |

## Problem Statement

### Combining characters and grapheme clusters

The character `é` can be represented two ways in Unicode:

1. **Precomposed:** U+00E9 (LATIN SMALL LETTER E WITH ACUTE) — 1 code point
2. **Decomposed:** U+0065 U+0301 (LATIN SMALL LETTER E + COMBINING ACUTE
   ACCENT) — 2 code points

Both render identically. But today, TinyMUX:

- Rejects the decomposed form at the network boundary (combining
  characters are not in `CL_PRINT`)
- Returns `strlen(é)` = 1 for form 1 but would return 2 for form 2
  if it were accepted
- Cannot compare the two forms as equal

This extends to all combining marks (accents, diacritics, vowel signs), Hangul
jamo composition, and emoji sequences.

### Emoji sequences

Modern emoji are multi-code-point sequences:

- 👨‍👩‍👧‍👦 = U+1F468 U+200D U+1F469 U+200D U+1F467 U+200D U+1F466
  (4 emoji + 3 ZWJ = 7 code points, 1 visual character)
- 🇺🇸 = U+1F1FA U+1F1F8 (2 regional indicators, 1 flag)
- 👍🏽 = U+1F44D U+1F3FD (emoji + skin tone modifier, 1 visual character)

Users expect `strlen()` to return 1 for each of these.

### String comparison and sorting

- `sort()` uses binary code-point order, which puts `Z` before `a` and
  gives nonsensical results for non-English text
- Two strings that look identical can fail equality checks if they use
  different normalization forms
- Case-insensitive comparison works (via `mux_stricmp`) but only
  because both inputs come through the same path — a decomposed input   from

an external source would not match

### Unicode version

The current tables are based on **Unicode 10.0.0** (June 2017). The current
standard is **Unicode 16.0.0** (September 2024). Eight major versions of new
characters, emoji, scripts, and updated properties are missing.

## Design

### Guiding Principles

1. **NFC everywhere, no exceptions.** All text inside the server is in
   NFC (Canonical Decomposition followed by Canonical Composition). This    is

enforced at the input boundary.

2. **Grapheme cluster = user-perceived character.** All user-facing
   operations that deal with "characters" use grapheme clusters as the

unit. Code points become an internal implementation detail.

3. **DFA state machines for all Unicode properties.** Continue the
   existing pattern: Unicode data files — table generators — compiled

state machines in `utf8tables.cpp`. No runtime library dependencies.

4. **Incremental delivery.** Each layer is useful on its own and does
   not require the next layer to be complete.

5. **Backward compatibility at the softcode level.** For the vast
   majority of existing softcode (ASCII text), behavior is unchanged.

`strlen(hello)` still returns 5. `mid(hello, 1, 3)` still returns    `ell`.

### Layer 1: Unicode 16.0 Table Upgrade

**Goal:** Bring all existing DFA state machines up to Unicode 16.0.0.

**Scope:**

- Download Unicode 16.0.0 data files to `utf/`
- Regenerate all existing state machines:
  - `CL_PRINT` (printable characters)
  - `CL_PLAYERNAME`, `CL_OBJECTNAME`, `CL_ATTRNAME`
  - `CL_HANGUL`, `CL_HIRAGANA`, `CL_KANJI`, `CL_KATAKANA`
  - `TR_TOLOWER`, `TR_TOUPPER` (case mapping)
  - `ConsoleWidth` (East Asian Width)
  - Charset conversion tables
- Verify that existing smoke tests still pass
- No behavioral changes — just updated data

**Data files needed:**

- `UnicodeData.txt` (replace existing)
- `EastAsianWidth.txt` (replace existing)
- `CaseFolding.txt` (replace existing)
- `SpecialCasing.txt` (replace existing)

### Layer 2: NFC Normalization at the Input Boundary

**Goal:** All text entering the server is normalized to NFC. All text stored
in the database is NFC. Everything inside the server is NFC.

**New Unicode property tables:**

| Property | Source | Purpose |
|----------|--------|---------|
| Canonical_Combining_Class (ccc) | `UnicodeData.txt` field 3 | Reordering combining marks |
| Decomposition_Mapping | `UnicodeData.txt` field 5 | NFD canonical decomposition |
| Composition_Exclusions | `CompositionExclusions.txt` | Exclude from NFC composition |
| Full_Composition_Exclusion | `DerivedNormalizationProps.txt` | Derived exclusions |

**Normalization algorithm** (UAX #15):

1. **Decompose:** Recursively apply canonical decomposition mappings
2. **Reorder:** Sort combining marks by Canonical_Combining_Class
3. **Compose:** Apply canonical composition (combine pairs back into
   precomposed forms where possible)

**Implementation:**

- New function: `void utf8_normalize_nfc(const UTF8 *src, UTF8 *dst,
  size_t *nDst)` — normalizes a UTF-8 string to NFC in-place or into a

buffer

- **Integration point:** In the GANL telnet handler, after line assembly
  and before the command is queued. Every line of input from the network

passes through NFC normalization.

- The `CL_PRINT` DFA is updated to **accept** combining characters
  (they are now valid input, because normalization will compose them)
- **Database migration:** When importing flatfile data into SQLite, each
  attribute value is normalized to NFC during import

**Why NFC and not NFD:**

- NFC is the most compact form (precomposed where possible)
- NFC is the W3C recommendation for interchange
- NFC matches what most input methods produce
- Existing TinyMUX data is effectively already NFC (since combining
  characters were rejected, all accented characters are precomposed)

**Quick check function:** A fast `bool utf8_is_nfc(const UTF8 *src, size_t n)`
that returns true if a string is already in NFC (common case) without doing a
full normalize. This is a DFA that checks for NFC_QC=No and NFC_QC=Maybe
characters — if none are found, the string is already NFC. This avoids the
cost of normalization on strings that are already well-formed, which will be
the vast majority.

### Layer 3: Grapheme Cluster Segmentation

**Goal:** Define "user-perceived character" as a Unicode grapheme cluster per
UAX #29. Update `mux_cursor`, `mux_string`, and all string functions.

**New Unicode property table:**

| Property | Source | Purpose |
|----------|--------|---------|
| Grapheme_Cluster_Break | `GraphemeBreakProperty.txt` | Cluster boundary detection |
| `emoji-data.txt` | `Extended_Pictographic` property | Emoji sequence handling |

**The Grapheme_Cluster_Break property** classifies every code point into one
of these categories (Unicode 16.0):

- CR, LF, Control
- Extend (combining marks, variation selectors, ZWJ)
- SpacingMark
- Prepend
- L, V, T, LV, LVT (Hangul jamo)
- ZWJ
- Regional_Indicator
- Extended_Pictographic (emoji base)
- Other

The **Extended Grapheme Cluster** boundary rules (GB1–GB999) determine where
cluster breaks occur based on adjacent code points' break properties.

**Changes to mux_cursor:**

```cpp
class mux_cursor {
    LBUF_OFFSET m_byte;     // byte offset (unchanged)
    LBUF_OFFSET m_point;    // code point offset (kept for internal use)
    LBUF_OFFSET m_cluster;  // grapheme cluster offset (NEW — user-facing)
};
```

All user-facing operations use `m_cluster`. Internal operations that need
code-point precision (normalization, case mapping) use `m_point`.

**Changes to mux_string:**

- Color vector: one color state per **grapheme cluster base code point**.
  Combining marks and extending characters inherit the color of their   base.

This is consistent with how terminals render colored text.

- `cursor_from_point()` — `cursor_from_cluster()` for user-facing
  indexing
- `cursor_from_point()` remains available for internal use

**Changes to string functions:**

| Function | Current unit | New unit |
|----------|-------------|----------|
| `strlen(str)` | Code points | Grapheme clusters |
| `mid(str, pos, len)` | Code point indices | Cluster indices |
| `left(str, n)` | Code points | Clusters |
| `right(str, n)` | Code points | Clusters |
| `insert(str, pos, ...)` | Code point index | Cluster index |
| `delete(str, pos, len)` | Code point indices | Cluster indices |
| `iter()` / `##` | Code points? | Clusters (word-level unchanged) |

**New/updated softcode functions:**

`ord(str)`:

- Currently returns a single integer (one code point)
- Now returns a **space-separated list of code point integers** for the
  first grapheme cluster of `str`
- Example: `ord(é)` — `233` (NFC, precomposed U+00E9)
- Example: `ord(👨‍👩‍👧)` — `128104 8205 128105 8205 128103`
- Color information and private-use code points are stripped

`chr(int [int ...])`:

- Currently accepts a single integer
- Now accepts a **space-separated list of code point integers** and
  produces the corresponding grapheme cluster
- The output is NFC-normalized
- Invalid sequences (not forming a valid cluster) produce an error

**Display width:** `ConsoleWidth()` sums the widths of all code points in a
cluster. A combining mark has width 0; the base character carries the width.
This already works correctly because combining marks have East Asian Width =
Neutral (width 0 or 1) and non-spacing marks have width 0.

### Layer 4: Unicode Collation ✓

**Goal:** Language-aware sorting for `sort()`, `setunion()`, `setinter()`,
`setdiff()`, `comp()`.

Full in-process Unicode Collation Algorithm (UCA, UTS #10) implementation
using DUCET from Unicode 16.0 (`allkeys.txt`). See `docs/design-collation.md`
for the detailed six-phase design.

**Implementation:**

- DFA state machines generated by `integers.exe` and `pairs.exe`
  from DUCET data parsed by `utf/gen_ducet.pl`
- `utf8_collate.cpp`: runtime UCA comparison and sort key generation
- Multi-level comparison: primary (base), secondary (accent), tertiary (case)
- Two sort types: `'u'` (full UCA) and `'c'` (case-insensitive, L1+L2 only)
- `comp()` defaults to UCA; optional third arg selects mode
- Implicit weights for CJK and unassigned code points (UCA Section 10.1)
- 964 two-CP contractions via pairs DFA, 8 three-CP contractions hardcoded
- Auto-detection defaults to Unicode collation for non-numeric lists

### Layer 5: Extended Emoji Support (Future)

Most emoji support falls out of Layer 3 (grapheme cluster segmentation with
Extended_Pictographic and ZWJ handling). Additional work:

- Emoji presentation vs text presentation (VS15/VS16)
- Emoji tag sequences (subdivision flags)
- Emoji keycap sequences

**Decision deferred** — Layer 3 handles the core cases. Edge cases can be
addressed based on user demand.

## DFA Pipeline

### Existing Pipeline

The `utf/` directory contains:

1. **Unicode data files** (`.txt`) — downloaded from unicode.org
2. **Transformation rule files** (`tr_*.txt`, `cl_*.txt`) — define which
   code points belong to each class or transformation
3. **Table generators** — Perl/Python scripts that compile Unicode
   properties into DFA state machines
4. **Output** — `utf8tables.cpp` and `utf8tables.h` containing the
   compiled state machines as static arrays

### New Tables to Generate

| Table | Type | Source | Layer |
|-------|------|--------|-------|
| `CL_GCB_*` | Classification | `GraphemeBreakProperty.txt` | 3 |
| `CL_EXTPICT` | Classification | `emoji-data.txt` | 3 |
| `TR_NFD` | Transformation | `UnicodeData.txt` field 5 | 2 |
| `CCC` | Lookup table | `UnicodeData.txt` field 3 | 2 |
| `NFC_QC` | Classification | `DerivedNormalizationProps.txt` | 2 |
| `NFC_COMPOSE` | Lookup table | Derived from `UnicodeData.txt` | 2 |

The Canonical_Combining_Class (CCC) and NFC composition pairs may be better
served by lookup tables (array or hash) rather than DFAs, since they return
integer values or pairs rather than set membership.

## Integration Points

### Network Input (GANL)

```
bytes from network
    → GANL telnet handler (line assembly)
    → UTF-8 validation (existing)
    → CL_PRINT filtering (updated to accept combining characters)
    → NFC normalization (NEW — Layer 2)
    → command queue
```

### Database (SQLite)

- All attribute values stored in NFC
- Flatfile import normalizes during migration
- No normalization needed on read (already NFC)
- `ord()`/`chr()` operate on NFC data

### String Functions

```
softcode evaluator
    → mux_exec calls string function
    → function receives mux_string (NFC, with color)
    → indexing via mux_cursor using m_cluster
    → output is NFC (operations preserve NFC)
```

### Case Mapping

Case mapping can change string length and normalization form. The existing
`mux_tolower`/`mux_toupper` state machines already handle length changes.
After case mapping, a re-normalization pass may be needed to restore NFC (some
case mappings produce non-NFC output).

## Migration and Compatibility

### Existing databases

All existing TinyMUX databases are effectively already NFC because:

1. Combining characters were rejected at the network boundary
2. Direct database manipulation (`@set`, `&attr`) goes through the
   same input path
3. The only non-NFC data would come from external imports

During SQLite migration, all attribute values are normalized to NFC as a
safety measure.

### Existing softcode

For ASCII text (the vast majority of softcode), all operations produce
identical results. Grapheme cluster = code point = byte for ASCII.

For existing non-ASCII text that is already NFC precomposed (the only kind
currently accepted), cluster count = code point count in almost all cases. The
exception would be rare sequences where a precomposed character is followed by
additional combining marks, but these are currently rejected.

### ord() / chr() backward compatibility

`ord(A)` still returns `65`. `chr(65)` still returns `A`. The list-of-integers
behavior only manifests for multi-code-point clusters, which don't exist in
current databases. Existing softcode using `ord()`/`chr()` with single ASCII
or precomposed characters is unaffected.

## Implementation Order

1. **Unicode 16.0 table upgrade** — Foundation for everything else.
   No behavioral changes. All existing tests pass.

2. **NFC normalization** — New DFA tables for CCC, decomposition,
   composition. Quick-check DFA. Normalize at network input. Update

`CL_PRINT` to accept combining characters.

3. **Grapheme cluster segmentation** — New GCB DFA. Update
   `mux_cursor` with `m_cluster`. Update `mux_string` and all string

functions. Update `ord()`/`chr()`.

4. **Collation** — Done. Full UCA with DUCET 16.0. See
   `docs/design-collation.md`.

5. **Extended emoji** — Deferred. Mostly covered by Layer 3.

## Open Questions

1. **Should `ord()` on a precomposed character like é (U+00E9) return
   `233` or `101 769`?** Recommendation: return `233` (the NFC form). The

code points returned by `ord()` reflect the actual storage, which    is always
NFC.

2. **Should `chr()` accept decomposed input and normalize it?** Yes.
   `chr(101 769)` should produce the same result as `chr(233)` — both

yield NFC é (U+00E9).

3. **Should there be a `clusters()` or `graphemes()` function that
   returns cluster count, distinct from `strlen()`?** Probably not —

`strlen()` should just return the cluster count. If someone needs code-point
count, `ord()` on each cluster gives the constituent    code points.

4. **What about `bytes()` or `codepoints()`?** A `codepoints()` function
   could be useful for debugging/advanced use. Low priority.

5. **Title case:** `mux_totitle()` exists as a stub. Should it be
   implemented as part of this work? It's straightforward with the    existing

DFA infrastructure but requires `SpecialCasing.txt` data. Low priority — can
be added later.

6. **Locale-sensitive operations:** Beyond collation, some operations
   (e.g., Turkish İ/i case mapping) are locale-dependent. TinyMUX

currently uses locale-independent Unicode case mapping. Should this    change?
Recommendation: No. Locale-independent mapping is correct for    a multi-user
server where players may have different locales.
