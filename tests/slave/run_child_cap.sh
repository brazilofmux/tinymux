#!/bin/bash
#
#   run_child_cap.sh — #1853 / #1827 slave child-cap burst.
#
#   The #1827 fix recomputes live children after each waitpid so the cap
#   unblocks after *one* exit.  The old loop fixed the blocking flag from a
#   stale count and drained every outstanding child before accepting more
#   work — multi-minute stalls when any resolver was slow.
#
#   A burst against a fast resolver never reaches MAX_CHILDREN (20):
#   600 requests peaked at 13 (#1853).  This harness forces a stall via
#   SLAVE_TEST_HARNESS + "MS@addr" delays so the cap is real.
#
#   Shape (MAX_CHILDREN == 20):
#     - 19 slow children  (3000@…)
#     -  1 fast child     (100@…)   — first batch fills the cap
#     -  5 more fast      (100@…)   — can only start after the first
#                                     fast child exits under the new code
#
#   Under the fix, those 5 extra fast lookups finish within ~1s of start
#   (while the 19 slow still run).  Under the old loop they would not
#   start until the 3s slow cohort finishes — so an early snapshot of
#   fast responses is the discriminator, not the final total.
#
#   Peak concurrency is asserted from the slave's own report, not sampled:
#   in harness mode the slave prints PEAK_CHILDREN=<n> on stderr at exit,
#   computed by the cap's own accounting at each fork (#1912).  Sampling
#   ppid from outside is structurally racy — the parent exits as soon as
#   stdin drains, reparenting the children, and on a busy box one sampling
#   sweep can outlast that window entirely (observed: peak_children=0 with
#   20 live children).  A green without peak >= 20 is the vacuous pass this
#   repo keeps re-learning, so the assertion stays — only its evidence
#   changed from sampled to reported.
#
set -u

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)

SLAVE=""
for cand in \
    "$REPO_ROOT/mux/game/bin/slave" \
    "$REPO_ROOT/mux/src/slave"
do
    if [ -x "$cand" ]; then
        SLAVE=$cand
        break
    fi
done

if [ -z "$SLAVE" ]; then
    echo "SKIP: slave binary not found (run 'make install' first)."
    exit 0
fi

REQ_FILE=$(mktemp)
OUT_FILE=$(mktemp)
ERR_FILE=$(mktemp)
trap 'rm -f "$REQ_FILE" "$OUT_FILE" "$ERR_FILE"; kill $SLAVE_PID 2>/dev/null; wait $SLAVE_PID 2>/dev/null' EXIT

{
    for i in $(seq 1 19); do
        echo "3000@10.0.0.$i"
    done
    echo "100@10.0.0.20"
    for i in $(seq 21 25); do
        echo "100@10.0.0.$i"
    done
} > "$REQ_FILE"

export SLAVE_TEST_HARNESS=1

: >"$OUT_FILE"
"$SLAVE" <"$REQ_FILE" >"$OUT_FILE" 2>"$ERR_FILE" &
SLAVE_PID=$!

count_fast() {
    # Fast cohort: 10.0.0.20 .. 10.0.0.25 (6 addresses).
    grep -E '^10\.0\.0\.(2[0-5]) ' "$OUT_FILE" 2>/dev/null | wc -l | tr -d ' '
}

# Wait for the slave parent to exit (it does so once stdin is drained and
# every request is forked — under the fixed cap that is well under 2s, the
# serialized post-cap fast wave).  Bound the wait at ~2.5s: under the old
# sticky-waitpid bug the parent is still blocked on the 3s slow cohort at
# that point, and the early snapshot below then correctly reads ~1.
waited=0
while [ "$waited" -lt 25 ] && kill -0 "$SLAVE_PID" 2>/dev/null; do
    sleep 0.1
    waited=$((waited + 1))
done

# Early snapshot: taken at most ~2.5s in, strictly before the 3s slow
# cohort can have answered.  Post-cap fast work completing this early is
# the deterministic evidence that the cap released one slot per exit.
early_fast=$(count_fast)

# The slow children still run and write to the inherited stdout after the
# parent exits.  Wait for all 25 response lines (10s ceiling).
wait_s=0
while [ "$wait_s" -lt 40 ]; do
    total_now=$(grep -c . "$OUT_FILE" 2>/dev/null || true)
    if [ "$total_now" -ge 25 ]; then
        break
    fi
    sleep 0.25
    wait_s=$((wait_s + 1))
done
if kill -0 "$SLAVE_PID" 2>/dev/null; then
    # Parent still alive with incomplete output — sticky drain or hang.
    if [ "$(grep -c . "$OUT_FILE" 2>/dev/null || true)" -lt 25 ]; then
        echo "FAIL: slave still running after ~$((wait_s / 4))s with incomplete output"
        kill "$SLAVE_PID" 2>/dev/null || true
        exit 1
    fi
fi
wait "$SLAVE_PID" 2>/dev/null || true
SLAVE_PID=""

total_lines=$(grep -c . "$OUT_FILE" || true)
final_fast=$(count_fast)
peak=$(sed -n 's/^PEAK_CHILDREN=\([0-9][0-9]*\)$/\1/p' "$ERR_FILE" | tail -1)

echo "slave child-cap burst: peak_children=${peak:-unreported}  early_fast=$early_fast  final_fast=$final_fast  total=$total_lines"

fail=0
if [ -z "$peak" ]; then
    echo "FAIL: slave did not report PEAK_CHILDREN (stale binary, or report lost)"
    fail=1
else
    if [ "$peak" -lt 20 ]; then
        echo "FAIL: peak concurrent children $peak < MAX_CHILDREN (20) — vacuous burst"
        fail=1
    fi
    # The cap must also hold: more than MAX_CHILDREN live at once means the
    # gate itself is broken.
    if [ "$peak" -gt 20 ]; then
        echo "FAIL: peak concurrent children $peak > MAX_CHILDREN (20) — cap overrun"
        fail=1
    fi
fi
# Discriminator: post-cap fast work must complete while slow children still
# hold slots.  Need >=5 of the 6 fast-cohort lines within the early window.
if [ "$early_fast" -lt 5 ]; then
    echo "FAIL: only $early_fast fast-cohort responses in the early window (need >=5)"
    echo "      (old sticky waitpid would leave these at 1 until the 3s cohort exits)"
    fail=1
fi
if [ "$total_lines" -lt 25 ]; then
    echo "FAIL: only $total_lines total responses at end (need 25)"
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    echo "--- stdout ---"
    cat "$OUT_FILE" || true
    echo "--- stderr ---"
    cat "$ERR_FILE" || true
    exit 1
fi

echo "PASS: peak $peak == 20 (slave-reported), early_fast $early_fast, total $total_lines"
exit 0
