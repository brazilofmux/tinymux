# Server-message catalogs (NLS spike, #1419)

See `docs/plan-server-i18n.md`.

- **Player notifies** use `T("…")` → gettext when built with `--enable-nls`.
- **Softcode diagnostics** (`#-1 …`) use `S_("…")` and are **never** translated.

## Build a binary catalog

```bash
msgfmt -o ../game/locale/xx/LC_MESSAGES/tinymux.mo xx.po
```

## Runtime

Start netmux with cwd = `game/` (usual) and:

```bash
# gettext ignores LANGUAGE when the process locale is C/POSIX.
# Prefer a real UTF-8 locale, then select the catalog via LANGUAGE:
export LANG=en_US.UTF-8   # or C.UTF-8 is *not* enough on some glibc hosts
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

`#-1 …` diagnostics use `S_()` and are **never** passed to gettext. Player globals that used to initialize with `T()` at static load now use `N_()` + `mux_nls_refresh_messages()` after `bindtextdomain`.

## Empty strings (#1443)

`gettext("")` returns the catalog **header**, not `""`. `T()` goes through `mux_gettext`, which leaves empty msgids (and `nullptr`) untouched. Do not put an empty `msgid` in a player-facing catalog entry.

## Direction note (#1444)

Phase 1 still maps `T()` → gettext. That is tree-wide (identifiers, formats, `#-1` not yet `S_()`). A safer long-term shape is opt-in prose (`M_()` / similar) with `T()` remaining a cast — see issue **#1444**.
