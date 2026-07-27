#!/bin/bash
#
#   run.sh — comsys/mail state written by one implementation, read by the
#            other (#1589 stage 0b).
#
#   comsys and mail each ship two implementations: the engine's built-in
#   (mux/modules/engine/comsys.cpp, mail.cpp) and a loadable module
#   (mux/modules/comsys/comsys_mod.cpp, mail/mail_mod.cpp).  Every bug this
#   lane has produced -- #1564, #1585, #1620 -- exists only when the two
#   disagree about state one of them wrote.
#
#   Nothing could see that.  #1618 made a run say which implementation
#   answered, and test-smoke-builtin made the other side reachable, but the
#   smoke corpus scores 1561/1561 against BOTH: it cannot tell them apart.
#   A divergence needs one implementation to write and the other to read,
#   and no single run can produce that.  So this driver runs muxscript
#   repeatedly against ONE persistent database, changing only whether
#   `module comsys_mod` is in the config, and asserts across the handoff.
#
#   Every run asserts which implementation it actually got, and aborts if it
#   is not the one intended.  Without that the whole file would quietly
#   become built-in-vs-built-in -- which is exactly how #1585 came to be
#   marked "cannot reproduce" from a Windows box where the module never
#   loads at all.
#
#   TODO cases
#
#   Two assertions below are known-failing against current master and are
#   marked TODO.  A TODO failure does not fail the run; a TODO that PASSES
#   does, loudly, because that means the bug was fixed and the marker is now
#   lying.  That is the property worth having here: when stage 1 moves
#   channel history into SQL, this file tells us so rather than staying
#   silently green.
#
set -u

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
BIN="$REPO_ROOT/mux/game/bin"
WORK="$SCRIPT_DIR/work"

if command -v timeout >/dev/null 2>&1; then
    TIMEOUT="timeout 120"
elif command -v gtimeout >/dev/null 2>&1; then
    TIMEOUT="gtimeout 120"
else
    TIMEOUT=""
fi

if [ ! -x "$BIN/muxscript" ]; then
    echo "SKIP: $BIN/muxscript not found (run 'make install' first)."
    exit 0
fi
if [ ! -r "$BIN/comsys_mod.so" ] && [ ! -r "$BIN/comsys_mod.dll" ]; then
    echo "SKIP: comsys module not built; nothing to hand off to."
    exit 0
fi

npass=0; nfail=0; ntodo=0; nbonus=0

ok()   { npass=$((npass+1)); echo "ok $((npass+nfail+ntodo+nbonus)) - $1"; }
nope() { nfail=$((nfail+1)); echo "not ok $((npass+nfail+ntodo+nbonus)) - $1"
         [ -n "${2:-}" ] && echo "  $2"; return 0; }
# $1 = description, $2 = issue, $3 = ok/fail, $4 = detail
todo() {
    if [ "$3" = "pass" ]; then
        nbonus=$((nbonus+1))
        echo "not ok $((npass+nfail+ntodo+nbonus)) - $1 [TODO $2] UNEXPECTEDLY PASSES"
        echo "  $2 appears to be fixed.  Remove the TODO marker and let this"
        echo "  assert normally -- a stale marker hides the next regression."
    else
        ntodo=$((ntodo+1))
        echo "ok $((npass+nfail+ntodo+nbonus)) - # TODO $1 (known: $2)"
        [ -n "${4:-}" ] && echo "  $4"
    fi
}

rm -rf "$WORK"; mkdir -p "$WORK/data" "$WORK/logs"
cp "$REPO_ROOT/mux/game/alias.conf" "$REPO_ROOT/mux/game/compat.conf" "$WORK/"
ln -s "$BIN" "$WORK/bin"
ln -s "$REPO_ROOT/mux/game/text" "$WORK/text"
cp "$REPO_ROOT/mux/game/data/netmux.db" "$WORK/data/"

write_conf() {   # $1 = name, $2 = extra directives
    cat > "$WORK/$1.conf" <<EOF
input_database  data/netmux.db
output_database data/netmux.db.new
crash_database  data/netmux.db.CRASH
mail_database   data/mail.db
comsys_database data/comsys.db
port 6294
mud_name Handoff
include alias.conf
include compat.conf
$2
EOF
}
write_conf engine ""
write_conf module "module comsys_mod"

