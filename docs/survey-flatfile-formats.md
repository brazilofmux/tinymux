# Survey of TinyMUX flatfile formats (1.6 → 2.14)

A practical guide to recognizing, dating, and diagnosing a TinyMUX flatfile from
its first few lines. If someone hands you a `.flat`/`.db` and asks "what is this
and will it load?", start here.

For the mechanics of *importing* a flatfile into a modern server (the two-file
SQLite model, `db_load`, boot precedence), see
[`importing-a-game.md`](importing-a-game.md). This document is about the *format*
of the file itself.

> TL;DR — Modern TinyMUX (2.7+) flatfiles carry a `+X<number>` version header on
> the very first line. The low byte of that number is the **format version**
> (1–5); the upper bits are **feature flags**. The current server (2.14) reads
> format versions **1 through 5**. A flatfile from **1999** predates all of this
> and almost certainly will *not* load directly — it needs to be walked forward
> through an intermediate TinyMUX before a modern server will take it.

## Anatomy of a modern header

A flatfile produced by `db_unload` (or written on boot) begins with a small
header, then the objects, then attribute payloads. A fresh 2.14 export starts:

```
+X996101        <- MUX version + feature flags (see below)
+S309           <- size hint: next dbref to allocate (object count)
+N716           <- next user-attribute number to allocate
-R1             <- recorded peak player count
+A256           <- user-named attribute #256 ...
"1:DB"          <-   ... its owner:name
+A257
"1:SGP-OBJECTS"
...
```

Only `+X`, `+S`, `+N`, and `+A` (plus `-R`) are recognized in the header by the
current reader (`mux/modules/engine/db_rw.cpp`, `db_read()`). Anything else in
header position is logged as unexpected and skipped.

### Decoding the `+X` number

The number after `+X` packs two things:

```
+X996101  =  0x000F3305
                     ^^   low byte  = format VERSION = 0x05 = 5
             ^^^^^^      upper bits = feature FLAGS  = 0xF3300
```

| Bit        | Hex        | Meaning                                          |
|------------|------------|--------------------------------------------------|
| `V_ZONE`   | `0x00000100` | object has a ZONE/DOMAIN field                  |
| `V_LINK`   | `0x00000200` | object has a LINK (home) field                  |
| `V_DATABASE` | `0x00000400` | attrs stored in a *separate* db (structure file, not a true flatfile) |
| `V_ATRNAME`  | `0x00000800` | NAME is an attribute, not in the header        |
| `V_ATRKEY`   | `0x00001000` | KEY/lock is an attribute, not in the header    |
| `V_PARENT`   | `0x00002000` | object has a PARENT field                       |
| `V_ATRMONEY` | `0x00008000` | money kept in an attribute                      |
| `V_XFLAGS`   | `0x00010000` | a second word of object flags                   |
| `V_POWERS`   | `0x00020000` | powers present                                  |
| `V_3FLAGS`   | `0x00040000` | a third word of object flags                    |
| `V_QUOTED`   | `0x00080000` | quoted strings (PennMUSH-style), required ≥ v2  |

(These are `V_*` in `mux/include/db.h`; the decode table is mirrored in the
Omega converter as `t5x_gameflagnames[]`.)

So `+X996101` = version 5 with flags
`V_ZONE | V_LINK | V_ATRKEY | V_PARENT | V_XFLAGS | V_POWERS | V_3FLAGS | V_QUOTED`
— which is exactly `MANDFLAGS_V5`, the mandatory flag set the modern writer emits
(`UNLOAD_FLAGS` in `mux/include/config.h`).

You will also see, in the wild, `+X996099` (= version **3**, same flag set) — for
example the seed `netmux.db` checked into the tree. That just means it was last
written by an older build; it still loads.

## The format versions

The current server defines (`mux/include/config.h`):

```
#define MIN_SUPPORTED_VERSION 1
#define MAX_SUPPORTED_VERSION 5
#define OUTPUT_VERSION        5     // what a fresh export writes today
```

