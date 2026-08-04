#!/bin/bash
#
#   run.sh — algorithmic growth battery.
#
#   Asserts the COMPLEXITY CLASS of softcode evaluation, not its speed.  For a
#   doubling of the input size, a linear implementation costs 2.0x and a
#   quadratic one costs 4.0x -- on a fast box, a slow box, an idle box and a
#   busy box alike, because the hardware cancels out of the ratio.
#
#   That is what makes this the portable half of performance coverage.
#   tests/perf compares ns/call to a per-machine baseline and needs one
#   baseline per box with hand-calibrated tolerances (#2046); this needs
#   neither.  It cannot tell you the server got 10% slower.  It can tell you an
#   O(n) loop became O(n^2), which is the failure that actually ruins a live
#   game -- and which a threshold test with a 20% tolerance will report as
#   "regression" long after the damage is done, without ever saying why.
#
#   Runs under muxscript: no network, no port, no server lifecycle.
#
#   Opt-in, NOT part of `make test`.  It takes minutes, and the timings, while
#   far more robust than absolute thresholds, are still timings.
#
#   Known defects are recorded as xfail in driver.py's CASES table, with the
#   issue number.  An xfail that starts PASSING fails the run: that means the
#   fix landed and the table needs updating.  A silently-stale xfail list is
#   how a fixed bug gets un-fixed later.
#
#   Usage: tests/growth/run.sh
#
set -u

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)

if ! command -v python3 >/dev/null 2>&1; then
    echo "SKIP: python3 not found."
    echo "      Nothing was measured; this is not a pass."
    exit 0
fi

if [ ! -x "$REPO_ROOT/mux/game/bin/muxscript" ]; then
    echo "SKIP: mux/game/bin/muxscript not found — run 'make install' first."
    echo "      Nothing was measured; this is not a pass."
    exit 0
fi

exec python3 "$SCRIPT_DIR/driver.py" "$@"