# Run a command stream under $1 (engine|module) and leave output in $WORK/out.
# Aborts the whole file if the implementation is not the one requested: a
# silent fallback would turn every assertion below into a tautology.
run_as() {
    local which="$1" stream="$2"
    printf '%s\n' "$stream" > "$WORK/in.txt"
    ( cd "$WORK" || exit 1
      LD_LIBRARY_PATH="$BIN" $TIMEOUT "$BIN/muxscript" -g . -c "$which.conf" \
          < in.txt > out 2>&1 )
    local want
    if [ "$which" = "module" ]; then want="Comsys: using module implementation."
    else want="Comsys: using built-in engine implementation."; fi
    if ! grep -qF "$want" "$WORK/out"; then
        echo "Bail out!  wanted '$want' but the run reported:"
        grep -h "using .* implementation" "$WORK/out" | sed 's/^/    /'
        echo "  Every assertion in this file compares implementations, so a"
        echo "  fallback makes the results meaningless rather than merely"
        echo "  wrong.  Refusing to continue."
        exit 1
    fi
}

# muxscript emits CRLF, so strip the CR -- otherwise every comparison here
# tests against "1\r" and fails while printing something that looks correct.
val() { sed -n "s/^$1=//p" "$WORK/out" | head -1 | tr -d '\r'; }

# ---------------------------------------------------------------------------
# Seed under the ENGINE.  Membership persists in the database, so addcom here
# is what lets every later run call crecall(), which requires it.
# ---------------------------------------------------------------------------
run_as engine '@ccreate hchan
@create HObj
@cset/object hchan=HObj
@cset/log hchan=10
@cset/timestamp_logs hchan=1
addcom h=hchan
@cemit hchan=EngineWrote
think BUF=[cbuffer(hchan)]
think REC=[crecall(hchan,1,|)]'

# Asserted through recall output rather than get(HObj/LOG_TIMESTAMPS): the
# flag is a column now (stage 2) and the attribute, where it still exists at
# all, is inert.  Reading it would test storage that no longer decides
# anything -- and would keep passing after the flag stopped mattering.
case "$(val REC)" in
    \[*\]*EngineWrote*) ok "engine enables timestamp logging" ;;
    *) nope "engine enables timestamp logging" "REC=$(val REC)" ;;
esac

[ "$(val BUF)" = "10" ] \
    && ok "engine records retention on the channel row" \
    || nope "engine records retention on the channel row" "BUF=$(val BUF)"

# ---------------------------------------------------------------------------
# The module must READ what the engine wrote -- through crecall(), which is
# the supported interface.  Asserting on get(<chanobj>/HISTORY_n) is what this
# file used to do, and that was only ever right while history WAS attributes.
# ---------------------------------------------------------------------------
run_as module 'think BUF=[cbuffer(hchan)]
think REC=[crecall(hchan,5,|)]'

[ "$(val BUF)" = "10" ] \
    && ok "module reads the retention the engine set" \
    || nope "module reads the retention the engine set" "BUF=$(val BUF)"

case "$(val REC)" in
    *EngineWrote*) ok "module reads history the engine wrote" ;;
    *) nope "module reads history the engine wrote" "REC=$(val REC)" ;;
esac

# ---------------------------------------------------------------------------
# The module writes, and the ENGINE must see it.  This is the direction #1564
# broke: the module kept a counter and never wrote what the engine's reader
# looked for.
# ---------------------------------------------------------------------------
run_as module '@cemit hchan=ModuleWrote'
run_as engine 'think REC=[crecall(hchan,5,|)]
think COUNT=[cmsgs(hchan)]'

case "$(val REC)" in
    *ModuleWrote*) ok "engine reads history the module wrote" ;;
    *) nope "engine reads history the module wrote" "REC=$(val REC)" ;;
esac

# Both messages, in order, from one store.  Under the ring these came from
# two places that had no way to agree.
case "$(val REC)" in
    *EngineWrote*ModuleWrote*) ok "both implementations' messages, in order" ;;
    *) nope "both implementations' messages, in order" "REC=$(val REC)" ;;
