#!/bin/bash
#
#   run.sh — the same commands under both comsys/mail implementations, with
#            every difference in the output accounted for (#1614 step 4).
#
#   THE GAP THIS FILLS
#
#   Two conformance harnesses already exist and neither compares plain command
#   output:
#
#     tests/comsys_handoff   state written under one implementation, read under
#                            the other.  Finds bugs where the two disagree
#                            about STORED state (#1564, #1585, #1587, #1620).
#     tests/comsys_mogrify   hooks and CHATFORMAT during delivery, each side
#                            joining and speaking within one process (#1572).
#
#   Neither runs `@clist/full` and looks at what came back.  So the module
#   ignoring that switch entirely, dropping comtitle from join/leave, printing
#   a bare name from @cwho, and answering @mail/stats, /dstats and /fstats with
#   one identical line, all sat in master unreported until this file was
#   written -- see #1640 and #1631.  Two engine-side defects fell out of the
#   same run (#1637, #1639).
#
#   HOW IT WORKS, AND WHY IT IS A DIFF AND NOT ASSERTIONS
#
#   One command stream, two runs against identical fresh databases, differing
#   only in whether the module directives are present.  The outputs are
#   normalised and diffed, and the diff is compared against a checked-in
#   baseline of divergences we already know about.
#
#   Asserting per-behaviour would have been the obvious shape and is the wrong
#   one here: it can only catch divergences someone already thought to write a
#   case for, which is precisely how four of them survived.  A whole-output
#   diff catches the ones nobody predicted, which is the entire point.
#
#   Consequences, both intended:
#
#     * A NEW divergence fails the run, even in a command nobody was thinking
#       about.
#     * A KNOWN divergence disappearing also fails the run, loudly, because the
#       baseline is now lying.  Regenerate it (see below) as part of the fix.
#
#   Any wording change to a message either implementation prints will also
#   churn the baseline.  That is a feature at this stage of #1614: both
#   implementations are being actively changed, and a look at the diff is
#   exactly what should happen.
#
#   REGENERATING THE BASELINE
#
#       tests/comsys_conformance/run.sh --bless
#
#   Read the delta before blessing.  A baseline regenerated without reading it
#   converts this file from a guard into a record of whatever master happened
#   to do that day.
#
set -u

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
BIN="$REPO_ROOT/mux/game/bin"
WORK="$SCRIPT_DIR/work"
BASELINE="$SCRIPT_DIR/known-divergences.diff"

BLESS=0
[ "${1:-}" = "--bless" ] && BLESS=1

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
if [ ! -r "$BIN/mail_mod.so" ] && [ ! -r "$BIN/mail_mod.dll" ]; then
    echo "SKIP: mail module not built; nothing to compare against."
    exit 0
fi

rm -rf "$WORK"; mkdir -p "$WORK/data" "$WORK/logs"
cp "$REPO_ROOT/mux/game/alias.conf" "$REPO_ROOT/mux/game/compat.conf" "$WORK/"
ln -s "$BIN" "$WORK/bin"
ln -s "$REPO_ROOT/mux/game/text" "$WORK/text"

for n in engine module; do
    cat > "$WORK/$n.conf" <<EOF
input_database  data/netmux.db
output_database data/netmux.db.new
crash_database  data/netmux.db.CRASH
mail_database   data/mail.db
comsys_database data/comsys.db
port 6297
mud_name Conform
include alias.conf
include compat.conf
EOF
done
printf 'module comsys_mod\nmodule mail_mod\n' >> "$WORK/module.conf"

# ---------------------------------------------------------------------------
# The stream.  Every line here is an entry point on mux_IComsysControl or
# mux_IMailControl -- the ABI is the definitive list of where the two can
# diverge, so the stream is organised by it rather than by feature.
#
# Keep it deterministic: no wall clock, no randomness, no dbref allocation
# that depends on earlier output.  Timestamps that do leak through are
# normalised below; anything else that varies run to run belongs out of here,
# not in the normaliser.
# ---------------------------------------------------------------------------
cat > "$WORK/in.txt" <<'EOF'
@ccreate cchan
@create CObj
@cset/object cchan=CObj
@cset/header cchan=[HDR]
addcom c=cchan
comtitle c=Tester
@clist
@clist/full
@clist/headers
@cwho cchan
comlist
@editchannel/charge cchan=5
@cset/public cchan
@cset/private cchan
@cset/loud cchan
@cset/quiet cchan
@cset/spoof cchan
@cset/nospoof cchan
@cset/log cchan=10
@cemit cchan=Broadcast one
@cemit/noheader cchan=Broadcast two
c Spoken one
allcom off
allcom on
@mail Wizard=Subject One
-Body of the first message.
@mail/send
@mail/list
@mail 1
@mail/stats
@mail/dstats
@mail/fstats
@mail/tag 1
@mail/untag 1
@mail/urgent 1
@mail/safe 1
@mail/unsafe 1
@mail/clear 1
@mail/unclear 1
@malias/list
@malias mytest=Wizard
@malias/list
@malias/status
@folder
@folder/list
delcom c
@cdestroy cchan
EOF

