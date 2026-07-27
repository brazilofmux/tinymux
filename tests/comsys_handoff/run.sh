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
# Seed under the ENGINE: a logging channel with one message in the ring.
# ---------------------------------------------------------------------------
run_as engine '@ccreate hchan
@create HObj
@cset/object hchan=HObj
@cset/log hchan=10
@cset/timestamp_logs hchan=1
@cemit hchan=EngineWrote
think TS=[get(HObj/LOG_TIMESTAMPS)]'

[ "$(val TS)" = "1" ] \
    && ok "engine enables timestamp logging" \
    || nope "engine enables timestamp logging" "TS=$(val TS)"

# ---------------------------------------------------------------------------
# The module must READ what the engine wrote.  This is the direction #1564
# broke: the module kept its own counter and never wrote the history the
# engine's reader looked for.
# ---------------------------------------------------------------------------
run_as module 'think TS=[get(HObj/LOG_TIMESTAMPS)]
think H1=[get(HObj/HISTORY_1)]'

[ "$(val TS)" = "1" ] \
    && ok "module reads the flag the engine set" \
    || nope "module reads the flag the engine set" "TS=$(val TS)"

case "$(val H1)" in
    *EngineWrote*) ok "module reads history the engine wrote" ;;
    *) nope "module reads history the engine wrote" "H1=$(val H1)" ;;
esac

# ---------------------------------------------------------------------------
# #1585 -- the module must be able to turn the flag back off.
#
# It cannot: the engine wrote the attribute as GOD with AF_CONST, and
# bCanSetAttr denies AF_CONST in every branch, God included.  Since #1624 the
# module at least says so instead of reporting success.
# ---------------------------------------------------------------------------
run_as module '@cset/timestamp_logs hchan=0
think TS=[get(HObj/LOG_TIMESTAMPS)]'

[ -z "$(val TS)" ] \
    && ok "module can clear a flag the engine set (#1585)" \
    || nope "module can clear a flag the engine set (#1585)" \
            "TS=$(val TS) -- still set after the module cleared it"

# The clear must be reported as a success now, since it IS one.  This case
# replaces the #1624 assertion that a REFUSAL was reported honestly: there is
# no refusal left to report, and asserting on the old failure text would fail
# for the right reason in the wrong direction.
grep -q "@cset: Channel .* timestamp logging set" "$WORK/out" \
    && ok "the clear reports success, because it now is one" \
    || nope "the clear reports success, because it now is one" \
            "no success message in the output"

# ---------------------------------------------------------------------------
# #1620 -- the module must be able to overwrite a history slot the engine
# wrote, once the ring wraps onto it.  MAX_LOG is 10 and the engine's message
# landed in slot 1, so message 11 targets it.
# ---------------------------------------------------------------------------
run_as module '@cemit hchan=Mod2
@cemit hchan=Mod3
@cemit hchan=Mod4
@cemit hchan=Mod5
@cemit hchan=Mod6
@cemit hchan=Mod7
@cemit hchan=Mod8
@cemit hchan=Mod9
@cemit hchan=Mod10
@cemit hchan=Mod11
think H1=[get(HObj/HISTORY_1)]
think H2=[get(HObj/HISTORY_2)]'

case "$(val H1)" in
    *EngineWrote*) nope "module overwrites an engine-written history slot (#1620)" \
                        "slot still holds the engine's message; the wrap write was refused" ;;
    *) ok "module overwrites an engine-written history slot (#1620)" ;;
esac

# Control: a slot the module owns is writable, so the case above is about
# provenance and not about the module having lost the ability to write.
case "$(val H2)" in
    *Mod*) ok "module rewrites a slot it owns (control)" ;;
    *) nope "module rewrites a slot it owns (control)" "H2=$(val H2)" ;;
esac

# ---------------------------------------------------------------------------
# And back the other way: the engine must read what the module wrote.
# ---------------------------------------------------------------------------
run_as engine 'think H3=[get(HObj/HISTORY_3)]'

case "$(val H3)" in
    *Mod*) ok "engine reads history the module wrote" ;;
    *) nope "engine reads history the module wrote" "H3=$(val H3)" ;;
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