esac

# ---------------------------------------------------------------------------
# #1620 -- the module writing after the engine.  Under the ring, a slot the
# engine had written carried AF_CONST and refused the module's write forever
# once the ring wrapped onto it.  There is no attribute to freeze now, so this
# asserts normally rather than carrying a TODO.
# ---------------------------------------------------------------------------
run_as module '@cemit hchan=Wrap2
@cemit hchan=Wrap3
@cemit hchan=Wrap4
@cemit hchan=Wrap5
@cemit hchan=Wrap6
@cemit hchan=Wrap7
@cemit hchan=Wrap8
@cemit hchan=Wrap9
@cemit hchan=Wrap10
@cemit hchan=Wrap11
think REC=[crecall(hchan,20,|)]
think COUNT=[cmsgs(hchan)]'

case "$(val REC)" in
    *Wrap11*) ok "module keeps writing past the point the ring would wrap" ;;
    *) nope "module keeps writing past the point the ring would wrap" \
            "REC=$(val REC)" ;;
esac

# Retention is 10, so the oldest entries must have gone -- and cmsgs() is the
# retained count now, not a monotonic total, so it must agree with it.
case "$(val REC)" in
    *EngineWrote*) nope "retention expires the oldest entries" \
                        "EngineWrote survived a 10-entry limit" ;;
    *) ok "retention expires the oldest entries" ;;
esac

[ "$(val COUNT)" = "10" ] \
    && ok "cmsgs() reports the retained count, matching retention" \
    || nope "cmsgs() reports the retained count, matching retention" \
            "COUNT=$(val COUNT), expected 10"

# ---------------------------------------------------------------------------
# #1585 -- the module clearing a flag the ENGINE set.
#
# This was the last TODO in this file.  As an attribute the engine wrote it as
# GOD with AF_CONST, bCanSetAttr denies AF_CONST in every branch including
# God's, and the module's permission-checked write was refused -- so the flag
# could not be turned off from the side that had not set it.  It is a column
# now and there are no permission bits to refuse with.
#
# Asserted through BEHAVIOUR rather than through get(HObj/LOG_TIMESTAMPS):
# the attribute still exists on upgraded games but is inert, so reading it
# would test the wrong thing and would keep passing after the flag stopped
# meaning anything.  What matters is whether recall renders a timestamp.
# ---------------------------------------------------------------------------
run_as module '@cemit hchan=TimestampProbe
think REC=[crecall(hchan,1,|)]'

case "$(val REC)" in
    \[*\]*TimestampProbe*) ok "timestamps the engine enabled apply under the module" ;;
    *) nope "timestamps the engine enabled apply under the module" \
            "REC=$(val REC)" ;;
esac

run_as module '@cset/timestamp_logs hchan=0
@cemit hchan=AfterClear
think REC=[crecall(hchan,1,|)]'

case "$(val REC)" in
    \[*\]*) nope "module can clear a flag the engine set (#1585)" \
                  "still timestamped: REC=$(val REC)" ;;
    *AfterClear*) ok "module can clear a flag the engine set (#1585)" ;;
    *) nope "module can clear a flag the engine set (#1585)" "REC=$(val REC)" ;;
esac

# And the ENGINE must see the module's change -- the direction that was
# impossible before, and the whole point of moving config off attributes.
run_as engine 'think REC=[crecall(hchan,1,|)]'

case "$(val REC)" in
    \[*\]*) nope "engine sees the flag the module cleared" \
                  "still timestamped: REC=$(val REC)" ;;
    *) ok "engine sees the flag the module cleared" ;;
esac

echo "=== comsys handoff: $npass passed, $nfail failed, $ntodo known-failing"\
     "($nbonus unexpectedly passing) ==="

if [ "$nbonus" -gt 0 ]; then
    echo "A TODO case started passing.  That is good news and a required edit:"
    echo "remove its marker so it guards the fix from now on."
    exit 1
fi
[ "$nfail" -eq 0 ] || exit 1
rm -rf "$WORK"
exit 0