# ---------------------------------------------------------------------------
# Run and normalise.
#
# Dropped: SQLite migration chatter, the implementation banner (it is asserted
# separately and differs by construction), and muxscript's own load line.
#
# Rewritten: wall-clock dates and the leading timestamp on log lines, which
# are the only nondeterminism the stream produces.
#
# Log lines are otherwise KEPT.  Until #1633/#1644 muxscript discarded every
# one of them, so this file's first baseline had no diagnostics in it at all;
# now that they reach the output, whether the two implementations report the
# same things is itself a conformance property -- the module announces its
# shutdown and the engine does not, which shows up here as a divergence rather
# than as nothing.
#
# Trailing whitespace is NOT stripped -- the engine pads several listings to a
# fixed column width and the module does not, which is a real user-visible
# difference and belongs in the diff.  \r is stripped because it is a
# transport artefact, not output.
# ---------------------------------------------------------------------------
normalise() {
    grep -v -e '^CSQLiteDB' -e 'MigrateSchema' -e 'using .* implementation' \
            -e '^muxscript:' "$1" \
    | tr -d '\r' \
    | sed -E \
        -e 's/(Mon|Tue|Wed|Thu|Fri|Sat|Sun) (Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec) [ 0-9][0-9] [0-9]{2}:[0-9]{2}(:[0-9]{2})? [0-9]{4}/<DATE>/g' \
        -e 's/^[0-9]{4}\.[0-9]{4}:[0-9]{6} /<TS> /'
}

run_one() {   # $1 = engine|module
    rm -rf "$WORK/data"; mkdir -p "$WORK/data"
    cp "$REPO_ROOT/mux/game/data/netmux.db" "$WORK/data/"
    ( cd "$WORK" || exit 1
      LD_LIBRARY_PATH="$BIN" $TIMEOUT "$BIN/muxscript" -g . -c "$1.conf" \
          < in.txt > "raw.$1" 2>&1 )

    # Assert the run got the implementation it asked for, BOTH subsystems.
    # Without this the whole file silently becomes engine-versus-engine, which
    # is how #1585 came to be marked "cannot reproduce" from a box where the
    # module never loads (#1594).
    local wantc wantm
    if [ "$1" = "module" ]; then
        wantc="Comsys: using module implementation."
        wantm="Mail: using module implementation."
    else
        wantc="Comsys: using built-in engine implementation."
        wantm="Mail: using built-in engine implementation."
    fi
    if ! grep -qF "$wantc" "$WORK/raw.$1" || ! grep -qF "$wantm" "$WORK/raw.$1"; then
        echo "Bail out!  '$1' run did not get the implementation it asked for."
        grep -h "using .* implementation" "$WORK/raw.$1" | sed 's/^/    /'
        echo "    (wanted: $wantc / $wantm)"
        exit 1
    fi
    normalise "$WORK/raw.$1" > "$WORK/norm.$1"
}

echo "=== tests/comsys_conformance — engine vs module, whole-output diff ==="
echo

run_one engine
run_one module

diff -u "$WORK/norm.engine" "$WORK/norm.module" \
    | sed -e '1s|.*|--- engine|' -e '2s|.*|+++ module|' > "$WORK/current.diff"

if [ "$BLESS" -eq 1 ]; then
    cp "$WORK/current.diff" "$BASELINE"
    echo "Baseline written to $BASELINE"
    echo "$(grep -c '^[-+][^-+]' "$BASELINE") divergent line(s) recorded."
    echo
    echo "Read it before committing.  A baseline blessed without reading is a"
    echo "record of what master did today, not a guard."
    rm -rf "$WORK"
    exit 0
fi

if [ ! -r "$BASELINE" ]; then
    echo "no baseline at $BASELINE -- run with --bless to create one."
    exit 1
fi

# Compare CR-blind.  On a Windows checkout with core.autocrlf=true the
# baseline arrives with CRLF while current.diff is generated with LF, and
# the byte comparison failed with a delta that was line-for-line identical
# on screen (#1641).  .gitattributes now pins the baseline to LF, but that
# only helps checkouts made after the pin; this keeps existing clones
# working either way.
#
tr -d '\r' < "$BASELINE" > "$WORK/baseline.lf"
BASELINE="$WORK/baseline.lf"

if diff -q "$BASELINE" "$WORK/current.diff" >/dev/null 2>&1; then
    n=$(grep -c '^[-+][^-+]' "$BASELINE" || true)
    echo "=== comsys conformance: PASSED ==="
    echo "    Output matches the recorded baseline: $n known-divergent line(s),"
    echo "    tracked in #1637 (and residual debug/lifecycle labels).  No new divergence."
    rm -rf "$WORK"
    exit 0
fi

echo "=== comsys conformance: FAILED ==="
echo
echo "The engine/module difference is not the one recorded in the baseline."
echo "Both directions matter:"
echo
echo "  a divergence APPEARED  -> the two implementations just drifted apart"
echo "                            somewhere new.  That is a bug, in whichever"
echo "                            side changed."
echo "  a divergence VANISHED  -> something was fixed.  Good; the baseline is"
echo "                            now lying and must be regenerated as part of"
echo "                            that fix, or it hides the next regression."
echo
echo "--- baseline vs this run (lines starting - are baseline, + are now) ---"
diff -u "$BASELINE" "$WORK/current.diff" | tail -n +3 | head -80
echo
echo "Full output kept at $WORK/norm.engine and $WORK/norm.module."
echo "After reading the delta:  tests/comsys_conformance/run.sh --bless"
exit 1
