#!/bin/sh
#
# ifelse()/if() condition-truth oracle (#1157).
#
# Compares the PRODUCTION evaluation of ifelse()/if() conditions between
# two workspaces, the same way tools/jit_qreg/oracle.sh does:
#
#   J side: default conf — jit_eval_brackets is ON by default, so each
#           u()-carried bracket case is JIT-compiled.
#   I side: jit_eval_brackets 0 — the eval-bracket bail routes every
#           case to the AST evaluator (the faithful interpreter oracle).
#
# The interpreter decides these conditions with xlate() (fun_ifelse in
# funceval.cpp).  xlate() is not "atol() != 0" and not an integer
# truncation, so the JIT has three distinct ways to get it wrong, and
# all three shipped at once in #1157:
#
#   carg    a bare %0-%9 lowers via emit_sref(): an HIR_SCONST with an
#           empty sval and a runtime_ref marking, whose value only
#           exists once run_cached_program fills CARGS_BASE.  Folding it
#           read "" and picked the else arm on every single call.
#   const   a genuine literal folded with mux_atol(), so "abc", "#5" and
#           "0abc" -- all true to xlate() -- came out false.
#   float   a float condition truncated with FTOI, so 0.5 and -0.5 came
#           out false.
#
# Each case is stored raw in an attribute and evaluated through a fresh
# u() program, so the condition is a real runtime argument rather than
# something the lowerer can fold.  ~ markers keep the grep unambiguous.
#
# Runs as part of `make test` (test-jit-ifelse target; skipped on
# non-JIT builds via exit 2).
#
# Usage:  oracle.sh
# Exit status: 0 = all shapes agree, 1 = divergence, 2 = setup error.

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

WORK=$(mktemp -d "${TMPDIR:-/tmp}/jitifelse.XXXXXX") || exit 2
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

# Canary: the J workspace must actually JIT an ifelse carried through
# u().  Without this, a build where the toggle default regressed or the
# JIT declines the shape would compare the AST against itself and report
# a vacuous pass — which is exactly the failure mode that let #1157 sit
# undetected.
cat > "$WORK/canary.txt" <<'EOF'
&can me=[ifelse(%0,Y,N)]
think [jitstats(reset)]
think CANARY~<[u(me/can,1)]>~
think CANSTATS~[jitstats()]~
EOF
rm -f "$WORK"/data/exp.sqlite*
$TIMEOUT "$BIN/muxscript" -g "$WORK" -c jit.conf < "$WORK/canary.txt" > "$WORK/canary.log" 2>&1
if ! grep -a "CANSTATS~" "$WORK/canary.log" | grep -qE "eval_handled=[1-9]"; then
    echo "ERROR: the J-side canary did not JIT (eval_handled=0) — the" >&2
    echo "toggle default appears off or the JIT is declining; a divergence" >&2
    echo "run would compare the AST against itself." >&2
    exit 2
fi

# One command file, run in BOTH workspaces.
cat > "$WORK/cases.txt" <<'EOF'
&cond me=[ifelse(%0,Y,N)]
&if1 me=[if(%0,Y)]
&nest me=[ifelse(%0,[ifelse(%1,AA,AB)],BB)]
&k_abc me=[ifelse(abc,Y,N)]
&k_ref me=[ifelse(#5,Y,N)]
&k_0abc me=[ifelse(0abc,Y,N)]
&k_negref me=[ifelse(#-1,Y,N)]
&k_zero me=[ifelse(0,Y,N)]
&k_zerof me=[ifelse(00.00,Y,N)]
&k_empty me=[ifelse(,Y,N)]
&f_half me=[ifelse(0.5,Y,N)]
&f_sum me=[ifelse(add(0.2,0.3),Y,N)]
&f_neg me=[ifelse(sub(0.0,0.5),Y,N)]
&r_qreg me=[ifelse(%q0,Y,N)]
&r_strlen me=[ifelse(strlen(%0),Y,N)]
&r_gt me=[ifelse(gt(%0,0),Y,N)]
&r_first me=[ifelse(first(%0),Y,N)]
think CARG1~<[u(me/cond,7)]>~
think CARG0~<[u(me/cond,0)]>~
think CARGS~<[u(me/cond,abc)]>~
think CARGI~<[u(me/if1,7)]>~
think NEST~<[u(me/nest,1,1)]>~
think NESTB~<[u(me/nest,0,1)]>~
think KABC~<[u(me/k_abc)]>~
think KREF~<[u(me/k_ref)]>~
think K0ABC~<[u(me/k_0abc)]>~
think KNEG~<[u(me/k_negref)]>~
think KZERO~<[u(me/k_zero)]>~
think KZEROF~<[u(me/k_zerof)]>~
think KEMPTY~<[u(me/k_empty)]>~
think FHALF~<[u(me/f_half)]>~
think FSUM~<[u(me/f_sum)]>~
think FNEG~<[u(me/f_neg)]>~
think RQREG~[setq(0,1)]<[u(me/r_qreg)]>~
think RSLEN~<[u(me/r_strlen,abc)]>~
think RGT~<[u(me/r_gt,5)]>~
think RFIRST~<[u(me/r_first,1 2)]>~
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

# carg conditions — the reported defect.
check "carg-true"    CARG1  "Y"
check "carg-false"   CARG0  "N"
check "carg-string"  CARGS  "Y"
check "carg-if"      CARGI  "Y"
check "carg-nest"    NEST   "AA"
check "carg-nest-el" NESTB  "BB"
# constant conditions — xlate() vs atol().
check "const-abc"    KABC   "Y"
check "const-dbref"  KREF   "Y"
check "const-0abc"   K0ABC  "Y"
check "const-negref" KNEG   "N"
check "const-zero"   KZERO  "N"
check "const-zerof"  KZEROF "N"
check "const-empty"  KEMPTY "N"
# float conditions — must not truncate toward zero.
check "float-half"   FHALF  "Y"
check "float-sum"    FSUM   "Y"
check "float-neg"    FNEG   "Y"
# shapes that already worked — guard against over-correcting.
check "regr-qreg"    RQREG  "Y"
check "regr-strlen"  RSLEN  "Y"
check "regr-gt"      RGT    "Y"
check "regr-first"   RFIRST "Y"

if [ "$fails" -eq 0 ]; then
    echo "OK: all ifelse()/if() condition shapes agree"
    exit 0
fi
echo "$fails divergence(s)"
exit 1
