# Server-message catalogs (NLS, #1419 / #1444 / #1473)

See `docs/plan-server-i18n.md`.

## What is in the catalog

| Macro | Role | In `.pot`? |
|-------|------|------------|
| **`M_("…")`** | Player/staff prose (opt-in) | Yes — primary keyword |
| **`N_("…")`** | Static msgid storage before `M_()` rebind | Yes — same strings as `M_` |
| **`T("…")`** | UTF-8 cast only (names, formats, internals) | **No** |
| **`S_("…")`** | Softcode / machine ABI (`#-1 …`) | **No** |

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
# or:  msgfmt -o ../game/locale/xx/LC_MESSAGES/tinymux.mo xx.po
```

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
