#!/bin/bash
#
#   bisect.sh — which chained edge causes #2019?
#
#   Needs a runner built against 2019-chain-bisect.patch, which adds two
#   scratch knobs to dbt.cpp:
#
#     DBT_CHAIN_DUMP=1        list every guest PC that gets chained
#     DBT_CHAIN_SKIP=a,b,c    skip backpatching for those targets (hex)
#
#   Then this binary-searches the target set for the smallest subset whose
#   suppression makes the failure go away.  On 2026-08-03 that converged to
#   a single PC out of 70 -- the fall-through of a shrink-wrapped early-out
#   in wc_next.  See #2019.
#
#   Because the failure is intermittent (~half of runs), "0 wrong" needs
#   enough samples to mean something: at RUNS=30 a still-broken
#   configuration reads clean with probability around 1e-9.  Do not lower
#   it without redoing that arithmetic.
#
#   Build (adjust the backend for your host):
#     cp mux/modules/engine/dbt.cpp /tmp/dbt_bisect.cpp
#     patch /tmp/dbt_bisect.cpp < tests/codiff/repro/2019-chain-bisect.patch
#     g++ -std=c++17 -O2 -DHAVE_CONFIG_H -DTINYMUX_JIT -Imux/include \
#         -o /tmp/runner_bisect tests/codiff/runner.cpp \
#         mux/modules/engine/dbt_interp.cpp mux/modules/engine/dbt_elf64.cpp \
#         /tmp/dbt_bisect.cpp mux/modules/engine/dbt_a64_sysv.cpp
#
#   Usage: RUNNER=/tmp/runner_bisect ./bisect.sh
#
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
RUNNER=${RUNNER:-/tmp/runner_bisect}
ELF=${ELF:-$HERE/build/min.elf}
RUNS=${RUNS:-30}
WANT=${WANT:-20002}

if [ ! -x "$RUNNER" ]; then
    echo "SKIP: no runner at $RUNNER -- see the header for how to build it."
    exit 0
fi
if [ ! -f "$ELF" ]; then
    echo "SKIP: no $ELF -- run repro/run-2019.sh first to build it."
    exit 0
fi

one() {   # one(skip-set) -> number of wrong runs
    local b=0 i
    for i in $(seq 1 "$RUNS"); do
        DBT_CHAIN_SKIP="$1" "$RUNNER" "$ELF" 2>/dev/null \
            | awk '/^--- dbt/{d=1} d && /^test ret=/{print; exit}' \
            | grep -q "ret=$WANT$" || b=$((b + 1))
    done
    echo $b
}

DBT_CHAIN_DUMP=1 "$RUNNER" "$ELF" 2>&1 >/dev/null \
    | sed -n 's/^CHAINPC //p' | sort -u > "$HERE/build/pcs.txt"
mapfile -t CAND < "$HERE/build/pcs.txt"
echo "chained targets: ${#CAND[@]}"

all=$(IFS=,; echo "${CAND[*]}")
echo "  skip none : $(one '') / $RUNS wrong"
echo "  skip all  : $(one "$all") / $RUNS wrong   (expect 0 -- if not, stop here)"

while [ ${#CAND[@]} -gt 1 ]; do
    half=$(( ${#CAND[@]} / 2 ))
    A=("${CAND[@]:0:$half}")
    B=("${CAND[@]:$half}")
    sa=$(IFS=,; echo "${A[*]}")
    ra=$(one "$sa")
    echo "  skipping first ${#A[@]} of ${#CAND[@]} -> $ra/$RUNS wrong"
    if [ "$ra" = 0 ]; then CAND=("${A[@]}"); else CAND=("${B[@]}"); fi
done

echo "CULPRIT: 0x${CAND[0]}"
echo "confirm alone: $(one "${CAND[0]}") / $RUNS wrong"
