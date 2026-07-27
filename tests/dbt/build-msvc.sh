#!/bin/sh
#
# build-msvc.sh — build and run the DBT/RV64 tests with MSVC (#1441).
#
# The Makefile here drives g++ and picks its host backend from `uname -m`,
# which is a Unix-only mapping.  A Windows box that can build TinyMUX has
# Visual Studio and no make, so none of this ever ran on Win64 -- the layer
# where #1147, #1148, #1151, #1152, #1153, #1311, #1313 and #1320 all lived.
#
#     ./tests/dbt/build-msvc.sh                # build and run all five
#     ./tests/dbt/build-msvc.sh chain cache    # a subset
#     ./tests/dbt/build-msvc.sh --build-only
#
# The five binaries mirror the Makefile's, and for the same reasons: each
# needs a genuinely different link and they cannot coexist in one image.
# See the Makefile header for the per-target rationale.
#
# THE ONE REAL DIFFERENCE FROM THE MAKEFILE, and the point of doing this:
# the Makefile maps x86_64 -> dbt_x64_sysv, but Windows x64 uses the Win64
# calling convention, so `exec` and `fuzz` must link dbt_x64_win64 instead.
# That backend is compiled by `chain` on every platform (byte inspection
# only) but has never been EXECUTED by these tests anywhere -- Linux and
# macOS both run a SysV backend.  Running it here is new coverage, not a
# port of existing coverage.

set -e

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
ENGINE="$REPO_ROOT/mux/modules/engine"
INCDIR="$REPO_ROOT/mux/include"
ELF="$ENGINE/dbt_rt/test_rv64.elf"

RUN=1
TARGETS=""
for a in "$@"; do
    case "$a" in
        --build-only) RUN=0 ;;
        -*) echo "unknown option: $a" >&2; exit 2 ;;
        *)  TARGETS="$TARGETS $a" ;;
    esac
done
[ -n "$TARGETS" ] || TARGETS="chain cache interp exec fuzz"

VSWHERE="/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
[ -x "$VSWHERE" ] || { echo "ERROR: vswhere.exe not found." >&2; exit 1; }
VSPATH=$("$VSWHERE" -latest -products '*' \
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 \
    -property installationPath)
[ -n "$VSPATH" ] || { echo "ERROR: no VS with the C++ toolset." >&2; exit 1; }
VCVARS="$VSPATH\\VC\\Auxiliary\\Build\\vcvars64.bat"

WIN_ENGINE=$(cygpath -w "$ENGINE")
WIN_INC=$(cygpath -w "$INCDIR")
WIN_DIR=$(cygpath -w "$SCRIPT_DIR")
WIN_ELF=$(cygpath -w "$ELF")

# /wd4267 /wd4244 /wd4146: size_t/int narrowing and unary minus on unsigned,
# throughout the engine sources.  These are code under test, not code written
# here -- the Makefile makes the same split, applying -Wall -Wextra to the
# drivers only so real failures stay visible.
CF="/nologo /EHsc /O2 /std:c++17 /DHAVE_CONFIG_H /DTINYMUX_JIT /DWIN32 /DNDEBUG /DUNICODE /D_CRT_SECURE_NO_WARNINGS /wd4267 /wd4244 /wd4146 /I\"$WIN_INC\""

# Windows x64 is Win64 ABI, NOT SysV -- see the header note.
HOST_BACKEND=dbt_x64_win64

rename() {
    p="$1"
    printf '%s' "/Ddbt_backend_backpatch_jmp=${p}_backpatch_jmp /Ddbt_backend_decode_jmp_target=${p}_decode_jmp_target /Ddbt_backend_emit_trampoline=${p}_emit_trampoline /Ddbt_backend_translate_block=${p}_translate_block /Ddbt_register_intrinsic=${p}_register_intrinsic"
}

BAT=$(mktemp -t dbtmsvc.XXXXXX); mv "$BAT" "$BAT.bat"; BAT="$BAT.bat"
trap 'rm -f "$BAT"' EXIT

