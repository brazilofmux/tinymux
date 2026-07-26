#!/usr/bin/env bash
#
# Regression test for run.sh's adjudicate() — the harness testing itself,
# in the same spirit as selftest.py (which covers probe.py's reader).
#
# The bug (#1368): the 2.13 leg intermittently drops a row, probe.py writes
# <NO-OUTPUT> for it, and adjudicate() compared that sentinel as though it
# were an answer from the engine.  A shape whose three legs actually agree
# was reported as VIOLATED — the harness inventing a divergence that never
# happened, which is the one failure an oracle must not have.
#
# The drop is rare and has never reproduced on demand, so this drives
# adjudicate() directly with synthetic probe output instead of trying to
# provoke it.  No netmux, no python3 and no 2.13 tree are needed, which
# also means it runs on hosts that cannot run the harness itself.
#
# Usage: ./selftest_adjudicate.sh        (exit 0 = pass)
#
set -u

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
RUNSH="$SCRIPT_DIR/run.sh"
[ -r "$RUNSH" ] || { echo "cannot read $RUNSH"; exit 1; }

T=$(mktemp -d) || exit 1
trap 'rm -rf "$T"' EXIT
export WORK="$T"

# Lift verdict_table() and adjudicate() out of run.sh verbatim rather than
# duplicating them here, so this cannot drift from what actually ships.
awk '/^# Extract NAME<TAB>VERDICT/,/^}$/ { print }'          "$RUNSH" >  "$T/fns.sh"
awk '/^# adjudicate <r213>/,0 { print; if (/^}$/) exit }'    "$RUNSH" >> "$T/fns.sh"
# shellcheck disable=SC1090
. "$T/fns.sh" 2>/dev/null

# Fail loudly if the lift broke, rather than reporting every case as a
# failure.  This has already caught one real defect: an apostrophe in a
# comment inside adjudicate's single-quoted awk program closed the quote
# early and broke the script.
if ! command -v adjudicate >/dev/null 2>&1; then
    echo "FATAL: could not lift adjudicate() out of run.sh."
    echo "       Check quoting in run.sh (an apostrophe inside the awk"
    echo "       program will do this) and that the function markers"
    echo "       '# adjudicate <r213>' and a column-0 '}' still exist."
    exit 1
fi

pass=0
fail=0

# leg <file> <row>...   each row is NAME<TAB>VALUE
leg() { f=$1; shift; : > "$f"; for r in "$@"; do printf '%s\n' "$r" >> "$f"; done; }

# check <label> <want-exit> <want-substring> <must-not-contain>
check() {
    label=$1; want=$2; wantstr=$3; notstr=$4
    out=$(adjudicate "$T/r213" "$T/jit" "$T/ast" "$T/corpus.txt" 2>&1)
    got=$?
    ok=1
    [ "$got" -eq "$want" ] || { ok=0; echo "    exit: want $want, got $got"; }
    case "$out" in *"$wantstr"*) ;; *) ok=0; echo "    missing: $wantstr";; esac
    if [ -n "$notstr" ]; then
        case "$out" in *"$notstr"*) ok=0; echo "    unexpected: $notstr";; *) ;; esac
    fi
    if [ "$ok" -eq 1 ]; then
        echo "ok   - $label"
        pass=$((pass + 1))
    else
        echo "FAIL - $label"
        printf '%s\n' "$out" | sed 's/^/       | /'
        fail=$((fail + 1))
    fi
}

echo "=== adjudicate() regression checks (#1368) ==="

# Controls: the two verdicts that must keep working exactly as before.
printf 'SHAPE_A|[x]|both\n' > "$T/corpus.txt"
leg "$T/r213" "$(printf 'SHAPE_A\t3 mul(2,3)')"
leg "$T/jit"  "$(printf 'SHAPE_A\t3 mul(2,3)')"
leg "$T/ast"  "$(printf 'SHAPE_A\t3 mul(2,3)')"
check "three agreeing legs are satisfied" 0 "satisfied:     1" "VIOLATED:      1"

printf 'SHAPE_B|[x]|2.13\n' > "$T/corpus.txt"
leg "$T/r213" "$(printf 'SHAPE_B\tone')"
leg "$T/jit"  "$(printf 'SHAPE_B\ttwo')"
leg "$T/ast"  "$(printf 'SHAPE_B\ttwo')"
check "a real divergence is still VIOLATED" 1 "VIOLATED:      1" "UNMEASURED"

# The defect: a dropped row must not become a verdict, on any leg.
printf 'SHAPE_C|[x]|both\n' > "$T/corpus.txt"
leg "$T/r213" "$(printf 'SHAPE_C\t<NO-OUTPUT>')"
leg "$T/jit"  "$(printf 'SHAPE_C\t3 mul(2,3)')"
leg "$T/ast"  "$(printf 'SHAPE_C\t3 mul(2,3)')"
check "dropped 2.13 row is UNMEASURED, not VIOLATED" 2 "UNMEASURED:    1" "VIOLATED:      1"

printf 'SHAPE_D|[x]|2.14\n' > "$T/corpus.txt"
leg "$T/r213" "$(printf 'SHAPE_D\tone')"
leg "$T/jit"  "$(printf 'SHAPE_D\t<NO-OUTPUT>')"
leg "$T/ast"  "$(printf 'SHAPE_D\tone')"
check "dropped 2.14 row is UNMEASURED too" 2 "UNMEASURED:    1" "VIOLATED:      1"

# ...nor may it be quietly counted as satisfied.
printf 'SHAPE_F|[x]|both\n' > "$T/corpus.txt"
leg "$T/r213" "$(printf 'SHAPE_F\t<NO-OUTPUT>')"
leg "$T/jit"  "$(printf 'SHAPE_F\t<NO-OUTPUT>')"
leg "$T/ast"  "$(printf 'SHAPE_F\t<NO-OUTPUT>')"
check "all legs dropped is not satisfied" 2 "satisfied:     0" "VIOLATED:      1"

# A real violation alongside an unmeasured row: the violation decides the
# exit code, because it is the more serious finding and must not be masked.
printf 'SHAPE_B|[x]|2.13\nSHAPE_C|[x]|both\n' > "$T/corpus.txt"
leg "$T/r213" "$(printf 'SHAPE_B\tone')"  "$(printf 'SHAPE_C\t<NO-OUTPUT>')"
leg "$T/jit"  "$(printf 'SHAPE_B\ttwo')"  "$(printf 'SHAPE_C\tsame')"
leg "$T/ast"  "$(printf 'SHAPE_B\ttwo')"  "$(printf 'SHAPE_C\tsame')"
check "a violation outranks an unmeasured row" 1 "UNMEASURED:    1" ""

echo "=== $pass passed, $fail failed ==="
[ "$fail" -eq 0 ]
