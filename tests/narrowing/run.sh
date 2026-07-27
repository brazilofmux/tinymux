#!/bin/bash
#
#   run.sh — 64-bit parse into a narrower destination (#1402).
#
#   mux_atoi64() returns int64_t.  Storing that in a narrower destination
#   truncates, and the truncation happens BEFORE anything can judge the value,
#   so a range check placed after it passes on input it was written to reject.
#   #1404 fixed three instances of the shape -- centerjustcombo's width (which
#   had silently reintroduced #860 at a higher threshold), fun_printf's %d, and
#   fun_shl's defeated `0 <= b && b < 64`.  These are two more.
#
#   Both are observable on LP64, unlike the rest of #1402.  `long` is 64-bit on
#   Linux and macOS, so the LLP64 half of that issue cannot fail here and its
#   smoke cases pass on this box whether the fix is present or not -- a fact
#   established on #1404 by reverting its engine changes and watching its own
#   tests still pass.  `int` is 32-bit everywhere, so these two do fail here,
#   which is the whole reason they are worth a harness.
#
#   Why not smoke:
#
#     * @poor is CA_GOD, and smoke runs as an object rather than God, so a
#       tr.tc* case would be silently refused.  Worse, @poor walks the WHOLE
#       database -- a smoke case would rewrite every other test's money.
#     * hasquota() needs `quotas yes`.  Smoke deliberately runs with quotas off,
#       and powersee_fn.mux TC005 asserts the disabled path, so enabling them
#       globally to reach this would break an existing case.
#
#   So each case gets a throwaway game of its own, same shape as tests/luajit.
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

fails=0

# Run a script against a throwaway game.  $1 = extra config lines, $2 = the
# command stream.  Echoes the log.
run_game() {
    local extra="$1" stream="$2"
    rm -rf "$WORK"; mkdir -p "$WORK/data" "$WORK/logs" "$WORK/text"
    ( cd "$WORK" || exit 1
      ln -s "$BIN" bin
      cp "$REPO_ROOT/mux/game/alias.conf" "$REPO_ROOT/mux/game/compat.conf" . 2>/dev/null
      { cat <<EOF
input_database  data/p.db
output_database data/p.db.new
crash_database  data/p.db.CRASH
mail_database   data/mail.db
comsys_database data/comsys.db
port 2896
mud_name Narrowing
command_quota_increment 200000
command_quota_max 200000
include alias.conf
include compat.conf
EOF
        printf '%s\n' "$extra"
      } > p.conf
      printf '%s\n' "$stream" > in.txt
      LD_LIBRARY_PATH="$BIN" $TIMEOUT "$BIN/muxscript" -g . -c p.conf < in.txt > out.log 2>&1
      echo "$?" > rc.txt
    )
    cat "$WORK/out.log" 2>/dev/null
}

# ---------------------------------------------------------------
# @poor: the limit is compared against Pennies(), which is int.
#
# A limit outside int range cannot describe wealth any player can hold, so it
# must reduce nobody.  Before the fix the argument wrapped and became its own
# opposite: `@poor 4294967296` truncated to 0 and zeroed every player's money,
# and `@poor 2147483648` truncated to INT_MIN and left everyone holding
# -2147483648 pennies.  Nothing range-checked, so the wrap was the arithmetic.
#
# God starts with 1000 pennies in a fresh database, which is what these read.
# The in-range cases are the controls: a clamp that swallowed everything would
# pass the out-of-range rows and fail these.
# ---------------------------------------------------------------
echo "=== @poor: out-of-int-range limit must reduce nobody (#1402) ==="
printf '%-22s %-12s %-12s %s\n' 'limit' 'expected' 'got' 'verdict'
poor_case() {
    local limit="$1" expect="$2" note="$3"
    local log got
    log=$(run_game "" "$(printf '%s\n' \
        "think BEFORE|[money(me)]" \
        "@poor $limit" \
        "think AFTER|[money(me)]" \
        "@shutdown")")
    got=$(printf '%s' "$log" | grep -oE '^AFTER\|.*' | head -1)
    got=${got#AFTER|}
    # think's output carries trailing whitespace; compare the value, not the
    # padding.
    got=$(printf '%s' "$got" | tr -d ' \r\t')
    local verdict="ok"
    if [ "$got" != "$expect" ]; then
        verdict="FAIL ($note)"
        fails=$((fails+1))
    fi
    printf '%-22s %-12s %-12s %s\n' "$limit" "$expect" "${got:-<none>}" "$verdict"
}
poor_case 50             50    "in-range limit below the balance must reduce"
poor_case 0              0     "zero must zero everyone"
poor_case 2000           1000  "in-range limit above the balance is a no-op"
poor_case 2147483647     1000  "INT_MAX is above any balance: no-op"
poor_case 2147483648     1000  "2^31 truncated to INT_MIN and went negative"
poor_case 4294967296     1000  "2^32 truncated to 0 and zeroed every player"
poor_case 4294967297     1000  "2^32+1 truncated to 1"
poor_case 99999999999999 1000  "far out of range"

# ---------------------------------------------------------------
# hasquota(): both operands come from mux_atoi64, but only the request kept
# its width.  Narrowing the stored quota made the two sides disagree about
# their own range, so a player holding four billion quota was refused one
# object.  Needs a non-wizard subject: Free_Quota() is true for any Wizard,
# and God short-circuits the comparison entirely.
# ---------------------------------------------------------------
echo
echo "=== hasquota(): a large stored quota must still satisfy a small request ==="
printf '%-22s %-12s %-12s %s\n' 'RQUOTA' 'expected' 'got' 'verdict'
quota_case() {
    local rq="$1" expect="$2" note="$3"
    local log got
    log=$(run_game "quotas yes" "$(printf '%s\n' \
        "@pcreate pleb=xyzzy1" \
        "&RQUOTA *pleb=$rq" \
        "think HQ|[hasquota(*pleb,1)]" \
        "@shutdown")")
    got=$(printf '%s' "$log" | grep -oE '^HQ\|.*' | head -1)
    got=${got#HQ|}
    got=$(printf '%s' "$got" | tr -d ' \r\t')
    local verdict="ok"
    if [ "$got" != "$expect" ]; then
        verdict="FAIL ($note)"
        fails=$((fails+1))
    fi
    printf '%-22s %-12s %-12s %s\n' "$rq" "$expect" "${got:-<none>}" "$verdict"
}
quota_case 100        1  "an ordinary quota must be granted"
quota_case 0          0  "no quota left must be refused"
quota_case 2147483648 1  "2^31 truncated to INT_MIN and refused everything"
quota_case 4294967296 1  "2^32 truncated to 0 and refused a player holding 4e9"

rm -rf "$WORK"
echo
if [ "$fails" -ne 0 ]; then
    echo "=== tests/narrowing: FAILED ($fails case(s)) ==="
    echo "A 64-bit parse is being stored somewhere narrower, and the value was"
    echo "judged after the truncation rather than before it.  See #1402."
    exit 1
fi
echo "=== tests/narrowing: PASSED (12 cases) ==="
exit 0
