#!/bin/bash
#
#   run.sh — Lua JIT differential harness (#1423, #1426, #1512).
#
#   Three contracts, three case lists:
#
#   SURVIVE_CASES  — process must not die.  Result mismatch is *reported*
#                    (often table/bridge bugs still open) but does not fail.
#
#   AGREE_CASES    — result must match the interpreter.  Decline is OK:
#                    interpreter answers.  Used for named-bridge shapes where
#                    fail-closed decline is the correct compile-path outcome
#                    until the bridge is implemented (#1512).
#
#   EXEC_CASES     — result must match AND lua_run_ok must advance on the
#                    jit path.  A decline that still "agrees" is a FAILURE:
#                    that is the vacuity #1426 named.  These chunks avoid
#                    globals/stdlib so they can compile without the Lua-VM
#                    bridge ECALLs.
#
#   Config: lua_jit 1 alone is not enough under default softcode JIT —
#   fun_lua is ECALLed from a DBT program and run_cached_program refuses a
#   nested run (#1326).  jit_eval_brackets 0 is required so the compiled
#   Lua path actually executes.
#
#   Deliberately not smoke: smoke cannot set that config without changing
#   the softcode-JIT surface, and result-only smoke is green when the
#   compiled path runs nothing.
#
set -u

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
BIN="$REPO_ROOT/mux/game/bin"
WORK="$SCRIPT_DIR/work"

if command -v timeout >/dev/null 2>&1; then
    TIMEOUT="timeout 60"
elif command -v gtimeout >/dev/null 2>&1; then
    TIMEOUT="gtimeout 60"
else
    TIMEOUT=""
fi

if [ ! -x "$BIN/muxscript" ]; then
    echo "SKIP: $BIN/muxscript not found (run 'make install' first)."
    exit 0
fi

# ---------------------------------------------------------------------------
# SURVIVE — table ops and anything that has historically aborted (#1423).
# Result divergence is reported, not fatal.
#
# As of #1518 these also happen to *agree* with the interpreter -- but by
# declining, not by lowering correctly.  The four #1424 shapes are therefore
# also listed in AGREE_CASES below, where agreement is enforced; see the note
# there (#1557).
# ---------------------------------------------------------------------------
SURVIVE_CASES=(
    'local t={10,20,30} return t[2]'
    'local t={a=7} return t.a'
    'local t={} t[1]=5 return t[1]'
    'local t={} t.x=5 return t.x'
    'local t={1,2,3} return #t'
    'local t={1,2,3} table.insert(t,4) return #t'
    'local t={10,20,30} return t[1]+t[3]'
    'local t={} for i=1,3 do t[i]=i*10 end return t[2]'
)

# ---------------------------------------------------------------------------
# AGREE — must match interpreter; decline OK (bridge / not-yet-lowered).
# Prefer `local x=f(...) return x` over `return f(...)` (OP_TAILCALL declines).
# ---------------------------------------------------------------------------
AGREE_CASES=(
    'local x=tostring(42) return x'
    'local x=type(42) return x'
    'local x=tonumber("17") return x'
    'local x=math.floor(3.7) return x'
    'local x=math.max(3,9) return x'
    'local x=math.min(3,9) return x'
    'local x=string.upper("ab") return x'
    'local x=string.lower("AB") return x'
    'local x=string.sub("hello",2,3) return x'
    'local x=string.format("%d",7) return x'
    'local x=string.len("hello") return x'
    'local x=table.concat({1,2,3},",") return x'
    'local x=select("#",1,2,3) return x'
    'local x=math.pi return x'
    'local x=math.maxinteger return x'
    'local x=math.abs(-7) return x'
    'local x=tonumber(mux.args[1]) return x'
    'local x=tonumber(mux.args[1]) return x+1'
    'local x=tonumber(mux.args[1]) return x*2'
    'local x=math.floor(3.7) return x+1'
    'local x=string.len("hello") return x*2'
    'return math.huge'
    'return os.time()>0'
    # Runtime ^: must not answer pow(0,0)==1 (#1538).  May still compile-fail
    # and fall back to the interpreter; agreement is required either way.
    'local a=mux.args[1]+0 return a ^ 2'
    'local a=mux.args[1]+0 local b=mux.args[2]+0 return a ^ b'
    'local a=mux.args[1]+0 return (a ^ 2) == 4'
    'local a=mux.args[1]+0 local b=mux.args[2]+0 return (a ^ b) == 8'
    # Loops: currently decline; still must not invent a wrong answer.
    'local s=0 for i=1,4 do s=s+i end return s'
    'local i=0 repeat i=i+1 until i>=5 return i'
    # ---- #1424's four shapes, whose symptoms #1518 closed (#1557) ----
    #
    # Used to return stack-index garbage (22) or abort.  Now agree by
    # declining / run-fail, not by correct lowering (#1519).  Fatal here so
    # containment cannot regress silently.  Re-measure lua_run_ok when the
    # bridge is brought up -- green alone does not mean they execute.
    'local t={1,2,3} return #t'
    'local t={1,2,3} table.insert(t,4) return #t'
    'local t={10,20,30} return t[2]'
    'local t={a=7} return t.a'
)

