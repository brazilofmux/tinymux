#!/bin/bash
#
#   run.sh — run the smoke suite against a deliberately hostile allocator.
#
#   Every large malloc comes back filled with 0xAA instead of the fresh
#   zeroed pages the kernel usually supplies, so a read past the count
#   something wrote lands on a non-canonical pointer and faults, instead of
#   reading zero and behaving exactly like correct code.  See poison.c for
#   why that distinction is the whole point.
#
#   Opt-in via `make test-poison`; deliberately NOT part of `make test`,
#   which is Linux-only and pays a memset on every large allocation.
#
#   THE SHIM IS VERIFIED BEFORE IT IS TRUSTED.  A suite that passes under a
#   shim that silently failed to load is indistinguishable from a suite that
#   passes under a working one, and worth nothing.  If the self-test does not
#   behave exactly as it must, this script fails rather than reporting a
#   green run it cannot stand behind.
#
set -u

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
BIN="$REPO_ROOT/mux/game/bin"
BUILD="$SCRIPT_DIR/build"
SHIM="$BUILD/poison.so"
SELFTEST="$BUILD/selftest"

# --- Preflight (skip, don't fail, when prerequisites are absent) ------------
if [ "$(uname -s)" != "Linux" ]; then
    echo "SKIP: LD_PRELOAD malloc interposition is Linux/glibc-specific."
    echo "      macOS interposes through malloc zones (DYLD_INSERT_LIBRARIES"
    echo "      plus a zone shim), which is a different mechanism and not"
    echo "      implemented here."
    exit 0
fi
if ! command -v gcc >/dev/null 2>&1 || ! command -v g++ >/dev/null 2>&1; then
    echo "SKIP: gcc/g++ not found — cannot build the shim."
    exit 0
fi
if [ ! -x "$BIN/muxscript" ]; then
    echo "SKIP: $BIN/muxscript not found — run 'make install' first."
    exit 0
fi

mkdir -p "$BUILD"

echo "==> Building the poison shim"
gcc -O2 -shared -fPIC -o "$SHIM" "$SCRIPT_DIR/poison.c" -ldl || {
    echo "FAIL: could not build $SHIM"; exit 1; }
g++ -O2 -o "$SELFTEST" "$SCRIPT_DIR/selftest.cpp" || {
    echo "FAIL: could not build $SELFTEST"; exit 1; }

rc=0

# --- Self-test: does the shim actually do anything? ------------------------
#
# The two poisoned expectations are hard gates: they are what makes a green
# suite below mean something.  The unpoisoned legs establish the contrast and
# catch a stuck LD_PRELOAD.
echo
echo "==> Verifying the shim before trusting it"

clean_fill=$("$SELFTEST" --check-fill 2>&1)
echo "    unpoisoned large allocation: $clean_fill"
if [ "$clean_fill" = "poisoned" ]; then
    echo "FAIL: an unpoisoned run came back poisoned — LD_PRELOAD is already"
    echo "      set in this environment; the comparison below is meaningless."
    exit 1
fi

pois_fill=$(LD_PRELOAD="$SHIM" "$SELFTEST" --check-fill 2>&1)
echo "    poisoned   large allocation: $pois_fill"
if [ "$pois_fill" != "poisoned" ]; then
    echo "FAIL: the shim did not poison a large allocation.  Every result"
    echo "      below would be a vacuous pass, so this is fatal rather than"
    echo "      a warning."
    exit 1
fi

# The injected defect: exactly the bug class this target exists to catch.
#
# Run through a nested shell so its stderr swallows the "Segmentation fault"
# job-control notice.  The crash is the expected result here, and printing it
# unqualified in a passing run trains people to skim past a real one.
"$SELFTEST" --past-read >/dev/null 2>&1
clean_rc=$?
# The trailing `exit $?` matters: without it bash exec's the binary directly
# and OUR shell reports the death, so the notice escapes the redirection.
bash -c 'LD_PRELOAD="$1" "$2" --past-read >/dev/null 2>&1; exit $?' \
     _ "$SHIM" "$SELFTEST" 2>/dev/null
pois_rc=$?
echo "    injected past-count read: unpoisoned rc=$clean_rc, poisoned rc=$pois_rc"

if [ "$clean_rc" -ne 0 ]; then
    echo "FAIL: the injected past-count read did not survive an unpoisoned run."
    echo "      It is supposed to be invisible there — that is the premise."
    exit 1
fi
if [ "$pois_rc" -lt 128 ]; then
    echo "FAIL: the injected past-count read did NOT fault under poison"
    echo "      (expected death by signal, got rc=$pois_rc).  The shim cannot"
    echo "      catch the bug class it exists for; refusing to report a pass."
    exit 1
fi
echo "    shim verified: the defect is invisible clean and fatal poisoned."

# --- Informational: why any of this is necessary ---------------------------
echo
echo "==> What a past-count read really yields once a block is recycled"
"$SELFTEST" --recycle 2>&1 | sed 's/^/    /'

# --- The actual run --------------------------------------------------------
#
# Tools are built unpoisoned; only the server runs under the shim.
echo
echo "==> Building smoke tooling"
make -C "$REPO_ROOT/testcases/tools" >/dev/null || {
    echo "FAIL: could not build smoke tooling"; exit 1; }

echo "==> Running the smoke suite under the poisoned allocator"
(
    cd "$REPO_ROOT/testcases" || exit 1
    export LD_PRELOAD="$SHIM"
    ./tools/Makesmoke && ./tools/Smoke
)
smoke_rc=$?

echo
if [ "$smoke_rc" -eq 0 ]; then
    echo "PASS: smoke suite green under the poisoned allocator."
else
    echo "FAIL: smoke suite failed under the poisoned allocator (rc=$smoke_rc)."
    echo "      A failure here that does not reproduce in a plain 'make test'"
    echo "      is the signature of a read past an uninitialized buffer's"
    echo "      written count — see testcases/smoke.log."
    rc=1
fi

exit $rc
