#!/usr/bin/env python3
"""Static checks over the NLS marking and catalogues (#1505 / #1419).

The marking work lands in slices -- one file or one subsystem at a time -- and
until now the only thing standing between a bad slice and master was somebody
reading the .pot diff by eye.  I did that four times by hand while reviewing
#1489, #1500, #1502 and #1515; it is not a control that scales to a fleet of
contributors, and the failure modes are not the kind a reviewer reliably spots.

What this does NOT check, and why:

  Whether mux_vsnprintf implements every conversion a msgid uses -- that is
  tests/format/check_formats.py rule 1, which unwraps M_()/T()/S_()/N_() and
  checks the source literal.  What THIS file owns for formats is the other
  half of the Phase-3 contract: every catalogue msgstr for a format msgid
  must carry the same conversion sequence (type and order).  Without that,
  a bad .mo is a live format-string bug at runtime (#1492 / #1419).

Exit status is 0 when everything passes, 1 on the first category with findings.
Run standalone or via `make test-nls`.
"""

import os
import re
import subprocess
import sys
import tempfile

# Softcode-visible tokens.  Anything a game's softcode can string-compare is
# ABI, not prose, and belongs in S_() where it is never extracted.
ABI_PATTERN = re.compile(r'#-\d')

# printf conversions.  %% is an escaped literal percent and %0..%9 are softcode
# substitutions, neither of which is a conversion -- excluding them keeps the
# check from crying wolf over ordinary game prose.
FORMAT_PATTERN = re.compile(r'%[-+ #0-9.*]*[diouxXeEfgGaAcspn]')

MARK_RE = r'(?:M_|T)\s*\(\s*((?:"(?:[^"\\]|\\.)*"\s*)+)\)'


def repo_root():
    return os.path.normpath(
        os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))


def parse_po(path):
    """Return [(msgid, [msgstr, ...], is_fuzzy)] from a .po/.pot.

    Plural entries (#1622) carry msgid_plural and msgstr[0..n-1].  The list
    is every translation form: one element for a singular entry, nplurals for
    a plural one.  Treating msgid_plural as a continuation of msgid -- which
    an earlier version effectively did -- silently welds the two originals
    into one nonsense key, and every check downstream then compares against a
    msgid that does not exist.

    The header (empty msgid) is excluded.
    """
    entries = []
    msgid = None
    msgstrs = []
    fuzzy = False
    pending_fuzzy = False
    target = None

    def flush():
        if msgid is not None:
            entries.append((msgid, list(msgstrs) or [""], fuzzy))

    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.rstrip("\n")
            if line.startswith("#, ") and "fuzzy" in line:
                pending_fuzzy = True
                continue
            if line.startswith("msgid_plural "):
                # Recorded only to keep it out of msgid; the singular is the
                # key everywhere else in this file.
                target = "plural"
                continue
            if line.startswith("msgid "):
                flush()
                msgid = _lit(line[6:])
                msgstrs = []
                fuzzy = pending_fuzzy
                pending_fuzzy = False
                target = "id"
                continue
            if line.startswith("msgstr["):
                msgstrs.append(_lit(line[line.index("]") + 1:]))
                target = "str"
                continue
            if line.startswith("msgstr "):
                msgstrs = [_lit(line[7:])]
                target = "str"
                continue
            if line.startswith('"'):
                if target == "id":
                    msgid = (msgid or "") + _lit(line)
                elif target == "str" and msgstrs:
                    msgstrs[-1] += _lit(line)
                # "plural" continuations are deliberately dropped.
                continue
            if not line.strip():
                target = None
    flush()
    return [e for e in entries if e[0] != ""]


def _lit(s):
    out = []
    for m in re.finditer(r'"((?:[^"\\]|\\.)*)"', s):
        out.append(m.group(1))
    return "".join(out)


