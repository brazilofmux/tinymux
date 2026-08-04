#!/bin/bash
#
#   run.sh -- performance battery (#2046).
#
#   Extracts the rvbench timings that testcases/rvbench_fn.mux already emits on
#   every smoke run and compares them against a per-machine baseline.
#
#   REPORT-ONLY by default.  A perf test that fails on a busy laptop gets
#   disabled, and a disabled test is worse than none.  Set PERF_GATE=1 to make
#   a regression beyond the per-leg tolerance exit non-zero.  PERF_TOLERANCE
#   overrides all three legs with a single number if you want one.
#
#   VALIDATE PERF_GATE=1 ON YOUR OWN BOX BEFORE TRUSTING IT.  Record a
#   baseline, run it again against the SAME build, and see whether it comes
#   back clean.  The per-leg tolerances in compare.py were calibrated on
#   Darwin/arm64; on Linux/x86_64 the `compile` leg -- the only gateable one --
#   is about 5x noisier and fires three times on an unchanged build.
#
#   Baselines are PER-MACHINE and are not comparable across boxes: absolute ns
#   depend on the hardware.  Ratios are no better -- native and cached are timed
#   in separate loops, so their noise is independent and dividing them compounds
#   it (measured: ratio spread 8.4% against 6.0%/7.4% for the legs themselves).
#   A missing baseline is a SKIP of the comparison, not a failure.
#
set -u

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
SMOKELOG=${PERF_SMOKELOG:-$REPO_ROOT/testcases/smoke.log}

GATE=${PERF_GATE:-0}
ARCH="$(uname -s)-$(uname -m)"
BASELINE="$SCRIPT_DIR/baseline.$ARCH.json"

if ! command -v python3 >/dev/null 2>&1; then
    echo "SKIP: python3 not found."
    exit 0
fi

if [ ! -r "$SMOKELOG" ]; then
    echo "SKIP: no smoke log at $SMOKELOG."
    echo "      The timings come from rvbench_fn.mux, which runs during"
    echo "      \`make test-smoke\`.  Run that first."
    exit 0
fi

RESULT=$(mktemp)
trap 'rm -f "$RESULT"' EXIT

echo "==> rvbench timings from $(basename "$SMOKELOG")"
python3 "$SCRIPT_DIR/extract.py" "$SMOKELOG" --json "$RESULT" || exit 1

# extract.py exits 0 with no JSON when the log holds no BENCH lines (the
# sanitiser run excludes rvbench_fn), so treat that as the skip it is.
if [ ! -s "$RESULT" ]; then
    exit 0
fi

if [ -n "${PERF_OUT:-}" ]; then
    cp "$RESULT" "$PERF_OUT"
    echo "==> wrote $PERF_OUT"
fi

if [ ! -r "$BASELINE" ]; then
    echo
    echo "==> no baseline for $ARCH (looked for $(basename "$BASELINE"))."
    echo "    To record one:  PERF_OUT=$BASELINE $0"
    echo "    Baselines are per-machine; absolute timings do not transfer."
    exit 0
fi

echo
echo "==> comparing against $(basename "$BASELINE")"
# PERF_TOLERANCE is deliberately NOT defaulted here: compare.py treats it as an
# override of its per-leg tolerances, so passing a value unconditionally would
# collapse them to one number and defeat the point.  Only forward what the
# caller actually set.
PERF_GATE="$GATE" python3 "$SCRIPT_DIR/compare.py" "$BASELINE" "$RESULT"
