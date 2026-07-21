#!/bin/sh
#
# q-register scope oracle for the eval-bracket guard-lift work
# (docs/plan-jit-evalbracket-lift.md, Phase 0).
#
# Drives the three known JIT-vs-AST q-register divergence shapes through
# jiteval() (which bypasses the jit_can_handle() production gate) and
# compares against asteval().  Expressions are stored in attributes and
# fetched with v() so both sides receive the RAW text — passing bracketed
# text directly as an argument would be pre-evaluated by the outer
# evaluator and compare two literals (a footgun this harness exists to
# avoid).
#
#   D1-letq      inlined letq scope restore leaves the SUBST slot stale
#   D1-localize  nested localize restore leaves the SUBST slot stale
#   D2-ecall-u   a non-inlined u() ECALL body's setq mutates global_regs
#                but never the slot a later %q read uses
#   R1-letq-r    tracked r(n) inside a letq body reads the pre-letq SSA
#                (letq assignments never updated compile-time tracking)
#   R2-scope-r   tracked r(n) after a scope restore reads the inner SSA
#                (no compile-time tracking restore at scope exit)
#   RW-strcat    read-write-read: %q srefs were deferred pointer derefs
#                observed at consumption time, so a read BEFORE a setq
#                returned the post-setq value (production-reachable,
#                no brackets needed: strcat(%q0,setq(0,B),%q0) -> "BB")
#   D2-ecall-r   tracked r(n) after an opaque ECALL reads the stale SSA
#                (compile-time tracking not invalidated across calls
#                whose callee may mutate global_regs)
#
# With plan Phases 2-3 landed, all shapes are green and this script
# guards them — it runs as part of `make test` (test-jit-qreg target;
# skipped automatically on non-JIT builds via exit 2).
#
# Flag asymmetry note for future case authors: the AST side runs under
# EV_FCHECK and the JIT side under EV_FMAND (the astbench convention).
# The current shapes are insensitive to that — and the AST self-check
# pins each expected literal — but an expression whose result differs
# between FCHECK and FMAND would report a "divergence" that is flag
# skew, not a guard-lift bug.  Keep oracle expressions flag-neutral.
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
printf 'input_database\tdata/exp.db\noutput_database\tdata/exp.db.new\n' > "$WORK/exp.conf"

DYLD_LIBRARY_PATH="$BIN"; export DYLD_LIBRARY_PATH
LD_LIBRARY_PATH="$BIN";   export LD_LIBRARY_PATH

# Probe that the build actually has the JIT (jiteval is only registered
# under TINYMUX_JIT).
printf '@pemit #1=JITPROBE~[jitstats()]~\n' > "$WORK/probe.txt"
$TIMEOUT "$BIN/muxscript" -g "$WORK" -c exp.conf < "$WORK/probe.txt" > "$WORK/probe.log" 2>&1
if ! grep -a "JITPROBE~" "$WORK/probe.log" | grep -q "="; then
    echo "ERROR: this build has no JIT (jitstats() missing) — reconfigure with" >&2
    echo "  cd mux && ./configure --enable-jit ... && make clean install" >&2
    exit 2
fi

# Canary: jiteval of a trivially-compilable expression must actually
# JIT.  On a build where the JIT declines everything (e.g. a missing or
# unloadable softlib.rv64 blob, #875), all three cases would report
# "#-1 JIT BAILOUT" — three FAILs indistinguishable from the real bug
# signal.  Exit 2 instead so red stays trustworthy.
printf '&canary me=add(1,1)\nthink CANARY~[jiteval(v(canary))]~\n' > "$WORK/canary.txt"
$TIMEOUT "$BIN/muxscript" -g "$WORK" -c exp.conf < "$WORK/canary.txt" > "$WORK/canary.log" 2>&1
canary=$(grep -a "CANARY~" "$WORK/canary.log" | head -1 | sed 's/.*CANARY~\(.*\)~.*/\1/')
if [ "$canary" != "2" ]; then
    echo "ERROR: jiteval canary returned '$canary' (wanted 2) — the JIT is" >&2
    echo "declining expressions (softlib.rv64 blob missing?); a divergence" >&2
    echo "run would be meaningless." >&2
    exit 2
fi

# One command file: store each expression raw in an attribute, then run
# each side in its own think with its own register preload.  ~ markers
# keep the grep unambiguous.
cat > "$WORK/cases.txt" <<'EOF'
&qf me=[setq(0,MUTATED)]
&e1 me=[setq(b,OUTER)][letq(b,INNER,%qb)][%qb]
&e2 me=[setq(0,A)][localize([setq(0,B)][localize([setq(0,C)]%q0)]%q0)]%q0
&e3 me=[u(me/%q9)][%q0]
&e4 me=[setq(0,PRE)][letq(0,IN,r(0))][r(0)]
&e5 me=[setq(0,A)][localize(setq(0,B))][r(0)]
&e6 me=strcat(%q0,setq(0,B),%q0)
&e7 me=[setq(0,PRE)][u(me/%q9)][r(0)]
think CASE1A~[asteval(v(e1))]~
think CASE1J~[jiteval(v(e1))]~
think CASE2A~[asteval(v(e2))]~
think CASE2J~[jiteval(v(e2))]~
think CASE3A~[setq(9,qf)][setq(0,ENTRY)][asteval(v(e3))]~
think CASE3J~[setq(9,qf)][setq(0,ENTRY)][jiteval(v(e3))]~
think CASE4A~[asteval(v(e4))]~
think CASE4J~[jiteval(v(e4))]~
think CASE5A~[asteval(v(e5))]~
think CASE5J~[jiteval(v(e5))]~
think CASE6A~[setq(0,A)][asteval(v(e6))]~
think CASE6J~[setq(0,A)][jiteval(v(e6))]~
think CASE7A~[setq(9,qf)][asteval(v(e7))]~
think CASE7J~[setq(9,qf)][jiteval(v(e7))]~
EOF

rm -f "$WORK"/data/exp.sqlite*
$TIMEOUT "$BIN/muxscript" -g "$WORK" -c exp.conf < "$WORK/cases.txt" > "$WORK/cases.log" 2>&1

get() { grep -a "$1~" "$WORK/cases.log" | head -1 | sed "s/.*$1~\(.*\)~.*/\1/"; }

fails=0
check() {
    name=$1; amark=$2; jmark=$3; expect=$4
    a=$(get "$amark"); j=$(get "$jmark")
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

check "D1-letq"     CASE1A CASE1J "INNEROUTER"
check "D1-localize" CASE2A CASE2J "CBA"
check "D2-ecall-u"  CASE3A CASE3J "MUTATED"
check "R1-letq-r"   CASE4A CASE4J "INPRE"
check "R2-scope-r"  CASE5A CASE5J "A"
check "RW-strcat"   CASE6A CASE6J "AB"
check "D2-ecall-r"  CASE7A CASE7J "MUTATED"

if [ "$fails" -eq 0 ]; then
    echo "OK: all q-register scope shapes agree"
    exit 0
fi
echo "$fails divergence(s)"
exit 1
