#!/usr/bin/env python3
"""Static guard: no smoke case may be incapable of failing.

A `tr.tc*` label that logs a Succeeded branch and has no non-success branch at
all reports the same verdict on every run.  It contributes to the headline
count and cannot ever go red, so it reads as coverage while asserting nothing.

That is not hypothetical.  Every one of these was found by a person noticing,
one at a time:

  #1434  protect_fn TC001 was literally
             @log smoke=TC001: @protect command exists. Succeeded.
         with no command run -- it would have passed if @protect had been
         deleted from the source.
  #1438  all six cron_fn cases logged Succeeded unconditionally.
  #1460  route_fn's five zone objects were never created (a one-argument
         create()), yet its seven zone-named cases passed either way (#1498).
  #1413  a regression test that only failed on the host it was written on.
  #1426  Lua JIT coverage asserting the attempt rather than the execution.

This turns that sweep into something the build does, the same way #1439 turned
"nothing in the tree uses these formats" into a build-time check rather than a
grep someone remembers to run.

What it does NOT catch: a case with both branches whose assertion is trivially
true anyway (route_fn's zone cases, post-#1491, are exactly that -- they can
report Failed, they just never will).  Detecting those needs a negative
control per case, not a parse.  This is the mechanical half.

Exit 1 on a finding, 0 otherwise.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from generate_smoke_suite import classify_labels  # noqa: E402


# Labels that report a measurement rather than assert a property, so "cannot
# fail" is correct for them.  Each entry is an assertion that the label is an
# observation; do not add one to silence a real case.
#
#   cachestats_fn TC004/TC005/TC007 -- attribute-cache timings and a hit/miss
#   baseline.  They exist to print numbers into the log for a human to compare
#   across runs; there is no pass/fail property to state.  #1434's manual
#   sweep reached the same conclusion about this file.
#
ALLOWED_OBSERVATIONS = {
    ("cachestats_fn.mux", "TC004"),
    ("cachestats_fn.mux", "TC005"),
    ("cachestats_fn.mux", "TC007"),
}


def main() -> int:
    tc_dir = Path(__file__).resolve().parent.parent
    findings = []
    examined = 0
    for fname, label, has_success, has_non_success in classify_labels(tc_dir):
        if not has_success:
            continue                      # a guard, or a banner -- not a case
        examined += 1
        if has_non_success:
            continue
        if (fname, label) in ALLOWED_OBSERVATIONS:
            continue
        findings.append((fname, label))

    if findings:
        print(f"=== vacuous-case guard: {len(findings)} finding(s) ===")
        for fname, label in findings:
            print(f"  {fname}: {label} logs Succeeded and has no "
                  f"non-success branch, so it cannot fail")
        print()
        print("  Give the case a branch that reports failure, or -- if it is a")
        print("  measurement rather than an assertion -- add it to")
        print("  ALLOWED_OBSERVATIONS in tools/check_vacuous.py with a reason.")
        return 1

    print(f"=== vacuous-case guard: {examined} cases, all able to fail "
          f"({len(ALLOWED_OBSERVATIONS)} allowed observations) ===")
    return 0


if __name__ == "__main__":
    sys.exit(main())
