#!/bin/bash
#
#   run_ko.sh — runtime oracle for a real translated locale (#1419).
#
#   tests/nls/run.sh proves the gettext plumbing works using the `xx`
#   pseudo-locale.  It cannot prove anything about *translation*, because xx is
#   English with a prefix: it preserves argument order, so every message it
#   renders would render identically under a broken implementation of argument
#   handling.  That blind spot is the reason Korean was chosen as the second
#   locale over Spanish -- Korean is SOV with postpositions and reorders
#   arguments where an SVO language does not (analysis on #1419).
#
#   Four cases, and the last two are the point of the file:
#
#     1  ko catalogue present, LANGUAGE=ko   -> Korean prose appears
#     2  ko catalogue absent,  LANGUAGE=ko   -> English returns (anti-vacuity)
#     3  source-order Korean for a 2-argument message -> arguments substitute
#     4  reordered Korean, same message, positional specs -> DOES NOT WORK
#
#   Case 4 pins a known defect rather than a desired behaviour.  msgfmt -c
#   accepts %N$ (measured), so a translator gets a clean build and a broken
#   game; mux_vsnprintf stops at the '$' and echoes the rest of the format
#   literally (#1429 stop policy, stringutil.cpp).  The 7 reordering entries in
#   ko.po are therefore marked fuzzy so msgfmt excludes them.
#
#   When positional support lands, case 4 FLIPS TO FAILING.  That is intended:
#   it is the reminder to drop the fuzzy markers in ko.po and re-point this
#   case at the substituted text.  Do not "fix" it by deleting the case.
#
#   Mechanics inherited from run.sh: mux_nls_init() binds the domain against
#   the relative path "locale", and muxscript chdir()s to its -g directory, so
#   the catalogue must live in the game directory the harness runs from.
#   gettext also ignores LANGUAGE when the process locale is C/POSIX, so a real
#   UTF-8 locale has to be selected first.
#
set -u

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
BIN="$REPO_ROOT/mux/game/bin"
WORK="$SCRIPT_DIR/work-ko"
PO="$REPO_ROOT/mux/po/ko.po"

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

# Same locale discovery as run.sh: gettext ignores LANGUAGE under C/POSIX.
NLS_LOCALE=""
NLS_LOCPATH=""
if command -v locale >/dev/null 2>&1; then
    for cand in $(locale -a 2>/dev/null | grep -E 'en_US\.(utf8|UTF-8)$' || true); do
        NLS_LOCALE="$cand"
        break
    done
fi
if [ -z "$NLS_LOCALE" ]; then
    USER_LOCALE_DIR="${HOME}/.locale"
    USER_LOCALE="en_US.UTF-8"
    if [ ! -d "$USER_LOCALE_DIR/$USER_LOCALE" ] && command -v localedef >/dev/null 2>&1; then
        mkdir -p "$USER_LOCALE_DIR"
        localedef -f UTF-8 -i en_US "$USER_LOCALE_DIR/$USER_LOCALE" 2>/dev/null || true
    fi
    if [ -d "$USER_LOCALE_DIR/$USER_LOCALE" ]; then
        if LOCPATH="$USER_LOCALE_DIR" LC_ALL="$USER_LOCALE" locale charmap 2>/dev/null | grep -qi utf; then
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

# "@create" reaches "%s created as object #%d" -- two arguments, no fixture
# needed, and reachable as a plain object (the smoke player is not God).
# "zzzznotacommand" reaches the no-format "Huh?" message.
CMDS='zzzznotacommand
@create kowidget
@shutdown'

KO_HUH='네?  (도움말을 보려면 “help”를 입력하세요.)'

# The 2-argument message used by cases 3 and 4.  ko.po translates it in source
# order; case 4 swaps that for the reordered positional form.
OVERRIDE_ID='%s created as object #%d'
OVERRIDE_POSITIONAL='사물 #%2$d(으)로 %1$s을(를) 만들었습니다'

# $1 = mode: none | plain | positional
#   none        no catalogue at all
#   plain       ko.po as committed (already translates OVERRIDE_ID in source order)
#   positional  ko.po with OVERRIDE_ID's msgstr REPLACED by a reordered %N$ form
#
# The override must replace the existing entry, not be appended: ko.po already
# translates OVERRIDE_ID, and two definitions of one msgid are a fatal msgfmt
# error ("duplicate message definition"), not a last-wins override.
build_catalogue() {
    local mode="$1" dst="$2"
    [ "$mode" = "none" ] && return 0
    mkdir -p "$(dirname "$dst")"
    local tmp="$WORK/tmp.po"
    if [ "$mode" = "positional" ]; then
        awk -v id="msgid \"$OVERRIDE_ID\"" -v repl="$OVERRIDE_POSITIONAL" '
            { if (hit && $0 ~ /^msgstr /) { print "msgstr \"" repl "\""; hit=0; next }
              if ($0 == id) hit=1
              print }
        ' "$PO" > "$tmp"
        if ! grep -qF "$OVERRIDE_POSITIONAL" "$tmp"; then
            echo "HARNESS ERROR: could not substitute msgstr for $OVERRIDE_ID" >&2
            return 1
        fi
    else
        cp "$PO" "$tmp"
    fi
    # Same -c guard the Makefile uses.  Errors are surfaced, not swallowed: an
    # unbuilt catalogue makes every later case report "<none>", which reads
    # like a runtime failure and is not one.
    if ! msgfmt -c -o "$dst" "$tmp" 2>"$WORK/msgfmt.err"; then
        echo "HARNESS ERROR: msgfmt failed for mode '$mode':" >&2
        sed 's/^/    /' "$WORK/msgfmt.err" >&2
        return 1
    fi
    return 0
}

