#!/bin/bash
#
#   run.sh — runtime oracle for the xx pseudo-locale (#1523).
#
#   make test-nls (#1520) checks the catalogue and the markings statically, and
#   that is where the risk that has actually bitten lives.  What it cannot do is
#   show that translation happens at all: nothing in the suite observes notify()
#   prose, so the whole translatable surface is invisible to it.
#
#   The trap this exists to close is that the obvious experiment is vacuous.
#   Running the smoke suite with LANGUAGE=xx is green whether the catalogue is
#   correct, corrupt, or absent -- measured on #1523, identical numbers with the
#   catalogue in a directory the server never opens.  So a green LANGUAGE=xx run
#   is not evidence of anything unless something asserts that translation
#   *changed the output*, and that a run without the catalogue does not.
#
#   Both directions are checked here.  The final case is the load-bearing one:
#   with the catalogue moved aside, the [xx] prefixes must vanish.  Without it,
#   this script would pass on a build where gettext was never wired up.
#
#   Where the catalogue has to live: mux_nls_init() binds the domain against the
#   relative path "locale" (engine_com.cpp), and muxscript chdir()s to its -g
#   directory, so "locale" resolves under the game directory.  That is
#   consistent -- not a bug -- but it does mean a harness must put the catalogue
#   in the game directory it actually runs from.  testcases/ has no locale/,
#   which is why the smoke route could not have loaded one even if it looked.
#
#   Not a smoke case, for a reason that is not about convenience: the assertion
#   needs two runs of the same commands under different environments, and
#   Makesmoke/Smoke build one database and one config per invocation.  Nothing
#   in tr.tc* can vary LANGUAGE.
#
set -u

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
BIN="$REPO_ROOT/mux/game/bin"
WORK="$SCRIPT_DIR/work"
PO="$REPO_ROOT/mux/po/xx.po"

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
if ! grep -q '^#define HAVE_NLS' "$REPO_ROOT/mux/include/autoconf.h" 2>/dev/null; then
    echo "SKIP: this tree is not configured with NLS (configure --enable-nls)."
    exit 0
fi
if ! command -v msgfmt >/dev/null 2>&1; then
    echo "SKIP: msgfmt not found (install gettext) -- cannot build the catalogue."
    exit 0
fi
if [ ! -r "$PO" ]; then
    echo "SKIP: $PO not found."
    exit 0
fi

# gettext ignores LANGUAGE when the process locale is C/POSIX.  Prefer a
# real UTF-8 locale; if the host has none, try a user-localedef under
# $HOME/.locale (same recipe as mux/po/README.md).  Without that, SKIP
# rather than fail on a packaging gap.
#
# gettext ignores LANGUAGE when the process locale is C/POSIX.  Prefer a
# real non-C UTF-8 locale.  `locale` exits 0 even when LC_ALL is invalid
# (it falls back and prints warnings on stderr), so do not probe with a
# bare `locale` call -- use locale -a, then a user-localedef under
# $HOME/.locale if needed (mux/po/README.md).
#
NLS_LOCALE=""
NLS_LOCPATH=""
if command -v locale >/dev/null 2>&1; then
    # System-installed names vary: en_US.utf8 vs en_US.UTF-8.
    for cand in $(locale -a 2>/dev/null | grep -E 'en_US\.(utf8|UTF-8)$' || true); do
        NLS_LOCALE="$cand"
        break
    done
fi
if [ -z "$NLS_LOCALE" ]; then
    USER_LOCALE_DIR="${HOME}/.locale"
    USER_LOCALE="en_US.UTF-8"
    if [ ! -d "$USER_LOCALE_DIR/$USER_LOCALE" ]; then
        if command -v localedef >/dev/null 2>&1; then
            mkdir -p "$USER_LOCALE_DIR"
            localedef -f UTF-8 -i en_US "$USER_LOCALE_DIR/$USER_LOCALE" 2>/dev/null || true
        fi
    fi
    if [ -d "$USER_LOCALE_DIR/$USER_LOCALE" ]; then
        if LOCPATH="$USER_LOCALE_DIR" LC_ALL="$USER_LOCALE" locale charmap 2>/dev/null | grep -qi utf; then
            # Confirm setlocale did not fall back (no "Cannot set" on stderr).
            if ! LOCPATH="$USER_LOCALE_DIR" LC_ALL="$USER_LOCALE" locale 2>&1 | grep -q 'Cannot set'; then
                NLS_LOCALE="$USER_LOCALE"
                NLS_LOCPATH="$USER_LOCALE_DIR"
            fi
        fi
    fi
