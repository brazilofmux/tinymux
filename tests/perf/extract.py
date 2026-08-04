#!/usr/bin/env python3
"""
extract.py -- turn the rvbench lines in a smoke log into a perf result (#2046).

Reads BENCH lines that testcases/rvbench_fn.mux already emits on every smoke
run and writes them as JSON for compare.py.

Why parse the smoke log rather than drive a live server:

  A socket-driven harness was written first and measured.  It was far WORSE --
  82% run-to-run spread on the native leg, against 2-9% for the same
  benchmarks through smoke, and systematically slower (426-612ns vs ~320ns for
  add_simple).  Driving rvbench() over a connection interleaves network events,
  command dispatch and timer work with the timed loop; the smoke path runs it
  on an otherwise-idle server.

  Changing the estimator did not rescue it -- median and min were both tried,
  and min was no better (82.6% vs 82.6%).  The noise is in the environment, not
  in how the samples are reduced.  So: use the quiet vehicle that already
  exists rather than a noisy new one.

Usage: extract.py <smoke.log> [--json PATH]
"""
import argparse
import json
import re
import sys

# BENCH<nnn>:expr=<expr> iters=<n> ... | native=<x>ns/call | compile-each=... | cached=...
LINE_RE = re.compile(
    r"^(BENCH\d+):expr=(.*?)\s+iters=(\d+).*?"
    r"native=([\d.]+)ns/call.*?"
    r"compile-each=([\d.]+)ns/call.*?"
    r"cached=([\d.]+)ns/call"
)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("smokelog")
    ap.add_argument("--json", dest="json_path")
    args = ap.parse_args()

    results, iters = {}, None
    try:
        fh = open(args.smokelog, encoding="utf-8", errors="replace")
    except OSError as e:
        print("SKIP: cannot read %s (%s)" % (args.smokelog, e))
        return 0

    with fh:
        for line in fh:
            m = LINE_RE.match(line.strip())
            if not m:
                continue
            # Key on the EXPRESSION, not on BENCH<nnn>.  The numbers are
            # positional in rvbench_fn.mux, so inserting a benchmark would
            # renumber every one after it and silently re-point the baseline
            # at different code.
            name = m.group(2)
            iters = int(m.group(3))
            results[name] = {
                "native": float(m.group(4)),
                "compile": float(m.group(5)),
                "cached": float(m.group(6)),
            }

    if not results:
        print("SKIP: no BENCH lines in %s." % args.smokelog)
        print("      rvbench_fn.mux runs during `make test-smoke`; it is")
        print("      excluded from the sanitiser run (see generate_smoke_suite.py).")
        return 0

    out = {"iters": iters, "source": args.smokelog, "results": results}
    if args.json_path:
        with open(args.json_path, "w") as f:
            json.dump(out, f, indent=2, sort_keys=True)
            f.write("\n")

    print("  %-42s %9s %9s %9s" % ("expression", "native", "compile", "cached"))
    for name in sorted(results):
        r = results[name]
        print("  %-42s %9.1f %9.1f %9.1f"
              % (name[:42], r["native"], r["compile"], r["cached"]))
    print("  (ns/call, %d iterations)" % iters)
    return 0


if __name__ == "__main__":
    sys.exit(main())
