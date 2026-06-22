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
| **rhost** → t5x | `ConvertColorFromRhostSoftcode()` (`t5xgame.cpp`) | **correct** — `%c…` → PUA, 24-bit preserved (rhost→t5x now produces v3) |
| t5x → **tinymush** | `DowngradeToT6H` / `ConvertColorToANSI24`, text → Latin-1 | **correct** — color as raw ANSI, 256 (`38;5;N`) and 24-bit (`38;2;R;G;B`) preserved |
| **tinymush** → t5x | verbatim copy to v2, then the standard v2→v3 upgrade | **correct** — raw ANSI → PUA via `ConvertToUTF8` (now incl. 256/24-bit) and Latin-1 → UTF-8 (with `-v 3`+; a v2 target keeps ANSI) |
| **penn** → t5x | `ConvertColorFromPennMarkup()` (`t5xgame.cpp`) | **correct** — `\x02c…\x03` markup → PUA, 24-bit preserved (penn→t5x now produces v3) |
| t5x → **penn** | `DowngradeToPenn()` / `ConvertColorToPennMarkup()` | **correct** — PUA → `\x02c…\x03` markup, 24-bit preserved; writer sets DBF_SPIFFY_AF_ANSI; text kept UTF-8 |

So **all six cross-family color directions now work, at full 24-bit** (color
survives in both directions for PennMUSH, TinyMUSH, and RhostMUSH).  TinyMUSH
24-bit lands as raw ANSI escapes, which TinyMUSH 4.0 renders; pre-4.0 servers
ignore the unknown SGR parameters.  Text into TinyMUSH is still Latin-1 (its
8-bit storage), lossy only for code points outside Latin-1.

TinyMUSH note: TinyMUSH stores color as raw ANSI escapes and 8-bit Latin-1
text.  Color is carried at full depth (16/256/24-bit) via the ANSI escapes;
text outside Latin-1 is the only lossy part (TinyMUSH has no wider text
storage).  An earlier converter shortcut reduced color to 16 and mapped every
high byte to `'?'`; both are fixed.

Note (found while implementing rhost→t5x): `ConvertFromR7H` emitted object
headers without the mandatory ZONE/LINK fields for zone-/link-less objects,
while declaring `V_ZONE`/`V_LINK` — so the produced t5x desynced the reader at
the first such object, silently dropping the rest of the database.  Fixed by
writing `-1` (NOTHING) for those fields.  `ConvertFromP6H`/`ConvertFromT6H`
happen to set them for every object, but a real PennMUSH db with a zone-less
object could hit the same class of bug — worth a look when those paths are
revisited.

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
2. **rhost → t5x**: DONE — `ConvertColorFromRhostSoftcode()` parses the `%c…`
   vocabulary into PUA (inverse of `ConvertColorToRhostSoftcode`), and
   `ConvertFromR7H` now produces a v3 (UTF-8/PUA) flatfile.
3. **tinymush ⇄ t5x**: DONE — color crosses as raw ANSI ⇄ PUA using the existing
   `Downgrade2`/`ConvertToUTF8` machinery; `ConvertT5XValue` no longer maps
   representable Latin-1 to `'?'`.
4. **penn ⇄ t5x**: DONE — `ConvertColorFromPennMarkup()` (inbound, penn→t5x v3)
   and `ConvertColorToPennMarkup()`/`DowngradeToPenn()` (outbound) transcode the
   `\x02c<codes>\x03` markup ⇄ PUA; the penn writer sets DBF_SPIFFY_AF_ANSI.

All six cross-family color directions are implemented at full 24-bit.
`ConvertToUTF8` now parses 256 (`38;5;N`) and 24-bit (`38;2;R;G;B`) ANSI (plus
bright `90-97`/`100-107`), and `ConvertColorToANSI24` emits them, so the
TinyMUSH path no longer reduces to 16.

Remaining color/Unicode follow-ups (not blocking): the direct penn->t6h path
(ConvertFromP6H on the t6h side) is not part of the t5x-mediated color plumbing;
and text into TinyMUSH/Rhost is still Latin-1/ASCII.

Sources are cloned under `/tmp/srv/{penn,tm,rhost}` for follow-up work.
