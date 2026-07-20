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
#
# This script is EXPECTED TO FAIL (exit 1) until the slot-resync work
# lands (plan Phases 2-3); it flips green as those phases land and then
# guards them.  Opt-in — not part of `make test` while red.
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

# One command file: store each expression raw in an attribute, then run
# each side in its own think with its own register preload.  ~ markers
# keep the grep unambiguous.
cat > "$WORK/cases.txt" <<'EOF'
&qf me=[setq(0,MUTATED)]
&e1 me=[setq(b,OUTER)][letq(b,INNER,%qb)][%qb]
&e2 me=[setq(0,A)][localize([setq(0,B)][localize([setq(0,C)]%q0)]%q0)]%q0
&e3 me=[u(me/%q9)][%q0]
think CASE1A~[asteval(v(e1))]~
think CASE1J~[jiteval(v(e1))]~
think CASE2A~[asteval(v(e2))]~
think CASE2J~[jiteval(v(e2))]~
think CASE3A~[setq(9,qf)][setq(0,ENTRY)][asteval(v(e3))]~
think CASE3J~[setq(9,qf)][setq(0,ENTRY)][jiteval(v(e3))]~
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

if [ "$fails" -eq 0 ]; then
    echo "OK: all q-register scope shapes agree"
    exit 0
fi
echo "$fails divergence(s) — expected while plan Phases 2-3 are unlanded"
exit 1
