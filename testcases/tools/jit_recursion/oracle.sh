#!/bin/sh
#
# Runaway-recursion termination cost oracle (#1994).
#
# Terminating a runaway self-recursive ufun on the compiled route cost
# time exponential in function_recursion_limit.  The shared heap has one
# dbt_state_t -- one guest register context, one stack pointer, one heap
# arena -- and eval() re-entered from a host ECALL of a suspended eval()
# reset all of it underneath the outer run.  The outer run then resumed at
# a program counter that was not its own, the backend refused to translate
# there, dbt_resume returned -1, and jit_eval handed the whole subtree back
# to mux_exec to redo through the AST.  That subtree contains the next
# recursion level, so every level cost two evaluations:
#
#   function_recursion_limit    eval_attempts before      after
#      8                                  97                 9
#     12                               1,537                13
#     16                              24,577                17
#     20                             393,217                21
#     24                           6,291,457                25
#
# WHY THIS ASSERTS A COUNTER AND NOT A CLOCK.  The defect is a cost shape,
# and the answer is correct either way -- the AST re-run recomputes the
# same string, which is exactly why it went unnoticed.  So a result-equality
# check cannot see this bug at all.  eval_attempts is deterministic and the
# two regimes differ by four orders of magnitude at limit 20, so the
# threshold below needs no timing tolerance and cannot flake on a loaded box.
#
# WHY THE CANARY MATTERS.  If the J workspace declined everything up front
# (toggle default regressed, JIT absent, shape not compiled) eval_attempts
# would also be tiny and this would pass vacuously -- the SKIP-reads-as-PASS
# failure this suite has been bitten by before.  The canary requires the
# compiled route to have actually run and actually ECALLed into u().
#
# Runs as part of `make test` (test-jit-recursion); exit 2 on a non-JIT
# build so it skips rather than fails.
#
# Usage:  oracle.sh
# Exit status: 0 = pass, 1 = regression, 2 = setup error / no JIT.

set -u

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO=$(CDPATH= cd -- "$SCRIPT_DIR/../../.." && pwd)
BIN="$REPO/mux/game/bin"

# function_recursion_limit for the runaway case.  Small enough that a
# regressed build finishes in about a second rather than hanging the suite
# (limit 20 is ~0.6s pre-fix; limit 24 is ~8s and 32 would not finish).
LIMIT=20

# Post-fix eval_attempts is exactly LIMIT+1 (21).  Pre-fix it is 393,217.
# 200 is an order of magnitude above the correct value and three below the
# broken one, so neither a small future change in accounting nor a slow box
# can move it across.
LINEAR_MAX=200

if [ ! -x "$BIN/muxscript" ] || [ ! -e "$BIN/engine.so" ]; then
    echo "ERROR: $BIN/muxscript or engine.so missing — run 'make install' first." >&2
    exit 2
fi

if command -v timeout >/dev/null 2>&1; then TIMEOUT="timeout 120"
elif command -v gtimeout >/dev/null 2>&1; then TIMEOUT="gtimeout 120"
else TIMEOUT=""; fi

WORK=$(mktemp -d "${TMPDIR:-/tmp}/jitrecur.XXXXXX") || exit 2
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/data"
ln -sfn "$BIN" "$WORK/bin"

DYLD_LIBRARY_PATH="$BIN"; export DYLD_LIBRARY_PATH
LD_LIBRARY_PATH="$BIN";   export LD_LIBRARY_PATH

# Two workspaces.  J: default (jit_eval_brackets on).  I: AST oracle.
# function_invocation_limit is raised so that it cannot be the limit that
# bounds the work -- at stock 25000 it caps the blowup and hides the defect,
# which is why this reproduces only on a game that raises it.
{
    printf 'input_database\tdata/exp.db\n'
    printf 'output_database\tdata/exp.db.new\n'
    printf 'function_recursion_limit\t%s\n' "$LIMIT"
    printf 'function_invocation_limit\t10000000\n'
} > "$WORK/jit.conf"
cp "$WORK/jit.conf" "$WORK/ast.conf"
printf 'jit_eval_brackets\t0\n' >> "$WORK/ast.conf"

# A generous limit for the "legitimate recursion still works" leg, so the
# guard cannot be shown to have broken recursion that should complete.
sed "s/^function_recursion_limit.*/function_recursion_limit	500/" \
    "$WORK/jit.conf" > "$WORK/deep.conf"
sed "s/^function_recursion_limit.*/function_recursion_limit	500/" \
    "$WORK/ast.conf" > "$WORK/deepast.conf"

run() {
    # run <conf> <cmdfile> — fresh DB each time; a left-over exp.db.new or
    # sqlite code_cache from a previous leg changes the answer.
    rm -f "$WORK"/data/exp.sqlite* "$WORK"/data/exp.db.new
    $TIMEOUT "$BIN/muxscript" -g "$WORK" -c "$1" < "$2" 2>&1
}
val() { echo "$1" | grep -a "$2~" | head -1 | sed "s/.*$2~<\(.*\)>~.*/\1/"; }
stat_of() {
    # stat_of <output> <marker> <key> — jitstats() is a space-separated
    # key=value list, so split on the separators and match a whole key.
    # A sed with a leading .* silently returns the input line unchanged
    # when the key is absent, which then reads as a passing comparison.
    echo "$1" | grep -a "$2~" | head -1 | tr ' <>~' '\n\n\n\n' \
        | grep -a "^$3=" | head -1 | cut -d= -f2
}
# Every numeric read below goes through this: a non-numeric value (key
# renamed, marker missing) must fail loudly rather than make `[ -gt ]`
# error out and fall into the passing branch.
numeric() {
    case "${2:-}" in
        ''|*[!0-9]*) echo "BROKEN $1: expected a number, got '${2:-}'"; return 1 ;;
    esac
    return 0
}

