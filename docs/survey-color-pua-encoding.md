# Survey: PUA Encoding of 24-Bit Color

## Current Encoding Scheme

TinyMUX uses Unicode Private Use Area (PUA) code points to represent color
state inline within UTF-8 strings. This allows color to survive all string
operations (concatenation, truncation, substring) without special escaping.

### PUA Code Point Allocation

**BMP PUA (3-byte UTF-8, U+E000–U+F8FF):**

| Range         | Count | Purpose |
|---------------|-------|---------|
| U+F500        | 1     | Reset to normal |
| U+F501        | 1     | Intense (bold) |
| U+F504        | 1     | Underline |
| U+F505        | 1     | Blink |
| U+F507        | 1     | Inverse |
| U+F600–F6FF   | 256   | Foreground XTERM indexed color (index = cp - 0xF600) |
| U+F700–F7FF   | 256   | Background XTERM indexed color (index = cp - 0xF700) |

**SMP PUA (4-byte UTF-8, Plane 15, U+F0000–U+FFFFD):**

| Range             | Count | Purpose |
|-------------------|-------|---------|
| U+F0000–F00FF     | 256   | Foreground Red channel (value = cp - 0xF0000) |
| U+F0100–F01FF     | 256   | Foreground Green channel |
| U+F0200–F02FF     | 256   | Foreground Blue channel |
| U+F0300–F03FF     | 256   | Background Red channel |
| U+F0400–F04FF     | 256   | Background Green channel |
| U+F0500–F05FF     | 256   | Background Blue channel |

Total PUA allocation: 5 BMP attributes + 512 BMP indexed colors + 1536 SMP
RGB channels = 2053 code points.

### Encoding a 24-bit Color

When emitting a color transition (`ColorTransitionBinary`,
`stringutil.cpp:2428`), the encoder:

1. Finds the nearest XTERM palette entry for the target RGB
2. Emits the XTERM indexed PUA code point (3 bytes)
3. For each channel where the palette entry differs from the target, emits an
   RGB delta code point (4 bytes each)

The "deltas" are absolute channel values, not differences. The term "delta"
refers to the fact that they modify the base palette color.

Example: Target color RGB(200, 135, 100), nearest palette = entry 137
(175, 135, 95):

```
U+F689  = FG XTERM index 137           (3 bytes: EF 9A 89)
U+F00C8 = FG Red = 200                 (4 bytes: F3 B0 83 88)
U+F0264 = FG Blue = 100                (4 bytes: F3 B0 89 A4)
                                        Total: 11 bytes, 3 code points
```

Green is not emitted because palette[137].g == 135 == target.g.

### Size Characteristics

| Scenario | Code Points | Bytes |
|----------|-------------|-------|
| Exact XTERM match (FG) | 1 | 3 |
| Exact XTERM match (FG+BG) | 2 | 6 |
| 1 channel differs (FG) | 2 | 7 |
| 2 channels differ (FG) | 3 | 11 |
| 3 channels differ (FG) | 4 | 15 |
| Full FG+BG, both 24-bit, all channels differ | 8 | 30 |
| Max transition (reset + 4 attrs + FG + BG, all 24-bit) | 15 | 45 |

The `COLOR_MAXIMUM_BINARY_TRANSITION_LENGTH` constant is 45 bytes
(`stringutil.cpp:2423`): 7 BMP codes at 3 bytes (reset + 4 attrs + FG + BG) +
6 SMP codes at 4 bytes.

### ColorState Bitfield (uint64_t)

The in-memory representation packs everything into 64 bits:

```
Bits 63-57: (unused)
Bit  56:    BG indexed flag
Bits 55-48: BG Red
Bits 47-40: BG Green
Bits 39-32: BG Blue (or palette index if bit 56 set)
Bit  31:    Blink
Bit  30:    Underline
Bit  29:    Inverse
Bit  28:    Intense
Bits 27-25: (unused)
Bit  24:    FG indexed flag
Bits 23-16: FG Red
Bits 15-8:  FG Green
Bits 7-0:   FG Blue (or palette index if bit 24 set)
```

When indexed: bits 7-0 (FG) or 39-32 (BG) hold the palette index (0-255),
and the indexed flag is set. When 24-bit: all three channel bytes hold the
actual RGB, and the indexed flag is clear.

### Decoding

`UpdateColorState()` (`stringutil.cpp:2356`) applies a `COLOR_INDEX_*` value
to a ColorState. For FG RGB channels:

- If currently indexed, expand palette entry RGB into the channel bits first
- Then overwrite the specific channel (Red, Green, or Blue)

