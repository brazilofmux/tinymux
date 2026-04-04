#!/bin/bash
# ---------------------------------------------------------------------------
# run_stress.sh — Run Stage 5a stress test for both backends and compare.
#
# Usage:  cd tests/db && ./run_stress.sh [objects=N] [attrs=N] [ops=N]
# ---------------------------------------------------------------------------

set -euo pipefail

cd "$(dirname "$0")"

if [ ! -x stress_backend ]; then
    echo "Building stress_backend..."
    make stress_backend
fi

PARAMS="${*:-}"

echo "================================================================"
echo "  SQLite Backend"
echo "================================================================"
./stress_backend sqlite $PARAMS | tee stress_sqlite.log
echo ""

echo "================================================================"
echo "  mdbx Backend"
echo "================================================================"
./stress_backend mdbx $PARAMS | tee stress_mdbx.log
echo ""

echo "================================================================"
echo "  Comparison"
echo "================================================================"
echo ""
printf "%-18s %12s %12s %8s\n" "Phase" "SQLite" "mdbx" "Ratio"
printf "%-18s %12s %12s %8s\n" "----------" "--------" "--------" "-----"

for tag in STRESS_POPULATE STRESS_READ STRESS_WRITE STRESS_DELETE STRESS_GETALL; do
    s_usop=$(grep "^$tag:" stress_sqlite.log 2>/dev/null | awk '{print $(NF-1)}')
    m_usop=$(grep "^$tag:" stress_mdbx.log 2>/dev/null | awk '{print $(NF-1)}')

    if [ -n "$s_usop" ] && [ -n "$m_usop" ]; then
        ratio=$(awk "BEGIN{if($s_usop>0) printf \"%.2fx\",$m_usop/$s_usop; else print \"N/A\"}")
        printf "%-18s %10s us %10s us %8s\n" "$tag" "$s_usop" "$m_usop" "$ratio"
    fi
done

echo ""
rm -f stress_sqlite.log stress_mdbx.log
