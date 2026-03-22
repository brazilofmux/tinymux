# Survey: RGB-to-XTERM Nearest Neighbor Search

## Current Implementation

### Color Space and Distance Metric

TinyMUX converts 24-bit sRGB to Y'UV using fixed-point integer arithmetic
(`stringutil.cpp:1332`):

```
Y' = min(abs( 2104*R + 4310*G +  802*B + 4096 +  131072) >> 13, 235)
U  = min(abs(-1214*R - 2384*G + 3598*B + 4096 + 1048576) >> 13, 240)
V  = min(abs( 3598*R - 3013*G -  585*B + 4096 + 1048576) >> 13, 240)
```

The distance function (`stringutil.cpp:1629`) uses squared Euclidean distance
in YUV space with a 1.5x weight on luminance:

```
y2 = Y + Y/2
distance = (y2_a - y2_b)^2 + (U_a - U_b)^2 + (V_a - V_b)^2
```

The `y2` field is precomputed in the `YUV` struct to avoid runtime
multiplication. The comment says "the human eye is twice as sensitive to
changes in Y; we use 1.5 times."

### Data Structures

```c
// stringutil.h:1804
typedef struct { int r, g, b; } RGB;
typedef struct { int y, u, v, y2; } YUV;
typedef struct {
    RGB  rgb;
    YUV  yuv;
    int  child[2];   // K-d tree children: [0]=near, [1]=far; -1=leaf
    int  color8;     // nearest 8-color palette index
    int  color16;    // nearest 16-color palette index
} PALETTE_ENTRY;
```

### The XTERM 256-Color Palette

All 256 entries in `palette[]` (`stringutil.cpp:1369-1627`):

| Indices | Description | RGB Values |
|---------|-------------|------------|
| 0-7     | ANSI standard | User-configurable; TinyMUX assumes VGA-style (0,0,0), (187,0,0),... |
| 8-15    | ANSI bright | User-configurable; TinyMUX assumes (85,85,85), (255,85,85),... |
| 16-231  | 6x6x6 color cube | Levels: 0, 95, 135, 175, 215, 255 |
| 232-255 | Grayscale ramp | 24 shades from (8,8,8) to (238,238,238) |

### The ANSI 16-Color Problem

The first 16 entries (indices 0-15) have no standard RGB values. Every
terminal emulator assigns different colors. TinyMUX assumes one particular
mapping (VGA-ish for 0-7, xterm-ish for 8-15), but real clients diverge
significantly. To handle this, the K-d tree maintains two separate subtrees:

- **16-color tree** (root = entry 9): Entries 0-15 only, for terminals
  limited to 16 colors. The `color16` field of each palette entry gives its
  nearest 16-color match.
- **256-color tree** (root = entry 139): Entries 16-255 only (the
  "dependable" entries), for xterm-capable terminals.

Entry field `color8` maps each entry to the nearest 8-color index (0-7) via
brute-force linear search over 8 entries.

### K-d Tree Construction

The tree is built offline in `color/kdtree.cpp` and the resulting
`PALETTE_ENTRY palette[]` array (with precomputed `child[]`, `color8`,
`color16` fields) is pasted into `stringutil.cpp`.

Construction algorithm (`kdtree.cpp:489-519`):

1. Sort eligible palette entries by the current axis (Y, U, or V at depth % 3)
2. Pick the median as the current node
3. Recurse on left and right halves with depth+1
4. Store child indices in `table[median].child[0]` and `child[1]`

Two trees are built:

- One from the 16 "disabled" entries (indices 0-15, `fDisable=true`)
- One from the 240 "enabled" entries (indices 16-255, `fDisable=false`)

### K-d Tree Search

Three mutually-recursive functions cycle through Y, U, V axes
(`stringutil.cpp:1644-1735`):

```
NearestIndex_tree_y -> NearestIndex_tree_u -> NearestIndex_tree_v -> (back to y)
```

Standard K-d tree nearest-neighbor search with pruning: the far child is only
visited when `axis_distance^2 < current_best_distance`.

Public API:
```c
// stringutil.cpp:1737 — K-d tree search (16 or 256 color)
int FindNearestPaletteEntry(RGB &rgb, bool fColor256);

// stringutil.cpp:1748 — brute force over 8 entries
int FindNearestPalette8Entry(RGB &rgb);
```

### Validation

`kdtree.cpp` includes `ValidateTree()` which exhaustively tests all 16.7M RGB
values, comparing K-d tree results against brute-force linear search to verify
the tree is correct.

---

## Problems with Current Approach

### 1. YUV Is Not Perceptually Uniform

Y'UV (BT.601) is a broadcast color space designed for analog TV signal
efficiency, not for perceptual distance. The U and V axes are chrominance
difference signals that do not correspond to how humans perceive color
differences. The ad-hoc 1.5x Y weighting partially compensates but is not
principled.

Specific failure modes:

- Blues and purples are poorly distinguished (low sensitivity in U/V)
- Greens are over-separated relative to human perception
- Dark colors with different hues can appear "closer" than they look
- The grayscale ramp is not perceptually uniform in YUV

### 2. The 1.5x Y Weight Is Ad-Hoc

The comment says "twice as sensitive; we use 1.5 times." The factor is an
engineering compromise with no published basis. CIE research provides
well-characterized models.

### 3. K-d Tree Correctness Depends on Metric Matching Axes

