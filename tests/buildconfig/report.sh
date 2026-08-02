#!/bin/sh
#
#   report.sh — state the build configuration a test run is testing (#1946).
#
#   The problem this exists for:  several optional features default to NO,
#   and the tests that cover them skip cleanly when the feature is absent.
#   A green `make test` therefore does not mean the same thing on two boxes,
#   and nothing in the output says which meaning applied.  #1944 (stubslave
#   teardown) and #1943 (jitstats) landed on the same evening from opposite
#   directions: in both, the bug survived because the configuration was
#   optional, and the fix inherited exactly the same invisibility.
#
#   CLAUDE.md has warned about the JIT case for months.  Documentation was
#   the only thing standing between the project and a repeat, and it is not
#   load-bearing.  So: print the configuration, and make a run fail when
#   the configuration it prints is not the one the binaries were built with.
#
#   Where the values come from — resolved values only, never guesses:
#
#     jit          mux/src/Makefile        TINYMUX_JIT  = -DTINYMUX_JIT
#     realitylvls  mux/src/Makefile        REALITY_LVLS = -DREALITY_LVLS
#     wodrealms    mux/src/Makefile        WOD_REALMS   = -DWOD_REALMS
#     stubslave    mux/src/Makefile        am__EXEEXT_N/am__append_N (uncommented)
#     nls          mux/include/autoconf.h  #define HAVE_NLS 1
#
#   These are the substitutions and defines the compiler actually saw, not
#   the flags someone typed.  Parsing `--enable-X` off the configure line
#   would report intent; a feature can also be switched off by configure
#   itself when a dependency is missing, and intent would not show that.
#   The configure line is printed too, but as provenance, not as the answer.
#
#   Deliberately NOT derived from configure.ac defaults.  An early draft
#   scraped `AC_MSG_RESULT` out of each AC_ARG_ENABLE block and got four of
#   nine wrong, because the last result in the block is the enabled branch,
#   not the default.  A config banner that lies is worse than no banner.
#
#   Modes:
#     (none)      full banner, plus the assertions below
#     --oneline   just "jit=yes realitylvls=yes ..." for the run summary
#
#   Assertions (exit 1):
#     * binaries older than config.status — the tree was reconfigured and
#       not rebuilt, so the banner would describe a configuration that is
#       not in the artifacts under test.  This is the failure that makes
#       the banner trustworthy rather than decorative.
#     * EXPECT_CONFIG mismatch — for a box with a job.  A machine meant to
#       be the JIT box should fail loudly on a non-JIT build rather than
#       skip politely (#1946, option 3).
#
#   SKIPs (exit 0) when the tree has no config.status: MSVC builds do not
#   run configure at all, and this must not turn into a Windows failure.
#
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)

MUX="$ROOT/mux"
SRCMAKE="$MUX/src/Makefile"
AUTOCONF_H="$MUX/include/autoconf.h"
CONFIG_STATUS="$MUX/config.status"

ONELINE=no
if [ "${1:-}" = "--oneline" ]; then
    ONELINE=yes
fi

# ---- Resolve each feature from the file that gates its compilation. ----

# A substituted -D flag is present-and-non-empty exactly when enabled.
subst_flag()
{
    # $1 = variable name in the generated Makefile
    [ -f "$SRCMAKE" ] || { echo "unknown"; return; }
    val=$(sed -n "s/^$1 *= *//p" "$SRCMAKE" | head -1)
    if [ -n "$val" ]; then echo "yes"; else echo "no"; fi
}

feature_jit=$(subst_flag TINYMUX_JIT)
feature_reality=$(subst_flag REALITY_LVLS)
feature_wod=$(subst_flag WOD_REALMS)

# automake comments the conditional-append lines out when the conditional is
# false.  Matching any index rather than a fixed one survives automake
# renumbering if another `if` is ever added to mux/src/Makefile.am.
if [ ! -f "$SRCMAKE" ]; then
    feature_stubslave=unknown
elif grep -qE '^am__(EXEEXT|append)_[0-9]+ *=.*stubslave' "$SRCMAKE"; then
    feature_stubslave=yes