This means the decoder can receive deltas in any order and they accumulate
correctly atop the base palette color.

### aColors Table

The `aColors[]` table (`stringutil.cpp:1816`) maps each `COLOR_INDEX_*` value
to:

- `cs`: ColorState value to OR in
- `csMask`: ColorState bits to clear first
- ANSI escape sequence (for terminal output)
- PUA UTF-8 bytes (for internal storage)
- Softcode `%x<>` representation (for display/parsing)

There are 2054 entries: 2 special + 4 attrs + 256 FG + 256 BG + 6*256 RGB
channels.

---

## Problems with Current Encoding

### 1. Worst-Case is 15 Bytes Per Color State

A full 24-bit FG color that doesn't match any palette entry on any channel
costs 15 bytes (3 + 4 + 4 + 4). With both FG and BG, that's 30 bytes per
character position for color alone. Strings are limited to 8000 characters
(LBUF_SIZE); heavy 24-bit color use can consume storage budget quickly.

### 2. The "Delta" Scheme Doesn't Compress

The RGB channel code points encode absolute values (0-255) regardless of how
close they are to the palette entry. Encoding `rgb(175, 135, 96)` against
palette entry (175, 135, 95) still costs 4 bytes for the Blue channel—the
same as encoding `rgb(175, 135, 0)`.

### 3. Redundant Base Index

When all three channels are overridden, the base XTERM index is redundant —
it conveys no information that the three channel code points don't already
provide. Yet it's always emitted.

### 4. Large aColors Table

2054 entries with 8 fields each, including per-entry ANSI escape strings and
softcode strings. Most of the RGB channel entries follow a mechanical pattern
that could be computed rather than tabulated.

### 5. Sparse PUA Usage

The SMP PUA range U+F0000–U+FFFFD has 65,534 code points. We use only 1,536
(2.3%). The BMP PUA range U+E000–U+F8FF has 6,400 code points. We use only
517 (8.1%).

---

## Design Space for a Tighter Encoding

### Goal

Encode a 24-bit color (FG or BG) in fewer code points and bytes, while
maintaining:

- Color survives all string operations (no escape sequences)
- Lossless round-trip through storage and retrieval
- Backward compatibility pathway (flatfile version bump + migration)
- Decoding is cheap (inline DFA in the Ragel scanner)

### Observation: Information Content

A 24-bit color (one of FG or BG) is 24 bits = 3 bytes of information.
The current encoding uses 3-15 bytes. The minimum UTF-8 representation of
24 bits requires either:

- One 4-byte UTF-8 code point (21 usable bits—not quite enough)
- Two 3-byte BMP code points (2 * ~12 usable bits = 24 bits—exactly right)
- One 3-byte + one 4-byte code point (overkill but easy)

### Option A: Pack RGB into Two BMP PUA Code Points

Use 12 bits per code point (4096 code points per block):

```
FG color = U+E000 + (R << 4 | G >> 4),  U+E000 + ((G & 0xF) << 8 | B)
BG color = U+F000 + same split
```

Wait—we need to not collide with existing allocations. Let's pick ranges
more carefully.

Available BMP PUA that we currently don't use: U+E000–U+F4FF and
U+F800–U+F8FF.

Encoding: Split 24-bit RGB into two 12-bit halves:
```
FG_hi = U+E000 + (R << 4) + (G >> 4)           // U+E000–EFFF (4096 values)
FG_lo = U+F000 + ((G & 0xF) << 8) + B          // U+F000–F0FF (256 values... no, 4096)
```

Hmm, this uses U+F000–F0FF which doesn't collide with our U+F500+ allocation.
Actually:
```
FG_hi = U+E000 + ((R << 4) | (G >> 4))         // U+E000–EFFF
FG_lo = U+E000 + (((G & 0xF) << 8) | B) + 4096 // U+F000–F0FF... wait
```

Let me be more precise. We need 4096 values for each half, and we have two
halves per color (FG and BG). That's 4 * 4096 = 16384 code points. BMP PUA
has 6400. Not enough for this approach.

### Option B: Pack RGB into One SMP PUA Code Point + Flag

SMP PUA code points U+F0000–U+FFFFD give us ~65K values. Each encodes in
4 bytes UTF-8. We need to encode:

- FG/BG flag (1 bit)
- R (8 bits), G (8 bits), B (8 bits) = 24 bits

Total: 25 bits. SMP PUA has ~16 usable bits (U+F0000–U+FFFFF = 1,048,576
values in Plane 15, easily enough).

```
FG 24-bit: U+F0000 + (R << 16) + (G << 8) + B
BG 24-bit: U+F0000 + (1 << 24) + (R << 16) + (G << 8) + B
```