if ! echo "$(printf '@pemit #1=JITPROBE~<[jitstats()]>~\n' > "$WORK/p.txt"; \
     run jit.conf "$WORK/p.txt")" | grep -a "JITPROBE~" | grep -q "eval_attempts="; then
    echo "ERROR: this build has no JIT (jitstats() missing) — reconfigure with" >&2
    echo "  cd mux && ./configure --enable-jit ... && make clean install" >&2
    exit 2
fi

fails=0

# ---------------------------------------------------------------------
# Leg 1 — the runaway.  Both routes must terminate with the limit error,
# and the compiled route must do it in work linear in the limit.
# ---------------------------------------------------------------------
cat > "$WORK/runaway.txt" <<'EOF'
&RECUR me=[u(me/RECUR)]
think [jitstats(reset)]
think RUNAWAY~<[u(me/RECUR)]>~
think RSTATS~<[jitstats()]>~
EOF

AST_OUT=$(run ast.conf "$WORK/runaway.txt")
JIT_OUT=$(run jit.conf "$WORK/runaway.txt")

EXPECT="#-1 FUNCTION RECURSION LIMIT EXCEEDED"
a=$(val "$AST_OUT" RUNAWAY)
j=$(val "$JIT_OUT" RUNAWAY)

if [ "$a" != "$EXPECT" ]; then
    echo "BROKEN runaway-ast: AST produced '$a', expected '$EXPECT' — harness bug?"
    fails=$((fails + 1))
elif [ "$j" != "$EXPECT" ]; then
    echo "FAIL   runaway-result: AST='$a' JIT='$j'"
    fails=$((fails + 1))
else
    echo "PASS   runaway-result: both routes report the recursion limit"
fi

attempts=$(stat_of "$JIT_OUT" RSTATS eval_attempts)
handled=$(stat_of "$JIT_OUT" RSTATS eval_handled)
ecalls=$(stat_of "$JIT_OUT" RSTATS ecalls)
busy=$(stat_of "$JIT_OUT" RSTATS bail_shared_busy)

# Canary first: a vacuous pass here would look identical to a real one.
if ! numeric runaway-canary "$handled" || ! numeric runaway-canary "$ecalls"; then
    echo "ERROR: could not read eval_handled/ecalls from jitstats." >&2
    exit 2
fi
if [ "$handled" -lt 1 ] || [ "$ecalls" -lt 1 ]; then
    echo "ERROR: the compiled route did not run (eval_handled=${handled:-?}" >&2
    echo "ecalls=${ecalls:-?}) — jit_eval_brackets appears off or the JIT" >&2
    echo "declined this shape, so the cost assertion below is vacuous." >&2
    exit 2
fi
echo "PASS   runaway-canary: compiled route ran (eval_handled=$handled ecalls=$ecalls)"

if ! numeric runaway-cost "$attempts"; then
    fails=$((fails + 1))
elif [ "$attempts" -gt "$LINEAR_MAX" ]; then
    echo "FAIL   runaway-cost: eval_attempts=$attempts at function_recursion_limit=$LIMIT"
    echo "       (linear is $((LIMIT + 1)); >$LINEAR_MAX means the subtree is being"
    echo "       re-evaluated per level — the #1994 exponential is back)"
    fails=$((fails + 1))
else
    echo "PASS   runaway-cost: eval_attempts=$attempts (linear; bail_shared_busy=${busy:-0})"
fi

# ---------------------------------------------------------------------
# Leg 2 — over-correction guard.  Recursion that should complete must
# still complete, and agree with the AST.  sum(1..n) is checked against
# n(n+1)/2 rather than against the other route, so a fault common to both
# cannot pass.
# ---------------------------------------------------------------------
cat > "$WORK/deep.txt" <<'EOF'
&SUM me=[switch(gt(%0,0),1,[add(%0,u(me/SUM,sub(%0,1)))],0)]
think SUM10~<[u(me/SUM,10)]>~
think SUM30~<[u(me/SUM,30)]>~
EOF

DA_OUT=$(run deepast.conf "$WORK/deep.txt")
DJ_OUT=$(run deep.conf "$WORK/deep.txt")

for case in "SUM10 55" "SUM30 465"; do
    mark=${case% *}; want=${case#* }
    a=$(val "$DA_OUT" "$mark"); j=$(val "$DJ_OUT" "$mark")
    if [ "$a" != "$want" ]; then
        echo "BROKEN $mark: AST produced '$a', arithmetic says '$want' — harness bug?"
        fails=$((fails + 1))
    elif [ "$j" != "$want" ]; then
        echo "FAIL   $mark: recursion that should complete did not — AST='$a' JIT='$j'"
        fails=$((fails + 1))
    else
        echo "PASS   $mark: legitimate recursion completes on both routes ($j)"
    fi
done

if [ "$fails" -eq 0 ]; then
    echo "OK: runaway recursion terminates in linear work; deep recursion unaffected"
    exit 0
fi
echo "$fails failure(s)"
exit 1
