# Server-message catalogs (NLS, #1419 / #1444)

See `docs/plan-server-i18n.md`.

## What is in the catalog

| Macro | Role | In `.pot`? |
|-------|------|------------|
| **`M_("…")`** | Player/staff prose (opt-in) | Yes — primary keyword |
| **`N_("…")`** | Static msgid storage before `M_()` rebind | Yes — same strings as `M_` |
| **`T("…")`** | UTF-8 cast only (names, formats, internals) | **No** |
| **`S_("…")`** | Softcode / machine ABI (`#-1 …`) | **No** |

Maintaining translations is ongoing cost. Prefer **small, deliberate `M_()` sites** over bulk extraction of every `T()`.

Extract (once the pipeline exists):

```bash
xgettext -kM_ -kN_ -o tinymux.pot …
```

## Build a binary catalog

```bash
msgfmt -o ../game/locale/xx/LC_MESSAGES/tinymux.mo xx.po
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

`xx.po` translates `Permission denied.` → `[xx] Permission denied.` for manual checks only — the smoke suite must keep English (`LANG=C` / `LANGUAGE=`).

## Softcode ABI

`#-1 …` diagnostics use `S_()` and are **never** passed to gettext.

## Empty strings (#1443)

`gettext("")` returns the catalog **header**, not `""`. `M_()` goes through `mux_gettext`, which leaves empty msgids (and `nullptr`) untouched. `T("")` is only a cast and never hits gettext.