fi
if [ -z "$NLS_LOCALE" ]; then
    echo "SKIP: no non-C UTF-8 locale available (install en_US.UTF-8, or"
    echo "      localedef -f UTF-8 -i en_US \$HOME/.locale/en_US.UTF-8)."
    exit 0
fi

# The three messages below are M_()-marked and reachable as God with no
# fixture: @moniker set/clear, and an unknown command.  "Huh?" also carries
# typographic quotes, so it exercises the UTF-8 catalogue path rather than a
# pure-ASCII one.
#
# The softcode token is the control that matters most.  #-1 tokens are S_(),
# deliberately outside the catalogue (#1480), and `grep -c '^msgid ".*#-1'`
# over xx.po is 0 -- so if one ever came back translated, the S_()/M_() split
# has broken somewhere upstream of this test.
CMDS='@moniker me=Probe
@moniker me=
zzzznotacommand
think TOKEN|[zzzznotafunction(1)]
@shutdown'

# $1 = LANGUAGE value, $2 = "with-catalogue" | "without-catalogue".
# Echoes "<xx-count>|<token-line>".
run_case() {
    local langset="$1" mode="$2"
    rm -rf "$WORK"; mkdir -p "$WORK/data" "$WORK/logs" "$WORK/text"
    ( cd "$WORK" || exit 1
      ln -s "$BIN" bin
      cp "$REPO_ROOT/mux/game/alias.conf" "$REPO_ROOT/mux/game/compat.conf" . 2>/dev/null
      if [ "$mode" = "with-catalogue" ]; then
          mkdir -p locale/xx/LC_MESSAGES
          msgfmt -o locale/xx/LC_MESSAGES/tinymux.mo "$PO" 2>/dev/null
      fi
      cat > p.conf <<EOF
input_database  data/p.db
output_database data/p.db.new
crash_database  data/p.db.CRASH
mail_database   data/mail.db
comsys_database data/comsys.db
port 2901
mud_name NlsOracle
command_quota_increment 200000
command_quota_max 200000
include alias.conf
include compat.conf
EOF
      printf '%s\n' "$CMDS" > in.txt
      # LC_ALL must name a real locale or LANGUAGE is ignored by gettext.
      LOCPATH="${NLS_LOCPATH:-${LOCPATH-}}" LC_ALL="$NLS_LOCALE" LANGUAGE="$langset" LD_LIBRARY_PATH="$BIN" \
        $TIMEOUT "$BIN/muxscript" -g . -c p.conf < in.txt > out.log 2>&1
    )
    local n tok
    # No `|| echo 0` here: grep -c already prints 0 when it matches nothing,
    # and it also exits 1, so the fallback fired *as well* and produced two
    # lines -- which truncated the read below and blanked the token.
    n=$(grep -c '\[xx\]' "$WORK/out.log" 2>/dev/null)
    [ -n "$n" ] || n=0
    tok=$(grep -oE '^TOKEN\|.*' "$WORK/out.log" 2>/dev/null | head -1)
    tok=${tok#TOKEN|}
    tok=$(printf '%s' "$tok" | tr -d '\r')
    echo "$n|$tok"
}

fails=0
EXPECT_TOKEN='#-1 FUNCTION (ZZZZNOTAFUNCTION) NOT FOUND'

check() {
    local label="$1" got_n="$2" want_n="$3" got_tok="$4" note="$5"
    local verdict="ok"
    if [ "$got_n" != "$want_n" ]; then
        verdict="FAIL ($note)"
        fails=$((fails+1))
    elif [ "$got_tok" != "$EXPECT_TOKEN" ]; then
        verdict="FAIL (softcode token changed: '$got_tok')"
        fails=$((fails+1))
    fi
    printf '%-34s %-10s %-10s %s\n' "$label" "$want_n" "$got_n" "$verdict"
}

printf '%-34s %-10s %-10s %s\n' 'case' 'want[xx]' 'got[xx]' 'verdict'

# 1. Default locale: the marked prose must come back verbatim.  If this shows
#    [xx] the build is translating when it was not asked to.
IFS='|' read -r n tok <<EOF
$(run_case "" with-catalogue)
EOF
check "default locale, catalogue present" "$n" 0 "$tok" \
      "translated without LANGUAGE=xx being set"

# 2. LANGUAGE=xx: the same marked messages must come back [xx]-prefixed.
#    This is the only case that demonstrates the feature works.
#
#    The count is four, not three, since #1700 marked the shutdown
#    broadcast -- CMDS ends in @shutdown, so "GAME: Shutdown by %s" is in
#    the stream too.  It moves whenever a marking lands on prose this
#    command sequence happens to emit, which is a stale-expectation failure
#    rather than a real one: check WHICH lines carry [xx] before changing
#    the number, because a drop is the failure this case exists to catch.
IFS='|' read -r n tok <<EOF
$(run_case "xx" with-catalogue)
EOF
check "LANGUAGE=xx, catalogue present" "$n" 4 "$tok" \
      "marked prose did not translate -- catalogue not found, or M_() not wired"

# 3. LANGUAGE=xx with the catalogue absent: the prefixes must disappear.
#    This is what makes case 2 mean something.  If this also reported 4, the
#    prefixes were coming from somewhere other than the catalogue; if case 2
#    reported 0 and this passes, the suite is silently testing English -- the
#    exact vacuity #1523 was filed about.
IFS='|' read -r n tok <<EOF
$(run_case "xx" without-catalogue)
EOF
check "LANGUAGE=xx, catalogue absent" "$n" 0 "$tok" \
      "[xx] appeared without a catalogue -- case 2 proves nothing"

# ---------------------------------------------------------------------------
# 4. The catalogue chooses the plural FORM, not merely a translation (#1622).
#
# The [xx] counts above cannot see this.  Both forms are translated, so a
# build that always returned msgstr[0] -- or one where MN_() fell through to
# the English ternary -- would score identically on cases 1-3.
#
# The count is controlled rather than observed: @entrances reports how many
# exits lead to a room, so digging one exit gives exactly 1 and digging a
# second gives exactly 2.  Reading @entrances against the starter database
# instead would make this depend on that database's exit topology, which is
# not what is under test.
# ---------------------------------------------------------------------------
plural_case() {   # $1 = how many exits to dig
    rm -rf "$WORK"; mkdir -p "$WORK/data" "$WORK/logs" "$WORK/text"
    ( cd "$WORK" || exit 1
      ln -s "$BIN" bin
      cp "$REPO_ROOT/mux/game/alias.conf" "$REPO_ROOT/mux/game/compat.conf" . 2>/dev/null
      mkdir -p locale/xx/LC_MESSAGES
      msgfmt -o locale/xx/LC_MESSAGES/tinymux.mo "$PO" 2>/dev/null
      cat > p.conf <<EOF
input_database  data/p.db
output_database data/p.db.new
crash_database  data/p.db.CRASH
mail_database   data/mail.db
comsys_database data/comsys.db
port 2901
mud_name NlsPlural
command_quota_increment 200000
command_quota_max 200000
include alias.conf
include compat.conf
EOF
      # Move INTO the new room before counting: @entrances needs a locally
      # matchable target, and a freshly dug room is not matchable by name
      # from where it was dug ("I don't see that here").  Inside it, `here`
      # resolves and the count is exactly the exits leading in.
      {
        echo '@dig Target=to1,back1'
        echo '@teleport to1'
        [ "$1" -ge 2 ] && echo '@open two=here'
        echo '@entrances here'
        echo '@shutdown'
      } > in.txt
      LOCPATH="${NLS_LOCPATH:-${LOCPATH-}}" LC_ALL="$NLS_LOCALE" LANGUAGE=xx \
        LD_LIBRARY_PATH="$BIN" \
        $TIMEOUT "$BIN/muxscript" -g . -c p.conf < in.txt > out.log 2>&1
    )
    grep -aoE '\[xx\] [0-9]+ entrances? found\.' "$WORK/out.log" 2>/dev/null | head -1
}

one=$(plural_case 1)
case "$one" in
    *"1 entrance found."*)
        printf '%-34s %-10s %-10s %s\n' "plural: n=1 picks singular" "singular" "singular" "ok" ;;
    *)
        printf '%-34s %-10s %-10s %s\n' "plural: n=1 picks singular" "singular" "${one:-<none>}" "FAIL"
        fails=$((fails+1)) ;;
esac

two=$(plural_case 2)
case "$two" in
    *entrances\ found.*)
        printf '%-34s %-10s %-10s %s\n' "plural: n>1 picks plural" "plural" "plural" "ok" ;;
    *)
        printf '%-34s %-10s %-10s %s\n' "plural: n>1 picks plural" "plural" "${two:-<none>}" "FAIL"
        fails=$((fails+1)) ;;
esac

rm -rf "$WORK"
echo
if [ "$fails" -ne 0 ]; then
    echo "=== tests/nls: FAILED ($fails case(s)) ==="
    echo "Cases 2 and 3 are a pair: 2 shows translation happens, 3 shows it is"
    echo "the catalogue that makes it happen.  A failure in either means a"
    echo "LANGUAGE=xx run can no longer be trusted as evidence.  See #1523."
    exit 1
fi
echo "=== tests/nls: PASSED (5 cases; translation observed and shown to depend"
echo "    on the catalogue, softcode tokens unchanged throughout) ==="
exit 0
