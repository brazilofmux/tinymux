#!/bin/bash
#
#   run.sh — the comsys command surface, compared command-for-command
#            across both implementations (#1640).
#
#   comsys ships two implementations, and two harnesses already compare them:
#   comsys_handoff tests state one writes and the other reads, comsys_mogrify
#   tests hook agreement during delivery.  Neither compares PLAIN COMMAND
#   OUTPUT, and that is where every divergence in #1640 lived -- @clist/full
#   answered with the default listing, @cwho dropped the dbref and flags,
#   comtitles never reached any message, delcom gave the wrong feedback.
#   Four user-visible differences that both existing harnesses scored green.
#
#   Shape: one command stream, run against TWO fresh databases, one with the
#   module configured and one without, with output diffed.  Divergence is any
#   difference at all, so this needs no assertion per case -- it cannot fail
#   to notice something nobody thought to assert, which is precisely how the
#   four in #1640 survived.
#
#   COLOR AND NON-ASCII ARE THE POINT, not decoration.  Three of the bugs this
#   file locks down are invisible to an ASCII stream: the engine conflated
#   byte offset with column offset in @clist/full, the module stored channel
#   headers with ANSI uncollapsed, and column truncation used printf field
#   widths that count codepoints rather than display columns (#1649).  Every
#   one of those reads as perfect parity if the fixtures are plain ASCII.  An
#   earlier version of this file was plain ASCII and passed while all three
#   were live.
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
    echo "SKIP: comsys module not built; nothing to compare against."
    exit 0
fi

npass=0; nfail=0
ok()   { npass=$((npass+1)); echo "ok $((npass+nfail)) - $1"; }
nope() { nfail=$((nfail+1)); echo "not ok $((npass+nfail)) - $1"; }

# --- Two independent game directories, identical starting state -------------
rm -rf "$WORK"
for side in engine module; do
    mkdir -p "$WORK/$side/data" "$WORK/$side/logs"
    cp "$REPO_ROOT/mux/game/alias.conf" "$REPO_ROOT/mux/game/compat.conf" \
       "$WORK/$side/"
    ln -s "$BIN" "$WORK/$side/bin" 2>/dev/null || cp -r "$BIN" "$WORK/$side/bin"
    ln -s "$REPO_ROOT/mux/game/text" "$WORK/$side/text" 2>/dev/null \
        || cp -r "$REPO_ROOT/mux/game/text" "$WORK/$side/text"
    cp "$REPO_ROOT/mux/game/data/netmux.db" "$WORK/$side/data/"

    extra=""
    if [ "$side" = module ]; then
        extra=$'module comsys_mod\nmodule mail_mod'
    fi
    cat > "$WORK/$side/probe.conf" <<EOF
input_database  data/netmux.db
output_database data/netmux.db.new
crash_database  data/netmux.db.CRASH
mail_database   data/mail.db
comsys_database data/comsys.db
port 6295
mud_name CmdParity
include alias.conf
include compat.conf
$extra
EOF
done

# Run $2 (a command stream) under both, leaving comparable output in
# $WORK/<side>/out.  Asserts which implementation answered: a silent fallback
# would make this file compare the built-in against itself and pass forever,
# which is the failure mode #1594 produced for the whole Windows suite.
run_both() {
    local stream="$1"
    local side want
    for side in engine module; do
        printf '%s\n' "$stream" > "$WORK/$side/in.txt"
        ( cd "$WORK/$side" || exit 1
          LD_LIBRARY_PATH="$BIN" $TIMEOUT ./bin/muxscript -g . -c probe.conf \
              < in.txt > out.raw 2>&1 )

        if [ "$side" = module ]; then
            want="Comsys: using module implementation."
        else
            want="Comsys: using built-in engine implementation."
        fi
        if ! grep -qF "$want" "$WORK/$side/out.raw"; then
            echo "Bail out!  wanted '$want' but the run reported:"
            grep -h "using .* implementation" "$WORK/$side/out.raw" \
                | sed 's/^/    /'
            exit 1
        fi

        # Drop the server's own log lines and startup chatter; what remains is
        # what a player would have seen.
        tr -d '\r' < "$WORK/$side/out.raw" \
          | grep -vE '^[0-9]{4}\.[0-9]{4}:[0-9]{6} ' \
          | grep -vE '^(muxscript:|Comsys: using|Mail: using|GAME: Shutdown)' \
          > "$WORK/$side/out"
    done
}

compare() {   # $1 = description
    if diff -q "$WORK/engine/out" "$WORK/module/out" >/dev/null 2>&1; then
        ok "$1"
    else
        nope "$1"
        diff -u "$WORK/engine/out" "$WORK/module/out" \
            | sed -n '3,40p' | sed 's/^/  /'
    fi
}

# ---------------------------------------------------------------------------
# 1. The listings and the join/leave surface, plain ASCII.
#
# @clist/full ignored the switch entirely and printed the default listing,
# losing Header/Access/Users/Msgs; @cwho printed a bare name where the engine
# prints unparse_object(); delcom emitted the channel broadcast instead of the
# leaver's own confirmation.
# ---------------------------------------------------------------------------
run_both '@ccreate dchan
@cset/header dchan=HDR
@cset/public dchan
addcom d=dchan
@cemit dchan=hello
@clist/full
@clist/headers
@clist
@cwho dchan
comtitle d=Tester
allcom off
allcom on
delcom d
@shutdown'
compare "listings, join/leave and delcom agree (ASCII)"

# ---------------------------------------------------------------------------
# 2. Comtitles in every message shape.
#
# The module stored comtitles and round-tripped them through SyncChannelUser,
# then never applied them to anything: speech, pose, semipose, join, leave.
# On a spoof channel the comtitle is supposed to REPLACE the speaker's name.
# ---------------------------------------------------------------------------
run_both '@ccreate tchan
@cset/header tchan=HDR
@cset/public tchan
addcom t=tchan
comtitle t=Tester
t hello there
t :waves
t ;grins
comtitle/off t
t after title off
comtitle/on t
@cset/spoof tchan
comtitle t=Ghost
t spoofed line
@shutdown'
compare "comtitle applies to say, pose, semipose and spoof"

# ---------------------------------------------------------------------------
# 3. Color and non-ASCII.  The cases the other two shapes cannot see.
#
# A colored header makes byte offset and column offset differ, which is what
# exposed the engine's PadField conflation in @clist/full; it is also what
# exposed the module storing headers with ANSI uncollapsed.  A CJK channel
# name makes codepoint count and column count differ, which is what separates
# a printf field width from StripTabsAndTruncate.
# ---------------------------------------------------------------------------
run_both '@ccreate wide
@cset/header wide=[ansi(r,RED)][ansi(b,BLU)]
@cset/public wide
addcom w=wide
@ccreate 日本語
@cset/header 日本語=[ansi(g,GRN)]
@cset/public 日本語
addcom j=日本語
@clist/full
@clist/headers
@clist
@cwho wide
comtitle w=[ansi(y,YEL)]
w hello
@shutdown'
compare "colored headers and CJK channel names align identically"

echo "=== comsys cmdparity: $npass passed, $nfail failed ==="
[ "$nfail" -eq 0 ] || exit 1
exit 0
