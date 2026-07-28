#!/usr/bin/env python3
"""Merge newly marked M_()/N_()/MN_() msgids into pot/xx/ko without xgettext.

Used when gettext tools are unavailable (e.g. Windows agents).  Linux CI with
xgettext still regenerates a canonical pot via mux/po/update-pot.sh; this only
keeps catalogues structurally consistent so check_nls.py passes.

Msgid representation matches tests/nls/check_nls.py: C escapes stay as
two-character sequences (backslash + letter), not real control characters.

Usage (from repo root):
  python tools/merge_nls_markings.py
"""

from __future__ import annotations

import datetime as dt
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
sys.path.insert(0, os.path.join(ROOT, "tests", "nls"))
import check_nls  # noqa: E402

MUX = os.path.join(ROOT, "mux")
PO_DIR = os.path.join(MUX, "po")
POT = os.path.join(PO_DIR, "tinymux.pot")
XX = os.path.join(PO_DIR, "xx.po")
KO = os.path.join(PO_DIR, "ko.po")

MARK_RE = re.compile(
    r"\b(?:M_|N_|MN_)\s*\(\s*((?:\"(?:[^\"\\]|\\.)*\"\s*)+)\)",
    re.MULTILINE,
)
STR_RE = re.compile(r"\"((?:[^\"\\]|\\.)*)\"")


def po_quote(s: str) -> str:
    """Quote a check_nls-form msgid for a .po line (escapes already in s)."""
    return '"' + s.replace('"', '\\"') + '"'


def collect_marks():
    """msgid -> sorted list of 'relpath:line' refs."""
    found: dict[str, set[str]] = {}
    for base in ("include", "lib", "src", "modules"):
        root = os.path.join(MUX, base)
        if not os.path.isdir(root):
            continue
        for dirpath, _, files in os.walk(root):
            for name in files:
                if not name.endswith((".c", ".cpp", ".h")):
                    continue
                path = os.path.join(dirpath, name)
                rel = os.path.relpath(path, MUX).replace("\\", "/")
                try:
                    text = open(path, encoding="utf-8", errors="replace").read()
                except OSError:
                    continue
                for m in MARK_RE.finditer(text):
                    # Same form as check_nls._lit: raw insides of "..." pieces.
                    lit = "".join(STR_RE.findall(m.group(1)))
                    if not lit:
                        continue
                    line = text.count("\n", 0, m.start()) + 1
                    found.setdefault(lit, set()).add("%s:%d" % (rel, line))
    return {k: sorted(v) for k, v in found.items()}


def has_format(s: str) -> bool:
    # Percent conversions; \\% is not a conversion in this form.
    return bool(re.search(r"(?<!\\)%[-+ #0-9.*]*[diouxXeEfgGaAcspn]", s))


def append_entries(path: str, entries: list[tuple[str, list[str]]], xx: bool):
    if not entries:
        return
    with open(path, "a", encoding="utf-8", newline="\n") as fh:
        for msgid, refs in entries:
            fh.write("\n")
            for i in range(0, len(refs), 2):
                fh.write("#: %s\n" % " ".join(refs[i : i + 2]))
            if has_format(msgid):
                fh.write("#, c-format\n")
            fh.write("msgid %s\n" % po_quote(msgid))
            if xx:
                fh.write("msgstr %s\n" % po_quote("[xx] " + msgid))
            else:
                fh.write('msgstr ""\n')


def bump_pot_date(path: str, now: str) -> None:
    text = open(path, encoding="utf-8").read()
    text2 = re.sub(
        r'("POT-Creation-Date: )[^\\]+(\\n")',
        r"\g<1>%s\2" % now,
        text,
        count=1,
    )
    if text2 != text:
        open(path, "w", encoding="utf-8", newline="\n").write(text2)


def main() -> int:
    marks = collect_marks()
    pot_ids = {m for m, _, _ in check_nls.parse_po(POT)}
    new = sorted(set(marks) - pot_ids)
    if not new:
        print("No new M_/N_/MN_ msgids to merge (%d already in pot)." % len(pot_ids))
        return 0

    print("Merging %d new msgid(s):" % len(new))
    for m in new:
        print("  %r" % m)

    now = dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%d %H:%M+0000")
    for path in (POT, XX, KO):
        bump_pot_date(path, now)

    entries = [(m, marks[m]) for m in new]
    append_entries(POT, entries, xx=False)
    append_entries(XX, entries, xx=True)
    append_entries(KO, entries, xx=False)

    pot2 = {m for m, _, _ in check_nls.parse_po(POT)}
    xx2 = {m for m, _, _ in check_nls.parse_po(XX)}
    if pot2 != xx2:
        print("ERROR: pot/xx msgid sets diverge after merge", file=sys.stderr)
        print("  only pot:", sorted(pot2 - xx2)[:5], file=sys.stderr)
        print("  only xx:", sorted(xx2 - pot2)[:5], file=sys.stderr)
        return 1
    print("Updated tinymux.pot, xx.po, ko.po (%d msgids)" % len(pot2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
