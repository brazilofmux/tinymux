#!/usr/bin/env python3
"""
compare.py -- diff a perf result against a baseline (#2046).

Report-only unless PERF_GATE=1.  Even then it only fails on REGRESSIONS beyond
PERF_TOLERANCE percent; an improvement never fails, and a benchmark missing
from either side is reported rather than treated as a change.

Tolerances are per-leg and derived from measurement -- see LEG_TOLERANCE below.
A tight gate on a laptop that is also compiling something produces red runs
that teach everyone to ignore it, which costs more than the coverage is worth.
"""
import json
import os
import sys

# Per-leg tolerance, derived from measurement rather than picked.  Two smoke
# runs on the same build, 55 benchmarks, absolute difference per benchmark:
#
#   leg       median   p90     max
#   native      2.8%  15.7%   37.2%
#   compile     0.7%   1.6%    4.6%
#   cached      4.9%  24.9%  104.8%
#
# Stability tracks how long the measurement runs.  compile-each costs
# 3-22us/call so its loop dwarfs any scheduler excursion; cached runs at
# 13-160ns/call, where a single preemption is the whole sample.  One global
# tolerance would therefore either miss real regressions on compile or cry
# wolf on cached.
#
# cached is set above its observed maximum ON PURPOSE: at 10000 iterations it
# is not gateable, and a threshold that fires on noise is how a suite gets
# ignored.  Making it useful needs per-leg iteration counts inside rvbench()
# -- many for the cheap leg, few for the expensive one -- which is tracked on
# #2046 rather than bodged here.
LEG_TOLERANCE = {
    "native": 60.0,
    "compile": 10.0,
    "cached": 120.0,
}
LEGS = tuple(LEG_TOLERANCE)

# Legs PERF_GATE is allowed to fail on.  Only compile is trustworthy today:
# comparing a build against ITSELF produced a +44.1% native outlier, past the
# 40% the two-run sample suggested -- so the native tail is fatter than that
# sample showed, and cached is worse still.  Offering a gate that is known to
# fire on noise is how a suite earns its reputation for lying; report those
# legs instead and widen the set when per-leg iteration counts make them
# measurable (#2046).
GATEABLE = ("compile",)


def main():
    if len(sys.argv) != 3:
        print("usage: compare.py <baseline.json> <result.json>", file=sys.stderr)
        return 2
    with open(sys.argv[1]) as f:
        base = json.load(f)
    with open(sys.argv[2]) as f:
        cur = json.load(f)

    gate = os.environ.get("PERF_GATE", "0") == "1"
    # PERF_TOLERANCE overrides every leg; unset means the per-leg defaults.
    override = os.environ.get("PERF_TOLERANCE")
    tol_for = (lambda leg: float(override)) if override else LEG_TOLERANCE.get

    b_res, c_res = base.get("results", {}), cur.get("results", {})

    # A baseline taken at a different iteration count is not comparable: the
    # per-call cost includes a share of fixed loop overhead that shrinks as
    # iterations rise.  Say so rather than silently diffing apples to oranges.
    if base.get("iters") != cur.get("iters"):
        print("  NOTE: baseline iters=%s but this run used iters=%s; "
              "per-call costs are not comparable across iteration counts."
              % (base.get("iters"), cur.get("iters")))

    only_base = sorted(set(b_res) - set(c_res))
    only_cur = sorted(set(c_res) - set(b_res))
    for n in only_base:
        print("  MISSING: %s is in the baseline but was not run" % n)
    for n in only_cur:
        print("  NEW:     %s has no baseline entry" % n)

    regressions = []
    advisories = []
    print("  %-14s %-8s %10s %10s %9s" % ("benchmark", "leg", "base", "now", "delta"))
    for name in sorted(set(b_res) & set(c_res)):
        for leg in LEGS:
            b, c = b_res[name].get(leg), c_res[name].get(leg)
            if b is None or c is None or b <= 0:
                continue
            delta = (c - b) / b * 100.0
            tol = tol_for(leg)
            flag = ""
            if delta > tol:
                flag = "  <-- REGRESSION" if leg in GATEABLE else "  <-- slower"
                if leg in GATEABLE:
                    regressions.append((name, leg, delta))
                else:
                    advisories.append((name, leg, delta))
            elif delta < -tol:
                flag = "  <-- improved"
            print("  %-14s %-8s %10.1f %10.1f %+8.1f%%%s"
                  % (name, leg, b, c, delta, flag))

    print()
    if advisories:
        print("  %d leg(s) slower beyond tolerance, NOT gated (see GATEABLE):"
              % len(advisories))
        for name, leg, d in advisories:
            print("    %s/%s  %+.1f%% (tolerance %.0f%%)" % (name, leg, d, tol_for(leg)))
        print()
    if regressions:
        print("  %d regression(s) beyond the per-leg tolerance:" % len(regressions))
        for name, leg, d in regressions:
            print("    %s/%s  %+.1f%% (tolerance %.0f%%)" % (name, leg, d, tol_for(leg)))
        if gate:
            print("  PERF_GATE=1 -- failing.")
            return 1
        print("  Report-only (set PERF_GATE=1 to fail on this).")
    else:
        print("  No regression beyond tolerance (%s)."
              % ", ".join("%s %.0f%%" % (k, v) for k, v in LEG_TOLERANCE.items()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