else
    feature_stubslave=no
fi

if [ ! -f "$AUTOCONF_H" ]; then
    feature_nls=unknown
elif grep -qE '^#define HAVE_NLS 1' "$AUTOCONF_H"; then
    feature_nls=yes
else
    feature_nls=no
fi

SUMMARY="jit=$feature_jit stubslave=$feature_stubslave nls=$feature_nls"
SUMMARY="$SUMMARY realitylvls=$feature_reality wodrealms=$feature_wod"

if [ "$ONELINE" = yes ]; then
    echo "$SUMMARY"
    exit 0
fi

# ---- Banner. ----

echo "======================================================================"
echo "  BUILD CONFIGURATION — what this run is able to test"
echo "----------------------------------------------------------------------"

if [ ! -f "$CONFIG_STATUS" ]; then
    echo "  SKIP: no mux/config.status — this tree was not configured by"
    echo "        configure (an MSVC build does not run it).  Nothing to"
    echo "        report and nothing to check."
    echo "======================================================================"
    exit 0
fi

CFG_LINE=$("$CONFIG_STATUS" --config 2>/dev/null)
if [ -z "$CFG_LINE" ]; then
    CFG_LINE=$(sed -n "s/^ac_cs_config='\(.*\)'$/\1/p" "$CONFIG_STATUS" | head -1)
fi
[ -n "$CFG_LINE" ] || CFG_LINE="(none recorded)"

printf '  compiled:   jit=%s  stubslave=%s  nls=%s  realitylvls=%s  wodrealms=%s\n' \
    "$feature_jit" "$feature_stubslave" "$feature_nls" \
    "$feature_reality" "$feature_wod"
echo "  configure:  $CFG_LINE"

# ---- Is the build current with respect to that configuration? ----
#
# `find -newer` rather than arithmetic on stat(1): stat's flags differ
# between GNU and BSD, and this only needs the comparison, not the values.

newest_artifact=""
for a in "$MUX/src/netmux" "$MUX/modules/engine/engine.so" "$MUX/lib/libmux.so" \
         "$ROOT/game/bin/netmux" "$MUX/game/bin/netmux"
do
    [ -f "$a" ] || continue
    if [ -z "$newest_artifact" ] || [ "$a" -nt "$newest_artifact" ]; then
        newest_artifact="$a"
    fi
done

rc=0

if [ -z "$newest_artifact" ]; then
    echo "  binaries:   none found — cannot check the build is current."
elif [ "$CONFIG_STATUS" -nt "$newest_artifact" ]; then
    echo "  binaries:   STALE — $(basename "$newest_artifact") predates config.status."
    echo
    echo "  FAIL: this tree was reconfigured and not rebuilt.  The line above"
    echo "        describes a configuration the binaries under test do not"
    echo "        have, so every result below would be attributed to the"
    echo "        wrong build.  Run 'make clean && make install' first"
    echo "        (CLAUDE.md: always make clean after changing configure"
    echo "        flags)."
    rc=1
else
    echo "  binaries:   current (newer than config.status)"
fi

# ---- Expected-configuration assertion, for a box with a job. ----

if [ -n "${EXPECT_CONFIG:-}" ]; then
    echo "  expecting:  $EXPECT_CONFIG"
    for want in $EXPECT_CONFIG; do
        key=${want%%=*}
        val=${want#*=}
        case "$key" in
            jit)         got=$feature_jit ;;
            stubslave)   got=$feature_stubslave ;;
            nls)         got=$feature_nls ;;
            realitylvls) got=$feature_reality ;;
            wodrealms)   got=$feature_wod ;;
            *)
                echo "  FAIL: EXPECT_CONFIG names unknown feature '$key'."
                echo "        Known: jit stubslave nls realitylvls wodrealms"
                rc=1
                continue ;;
        esac
        if [ "$got" != "$val" ]; then
            echo "  FAIL: expected $key=$val, this tree has $key=$got."
            echo "        A box configured to cover $key must not report a"
            echo "        green run in which $key never ran."
            rc=1
        fi
    done
fi

echo "======================================================================"
exit $rc