# Emit the vcvars preamble shared by every compile.
preamble() {
    echo "@echo off"
    # Makesmoke-style callers clobber TMP with a scratch FILE; cl inherits it
    # as its temp DIRECTORY and dies with "cannot create linker response file".
    echo "set \"TMP=%LOCALAPPDATA%\\Temp\""
    echo "set \"TEMP=%LOCALAPPDATA%\\Temp\""
    echo "call \"$VCVARS\" >nul 2>nul"
    echo "if errorlevel 1 exit /b 1"
    echo "cd /d \"$WIN_DIR\""
}

build() {
    name="$1"; shift
    { preamble; for line in "$@"; do echo "$line"; done; } > "$BAT"
    if ! cmd.exe //c "$(cygpath -w "$BAT")" > /tmp/dbtbuild.log 2>&1; then
        echo "ERROR: build failed for $name" >&2
        grep -viE "warning C|note:" /tmp/dbtbuild.log | tail -15 >&2
        return 1
    fi
    return 0
}

fail=0
for t in $TARGETS; do
    echo "=== $t ==="
    case "$t" in
    chain)
        build chain \
          "cl $CF /c /Fo:test_chain.obj test_chain.cpp" \
          "cl $CF $(rename a64) /c /Fo:be_a64.obj \"$WIN_ENGINE\\dbt_a64_sysv.cpp\"" \
          "cl $CF $(rename x64sysv) /c /Fo:be_x64sysv.obj \"$WIN_ENGINE\\dbt_x64_sysv.cpp\"" \
          "cl $CF $(rename win64) /c /Fo:be_win64.obj \"$WIN_ENGINE\\dbt_x64_win64.cpp\"" \
          "cl $CF /Fe:test_chain.exe test_chain.obj be_a64.obj be_x64sysv.obj be_win64.obj" \
          || { fail=1; continue; }
        BIN=test_chain.exe ;;
    cache)
        build cache \
          "cl $CF /c /Fo:test_cache.obj test_cache.cpp" \
          "cl $CF /c /Fo:dbt_shared.obj \"$WIN_ENGINE\\dbt.cpp\"" \
          "cl $CF /Fe:test_cache.exe test_cache.obj dbt_shared.obj" \
          || { fail=1; continue; }
        BIN=test_cache.exe ;;
    interp)
        # #includes dbt_interp.cpp to reach file-static mem_check, so the
        # engine directory has to be on the include path.
        build interp \
          "cl $CF /I\"$WIN_ENGINE\" /c /Fo:test_interp.obj test_interp.cpp" \
          "cl $CF /Fe:test_interp.exe test_interp.obj" \
          || { fail=1; continue; }
        BIN=test_interp.exe ;;
    exec)
        build exec \
          "cl $CF /Fe:dbt_test.exe \"$WIN_ENGINE\\dbt_test.cpp\" \"$WIN_ENGINE\\dbt_interp.cpp\" \"$WIN_ENGINE\\dbt_elf64.cpp\" \"$WIN_ENGINE\\dbt.cpp\" \"$WIN_ENGINE\\$HOST_BACKEND.cpp\"" \
          || { fail=1; continue; }
        BIN=dbt_test.exe ;;
    fuzz)
        build fuzz \
          "cl $CF /Fe:fuzz_diff.exe fuzz_diff.cpp \"$WIN_ENGINE\\dbt_interp.cpp\" \"$WIN_ENGINE\\dbt.cpp\" \"$WIN_ENGINE\\$HOST_BACKEND.cpp\"" \
          || { fail=1; continue; }
        BIN=fuzz_diff.exe ;;
    *)
        echo "unknown target: $t" >&2; exit 2 ;;
    esac

    if [ "$RUN" = "1" ]; then
        args=""
        # The ELF leg carries most of exec's block-translation coverage; the
        # Makefile says so loudly when it is missing rather than silently
        # running the hand-assembled cases alone.
        if [ "$t" = "exec" ]; then
            if [ -f "$ELF" ]; then args="$WIN_ELF"
            else echo "NOTE: $ELF missing -- hand-assembled cases only,"
                 echo "      which drops the ELF leg's DBT block-translation coverage."
            fi
        fi
        if ( cd "$SCRIPT_DIR" && ./"$BIN" $args ); then
            echo "--- $t PASSED"
        else
            echo "--- $t FAILED (exit $?)"
            fail=1
        fi
    fi
done

exit $fail
