#!/bin/sh
#
# build-msvc.sh — build the testcases/tools utilities with MSVC.
#
# Windows boxes that can build TinyMUX (Visual Studio) generally have no
# make, gcc or cc, so tools/Makefile cannot run there.  Makesmoke calls this
# as a fallback; it is also fine to run by hand:
#
#     ./tools/build-msvc.sh              # unformat and reformat
#     ./tools/build-msvc.sh unformat     # just one
#
# Produces unformat.exe / reformat.exe next to the sources, which is what
# Makesmoke's "[ -x tools/unformat ]" preflight looks for -- the test
# resolves the .exe suffix transparently under Git Bash / MSYS.
#
# Requires: Visual Studio with the C++ toolset, and Git Bash or MSYS to run
# this script.  Nothing else; in particular not ragel, which is only needed
# to regenerate the .c files from the .rl sources.

set -e

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

# unformat only by default: it is the one Makesmoke needs.  reformat does not
# build under MSVC -- it uses POSIX getline() and ssize_t, which is a real
# portability job rather than a missing-keyword one, and nothing in the smoke
# path calls it.  Ask for it by name if you want to try.
TARGETS=${*:-unformat}

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

# cl.exe needs the environment vcvars64.bat sets up, and that is a .bat, so
# the compile has to happen inside cmd.exe rather than here.  Native paths
# throughout: cl does not understand /c/... style paths.
WIN_DIR=$(cygpath -w "$SCRIPT_DIR" 2>/dev/null || echo "$SCRIPT_DIR")
if [ -z "$WIN_DIR" ]; then
    echo "ERROR: could not convert $SCRIPT_DIR to a native path." >&2
    exit 1
fi

# Quoting a vcvars path containing spaces through sh -> cmd.exe -> cl is more
# escaping layers than it is worth getting right; a generated .bat has exactly
# one.  Written per invocation and removed on exit.
BAT=$(mktemp -t buildmsvc.XXXXXX)
mv "$BAT" "$BAT.bat"
BAT="$BAT.bat"
trap 'rm -f "$BAT"' EXIT

for t in $TARGETS; do
    if [ ! -f "$SCRIPT_DIR/$t.c" ]; then
        echo "ERROR: $t.c not found." >&2
        exit 1
    fi
    echo "cl $t.c -> $t.exe"

    # /FI msvc_compat.h  defines away ragel's __attribute__((unused)).
    # /wd4189 /wd4101    unreferenced local, which is what that attribute
    #                    was suppressing on the gcc side.
    # /D_CRT_SECURE_NO_WARNINGS  MSVC's objection to standard C string calls.
    {
        echo "@echo off"
        # Makesmoke assigns its own scratch *file* to the shell variable TMP,
        # which cl.exe inherits and treats as its temp *directory* -- it then
        # dies with "cannot create linker response file".  Harmless on Linux,
        # where gcc reads TMPDIR instead, so it went unnoticed.  Point TMP and
        # TEMP at a directory that is always valid before invoking the
        # toolchain, whoever our caller was.
        echo "set \"TMP=%LOCALAPPDATA%\\Temp\""
        echo "set \"TEMP=%LOCALAPPDATA%\\Temp\""
        # 2>nul as well as >nul: vcvars64.bat probes for its own copy of
        # vswhere.exe and complains to stderr when that probe misses, even
        # though it then succeeds by another route.  The errorlevel check
        # below is what decides whether it actually worked, so suppressing
        # the chatter loses nothing.
        echo "call \"$VCVARS\" >nul 2>nul"
        echo "if errorlevel 1 exit /b 1"
        echo "cd /d \"$WIN_DIR\""
        echo "cl /nologo /O2 /W3 /wd4189 /wd4101 /D_CRT_SECURE_NO_WARNINGS /FI msvc_compat.h /Fe:$t.exe /Fo:$t.obj $t.c"
    } > "$BAT"

    cmd.exe //c "$(cygpath -w "$BAT" 2>/dev/null || echo "$BAT")" \
        || { echo "ERROR: cl failed for $t.c" >&2; exit 1; }

    if [ ! -e "$SCRIPT_DIR/$t.exe" ]; then
        echo "ERROR: $t.exe was not produced." >&2
        exit 1
    fi
done

echo "build-msvc.sh: ok"
