# Server-message catalogs (NLS, #1419 / #1444 / #1473)

See `docs/plan-server-i18n.md`.

## What is in the catalog

| Macro | Role | In `.pot`? |
|-------|------|------------|
| **`M_("…")`** | Player/staff prose (opt-in), including format templates | Yes — primary keyword |
| **`N_("…")`** | Static msgid storage before `M_()` rebind | Yes — same strings as `M_` |
| **`T("…")`** | UTF-8 cast only (names, internals) | **No** |
| **`S_("…")`** | Softcode / machine ABI (`#-1 …`) | **No** |

### Format strings (Phase 3)

Whole notify sentences that take arguments are marked as
`tprintf(M_("You give %d %s to %s."), …)` (same for `mux_sprintf` /
`raw_broadcast`). Translators must keep the same conversion types in the
same order — `mux_vsnprintf` has no positional `%1$s` yet. `make test-nls`
rejects a msgstr whose conversion sequence differs from its msgid.

**Mark whole sentences.** If a sentence is assembled from pieces, either mark
*all* of it or *none* of it. A fragment is only safe under `M_()` when every
other piece of that sentence is also `M_()` and the assembly order is fixed
for all languages. Marking one piece while a neighbour stays `T()` guarantees
mixed-language output under a translated catalogue — worse than leaving the
whole sentence English. Prefer two whole-sentence msgids over a conditional
fragment (see #1575, #1588). The static guards cannot catch this; it is a
review habit.

Maintaining translations is ongoing cost. Prefer **small, deliberate `M_()` sites** over bulk extraction of every `T()`.

## Regenerate the template (`.pot`)

Requires GNU gettext (`xgettext`). From the repo:

```bash
make -C mux/po pot
# or:  mux/po/update-pot.sh
```

This scans `mux/{include,lib,src,modules}` for **`M_`** and **`N_`** only and writes `tinymux.pot`.

After marking new prose, re-run `pot`, then merge into language files:

```bash
msgmerge -U xx.po tinymux.pot   # update existing .po against new pot
```

## Build a binary catalog

```bash
make -C mux/po mo
# or:  msgfmt -c -o ../game/locale/xx/LC_MESSAGES/tinymux.mo xx.po
```

**Keep the `-c`.** It is `--check`, and for a `c-format` entry it verifies that
the translation uses the same conversions as the msgid. Without it `msgfmt`
will happily compile a catalogue in which `"You give %d %s to %s."` has been
translated with `%s` where the `%d` was — and `mux_vsnprintf` then walks the
integer argument as a `UTF8 *`. That is a **SIGSEGV on an ordinary `give`
command**, not a formatting glitch; it was measured that way while the format
slices were being designed.

`make test` runs the same check over every `.po` in the tree
(`tests/nls/check_nls.py`), but a translator building a catalogue outside the
repo never runs the suite. The flag is what protects them.

## Runtime

Start netmux with cwd = `game/` (usual) and:

```bash
# gettext ignores LANGUAGE when the process locale is C/POSIX.
# Prefer a real UTF-8 locale, then select the catalog via LANGUAGE:
export LANG=en_US.UTF-8
export LANGUAGE=xx
# ensure game/locale/xx/LC_MESSAGES/tinymux.mo exists
./bin/netmux
```

On minimal hosts without `en_US.UTF-8` installed, generate a user locale:

```bash
localedef -f UTF-8 -i en_US $HOME/.locale/en_US.UTF-8
export LOCPATH=$HOME/.locale LANG=en_US.UTF-8 LANGUAGE=xx
```

Without `--enable-nls`, catalogs are ignored and English is compiled in.

## Sample pseudo-locale

`xx.po` translates selected strings (e.g. `Permission denied.` → `[xx] Permission denied.`) for manual checks only — the smoke suite must keep English (`LANG=C` / `LANGUAGE=`).

## Softcode ABI

`#-1 …` diagnostics use `S_()` and are **never** passed to gettext.

## Empty strings (#1443)

`gettext("")` returns the catalog **header**, not `""`. `M_()` goes through `mux_gettext`, which leaves empty msgids (and `nullptr`) untouched. `T("")` is only a cast and never hits gettext.

## Testing (#1476)

**English suite / smoke (NLS-on builds):** never set `LANGUAGE=xx`. Prefer:

```bash
cd mux
./configure --enable-realitylvls --enable-wodrealms --enable-jit --enable-nls
# from repo root
make install
env -u LANGUAGE LANG=C LC_ALL=C make test
# or: cd testcases && env -u LANGUAGE LANG=C ./tools/Makesmoke && env -u LANGUAGE LANG=C ./tools/Smoke
```

**Catalog smoke (manual, not the suite):** process-wide locale must be non-C for `LANGUAGE` to apply:

```bash
cd mux/game
printf 'think [div(1,0)]\n@shutdown\n' | \
  LC_ALL=en_US.UTF-8 LANGUAGE=xx ./bin/muxscript --readonly -g .
# softcode stays English: #-1 DIVIDE BY ZERO
```