def source_files(root):
    files = []
    for d in ("mux/include", "mux/lib", "mux/src", "mux/modules"):
        for dirpath, _, names in os.walk(os.path.join(root, d)):
            if "/rv64/" in dirpath.replace("\\", "/"):
                continue
            for n in names:
                if n.endswith((".cpp", ".c", ".h")):
                    files.append(os.path.join(dirpath, n))
    return sorted(files)


def check_abi_tokens(pot):
    """A softcode-visible token must never be translatable."""
    bad = []
    for msgid, _, _ in pot:
        if ABI_PATTERN.search(msgid):
            bad.append(msgid)
    return [("%s" % m) for m in bad]


def conversion_sequence(s):
    """Ordered list of printf conversion characters (%% excluded).

    Used to compare a msgid against its msgstr: translators may reorder
    words, but must not drop, invent, or change conversion types.  mux_vsnprintf
    has no positional-arg support yet, so order of conversions must match too.
    Width and flags may differ (`%d` vs `%02d`); only the type letter matters.
    """
    # Strip %% so they are not mistaken for a conversion start.
    s = s.replace("%%", "")
    return [m[-1] for m in FORMAT_PATTERN.findall(s)]


def check_format_catalogues(root, pot):
    """Every translated format msgid keeps the same conversion sequence."""
    findings = []
    fmt_ids = {m: conversion_sequence(m) for m, _, _ in pot
               if conversion_sequence(m)}
    if not fmt_ids:
        return findings
    po_dir = os.path.join(root, "mux", "po")
    if not os.path.isdir(po_dir):
        return findings
    for name in sorted(os.listdir(po_dir)):
        if not name.endswith(".po"):
            continue
        path = os.path.join(po_dir, name)
        for msgid, msgstrs, fuzzy in parse_po(path):
            if msgid not in fmt_ids:
                continue
            if fuzzy or not any(msgstrs):
                # check_catalogues already reports fuzzy/untranslated.
                continue
            # Every plural form takes the same arguments, so every form must
            # carry the same conversion sequence as the msgid (#1622).
            for msgstr in msgstrs:
                if not msgstr:
                    continue
                got = conversion_sequence(msgstr)
                want = fmt_ids[msgid]
                if got != want:
                    findings.append(
                        "%s: format mismatch for %r -- msgid %s, msgstr %s"
                        % (name, msgid, want, got))
    return findings


def check_half_marking(root):
    """The same literal marked in one place and cast in another, same file.

    This is what makes a translated game render two languages side by side:
    one call site translates, its twin does not.  It is invisible in a .pot
    diff, because the .pot only ever shows the marked half.
    """
    findings = []
    for path in source_files(root):
        try:
            text = open(path, encoding="utf-8", errors="replace").read()
        except OSError:
            continue
        marked, cast = set(), set()
        for m in re.finditer(MARK_RE, text):
            lit = _lit(m.group(1))
            if not lit:
                continue
            (marked if m.group(0).lstrip().startswith("M_") else cast).add(lit)
        both = marked & cast
        for lit in sorted(both):
            findings.append("%s: %r is both M_() and T()"
                            % (os.path.relpath(path, root), lit))
    return findings


def po_header(path):
    """Header field dict from the leading empty-msgid entry."""
    fields = {}
    with open(path, encoding="utf-8") as fh:
        started = False
        for line in fh:
            line = line.strip()
            if line.startswith("msgid "):
                if started:
                    break
                started = _lit(line[6:]) == ""
                continue
            if not started or not line.startswith(('"', "msgstr")):
                continue
            for part in _lit(line[7:] if line.startswith("msgstr") else line).split("\\n"):
                if ":" in part:
                    k, v = part.split(":", 1)
                    fields[k.strip().lower()] = v.strip()
    return fields


