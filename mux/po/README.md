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

### Mark whole sentences — all of it or none of it

A sentence assembled from several pieces must be marked **entirely or not at
all**. Marking one piece of an assembled sentence makes the output *worse*
than leaving the whole thing unmarked, because an unmarked sentence is at
least consistently English.

This is real, not hypothetical — `speech.cpp` built its page echo as

```c
safe_str(T("From afar, "), omessage, &omp);                                // English
safe_tprintf_str(omessage, &omp, M_("to %s: "), aFriendly.get());          // translated
safe_tprintf_str(omessage, &omp, T("%s %s"), Moniker(executor), pMessage); // English
```

which renders two languages in one line under any catalogue.

Fragments are a problem even when every piece *is* marked. `create.cpp` once
had

```
"Home of %s(#%d) changed from "  +  "%s(#%d) to "  +  "%s(#%d)."
```

Three msgids for one sentence freezes English clause order — a translator
cannot move "changed from" relative to the subject, and languages that invert
from/to or put the verb last cannot express it. The trailing spaces are
load-bearing and invisible in most PO editors. And `"%s(#%d) to "` gives a
translator no context for what "to" joins. Both were folded into single
six-conversion formats.

When a piece is conditional, use one whole sentence per branch rather than a
fragment in the middle:

```c
if (bQualified)
    ... M_("From afar, to %s: %s %s"), who, actor, msg);
else
    ... M_("From afar, %s %s"), actor, msg);
```

**No automated check catches this.** `make test-nls` sees a perfectly valid
msgid either way; the guard cannot know the sentence continues in the next
call. It is a review habit.

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
