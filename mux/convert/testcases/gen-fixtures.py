#!/usr/bin/env python3
"""Regenerate the Omega converter test fixtures.

Fixtures live in ./fixtures and are committed so the test driver (run.sh) is
hermetic.  This script reproduces them from the tracked TinyMUX seed database
so they can be refreshed deterministically.

Usage:
    ./gen-fixtures.py [SEED_FLATFILE]

SEED_FLATFILE defaults to ../../game/data/netmux.db (a tracked v3 flatfile).
The built ../omega binary is required.

What it builds:
    fixtures/t5x-v5.flat        - the seed normalized to TinyMUX v5 (no 24-bit
                                  color); the canonical clean base.
    fixtures/t5x-v5-color.flat  - the same, with a known 24-bit FG and BG color
                                  injected into the first attribute value, in
                                  the v5 two-code-point PUA form.
    fixtures/t5x-v5-stress.flat - the base with a large, maximally color-dense
                                  attribute (used by run-asan.sh to probe the
                                  color buffer bounds).
    fixtures/p6h-new.flat       - the base converted to PennMUSH (new style).
    fixtures/t6h-3.1.flat       - the base converted to TinyMUSH (3.1).
    fixtures/r7h-v7.flat        - the base converted to RhostMUSH (v7).

The family fixtures are produced by omega's own cross-conversion (we have no
captured real-server flatfiles for the other codebases); they still exercise
each family's parser and writer for round-trip regressions.

The injected colors are chosen over palette base index 0 (black) so every
channel differs from the base; that exercises all four v5 SMP blocks and, after
a downgrade, all six v4 per-channel delta codes.
"""

import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OMEGA = os.path.join(HERE, "..", "omega")
FIXTURES = os.path.join(HERE, "fixtures")

# Known v5 (two-code-point) PUA sequences, base index 0:
#   FG RGB(200,135,100): base EF 98 80 ; CP1 F3 B0 B2 87 ; CP2 F3 B1 A1 A4
#   BG RGB( 20, 60, 90): base EF 9C 80 ; CP1 F3 B2 84 BC ; CP2 F3 B3 91 9A
FG_COLOR = bytes([0xEF, 0x98, 0x80, 0xF3, 0xB0, 0xB2, 0x87, 0xF3, 0xB1, 0xA1, 0xA4])
BG_COLOR = bytes([0xEF, 0x9C, 0x80, 0xF3, 0xB2, 0x84, 0xBC, 0xF3, 0xB3, 0x91, 0x9A])

# A maximally color-dense attribute value: every visible character is preceded
# by a full 16-color state flip (toggle intense, change FG and BG).  16-color
# codes survive RestrictToColor16 and expand through ConvertColorToANSI, so this
# drives the downgrade/restrict color buffers near their worst-case expansion --
# the input that the ASan harness (run-asan.sh) uses to probe buffer bounds.
RESET = bytes([0xEF, 0x94, 0x80])
INTENSE = bytes([0xEF, 0x94, 0x81])
def _fg(i): return bytes([0xEF, 0x98, 0x80 | i])   # U+F600 + i
def _bg(i): return bytes([0xEF, 0x9C, 0x80 | i])   # U+F700 + i


def stress_value(target):
    unit_a = INTENSE + _fg(1) + _bg(4) + b'X'
    unit_b = RESET + _fg(2) + _bg(1) + b'Y'
    out = bytearray()
    while len(out) < target - len(unit_a) - len(unit_b):
        out += unit_a
        out += unit_b
    return bytes(out)


def omega(*args):
    subprocess.run([OMEGA, *args], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def inject(src, dst, blob):
    data = open(src, "rb").read()
    # Insert bytes just inside the first attribute value's opening quote (lines
    # of the form  >NUM\n"value"  ).  PUA bytes are valid UTF-8 and are not
    # quote/backslash/control, so they survive the writer's escaping.
    m = re.search(rb'>\d+\n"', data)
    if not m:
        sys.exit("no attribute value found in %s" % src)
    pos = m.end()
    open(dst, "wb").write(data[:pos] + blob + data[pos:])


def main():
    seed = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        HERE, "..", "..", "game", "data", "netmux.db")
    if not os.path.isfile(OMEGA):
        sys.exit("omega not built; run 'make' in %s" % os.path.join(HERE, ".."))
    if not os.path.isfile(seed):
        sys.exit("seed flatfile not found: %s" % seed)

    os.makedirs(FIXTURES, exist_ok=True)
    base = os.path.join(FIXTURES, "t5x-v5.flat")
    color = os.path.join(FIXTURES, "t5x-v5-color.flat")

    omega("-v", "5", seed, base)
    inject(base, color, FG_COLOR + BG_COLOR)
    inject(base, os.path.join(FIXTURES, "t5x-v5-stress.flat"), stress_value(64000))
    # An attribute that expands on extraction ("  " -> "%b "); 32000 double
    # spaces become 32000 "%b " (~96KB), overflowing the old 1*LBUF extraction
    # buffer.  Guards the silent-truncation fix in EncodeSubstitutions.
    inject(base, os.path.join(FIXTURES, "t5x-v5-expand.flat"), b'  ' * 32000)

    # Family fixtures, produced by cross-conversion from the v5 base.
    #
    families = [
        ("pennmush",  "p6h-new.flat"),
        ("tinymush",  "t6h-3.1.flat"),
        ("rhostmush", "r7h-v7.flat"),
    ]
    for fam, fname in families:
        omega("-o", fam, base, os.path.join(FIXTURES, fname))

    # Sanity: the color fixture must parse and round-trip v5->v5 unchanged.
    rt = color + ".rt"
    omega(color, rt)
    if open(color, "rb").read() != open(rt, "rb").read():
        os.remove(rt)
        sys.exit("color fixture failed v5->v5 round-trip")
    os.remove(rt)

    for f in sorted(os.listdir(FIXTURES)):
        p = os.path.join(FIXTURES, f)
        if os.path.isfile(p):
            print("wrote %s (%d bytes)" % (p, os.path.getsize(p)))


if __name__ == "__main__":
    main()
