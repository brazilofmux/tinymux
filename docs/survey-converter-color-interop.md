# Survey: cross-codebase color interop in the Omega converter

How each MUSH codebase stores color *in a flatfile attribute*, what the Omega
converter (`mux/convert/`) currently does when crossing families, and where the
fidelity gaps are. Companion to [`survey-color-pua-encoding.md`](survey-color-pua-encoding.md)
(TinyMUX's own PUA scheme) and [`survey-flatfile-formats.md`](survey-flatfile-formats.md).

Source studied (shallow clones, June 2026): PennMUSH `pennmush/pennmush`,
TinyMUSH `TinyMUSH/TinyMUSH` (this is **4.0** — see the caveat below), RhostMUSH
`RhostMUSH/trunk`.

## How each codebase stores color in stored attribute text

Color lives *in band* in the attribute value in every codebase, but the
representation differs completely:

| Codebase | In-band color representation | 24-bit | 256 | Source |
|----------|------------------------------|--------|-----|--------|
| **TinyMUX** (v3+) | Unicode PUA code points (U+F500/F600/F700 + SMP) | yes | yes | `mux/lib/stringutil.cpp`, survey-color-pua-encoding.md |
| **TinyMUX** (v1/v2) | raw ANSI escapes `ESC[…m` (Latin-1) | no | no | — |
| **PennMUSH** | markup tags `\x02c<codes>\x03` (TAG_START `0x02`, `c`, payload, TAG_END `0x03`) | `#RRGGBB` | `+xtermN` | `penn/hdrs/ansi.h:41`, `penn/src/markup.c:713` |
| **TinyMUSH** | raw ANSI escapes `ESC[…m`, written as `\e` in the quoted string | `ESC[38;2;R;G;Bm` | `ESC[38;5;Nm` | `tm/src/netmush/db_objects.c:1069`, `tm/src/netmush/ansi.c` |
| **RhostMUSH** | percent-codes `%cr`, `%c0xHH`, `%c<#RRGGBB>`, `%c<R G B>` | `%c<#RRGGBB>` / `%c<R G B>` | `%c0xHH` | `rhost/Server/hdrs/rhost_ansi.h`, `rhost/Server/src/stringutil.c:945` |

PennMUSH payload vocabulary (inside `\x02c … \x03`): single letters `x r g y b m
c w` (FG) / uppercase (BG), `h` hilite / `u` underscore / `i` inverse / `f`
flash, `#RRGGBB` truecolor, `+xtermN` indexed, `c/` end / `c/a` end-all.

RhostMUSH vocabulary: `%cn` reset, `%ch/%cf/%cu/%ci` attrs, `%c[xrgybmcw]` FG /
uppercase BG, `%c0xHH` / `%c0XHH` xterm, `%c<#RRGGBB>` / `%c<R G B>` truecolor
(uppercase `%C…` for BG).

### TinyMUSH 3.x vs 4.0 caveat

Omega's `t6h` parser targets **TinyMUSH 3.x** (3.0–3.2); the cloned repo is
**4.0**. The 4.0 details above (272-entry `colorDefinitions[]`, CIEDE2000
downgrade in `ansi.c`) are the modern engine, but the on-disk story — raw ANSI
escapes in the attribute text — is the same lineage and is what matters here.
4.0's flatfile/version specifics were not cross-checked against `t6h`.

## What the converter does today (cross-family color matrix)

Verified in `mux/convert/`. "verbatim" = the attribute value is `StringClone`'d
unchanged, so the *source's* color bytes land in the target with no translation.

| Direction | Mechanism | Color result |
|-----------|-----------|--------------|
| t5x → **rhost** | `ConvertColorToRhostSoftcode()` (`t5xgame.cpp`) | **correct** — PUA → `%c…`, 24-bit preserved |
| t5x → **penn** | verbatim copy (`p6hgame.cpp` ConvertFromT5X) | **broken** — PUA bytes land raw, not `\x02c…\x03` |
| t5x → **tinymush** | `ConvertT5XValue()` (`t6hgame.cpp:510`) maps every byte ≥ 0x7F to `'?'` | **broken** — color *and all UTF-8* destroyed |
| **penn** → t5x | verbatim copy (`t5xgame.cpp` ConvertFromP6H) | **broken** — `\x02c…\x03` lands as literal control bytes |
| **tinymush** → t5x | verbatim copy (`t5xgame.cpp` ConvertFromT6H) | **partial** — raw ANSI survives only if output is t5x v1/v2; not parsed to PUA for v3+ |
| **rhost** → t5x | verbatim copy | **broken** — `%c…` lands as literal text (not MUX color) |

So of the six cross-family color directions, **one works** (t5x→rhost). The
others either garble color, or (t5x→tinymush) additionally destroy all non-ASCII
text.

Note: this is independent of the *extraction* (`-x`/@decomp) color path
(`EncodeSubstitutions`), which is a separate concern and was hardened
separately.

## What closing the gaps requires

Each direction needs an in-band transcoder between the source representation and
the TinyMUX PUA/ColorState model the converter already has
(`mux_color`/`UpdateColorState` decode PUA; `ConvertColorToRhostSoftcode` is the
model for an encoder):

1. **penn → t5x** and **t5x → penn**: a `\x02c<codes>\x03` ⇄ PUA transcoder
   (parse/emit the letter/`#RRGGBB`/`+xtermN` vocabulary). Penn's markup is the
   cleanest to map because it is already structured color, not raw ANSI.
2. **rhost → t5x**: a `%c…` parser (the inverse of the existing
   `ConvertColorToRhostSoftcode`). The outbound side already matches Rhost
   exactly, so this is "write the decoder."
3. **tinymush ⇄ t5x**: parse/emit raw ANSI `ESC[…m` ⇄ PUA. TinyMUX already has
   ANSI→PUA in `ConvertToUTF8` (the v2→v3 path) — reuse it. **Also fix
   `ConvertT5XValue`'s `'?'`-for-every-non-ASCII behavior**, which is a Unicode
   bug independent of color.

Priority order (most value / least risk first): rhost→t5x decoder (outbound
already proven) → tinymush ⇄ t5x (reuse existing ANSI↔PUA, fix the Unicode
loss) → penn ⇄ t5x (new transcoder, but well-structured source).

Sources are cloned under `/tmp/srv/{penn,tm,rhost}` for follow-up work.