# ---------------------------------------------------------------------------
# EXEC — must match AND lua_run_ok must advance (#1426).
# No globals/stdlib: pure arithmetic / compare / branch on mux.args.
# ---------------------------------------------------------------------------
EXEC_CASES=(
    'return mux.args[1] + mux.args[2]'
    'local x=mux.args[1]+0 return x*2'
    'local a=mux.args[1]+0 return a+1'
    'local a=mux.args[1]+0 return a-1'
    'local a=mux.args[1]+0 return a*3'
    'local a=mux.args[1]+0 return -a'
    'local a=mux.args[1]+0 return a//2'
    'local a=mux.args[1]+0 return a%2'
    'local a=mux.args[1]+0 return a < 5'
    'local a=mux.args[1]+0 return a > 5'
    'local a=mux.args[1]+0 return a == 2'
    'local a=mux.args[1]+0 return a ~= 3'
    'local a=mux.args[1]+0 return a <= 2'
    'local a=mux.args[1]+0 return a >= 2'
    'local a=1 local b=2 return a < b'
    'local a=1 local b=2 return a > b'
    'local a=mux.args[1]+0 if a < 5 then return 7 else return 8 end'
    'local a=mux.args[1]+0 if a > 5 then return 7 else return 8 end'
    'local a=mux.args[1]+0 local t=(a<5) return t'
    'local a=mux.args[1]+0 local t=(a==2) return t'
    'local a=mux.args[1]+0 local t=(a>1) return t'
    'local a=mux.args[1]+0 local b=mux.args[2]+0 return a+b*2'
    'local a=mux.args[1]+0 return (a+1)*(a-1)'
    # ---- #1488: integral floats keep the ".0" (Lua float subtype) ----
    'return 3.0'
    'local a=1.5 local b=2.5 return a+b'
    'local a=1.5 return a+1.25'
    'local a=mux.args[1]+0.0 return a+0.0'
    # ---- #1561 + #1538: runtime ^ must compile (tier2 loaded) and agree ----
    'local a=mux.args[1]+0 return a ^ 2'
    'local a=mux.args[1]+0 local b=mux.args[2]+0 return a ^ b'
)

keep_work() {
    local rc="$1" tag="$2"
    if [ "$rc" = "0" ] || [ ! -d "$WORK" ]; then
        return 0
    fi
    local dest="$SCRIPT_DIR/work.fail.$tag.$$"
    rm -rf "$dest"
    if mv "$WORK" "$dest" 2>/dev/null; then
        printf '%s' "$dest"
    fi
}

