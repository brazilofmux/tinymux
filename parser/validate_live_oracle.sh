#!/bin/bash
# validate_live_oracle.sh -- run escape_oracle_cases.txt through live MUX
#                            engines and flag drift against the corpus.
#
# Why this exists
# ---------------
# parser/eval.cpp is a stand-alone research evaluator. Its mux214 profile
# is a reimplementation of what the researcher believes mux_exec does for
# the escape + percent edge cases. `test_eval.sh` validates ./eval against
# the corpus, but neither ./eval nor the corpus is ever automatically
# compared against the real engine.so. That lets either side drift silently
# (the 2026-03-27 ISSUES tracker entry flagged this gap).
#
# Ground truth is the historical 2.13 engine — the language is what the
# 2.13 binaries say it is, regardless of how anyone else (researcher,
# reviewer, ./eval) thinks it "should" behave. When a mux2.13_N tree has
# been built into a usable muxscript, pass it as MUX213; the script will
# then run each case through both engines and report:
#
#   - whether live mux2.14 drifts from the corpus mux214 column
#   - whether live mux2.14 drifts from live mux2.13 (the authoritative
#     reference, if available)
#
# Per-case output tags:
#
#   PASS    live engine matches corpus mux214 column
#   DRIFT   live engine disagrees with corpus mux214 column
#   13DIFF  live mux2.14 disagrees with live mux2.13 (only printed when
#           a MUX213 binary is supplied)
#   LIVE    live_status != "confirmed" (manual check still outstanding)
#
# The script exits non-zero if any DRIFT or 13DIFF was seen. LIVE rows do
# not fail the script by themselves — they are reminders that the
# mux213/penn columns for those rows are still unverified guesses.
#
# Usage
# -----
#   ./validate_live_oracle.sh [cases_file] [muxscript] [mux213_muxscript]
#
# Defaults:
#   cases_file       = ./escape_oracle_cases.txt
#   muxscript        = ../mux/game/bin/muxscript
#   mux213_muxscript = (none; 2.13 comparison skipped)

set -u

CASES_FILE="${1:-./escape_oracle_cases.txt}"
MUXSCRIPT="${2:-../mux/game/bin/muxscript}"
MUXSCRIPT_213="${3:-}"

if [ ! -f "$CASES_FILE" ]; then
    echo "validate_live_oracle: missing cases file: $CASES_FILE" >&2
    exit 2
fi

if [ ! -x "$MUXSCRIPT" ]; then
    echo "validate_live_oracle: missing or non-executable muxscript: $MUXSCRIPT" >&2
    echo "  build it with 'make install' from the repo root." >&2
    exit 2
fi

# muxscript resolves its game directory relative to the binary; we pass
# an absolute -g so the script is cwd-independent.
GAME_DIR="$(cd "$(dirname "$MUXSCRIPT")/.." && pwd)"

run_live() {
    local expr="$1"
    # `think` runs its argument through a single mux_exec pass, which is
    # the primitive the corpus is meant to document.
    #
    # muxscript prints a one-line "loaded game" banner on stdout before
    # running the expression; think output has a trailing CR from MUX's
    # line handling. Filter both.
    "$MUXSCRIPT" -g "$GAME_DIR" -e "think $expr" 2>&1 \
        | grep -v '^muxscript: ' \
        | grep -v '^CSQLiteDB::' \
        | tr -d '\r' \
        | sed -n '1p'
}

PASS=0
DRIFT=0
LIVE_PENDING=0
DRIFT_IDS=()

while IFS='|' read -r id input mux214 mux213 penn live_status notes; do
    [ -z "$id" ] && continue
    case "$id" in
        \#*) continue ;;
    esac

    actual=$(run_live "$input")

    if [ "$actual" = "$mux214" ]; then
        PASS=$((PASS + 1))
        printf 'PASS   %s\n' "$id"
    else
        DRIFT=$((DRIFT + 1))
        DRIFT_IDS+=("$id")
        printf 'DRIFT  %s\n' "$id"
        printf '         expr:     %s\n' "$input"
        printf '         expected: [%s]\n' "$mux214"
        printf '         actual:   [%s]\n' "$actual"
    fi

    if [ "$live_status" != "confirmed" ]; then
        LIVE_PENDING=$((LIVE_PENDING + 1))
        printf 'LIVE   %s  (live_status=%s)\n' "$id" "$live_status"
    fi
done < "$CASES_FILE"

echo
echo "escape oracle live-validation summary:"
echo "  $PASS passed"
echo "  $DRIFT drifted from corpus mux214 column"
echo "  $LIVE_PENDING row(s) with live_status != confirmed"

if [ "$DRIFT" -ne 0 ]; then
    echo
    echo "Drifted rows — investigate ./eval's mux214 profile or the corpus:"
    for id in "${DRIFT_IDS[@]}"; do
        echo "  - $id"
    done
    exit 1
fi

exit 0
