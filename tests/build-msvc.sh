#!/bin/sh
#
# build-msvc.sh — build the tests/ harnesses with MSVC.
#
# The harnesses are Makefiles invoking g++, and a Windows box that can build
# TinyMUX has Visual Studio but no make, gcc or cc — so on Windows almost none
# of `make test` can run (#1441).  This builds the ones that link against
# libmux; it is the tests/ counterpart of testcases/tools/build-msvc.sh.
#
#     ./tests/build-msvc.sh              # build and run all supported
#     ./tests/build-msvc.sh format       # just one
#     ./tests/build-msvc.sh --build-only # build, do not run
#
# Requires a built tree (mux/bin_release/libmux.lib and libmux.dll), Visual
# Studio with the C++ toolset, and Git Bash or MSYS to run this script.
#
# tests/dbt is deliberately NOT handled here.  It builds five binaries, each
# needing a different link, and compiles all three DBT backends into one image
# via -D symbol renames; that is a real port rather than a compiler swap, and
# it is the highest-value one left (#1441).
#
# tests/parity213 and tests/scenario are shell and Python drivers that may
# already work under Git Bash — untested, and not this script's business.

set -e

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
BIN_RELEASE="$REPO_ROOT/mux/bin_release"

RUN=1
TARGETS=""
for a in "$@"; do
    case "$a" in
        --build-only) RUN=0 ;;
        -*) echo "unknown option: $a" >&2; exit 2 ;;
        *)  TARGETS="$TARGETS $a" ;;
    esac
done
[ -n "$TARGETS" ] || TARGETS="format netaddr alarm"

if [ ! -f "$BIN_RELEASE/libmux.lib" ]; then
    echo "ERROR: $BIN_RELEASE/libmux.lib not found. Build netmux.sln first." >&2
    exit 1
fi

VSWHERE="/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
if [ ! -x "$VSWHERE" ]; then
    echo "ERROR: vswhere.exe not found. Is Visual Studio installed?" >&2
    exit 1
fi
VSPATH=$("$VSWHERE" -latest -products '*' \
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 \
    -property installationPath)
if [ -z "$VSPATH" ]; then
    echo "ERROR: no Visual Studio install with the C++ toolset." >&2
    exit 1
fi
VCVARS="$VSPATH\\VC\\Auxiliary\\Build\\vcvars64.bat"

# Quoting a vcvars path with spaces through sh -> cmd.exe -> cl is more
# escaping layers than it is worth getting right; a generated .bat has one.
BAT=$(mktemp -t buildmsvc.XXXXXX)
mv "$BAT" "$BAT.bat"
BAT="$BAT.bat"
trap 'rm -f "$BAT"' EXIT

WIN_INC=$(cygpath -w "$REPO_ROOT/mux/include")
WIN_LIB=$(cygpath -w "$BIN_RELEASE/libmux.lib")

# WIN32/NDEBUG/UNICODE match what the vcxproj files define; config.h selects
# its Windows branch on WIN32 and errors out on the Unix networking path
# without it.
CLFLAGS="/nologo /EHsc /O2 /std:c++17 /W3 /DWIN32 /DNDEBUG /DUNICODE /D_CRT_SECURE_NO_WARNINGS"

fail=0
for t in $TARGETS; do
    dir="$SCRIPT_DIR/$t"
    src="test_$t.cpp"
    if [ ! -f "$dir/$src" ]; then
        echo "ERROR: $dir/$src not found." >&2
        exit 1
    fi

    # netaddr's Makefile links ../../mux/src/netmux-netaddr.o, which the MSVC
    # build does not produce as a standalone object -- netaddr.cpp is compiled
    # into netmux.exe.  Compile the source directly into the test instead.
    #
    # /Fo names a single output object, so it cannot be combined with a
    # second source file; let cl name the objects after their sources when
    # there is more than one.
    extra=""
    libs=""
    fo="/Fo:test_$t.obj"
    case "$t" in
        netaddr)
            extra=$(cygpath -w "$REPO_ROOT/mux/src/netaddr.cpp")
            fo=""
            # htons/ntohl/inet_pton/getaddrinfo live in ws2_32 on Windows;
            # on Unix they are in libc, which is why the Makefile links none.
            libs="ws2_32.lib"
            ;;
    esac

    echo "=== $t ==="
    WIN_DIR=$(cygpath -w "$dir")
    {
        echo "@echo off"
        # Makesmoke-style callers may have clobbered TMP with a scratch FILE,
        # which cl inherits as its temp DIRECTORY and dies on.
        echo "set \"TMP=%LOCALAPPDATA%\\Temp\""
        echo "set \"TEMP=%LOCALAPPDATA%\\Temp\""
        echo "call \"$VCVARS\" >nul 2>nul"
        echo "if errorlevel 1 exit /b 1"
        echo "cd /d \"$WIN_DIR\""
        echo "cl $CLFLAGS /I\"$WIN_INC\" /Fe:test_$t.exe $fo $src $extra \"$WIN_LIB\" $libs"
    } > "$BAT"

    if ! cmd.exe //c "$(cygpath -w "$BAT")"; then
        echo "ERROR: cl failed for $t" >&2
        fail=1
        continue
    fi

    # libmux.dll must sit beside the exe; there is no rpath equivalent.
    cp -f "$BIN_RELEASE/libmux.dll" "$dir/" 2>/dev/null || true

    if [ "$RUN" = "1" ]; then
        if ( cd "$dir" && ./test_$t.exe ); then
            echo "--- $t PASSED"
        else
            echo "--- $t FAILED (exit $?)"
            fail=1
        fi
    fi
done

exit $fail
