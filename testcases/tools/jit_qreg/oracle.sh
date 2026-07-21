#!/bin/sh
#
# q-register scope oracle (docs/plan-jit-evalbracket-lift.md).
#
# Compares the PRODUCTION evaluation of the q-register scope/ordering
# shapes fixed across plan Phases 2-3 and #996 between two workspaces:
#
#   J side: default conf — jit_eval_brackets is ON by default since the
#           Phase 5 flip, so each u()-carried case is JIT-compiled.
#   I side: jit_eval_brackets explicitly 0 — the eval-bracket bail
#           routes every case to the AST evaluator (the faithful
#           interpreter oracle, production flags).
#
# Each case's expression is stored in an attribute and evaluated via a
# fresh u() program; register preloads run in the carrier command.
# (The Phase 0-4 era used a jiteval() gate-bypass function here; it was
# retired with the default flip — the production route now reaches
# everything it existed for.)
#
#   D1-letq      inlined letq scope restore left the SUBST slot stale
#   D1-localize  nested localize restore left the SUBST slot stale
#   D2-ecall-u   a non-inlined u() ECALL body's setq mutated global_regs
#                but never the slot a later %q read used
#   R1-letq-r    tracked r(n) inside a letq body read the pre-letq SSA
#   R2-scope-r   tracked r(n) after a scope restore read the inner SSA
#   RW-strcat    read-write-read: %q srefs were deferred pointer derefs
#   D2-ecall-r   tracked r(n) after an opaque ECALL read the stale SSA
#   LR-scope-*   #996 four-transition proof: long q-register values
#                across localize scope entry/exit
#
# Runs as part of `make test` (test-jit-qreg target; skipped on
# non-JIT builds via exit 2).
#
# Usage:  oracle.sh
# Exit status: 0 = all shapes agree, 1 = divergence, 2 = setup error.
#
set -u

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO=$(CDPATH= cd -- "$SCRIPT_DIR/../../.." && pwd)
BIN="$REPO/mux/game/bin"

if [ ! -x "$BIN/muxscript" ] || [ ! -e "$BIN/engine.so" ]; then
    echo "ERROR: $BIN/muxscript or engine.so missing — run 'make install' first." >&2
    exit 2
fi

if command -v timeout >/dev/null 2>&1; then TIMEOUT="timeout 90"
elif command -v gtimeout >/dev/null 2>&1; then TIMEOUT="gtimeout 90"
else TIMEOUT=""; fi

WORK=$(mktemp -d "${TMPDIR:-/tmp}/jitqreg.XXXXXX") || exit 2
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/data"
ln -sfn "$BIN" "$WORK/bin"
printf 'input_database\tdata/exp.db\noutput_database\tdata/exp.db.new\n' > "$WORK/jit.conf"
cp "$WORK/jit.conf" "$WORK/ast.conf"
printf 'jit_eval_brackets\t0\n' >> "$WORK/ast.conf"

DYLD_LIBRARY_PATH="$BIN"; export DYLD_LIBRARY_PATH
LD_LIBRARY_PATH="$BIN";   export LD_LIBRARY_PATH

# Probe that the build actually has the JIT (jitstats() is only
# registered under TINYMUX_JIT).
printf '@pemit #1=JITPROBE~[jitstats()]~\n' > "$WORK/probe.txt"
$TIMEOUT "$BIN/muxscript" -g "$WORK" -c jit.conf < "$WORK/probe.txt" > "$WORK/probe.log" 2>&1
if ! grep -a "JITPROBE~" "$WORK/probe.log" | grep -q "="; then
    echo "ERROR: this build has no JIT (jitstats() missing) — reconfigure with" >&2
    echo "  cd mux && ./configure --enable-jit ... && make clean install" >&2
    exit 2
fi

# Canary: in the J workspace, a u()-carried bracket case must actually
# JIT (eval_handled >= 1).  A build where the JIT declines everything
# (missing softlib.rv64, #875) or where the default flip regressed
# would otherwise produce a vacuous AST-vs-AST comparison.
cat > "$WORK/canary.txt" <<'EOF'
&canary me=[strcat(ab,cd)]
think [jitstats(reset)]
think CANARY~<[u(me/canary)]>~
think CANSTATS~[jitstats()]~
EOF
rm -f "$WORK"/data/exp.sqlite*
$TIMEOUT "$BIN/muxscript" -g "$WORK" -c jit.conf < "$WORK/canary.txt" > "$WORK/canary.log" 2>&1
if ! grep -a "CANARY~" "$WORK/canary.log" | grep -q "<abcd>"; then
    echo "ERROR: canary expression misevaluated — cannot trust the run." >&2
    exit 2
