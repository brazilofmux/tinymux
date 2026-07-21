#!/bin/sh
#
# Phase 5 toggle-on soak driver (docs/plan-jit-evalbracket-lift.md).
#
# Rounds of differential sweeps with varied seeds across every corpus
# mode combination — all with jit_eval_brackets ON for the J side —
# plus a periodic full toggle-on smoke run.  Any LOGIC divergence stops
# the soak with the work dir preserved for minimization.
#
# Usage:  soak.sh [rounds]        (default 40; each round = one 400-expr
#                                  sweep; every 10th round also runs the
#                                  full smoke suite toggle-on)
# Exit:   0 = soak clean, 1 = divergence or smoke failure, 2 = setup.
#
set -u

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO=$(CDPATH= cd -- "$SCRIPT_DIR/../../.." && pwd)

ROUNDS=${1:-40}
LOG="$REPO/testcases/soak.log"

# Append (timestamped) so history survives across soak sessions.
echo "=== JIT bracket toggle-on soak: $ROUNDS rounds ($(date)) ===" | tee -a "$LOG"

round=0
while [ "$round" -lt "$ROUNDS" ]; do
    round=$((round + 1))
    seed=$((1000 + round * 7))

    # Rotate corpus mode combinations; brackets mode is always on —
    # this is a soak of the newly opened surface.
    case $((round % 4)) in
        0) modes="" ;;
        1) modes="utf8" ;;
        2) modes="longreg" ;;
        3) modes="utf8 longreg" ;;
    esac
    env_str="JITDIFF_BRACKETS=1 SEED=$seed"
    for m in $modes; do
        case $m in
            utf8)    env_str="$env_str JITDIFF_UTF8=1" ;;
            longreg) env_str="$env_str JITDIFF_LONGREG=1" ;;
        esac
    done

    out=$(env $env_str "$SCRIPT_DIR/run.sh" 400 2>&1)
    rc=$?
    summary=$(printf '%s\n' "$out" | grep -a "compared," | tail -1)
    echo "round $round [brackets $modes seed=$seed]: ${summary:-NO SUMMARY} (rc=$rc)" | tee -a "$LOG"
    if [ $rc -ne 0 ]; then
        printf '%s\n' "$out" | tail -20 | tee -a "$LOG"
        echo "SOAK FAILED at round $round" | tee -a "$LOG"
        exit 1
    fi
    # A COLOR-encoding divergence is a regression too (post-#995 the
    # baseline is 0 everywhere) — halt on it even though run.sh only
    # sets a failing status for LOGIC.
    case $summary in
        *", 0 COLOR"*) ;;
        *)  echo "SOAK FAILED at round $round: COLOR divergence" | tee -a "$LOG"
            exit 1 ;;
    esac

    # Periodic full-suite toggle-on smoke.
    if [ $((round % 10)) -eq 0 ]; then
        smoke=$(cd "$REPO/testcases" \
                && SMOKE_EXTRA_CONF="jit_eval_brackets 1" ./tools/Smoke 2>&1 \
                | tail -1)
        echo "round $round smoke(toggle-on): $smoke" | tee -a "$LOG"
        case $smoke in
            *PASSED*) ;;
            *) echo "SOAK FAILED: smoke at round $round" | tee -a "$LOG"
               exit 1 ;;
        esac
    fi
done

echo "=== SOAK CLEAN: $ROUNDS rounds, $((ROUNDS * 400)) expressions ===" | tee -a "$LOG"
exit 0
