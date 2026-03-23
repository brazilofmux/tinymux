#!/bin/bash
set -u

CASES_FILE="${1:-./escape_oracle_cases.txt}"

if [ ! -f "$CASES_FILE" ]; then
    echo "missing cases file: $CASES_FILE" >&2
    exit 2
fi

PASS=0
FAIL=0
NEEDS_LIVE=0
RUN_EXPLORATORY_MUX213="${RUN_EXPLORATORY_MUX213:-0}"

run_case() {
    local id="$1"
    local input="$2"
    local expected="$3"
    local profile="$4"

    local actual
    if [ "$profile" = "mux214" ]; then
        actual=$(printf '%s\n' "$input" | ./eval 2>&1)
    else
        actual=$(printf '%s\n' "$input" | ./eval --profile "$profile" 2>&1)
    fi

    if [ "$actual" = "$expected" ]; then
        PASS=$((PASS + 1))
    else
        FAIL=$((FAIL + 1))
        echo "FAIL [$profile] $id"
        echo "  expr:     $input"
        echo "  expected: $expected"
        echo "  actual:   $actual"
    fi
}

while IFS='|' read -r id input mux214 mux213 penn live_status notes; do
    [ -z "$id" ] && continue
    case "$id" in
        \#*) continue ;;
    esac

    run_case "$id" "$input" "$mux214" "mux214"
    if [ "$RUN_EXPLORATORY_MUX213" = "1" ]; then
        run_case "$id" "$input" "$mux213" "mux213"
    fi
    run_case "$id" "$input" "$penn" "penn"

    if [ "$live_status" != "confirmed" ]; then
        NEEDS_LIVE=$((NEEDS_LIVE + 1))
        echo "LIVE [$live_status] $id"
        echo "  expr:  $input"
        echo "  note:  $notes"
    fi
done < "$CASES_FILE"

echo
if [ "$RUN_EXPLORATORY_MUX213" != "1" ]; then
    echo "note: skipped exploratory mux213 study profile"
fi
echo "escape oracle summary: $PASS passed, $FAIL failed, $NEEDS_LIVE still need live-server checks"

if [ "$FAIL" -ne 0 ]; then
    exit 1
fi