def catalogue_policy(path):
    """'complete' or 'partial' -- how strictly to judge coverage.

    Declared per catalogue in the .po header as X-Tinymux-Catalogue.  It is a
    real distinction, not a way to silence the check:

      complete  the catalogue is generated mechanically and every msgid must be
                translated (xx, the pseudo-locale).  A gap there means the
                marking or generation pipeline broke, which is exactly what
                this check exists to catch.

      partial   a human translation.  Partial coverage is the normal gettext
                state -- untranslated msgids fall back to English by design --
                and `fuzzy` is the standard marker a translator uses for "needs
                work", which msgmerge also sets on its own.  Failing the build
                on either would mean no human locale could ever be committed
                except complete and perfect, so nobody would commit one.

    Defaults to partial: a new human catalogue that forgets the header gets the
    lenient policy, which is the safe direction.  Structural errors (stale or
    missing msgids, format-sequence mismatches) stay fatal under both.
    """
    return po_header(path).get("x-tinymux-catalogue", "partial").lower()


def check_catalogues(root, pot):
    """Catalogue integrity, judged against each catalogue's declared policy.

    Fuzzy matters more than it looks under a 'complete' policy: msgfmt DROPS
    fuzzy entries when building the .mo, silently.  A catalogue regenerated
    with a plain `msgmerge -U` inherits fuzzy matches for its new strings and
    then ships with those strings untranslated, while every count in the .po
    still looks complete.
    """
    findings = []
    ids = {m for m, _, _ in pot}
    po_dir = os.path.join(root, "mux", "po")
    for name in sorted(os.listdir(po_dir)):
        if not name.endswith(".po"):
            continue
        path = os.path.join(po_dir, name)
        entries = parse_po(path)
        have = {m for m, _, _ in entries}
        fuzzy = [m for m, _, f in entries if f]
        # A list of forms now (#1622).  Empty if nothing is translated;
        # partially filled is still a defect, since msgfmt will emit the
        # blank forms and those messages render empty at runtime.
        untranslated = [m for m, forms, _ in entries if not any(forms)]
        partial = [m for m, forms, _ in entries
                   if any(forms) and not all(forms)]
        missing = ids - have
        extra = have - ids

        # Structural: fatal regardless of policy.  A stale msgid is dead weight
        # a translator will keep maintaining; a missing one means the catalogue
        # was never msgmerge'd against the current .pot.
        for m in sorted(missing)[:5]:
            findings.append("%s: missing msgid %r (in .pot, not in catalogue)"
                            % (name, m))
        for m in sorted(extra)[:5]:
            findings.append("%s: stale msgid %r (in catalogue, not in .pot)"
                            % (name, m))

        if catalogue_policy(path) == "complete":
            for m in sorted(fuzzy)[:5]:
                findings.append("%s: fuzzy msgid %r -- msgfmt will DROP this"
                                % (name, m))
            for m in sorted(partial)[:5]:
                findings.append(
                    "%s: msgid %r has some plural forms translated and some "
                    "empty -- the empty ones render blank" % (name, m))
            for m in sorted(untranslated)[:5]:
                findings.append("%s: untranslated msgid %r" % (name, m))
    return findings


def coverage_report(root, pot):
    """Per-catalogue coverage, printed whether or not anything failed.

    Partial coverage is allowed but should never be invisible: a locale
    quietly rotting from 90% to 40% as the .pot grows is a real regression,
    and without a number nobody would notice it in a diff.
    """
    lines = []
    total = len({m for m, _, _ in pot})
    po_dir = os.path.join(root, "mux", "po")
    for name in sorted(os.listdir(po_dir)):
        if not name.endswith(".po"):
            continue
        path = os.path.join(po_dir, name)
        entries = parse_po(path)
        done = len([m for m, forms, f in entries if all(forms) and not f])
        fuzzy = len([m for m, _, f in entries if f])
        pct = (100.0 * done / total) if total else 0.0
        lines.append("  %-10s %4d/%-4d (%5.1f%%) translated, %2d fuzzy  [%s]"
                     % (name, done, total, pct, fuzzy, catalogue_policy(path)))
    return lines