The K-d tree prunes branches by comparing `axis_distance^2` against the full
distance. This is valid for weighted Euclidean metrics (which the current YUV
metric is), but CIEDE2000 is NOT a simple weighted Euclidean metric—it has
cross-terms (the rotation term RT). If we switch to CIEDE2000, the K-d tree
pruning criterion needs rethinking or we need a different spatial index.

---

## Proposed: CIE Color Difference Metrics

### Reference: TinyMUSH Implementation

TinyMUSH (`/tmp/tinymush/src/netmush/ansi.c`) implements full CIEDE2000 with:

1. sRGB inverse gamma correction (threshold at 0.04045)
2. sRGB to XYZ (D65 illuminant, standard 3x3 matrix)
3. XYZ to CIELAB (piecewise cube-root function)
4. CIEDE2000 distance with all correction terms (SL, SC, SH, RT)

They pre-compute CIELAB values for all 256 palette entries and do a linear
scan at query time. No spatial index.

### Candidate Metrics (Increasing Complexity)

**CIE76** — Euclidean distance in CIELAB:
```
ΔE*ab = sqrt((L1-L2)^2 + (a1-a2)^2 + (b1-b2)^2)
```

- Pros: Simple, fast, perceptually much better than YUV. K-d tree-compatible
  (standard Euclidean in L*a*b* space).
- Cons: Known weaknesses in blue and saturated regions.

**CIE94** — Weighted CIELAB with chroma-dependent terms:
```
ΔE*94 = sqrt((ΔL/SL)^2 + (ΔC/SC)^2 + (ΔH/SH)^2)
```
where SC = 1 + 0.045*C1, SH = 1 + 0.015*C1

- Pros: Better than CIE76 for saturated colors.
- Cons: Not symmetric (d(a, b) != d(b, a)). Still K-d tree compatible if you
  use the palette-side chroma for SC/SH (since palette is fixed).

**CIEDE2000** — The current standard:
```
ΔE00 = sqrt((ΔL'/SL)^2 + (ΔC'/SC)^2 + (ΔH'/SH)^2 + RT*(ΔC'/SC)*(ΔH'/SH))
```
with modified a* axis, hue rotation in blue region, etc.

- Pros: Most accurate perceptual model. CIE standard.
- Cons: Complex. Has cross-terms (RT) that invalidate K-d tree pruning.
  Requires `atan2`, `pow`, `sqrt`, `cos` per comparison. Not symmetric.

### Recommendation: CIE76 with CIELAB

CIE76 (Euclidean distance in CIELAB) is the sweet spot for TinyMUX:

1. **Perceptually much better than YUV** — CIELAB was designed for perceptual
   uniformity.
2. **K-d tree compatible** — Standard Euclidean metric, so the existing K-d
   tree pruning logic works directly. Just rotate through L*, a*, b* instead
   of Y, U, V.
3. **Integer-friendly** — L* ranges [0,100], a* ranges [-128,128],
   b* ranges [-128,128]. Scale by 100 and use integer arithmetic, same as
   current YUV approach.
4. **Pre-computation** — Convert all 256 palette entries to CIELAB at build
   time (in `color/` tool), embed in `palette[]`. Only the query color needs
   runtime conversion.

The runtime conversion (sRGB—XYZ—CIELAB) involves gamma correction and cube
roots, which is heavier than the current integer YUV multiply-and-shift. Two
approaches:

- **Float at runtime** (~20 FP ops per query): acceptable given this runs once
  per color transition, not per character.
- **Lookup table:** 256-entry sRGB—linear table, then fixed-point XYZ—Lab
  with a small cube-root table.

If CIE76 proves insufficient for the blue region (the main known weakness),
upgrading to CIEDE2000 with a brute-force scan over 256 entries is also fine —
256 distance computations is trivially fast.

### Impact on K-d Tree

If we stay with CIE76 (plain Euclidean in Lab), the tree structure is
identical—just change the three axes from (y2, u, v) to (L*, a*, b*) and
rebuild. `kdtree.cpp` comparators change from `yuv.y2/u/v` to `lab.L/a/b`.

If we go to CIEDE2000, the K-d tree cannot be used as-is because the RT
cross-term means `axis_distance^2` is not a valid lower bound. Options:

- **Drop the tree:** 240-entry brute-force scan. Still fast.
- **Use the tree as a heuristic:** Find the K-d tree's best, then brute-force
  verify against entries within some radius. Fragile.
- **Vantage-point tree (VP-tree):** Works with any metric, but more complex to
  build and query.

Given that N=240 and this runs ~once per color change, brute-force with
CIEDE2000 is a perfectly reasonable fallback.

---

## Proposed Work (~/tinymux/color/)

The existing `color/kdtree.cpp` already serves as the offline K-d tree builder.
The plan:

1. **Add CIE conversion functions** to `color/`: sRGB—XYZ—CIELAB, CIEDE2000
2. **Add a CIELAB palette table generator** that outputs a new `palette[]`
   with precomputed Lab values
3. **Validate:** exhaustive 16.7M-point comparison between brute-force
   CIEDE2000 and K-d-tree CIE76, to measure how many mismatches exist and how
   large the perceptual error is
4. **Replace** the YUV conversion and distance in `stringutil.cpp` with CIELAB
5. **Rebuild** the K-d tree with Lab axes and update `palette[]`