| Ver | Text encoding | Color representation | What changed at this version | TinyMUX line |
|-----|----------|----------|------------------------------|--------------|
| **1** | Latin-1 | raw ANSI escapes | Earliest `+X` MUX flatfile the modern reader accepts. Mandatory flags are `V_LINK V_PARENT V_XFLAGS V_ZONE V_POWERS V_3FLAGS V_QUOTED` (`MANDFLAGS_V2`); `V_ATRKEY` (locks stored as an attribute) is *optional* here (`OFLAGS_V2`). | early 2.x |
| **2** | Latin-1 | raw ANSI escapes | Same mandatory flags as v1 — the reader's version/flag assert treats v1 and v2 identically. `V_ATRKEY` remains optional; it becomes mandatory only at v3 (`MANDFLAGS_V3`). | 2.x up to **2.6** |
| **3** | **UTF-8** | **PUA code points** | **The big one.** Three changes at once: text converts Latin-1 → UTF-8; **raw ANSI escape sequences (`ESC [ … m`) become Private-Use-Area color code points**; attribute *names* become UTF-8; and the default lock is folded into attribute `A_LOCK`. This is the **2.6 → 2.7** boundary. | **2.7** onward |
| **4** | UTF-8 | PUA code points (v1 form) | Version-number bump; same mandatory flags as v3. (Carrier for later encoding work.) | later 2.x |
| **5** | UTF-8 | PUA code points (v2 form) | The 24-bit / 256-color PUA encoding was **redesigned** (per-channel deltas → fixed two-code-point form). Reader auto-migrates v≤4 color on load. | **2.14** (Mar 2026) |

Two transitions matter most when diagnosing an old file:

1. **v2 → v3 = TinyMUX 2.6 → 2.7.** This is more than a charset change. A v1/v2
   file is Latin-1 *and* stores color as **raw ANSI escape sequences** embedded
   in attribute values; on import the `g_version <= 2` branch in `db_read`
   transcodes text Latin-1 → UTF-8 **and** parses those `ESC [ … m` sequences,
   replacing them with PUA color code points (see `ConvertToUTF8` in
   `mux/lib/stringutil.cpp`). Omega performs the same conversion and notes its
   only charset conversion is "between TinyMUX 2.6 and TinyMUX 2.7."
2. **v4 → v5 = the 2.14 era.** Both store color as PUA code points; only the
   *encoding* of 24-bit/256 color within the PUA changed. Loading a pre-v5 file
   logs "Migrating V4 24-bit color encoding to V5".

### The color-representation lineage

Color is worth calling out separately because it changed *twice*, on different
boundaries than text encoding:

| Era | How color lives in the flatfile |
|-----|---------------------------------|
| **≤ 2.6** (v1/v2) | Raw terminal **ANSI escape sequences** (`ESC [ 1;31 m`, …) sit directly in attribute values. Only the classic 8/16-color + attribute codes exist — there is no 24-bit color in this era. |
| **2.7 – 2.13** (v3/v4) | Color is normalized to **Private-Use-Area code points**. The 2.6→2.7 import parses the old ANSI sequences into this form. 24-bit/256-color is encoded with the original (v1) PUA scheme. |
| **2.14+** (v5) | Same PUA approach, but the 24-bit/256-color encoding is **redesigned** to a fixed two-code-point form. |

Practical consequence: a pre-2.7 flatfile carries human-readable `ESC[…m`
escapes in its attribute text; a 2.7+ flatfile carries UTF-8 PUA code points that
look like multi-byte gibberish in a plain text editor. Both render as color in
the game — they are just different on-disk representations.

The version/flag *consistency* is asserted on read: for v1/v2 the file must carry
`MANDFLAGS_V2`, for v3+ it must carry `MANDFLAGS_V3` (which adds `V_ATRKEY`).
This is why a flatfile that predates those mandatory flags cannot simply be
relabeled — see below.

## Before `+X`: the 1.6 / pre-2.0 era (≈ the 1999 problem)

This is the case that prompted this document. A database from **1999** predates
the format-version scheme above:

- **TinyMUX 1.x** (David Passmore's line, descended from TinyMUSH 2.2) was the
  codebase in 1999; **1.6** was the last of it.
- **TinyMUX 2.0** (Stephen Dennis's rewrite) was *finally released in 2000*, but
  games were running pre-release 2.0 before that. So a "1999 database" could be
  a **1.6** flatfile *or* an early **2.0** flatfile — you usually can't tell
  without looking.

Why a raw 1999 file generally **won't** load into a modern server:

- Even format **version 1** (the minimum the modern reader accepts) requires the
  full `MANDFLAGS_V2` flag set — including `V_QUOTED` (PennMUSH-style quoted
  strings) and `V_3FLAGS` (a third object-flag word). Those features were added
  *during* 2.x development. A genuine 1.6-era flatfile won't have them, so the
  reader's mandatory-flags assertion fails.
- 1.x flatfile options "were more varied," in Omega's words; the modern reader
  (and even Omega's `t5x` parser, which only tokenizes `+X/+S/+N/+A`) doesn't
  cover that variety.

### Look at the first line to triage

| First line looks like…                | Likely origin | Path forward |
|---------------------------------------|---------------|--------------|
| `+X<big number>`, low byte 3/4/5      | TinyMUX 2.7+  | Loads directly into 2.14. |
| `+X<big number>`, low byte 1/2        | TinyMUX 2.0–2.6 (Latin-1) | Loads into 2.14; transcoded to UTF-8 on import. |
| `+X<number>` but mandatory flags missing / assertion fails | early/odd 1.6 or pre-release 2.0 | Walk it through a real TinyMUX 2.0, then forward. |
| `+V…`, `+T…`, `+F…`, or no `+X` at all | TinyMUSH-lineage / 1.x variant | Not a modern MUX flatfile; needs 1.6/2.0 to read and re-dump. |

### Two routes for an old (1.6 / pre-2.0) flatfile

1. **Through TinyMUX 2.0 first (recommended for genuine 1.6).** Load the old
   flatfile into a TinyMUX 2.0/2.x server, let it normalize the object structure
   and flags, `@dump`/`db_unload` a fresh flatfile, then move forward (2.6→2.7
   handles Latin-1→UTF-8) up to 2.14. This is exactly what Omega's README
   advises: *"It is best to run it through TinyMUX 2.0 first."*
2. **Omega (`mux/convert/`).** Omega can upgrade/downgrade TinyMUX flatfiles
   across 2.0 → 2.14 and "may also be able to accept TinyMUX 1.6, but flatfiles
   from that era were more varied… Your 1.6 flatfile may use options which Omega
   doesn't expect." Treat Omega as the fast path that *might* work; the 2.0
   round-trip is the reliable fallback.

Either way the destination is a modern (v3–v5, UTF-8) `+X` flatfile, which the
2.14 server imports normally. **Passwords survive**: the hashes
(`$SHA1$…`, `$1$…`, …) are carried through every step unchanged.

## Quick reference: decode a header by hand

```
take the number after +X
version = number & 0xFF            # 1..5
flags   = number & ~0xFF           # OR of the V_* bits in the table above
```

Example: `+X996099` → `0xF3303` → version **3**, flags `0xF3300`
(`V_ZONE V_LINK V_ATRKEY V_PARENT V_XFLAGS V_POWERS V_3FLAGS V_QUOTED`).

## Source of truth

- `mux/include/db.h` — `V_*` flag bits, `F_MUX` format id.
- `mux/include/config.h` — `MANDFLAGS_V2..V5`, `OUTPUT_VERSION`,
  `MIN/MAX_SUPPORTED_VERSION`, `UNLOAD_FLAGS`.
- `mux/modules/engine/db_rw.cpp` — `db_read()`/`db_write()`; the Latin-1→UTF-8
  (`g_version <= 2`) and V4→V5 color (`g_version <= 4`) migration branches.
- `mux/lib/stringutil.cpp` — `ConvertToUTF8()`: the v2→v3 routine that both
  transcodes Latin-1 → UTF-8 *and* parses raw `ESC [ … m` ANSI sequences into
  PUA color code points (mirrored in `mux/convert/t5xgame.cpp`).
- `mux/convert/` — Omega and the `t5x`/`t6h`/`p6h`/`r7h` converters; see its
  `README` for the cross-codebase conversion matrix and the 2.6↔2.7 charset note.