def check_pot_fresh(root, pot):
    """The .pot must match what the sources actually mark.

    A slice that marks prose and forgets to regenerate leaves those strings
    out of every catalogue.  Nothing else notices: the code compiles, the
    suite passes, and the strings simply never translate.
    """
    script = os.path.join(root, "mux", "po", "update-pot.sh")
    if not os.path.exists(script):
        return ["mux/po/update-pot.sh not found"]
    try:
        subprocess.run(["xgettext", "--version"],
                       capture_output=True, check=True)
    except (OSError, subprocess.CalledProcessError):
        print("  note: xgettext not installed -- skipping freshness check")
        return []

    real = os.path.join(root, "mux", "po", "tinymux.pot")
    with open(real, encoding="utf-8") as fh:
        saved = fh.read()
    with tempfile.NamedTemporaryFile("w", suffix=".pot", delete=False) as tf:
        backup = tf.name
        tf.write(saved)
    try:
        res = subprocess.run(["bash", script], capture_output=True, text=True)
        if res.returncode != 0:
            return ["update-pot.sh failed: %s" % res.stderr.strip()[:200]]
        fresh = {m for m, _, _ in parse_po(real)}
        current = {m for m, _, _ in pot}
        findings = []
        for m in sorted(fresh - current)[:8]:
            findings.append("marked in source but absent from .pot: %r "
                            "(regenerate: make -C mux/po pot)" % m)
        for m in sorted(current - fresh)[:8]:
            findings.append("in .pot but no longer marked in source: %r" % m)
        return findings
    finally:
        with open(real, "w", encoding="utf-8") as fh:
            fh.write(saved)
        os.unlink(backup)


def main():
    root = repo_root()
    pot_path = os.path.join(root, "mux", "po", "tinymux.pot")
    if not os.path.exists(pot_path):
        print("no mux/po/tinymux.pot -- nothing to check")
        return 0
    pot = parse_po(pot_path)

    categories = [
        ("softcode ABI tokens in translatable strings",
         check_abi_tokens(pot),
         "A '#-1 ...' token is what softcode string-compares against.  "
         "Translating it breaks every game that tests for it.  Use S_()."),
        ("printf conversions in catalogue translations",
         check_format_catalogues(root, pot),
         "A format msgid is fine in the pot (Phase 3), but its msgstr must "
         "carry the same conversion sequence -- same types, same order.  "
         "mux_vsnprintf has no positional args, and a mismatched .mo is a "
         "live format-string bug at runtime."),
        ("half-marked literals",
         check_half_marking(root),
         "The same text is M_() in one place and T() in another within one "
         "file, so a translated game renders both languages at once."),
        ("catalogue integrity",
         check_catalogues(root, pot),
         "Stale and missing msgids are errors in every catalogue.  Under an "
         "X-Tinymux-Catalogue: complete policy, fuzzy and untranslated entries "
         "are too -- msgfmt drops fuzzy entries without saying so."),
        (".pot freshness",
         check_pot_fresh(root, pot),
         "The .pot does not match what the sources mark.  Strings marked but "
         "not extracted never reach a catalogue and never translate."),
    ]

    total = 0
    for title, findings, advice in categories:
        if findings:
            total += len(findings)
            print("=== NLS guard: %s -- %d finding(s) ===" % (title,
                                                              len(findings)))
            for f in findings:
                print("  %s" % f)
            print("  %s" % advice)
            print()

    print("=== NLS guard: catalogue coverage ===")
    for line in coverage_report(root, pot):
        print(line)
    print()

    if total:
        print("=== NLS guard: %d finding(s) ===" % total)
        return 1

    n_files = len({f for f in source_files(root)})
    print("=== NLS guard: %d msgids, %d source files, all clean ==="
          % (len(pot), n_files))
    return 0


if __name__ == "__main__":
    sys.exit(main())