# $1 = mode.  Echoes the whole captured output.
run_case() {
    local mode="$1"
    rm -rf "$WORK"; mkdir -p "$WORK/data" "$WORK/logs" "$WORK/text"
    ( cd "$WORK" || exit 1
      ln -s "$BIN" bin
      cp "$REPO_ROOT/mux/game/alias.conf" "$REPO_ROOT/mux/game/compat.conf" . 2>/dev/null
      build_catalogue "$mode" "locale/ko/LC_MESSAGES/tinymux.mo" || exit 1
      cat > p.conf <<EOF
input_database  data/p.db
output_database data/p.db.new
crash_database  data/p.db.CRASH
mail_database   data/mail.db
comsys_database data/comsys.db
port 2902
mud_name KoOracle
command_quota_increment 200000
command_quota_max 200000
include alias.conf
include compat.conf
EOF
      printf '%s\n' "$CMDS" > in.txt
      LOCPATH="${NLS_LOCPATH:-${LOCPATH-}}" LC_ALL="$NLS_LOCALE" LANGUAGE=ko LD_LIBRARY_PATH="$BIN" \
        $TIMEOUT "$BIN/muxscript" -g . -c p.conf < in.txt > out.log 2>&1
    )
    cat "$WORK/out.log" 2>/dev/null
}

fails=0
report() {
    local label="$1" verdict="$2" detail="$3"
    printf '%-46s %s\n' "$label" "$verdict"
    [ -n "$detail" ] && printf '%-46s   %s\n' "" "$detail"
    case "$verdict" in FAIL*) fails=$((fails+1));; esac
}

echo "=== tests/nls/run_ko.sh — Korean locale runtime oracle (#1419) ==="
echo

# 1. Translation happens at all.
out=$(run_case plain)
if printf '%s' "$out" | grep -qF "$KO_HUH"; then
    report "1  LANGUAGE=ko, catalogue present" "ok" "Korean prose observed"
else
    report "1  LANGUAGE=ko, catalogue present" "FAIL" \
           "expected Korean 'Huh?' translation, got: $(printf '%s' "$out" | grep -i 'huh\|네?' | head -1)"
fi

# 2. Negative control.  Without this, case 1 proves nothing -- a build with
#    gettext never wired up would still have to produce English here, and if it
#    somehow produced Korean the prose is not coming from the catalogue.
out=$(run_case none)
if printf '%s' "$out" | grep -qF "$KO_HUH"; then
    report "2  LANGUAGE=ko, catalogue absent" "FAIL" \
           "Korean appeared with no catalogue -- case 1 proves nothing"
elif printf '%s' "$out" | grep -q 'Huh?'; then
    report "2  LANGUAGE=ko, catalogue absent" "ok" "English returned"
else
    report "2  LANGUAGE=ko, catalogue absent" "FAIL" "neither Korean nor English 'Huh?' found"
fi

# 3. A two-argument message in source order: substitution must work, proving
#    the locale is not limited to argument-free prose.  Read from the same
#    committed catalogue as case 1 -- ko.po already translates OVERRIDE_ID.
out=$(run_case plain)
line=$(printf '%s' "$out" | grep -F '만들었습니다' | head -1)
if printf '%s' "$line" | grep -q 'kowidget' && printf '%s' "$line" | grep -qE '#[0-9]+'; then
    report "3  2-arg message, source order" "ok" "$(printf '%s' "$line" | tr -d '\r')"
else
    report "3  2-arg message, source order" "FAIL" "no substituted Korean line; got: ${line:-<none>}"
fi

# 4. The same message reordered with positional specs.  msgfmt accepts this
#    (measured), so nothing upstream of the runtime objects -- and then
#    mux_vsnprintf cannot parse '$' and echoes the remainder verbatim.
#
#    ASSERTS THE BUG.  See the header: when %N$ is supported this must fail.
out=$(run_case positional)
line=$(printf '%s' "$out" | grep -F '만들었습니다' | head -1)
if printf '%s' "$line" | grep -qF '%2$d'; then
    report "4  same message, positional (%N\$)" "ok (bug present, as expected)" \
           "$(printf '%s' "$line" | tr -d '\r')"
elif printf '%s' "$line" | grep -q 'kowidget'; then
    report "4  same message, positional (%N\$)" "FAIL — positional args now WORK" \
           "Drop the 7 fuzzy markers in mux/po/ko.po and update this case."
else
    report "4  same message, positional (%N\$)" "FAIL" "unexpected output: ${line:-<none>}"
fi

rm -rf "$WORK"
echo
if [ "$fails" -ne 0 ]; then
    echo "=== tests/nls (ko): FAILED ($fails case(s)) ==="
    exit 1
fi
echo "=== tests/nls (ko): PASSED (4 cases) ==="
echo "    Korean renders and substitutes; argument reordering does not work and"
echo "    is pinned by case 4.  See #1419."
exit 0