fi
if ! grep -a "CANSTATS~" "$WORK/canary.log" | grep -qE "eval_handled=[1-9]"; then
    echo "ERROR: the J-side canary did not JIT (eval_handled=0) — the" >&2
    echo "toggle default appears off or the JIT is declining; a divergence" >&2
    echo "run would compare the AST against itself." >&2
    exit 2
fi

# One command file, run in BOTH workspaces: store each expression raw
# in an attribute, evaluate via a fresh u() program with per-case
# register preloads in the carrier.  ~ markers keep the grep
# unambiguous.
#
# Honesty note: e6 (RW-strcat) is bracket-free, so the toggle cannot
# route it to the AST — BOTH workspaces JIT it, and that case is
# guarded by its expected constant (validated against the interpreter
# when the shape was fixed in Phase 2) rather than a live AST run.
cat > "$WORK/cases.txt" <<'EOF'
&qf me=[setq(0,MUTATED)]
&e1 me=[setq(b,OUTER)][letq(b,INNER,%qb)][%qb]
&e2 me=[setq(0,A)][localize([setq(0,B)][localize([setq(0,C)]%q0)]%q0)]%q0
&e3 me=[u(me/%q9)][%q0]
&e4 me=[setq(0,PRE)][letq(0,IN,r(0))][r(0)]
&e5 me=[setq(0,A)][localize(setq(0,B))][r(0)]
&e6 me=strcat(%q0,setq(0,B),%q0)
&e7 me=[setq(0,PRE)][u(me/%q9)][r(0)]
&e8 me=[setq(0,repeat(x,300))][localize([setq(0,S)][strlen(%q0)])][strlen(%q0)]
&e9 me=[setq(0,S)][localize([setq(0,repeat(x,300))][strlen(%q0)])][strlen(%q0)]
think CASE1~<[u(me/e1)]>~
think CASE2~<[u(me/e2)]>~
think CASE3~[setq(9,qf)][setq(0,ENTRY)]<[u(me/e3)]>~
think CASE4~<[u(me/e4)]>~
think CASE5~<[u(me/e5)]>~
think CASE6~[setq(0,A)]<[u(me/e6)]>~
think CASE7~[setq(9,qf)]<[u(me/e7)]>~
think CASE8~<[u(me/e8)]>~
think CASE9~<[u(me/e9)]>~
EOF

rm -f "$WORK"/data/exp.sqlite*
$TIMEOUT "$BIN/muxscript" -g "$WORK" -c jit.conf < "$WORK/cases.txt" > "$WORK/jit.log" 2>&1
rm -f "$WORK"/data/exp.sqlite*
$TIMEOUT "$BIN/muxscript" -g "$WORK" -c ast.conf < "$WORK/cases.txt" > "$WORK/ast.log" 2>&1

get() { grep -a "$2~" "$WORK/$1" | head -1 | sed "s/.*$2~.*<\(.*\)>.*/\1/"; }

fails=0
check() {
    name=$1; mark=$2; expect=$3
    a=$(get ast.log "$mark"); j=$(get jit.log "$mark")
    if [ "$a" != "$expect" ]; then
        echo "BROKEN $name: AST produced '$a', oracle expected '$expect' — harness bug?"
        fails=$((fails + 1))
    elif [ "$j" = "$expect" ]; then
        echo "PASS   $name: AST=JIT='$j'"
    else
        echo "FAIL   $name: AST='$a' JIT='$j'"
        fails=$((fails + 1))
    fi
}

check "D1-letq"      CASE1 "INNEROUTER"
check "D1-localize"  CASE2 "CBA"
check "D2-ecall-u"   CASE3 "MUTATED"
check "R1-letq-r"    CASE4 "INPRE"
check "R2-scope-r"   CASE5 "A"
check "RW-strcat"    CASE6 "AB"
check "D2-ecall-r"   CASE7 "MUTATED"
check "LR-scope-out" CASE8 "1300"
check "LR-scope-in"  CASE9 "3001"

if [ "$fails" -eq 0 ]; then
    echo "OK: all q-register scope shapes agree"
    exit 0
fi
echo "$fails divergence(s)"
exit 1