# $1 = lua_jit, $2 = chunk, $3 = tag
# Echoes: rc|result|run_ok|run_fail|compile_ok|compile_fail|kept
run_one() {
    local mode="$1" chunk="$2" tag="${3:-run}"
    rm -rf "$WORK"; mkdir -p "$WORK/data" "$WORK/logs" "$WORK/text"
    ( cd "$WORK" || exit 1
      ulimit -c unlimited 2>/dev/null || true
      ln -s "$BIN" bin
      cp "$REPO_ROOT/mux/game/alias.conf" "$REPO_ROOT/mux/game/compat.conf" . 2>/dev/null
      cat > p.conf <<EOF
input_database  data/p.db
output_database data/p.db.new
crash_database  data/p.db.CRASH
mail_database   data/mail.db
comsys_database data/comsys.db
port 2879
mud_name LuaJitDiff
command_quota_increment 200000
command_quota_max 200000
include alias.conf
include compat.conf
lua_jit $mode
jit_eval_brackets 0
EOF
      {
        echo "@create probe"
        echo "&A probe=$chunk"
        echo "think HASJIT|[strmatch(jitstats(),*eval_attempts=*)]"
        echo "think RESULT|[lua(probe/A,2,3)]"
        echo "think STATS|[jitstats()]"
        echo "@shutdown"
      } > in.txt
      LD_LIBRARY_PATH="$BIN" $TIMEOUT "$BIN/muxscript" -g . -c p.conf < in.txt > out.log 2>&1
      echo "$?" > rc.txt
    )
    local rc res kept stats rok rfail cok cfail
    rc=$(cat "$WORK/rc.txt" 2>/dev/null || echo 99)
    res=$(grep -oE "^RESULT\|.*" "$WORK/out.log" 2>/dev/null | head -1)
    res=${res#RESULT|}
    res=$(printf '%s' "$res" | tr -d '\r')
    stats=$(grep -oE "^STATS\|.*" "$WORK/out.log" 2>/dev/null | head -1)
    stats=${stats#STATS|}
    rok=$(printf '%s' "$stats" | grep -oE 'lua_run_ok=[0-9]+' | head -1 | cut -d= -f2)
    rfail=$(printf '%s' "$stats" | grep -oE 'lua_run_fail=[0-9]+' | head -1 | cut -d= -f2)
    cok=$(printf '%s' "$stats" | grep -oE 'lua_compile_ok=[0-9]+' | head -1 | cut -d= -f2)
    cfail=$(printf '%s' "$stats" | grep -oE 'lua_compile_fail=[0-9]+' | head -1 | cut -d= -f2)
    : "${rok:=0}" "${rfail:=0}" "${cok:=0}" "${cfail:=0}"
    kept=$(keep_work "$rc" "$tag")
    echo "$rc|$res|$rok|$rfail|$cok|$cfail|$kept"
}

split6() {
    R_RC=${1%%|*}; local r=${1#*|}
    R_RES=${r%%|*}; r=${r#*|}
    R_OK=${r%%|*}; r=${r#*|}
    R_FAIL=${r%%|*}; r=${r#*|}
    R_COK=${r%%|*}; r=${r#*|}
    R_CFAIL=${r%%|*}; r=${r#*|}
    R_KEPT=$r
}

# Preflight.
split6 "$(run_one 1 'return 1' baseline)"
if [ "$R_RC" != "0" ]; then
    echo "FAIL: baseline 'return 1' did not survive (rc=$R_RC)."
    [ -n "$R_KEPT" ] && echo "      evidence: $R_KEPT"
    exit 1
fi
if ! grep -q "^HASJIT|1" "$WORK/out.log" 2>/dev/null; then
    echo "SKIP: engine JIT not present in this build (configure --enable-jit)."
    rm -rf "$WORK"
    exit 0
fi
if [ "$R_OK" = "0" ]; then
    echo "FAIL: baseline 'return 1' did not advance lua_run_ok under"
    echo "      lua_jit 1 + jit_eval_brackets 0 (got ok=$R_OK fail=$R_FAIL)."
    echo "      The EXEC contract cannot be checked; see #1426 / #1326."
    exit 1
fi

crashes=0
surv_div=0
agree_wrong=0
exec_wrong=0
exec_no_run=0
n=0

printf '\n== SURVIVE (crash = fail; result diverge = report) ==\n'
printf '%-48s %-4s %-10s %-10s %-8s %s\n' chunk rc interp jit run_ok verdict
for chunk in "${SURVIVE_CASES[@]}"; do
    n=$((n+1))
    split6 "$(run_one 0 "$chunk" "s0-$n")"; irc=$R_RC; ires=$R_RES
    split6 "$(run_one 1 "$chunk" "s1-$n")"; jrc=$R_RC; jres=$R_RES; jok=$R_OK; jkept=$R_KEPT
    verdict="ok"
    if [ "$jrc" != "0" ]; then
        verdict="FAIL: died rc=$jrc"
        [ -n "$jkept" ] && verdict="$verdict evidence=$jkept"
        crashes=$((crashes+1))
    elif [ "$irc" != "0" ]; then
        verdict="FAIL: interp died rc=$irc"
        crashes=$((crashes+1))
    elif [ "$ires" != "$jres" ]; then
        verdict="diverges interp=$ires jit=$jres"
        surv_div=$((surv_div+1))
    fi
    printf '%-48s %-4s %-10s %-10s %-8s %s\n' \
        "${chunk:0:48}" "$jrc" "${ires:0:10}" "${jres:0:10}" "$jok" "$verdict"
done

printf '\n== AGREE (must match; decline OK) ==\n'
printf '%-48s %-4s %-10s %-10s %-8s %s\n' chunk rc interp jit run_ok verdict
for chunk in "${AGREE_CASES[@]}"; do
    n=$((n+1))
    split6 "$(run_one 0 "$chunk" "a0-$n")"; irc=$R_RC; ires=$R_RES
    split6 "$(run_one 1 "$chunk" "a1-$n")"; jrc=$R_RC; jres=$R_RES; jok=$R_OK; jfail=$R_FAIL; jkept=$R_KEPT
    verdict="ok"
    if [ "$jrc" != "0" ]; then
        verdict="FAIL: died rc=$jrc"
        [ -n "$jkept" ] && verdict="$verdict evidence=$jkept"
        crashes=$((crashes+1))
    elif [ "$irc" != "0" ]; then
        verdict="FAIL: interp died rc=$irc"
        crashes=$((crashes+1))
    elif [ "$ires" != "$jres" ]; then
        verdict="FAIL: diverges interp=$ires jit=$jres"
        agree_wrong=$((agree_wrong+1))
    elif [ "$jok" = "0" ]; then
        verdict="ok (declined/compile-fail; interp answered)"
    fi
    printf '%-48s %-4s %-10s %-10s %-8s %s\n' \
        "${chunk:0:48}" "$jrc" "${ires:0:10}" "${jres:0:10}" "$jok" "$verdict"
done

printf '\n== EXEC (must match AND lua_run_ok>0) ==\n'
printf '%-48s %-4s %-10s %-10s %-8s %s\n' chunk rc interp jit run_ok verdict
for chunk in "${EXEC_CASES[@]}"; do
    n=$((n+1))
    split6 "$(run_one 0 "$chunk" "e0-$n")"; irc=$R_RC; ires=$R_RES
    split6 "$(run_one 1 "$chunk" "e1-$n")"; jrc=$R_RC; jres=$R_RES; jok=$R_OK; jfail=$R_FAIL; jkept=$R_KEPT
    verdict="ok"
    if [ "$jrc" != "0" ]; then
        verdict="FAIL: died rc=$jrc"
        [ -n "$jkept" ] && verdict="$verdict evidence=$jkept"
        crashes=$((crashes+1))
    elif [ "$irc" != "0" ]; then
        verdict="FAIL: interp died rc=$irc"
        crashes=$((crashes+1))
    elif [ "$ires" != "$jres" ]; then
        verdict="FAIL: diverges interp=$ires jit=$jres"
        exec_wrong=$((exec_wrong+1))
    elif [ "$jok" = "0" ]; then
        # The #1426 vacuity: agree by declining / compile-fail / never running.
        verdict="FAIL: lua_run_ok=0 (compiled path did not execute; #1426)"
        exec_no_run=$((exec_no_run+1))
    fi
    printf '%-48s %-4s %-10s %-10s %-8s %s\n' \
        "${chunk:0:48}" "$jrc" "${ires:0:10}" "${jres:0:10}" "$jok" "$verdict"
done

rm -rf "$WORK"
total=$((${#SURVIVE_CASES[@]} + ${#AGREE_CASES[@]} + ${#EXEC_CASES[@]}))
echo
echo "chunks: $total   crashes: $crashes   survive_diverges: $surv_div"
echo "agree_wrong: $agree_wrong   exec_wrong: $exec_wrong   exec_no_run: $exec_no_run"

if [ "$crashes" -ne 0 ] || [ "$agree_wrong" -ne 0 ] \
   || [ "$exec_wrong" -ne 0 ] || [ "$exec_no_run" -ne 0 ]; then
    echo "=== tests/luajit: FAILED ==="
    if [ "$crashes" -ne 0 ]; then
        echo "  process deaths: preserved work.fail.* dirs next to this script"
        ls -d "$SCRIPT_DIR"/work.fail.*."$$" 2>/dev/null | sed 's/^/    /' || true
    fi
    if [ "$exec_no_run" -ne 0 ]; then
        echo "  EXEC cases with lua_run_ok=0: the compiled path never ran."
        echo "  Fix config (jit_eval_brackets 0) or the lowering, not the assert."
        echo "  See #1426."
    fi
    if [ "$agree_wrong" -ne 0 ] || [ "$exec_wrong" -ne 0 ]; then
        echo "  Result mismatch vs interpreter — see #1512 / open lua/jit bugs."
    fi
    exit 1
fi
echo "=== tests/luajit: PASSED ==="
echo "  SURVIVE: no crashes ($surv_div reported divergences, non-fatal)"
echo "  AGREE:   all match interpreter (decline allowed)"
echo "  EXEC:    all match and lua_run_ok advanced (#1426)"
exit 0
