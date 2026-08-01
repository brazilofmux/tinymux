# ansify

Convert TinyMUX percent-color substitutions (`%x` / `%c`) into real ANSI
escape sequences. Commonly used to “ansify” a connect screen or other MUX-style
text for display outside the server.

This is the proper home for the old FTP `contrib/ansify.l` tool
(Brazil@BrazilMUX). That version was a **flex** scanner; this one is a
**Ragel -G2** scanner (`ansify.rl` → `ansify.c`), same generator style as
`mux/muxescape` and `mux/lib/color_ops`.

## Build

```bash
cd tools/ansify
make          # compile checked-in ansify.c → ./ansify (no Ragel required)
make test
```

A C compiler is enough for a normal build. Generated `ansify.c` is checked in
so nobody needs [Ragel](https://www.colm.net/open-source/ragel/) just to compile
or run the tool.

After editing the scanner source (`ansify.rl`):

```bash
make regen    # ragel -G2 -C -o ansify.c ansify.rl
make test
# commit both ansify.rl and ansify.c
```

Do not hand-edit `ansify.c`. Not part of the top-level `mux` autotools build.

## Usage

```bash
# stdin → stdout
./ansify < connect.mux > connect.txt

# files
./ansify samples/connect.mux

# strip codes instead of expanding
./ansify --strip < colored.txt > plain.txt
```

## Codes

Prefix is `%x` or `%c` (case of the prefix is ignored).

| Kind | Letters |
|------|---------|
| Attributes | `n` normal, `h` hilite, `u` underline, `f` blink, `i` inverse |
| Foreground | `x`/`k` black, `r` `g` `y` `b` `m` `c` `w` |
| Background | `X`/`K` black, `R` `G` `Y` `B` `M` `C` `W` |

Black uses **`x`/`X`** in current TinyMUX (`aColors`). The original ansify.l
used **`k`/`K`**; both are accepted.

Extended forms:

| Form | Result |
|------|--------|
| `%x<#RRGGBB>` / `%x<#RGB>` | truecolor FG (`ESC[38;2;…m`) |
| `%x<bg#RRGGBB>` / `%x<#RRGGBB;bg>` | truecolor BG |
| `%x<N>` | xterm-256 FG (`N` = 0..255) |
| `%x<bgN>` | xterm-256 BG |
| `%%` | literal `%` |

`%c` is accepted as an alias for `%x` in all of the above.

## History

The FTP mirror once shipped `contrib/ansify.l` as a flex source with a
tongue-in-cheek README. That tree was retired from the public FTP. The tool
lives here as a Ragel machine so it stays consistent with the rest of TinyMUX’s
scanner tooling and can evolve with the server’s color language.

## License

Same as the TinyMUX repository.