Wait—Plane 15 is U+F0000–U+FFFFF, which is only 2^20 = 1,048,576 code
points. We need 2 * 2^24 = 33,554,432. Doesn't fit.

Actually, all SMP PUA code points across planes 15 and 16:

- Plane 15: U+F0000–U+FFFFD = 65,534 code points
- Plane 16: U+100000–U+10FFFD = 65,534 code points

Total SMP PUA = 131,068. We need 2 * 16,777,216 = 33M. Vastly insufficient
for a single-code-point encoding of full 24-bit color.

### Option C: Two SMP PUA Code Points (Compact)

Each SMP PUA code point is 4 bytes and can encode ~16 bits of payload
(within the 65K range of a plane). With two code points:
```
cp1 = U+F0000 + (FG_R << 8) + FG_G     // 16 bits: R and G
cp2 = U+F0000 + (FG_B << 8) + flags     // 16 bits: B and flags
```

8 bytes total, 2 code points. This is worse than Option D below.

### Option D: Repurpose the SMP RGB PUA Range (Recommended)

Keep the current XTERM indexed BMP code points (U+F600-F7FF)—they're
already compact at 3 bytes for exact palette matches. Redesign only the
24-bit extension.

**Current:** 1 XTERM base (3 bytes) + up to 3 channel overrides (4 bytes each)
= 3-15 bytes.

**Proposed:** When the color is NOT an exact palette match, encode it as:

```
FG 24-bit: U+F0000 + (R * 256 + G)     // code point 1: R, G  (4 bytes)
         + U+F0000 + (B * 256)          // code point 2: B     (4 bytes)
                                         Total: 8 bytes, 2 code points
```

But this wastes the low byte of the second code point. Better:

**Proposed (two code points, no waste):**

Allocate from SMP PUA Plane 15 (U+F0000–U+FFFFD, 65534 entries):

```
Block 0: U+F0000–F00FF  (256 entries) — FG red + green high nibble
Block 1: ...
```

Actually, with only 65534 SMP PUA code points, we cannot pack 16 bits of
payload into a single code point. The usable payload per SMP PUA code point
is log2(65534) ≈ 16 bits, which gives us exactly R+G or a similar split.

**Simplest correct design with two 4-byte code points:**

```
FG: U+F0000 + R*256 + G   (encodes R and G, needs R*256+G < 65534 ✓ max=65535)
    U+F0000 + B + 256      (encodes B, offset to avoid colliding with first range)
```

Hmm, collisions are tricky. Let me partition more carefully.

### Option E: One BMP + One SMP Code Point

```
FG: U+E000 + R*16 + (G >> 4)    // BMP PUA: 3 bytes, encodes R and high nibble of G
    U+F0000 + (G & 0xF)*256 + B // SMP PUA: 4 bytes, encodes low nibble of G and B
```

Total: 7 bytes, 2 code points. Saves 8 bytes over worst-case current (15),
saves 4 bytes over best non-exact case (7 for 1-channel delta). But the split
is ugly.

### Option F: Direct Encoding in Three BMP Code Points (6 Bytes)

Use three BMP PUA code points, each carrying one channel:

```
FG: U+E000 + R,  U+E100 + G,  U+E200 + B    // 3 code points, 9 bytes
```

9 bytes is worse than current best case (3 bytes for exact match) but better
than worst case (15 bytes). Not compelling.

### Option G: Hybrid (Recommended)

Keep the current XTERM-indexed encoding for exact palette matches (1 code
point, 3 bytes—this is already optimal). Replace the delta mechanism for
non-exact colors:

**New SMP encoding using two code points from Plane 15:**

```
FG 24-bit color:
  cp1 = U+F0000 + R*256 + G        // R and G packed, 4 bytes
  cp2 = U+F4000 + B                // B alone, 4 bytes
  Total: 8 bytes, 2 code points

BG 24-bit color:
  cp1 = U+F8000 + R*256 + G        // R and G packed, 4 bytes
  cp2 = U+FC000 + B                // B alone, 4 bytes
  Total: 8 bytes, 2 code points
```

Ranges used:

- U+F0000–F3FFF: FG R*256+G (need 65536 values, but range has 16384... too small)

Let me recalculate. Plane 15 has U+F0000–U+FFFFD = 65534 code points.
R*256+G ranges from 0 to 65535. Off by one—doesn't fit.

**Revised partitioning:**

Split R*256+G differently. Use two ranges within Plane 15:

```
FG:  U+F0000 + R*256 + G    where R*256+G ∈ [0, 65279]   → U+F0000–FFEDF
BG:  Use Plane 16: U+100000 + R*256 + G                    → U+100000–10FEDF
FG B: U+FFEE0 + B           where B ∈ [0, 255]            → U+FFEE0–FFFDF
BG B: U+10FEE0 + B                                         → U+10FEE0–10FFDF
```

Wait. R ranges 0-255, G ranges 0-255. R*256+G ranges 0-65535. Plane 15 has
65534 entries (U+F0000–U+FFFFD). One short. We can sacrifice R=255, G=255
(pure white already has an exact XTERM match at index 231), or just use
R*256+G for values 0-65533 and handle the edge case.

Actually, the two planes together give us 131068 code points. We need:

- FG: 65536 (R*256+G) + 256 (B) = 65792
- BG: 65536 (R*256+G) + 256 (B) = 65792
- Total: 131584

Just barely doesn't fit. But we can use a slightly different split:

### Option H: Practical Recommendation

**Observation:** For 256 palette colors, we already have perfect 3-byte
encoding. The 24-bit extension only needs to handle the remaining
256^3 - 256 = 16,776,960 colors. In practice, the nearest palette entry is
always close, so we could encode the *difference* to save bits. But variable-
length delta coding is complex.

**Simplest good option:** Two 4-byte SMP code points, one per FG/BG layer:

Partition Plane 15 + 16 into four blocks of ~32K each:

```
FG_RG: U+F0000 + R*256 + G       (R in 0-127: U+F0000–F7FFF, 32768 entries)
FG_B:  U+F8000 + B               (U+F8000–F80FF, 256 entries)
BG_RG: U+F8100 + R*256 + G       (R in 0-127: U+F8100–FFFFF + overflow)
BG_B:  ...
```

R only goes to 127? No, R goes to 255. This doesn't work either.

Let me step back. The fundamental constraint is: SMP PUA gives us ~131K code
points. We need to encode 2 colors * 3 channels * 256 values. With two code
points per color, we need 4 distinct "block starts" and some of those blocks
need 65536 entries.

**The real constraint:** Plane 15 and 16 each have ~65K usable code points.
One Plane 15 code point can distinguish 65534 values—almost but not quite
enough for R*256+G (65536 values).

**Practical resolution:** Use both planes.

```
FG R*256+G: U+F0000  + min(R*256+G, 65533)     Plane 15: U+F0000–FFFFD
FG B:       U+100000 + B                        Plane 16: U+100000–1000FF
BG R*256+G: U+100100 + min(R*256+G, 65533)     Plane 16: U+100100–10FF9D (65534)
BG B:       U+10FFA0 + B                        Plane 16: U+10FFA0–10FF9F+255
```

Hmm, Plane 16 only has U+100000–U+10FFFD = 65534 code points. After FG B
takes 256 and BG B takes 256, we have 65022 left—not enough for 65536.

This is getting fiddly. Let me just propose the pragmatic answer:

---

## Recommended Approach

### Keep XTERM Indexed Encoding As-Is

The 3-byte BMP encoding for XTERM palette matches is already optimal and
cannot be improved.

### Replace RGB Channel Deltas with a Single 4-Byte Code Point + Overflow

**Key insight:** We don't actually need lossless 24-bit storage in the PUA
encoding. The path is:

1. User specifies `%x<#RRGGBB>` (24-bit color)
2. We find the nearest XTERM palette entry
3. We store the XTERM index (3 bytes)
4. We store the exact 24-bit RGB for terminal output

But at the terminal, we emit either `ESC[38;5;Nm` (256-color) or
`ESC[38;2;R;G;Bm` (truecolor). The choice depends on client capability.
If the client supports truecolor, we need the exact RGB. If not, we need the
XTERM index. We need both.

**New encoding: 2 code points for full 24-bit FG or BG:**

Repurpose the 6 SMP PUA ranges (currently U+F0000–F05FF, 1536 code points)
as two ranges:

```
FG 24-bit:
  U+F0000 + ((R >> 4) << 8) + G       // high nibble of R, full G → 4096 values
  U+F1000 + ((R & 0xF) << 8) + B      // low nibble of R, full B → 4096 values

BG 24-bit:
  U+F2000 + ((R >> 4) << 8) + G       // same split for BG
  U+F3000 + ((R & 0xF) << 8) + B
```

Each range uses 4096 code points (12 bits: 4-bit nibble + 8-bit channel).
Total SMP PUA used: 16384 code points (U+F0000–F3FFF).

**Size:** 2 code points, 8 bytes per 24-bit color layer. Always exactly 8
bytes (no variable-length delta mess).

