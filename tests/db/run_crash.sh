#!/bin/bash
# ---------------------------------------------------------------------------
# run_crash.sh — Run Stage 5b crash recovery test for both backends.
#
# Usage:  cd tests/db && ./run_crash.sh [objects=N] [attrs=N] [inflight=N]
# ---------------------------------------------------------------------------

set -euo pipefail

cd "$(dirname "$0")"

if [ ! -x crash_backend ]; then
    echo "Building crash_backend..."
    make crash_backend
fi

PARAMS="${*:-}"
FAIL=0

echo "================================================================"
echo "  SQLite Crash Recovery"
echo "================================================================"
if LD_LIBRARY_PATH=../../mux/lib ./crash_backend sqlite $PARAMS; then
    echo ""
else
    echo ""
    FAIL=1
fi

echo "================================================================"
echo "  mdbx Crash Recovery"
echo "================================================================"
if LD_LIBRARY_PATH=../../mux/lib ./crash_backend mdbx $PARAMS; then
    echo ""
else
    echo ""
    FAIL=1
fi

if [ "$FAIL" -eq 0 ]; then
    echo "=== Both backends: PASSED ==="
else
    echo "=== One or more backends: FAILED ==="
    exit 1
fi