**Comparison:**

| Color Type | Current | Proposed |
|------------|---------|----------|
| Exact XTERM match | 3 bytes (1 cp) | 3 bytes (1 cp)—unchanged |
| 1 channel differs | 7 bytes (2 cp) | 8 bytes (2 cp) |
| 2 channels differ | 11 bytes (3 cp) | 8 bytes (2 cp) |
| 3 channels differ | 15 bytes (4 cp) | 8 bytes (2 cp) |
| FG+BG both 24-bit, all differ | 30 bytes (8 cp) | 16 bytes (4 cp) |

The proposed encoding is worse by 1 byte in the 1-channel case but saves 3-14
bytes in the common multi-channel case. More importantly, it's fixed-size,
which simplifies the DFA scanner and eliminates the need for the base XTERM
index when doing 24-bit.

**Alternative:** Keep the XTERM base index even in 24-bit mode (useful for
256-color fallback). Then encoding becomes:

```
Exact match:  U+F6xx                              (3 bytes, 1 cp)
24-bit FG:    U+F6xx + U+F0xxx + U+F1xxx          (11 bytes, 3 cp)
24-bit BG:    U+F7xx + U+F2xxx + U+F3xxx          (11 bytes, 3 cp)
Full FG+BG:   U+F6xx + U+F0xxx + U+F1xxx +        (22 bytes, 6 cp)
              U+F7xx + U+F2xxx + U+F3xxx
```

11 bytes for FG 24-bit vs current worst-case 15 bytes. Always exactly 11,
never variable. And the XTERM base provides the 256-color fallback without
recomputing nearest neighbor at output time.

### Why Keep the XTERM Base

The XTERM base index is computed once (nearest-neighbor search at attribute
write time) and stored. At output time, the same string is adapted for
potentially 100+ connected players with different client capabilities:

- **NOANSI:** Strip all PUA code points
- **ANSI:** Map index to 8/16-color via `palette[index].color8` /
  `palette[index].color16` — no search, just a table lookup
- **ANSI256:** Emit `ESC[38;5;{index}m` directly from the stored index
- **HTML/Truecolor:** Read the full RGB from the two SMP code points

Without the stored base, every output path that doesn't support truecolor
would need to recompute the nearest-neighbor search per color transition per
player. That's the wrong place to do expensive work.

### Recommended Encoding

```
Exact XTERM match (FG):   U+F6xx                           (3 bytes, 1 cp)
Exact XTERM match (BG):   U+F7xx                           (3 bytes, 1 cp)

24-bit FG:  U+F6xx  +  U+F0xxx  +  U+F1xxx                (11 bytes, 3 cp)
            base idx    R|G_hi      G_lo|B

24-bit BG:  U+F7xx  +  U+F2xxx  +  U+F3xxx                (11 bytes, 3 cp)
            base idx    R|G_hi      G_lo|B
```

Where the SMP code points split 24 bits of RGB as:

```
cp1 = U+F0000 + ((R >> 4) << 8) + G        // R high nibble + full G
cp2 = U+F1000 + ((R & 0xF) << 8) + B       // R low nibble + full B
```

Each SMP block uses 4096 code points (12 bits payload). Four blocks total:
U+F0000–F0FFF (FG cp1), U+F1000–F1FFF (FG cp2), U+F2000–F2FFF (BG cp1),
U+F3000–F3FFF (BG cp2).

**Size comparison:**

| Color Type | Current | Proposed |
|------------|---------|----------|
| Exact XTERM match | 3 bytes (1 cp) | 3 bytes (1 cp) |
| 1 channel differs | 7 bytes (2 cp) | 11 bytes (3 cp) |
| 2 channels differ | 11 bytes (3 cp) | 11 bytes (3 cp) |
| 3 channels differ | 15 bytes (4 cp) | 11 bytes (3 cp) |
| FG+BG both 24-bit | 30 bytes (8 cp) | 22 bytes (6 cp) |
| Max transition (all) | 45 bytes (15 cp) | 37 bytes (11 cp) |

The 1-channel case is 4 bytes worse, but it's fixed-size and the common
all-channels case saves 4 bytes. More importantly:

- Always exactly 3 cp per 24-bit layer (predictable, simpler DFA)
- No variable-length delta logic
- No dependency on knowing which channels match the palette entry
- The XTERM base is always present for all output paths

### Migration

- Bump flatfile format version
- On load of old format: decode old PUA sequences (base + channel deltas)
  via existing `UpdateColorState()` which reconstructs full RGB, then
  re-encode in new fixed-size format
- The migration is a simple scan-and-rewrite of attribute values
