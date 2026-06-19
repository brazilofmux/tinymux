#!/bin/bash
#   benchsetup.sh — Build an isolated game DB for JIT perf benchmarking.
#   Creates testcases/bench.d/ from smoke.flat (player #1 is a wizard).
#   Run once; then drive workloads with: tools/benchrun.sh < workload
set -euo pipefail
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
TC=$(cd "$SCRIPT_DIR/.." && pwd)
cd "$TC"
BIN=../mux/game/bin
DATA=./bench.d
export LD_LIBRARY_PATH="$(cd "$BIN" && pwd)"

[ -r smoke.flat ] || { echo "ERROR: smoke.flat missing — run Makesmoke"; exit 1; }

rm -rf "$DATA"; mkdir "$DATA"
[ -e bin ] || ln -s "$BIN" bin
cp ../mux/game/alias.conf . 2>/dev/null || true
cp ../mux/game/compat.conf . 2>/dev/null || true

cat > bench.conf <<'_EOF'
input_database	bench.d/bench.db
output_database	bench.d/bench.db.new
crash_database	bench.d/bench.db.CRASH
mail_database   bench.d/mail.db
comsys_database bench.d/comsys.db
port 2999
mud_name BenchMUX
command_quota_increment 10000000
command_quota_max 10000000
player_queue_limit 100000000
include alias.conf
include compat.conf
module exp3
module comsys_mod
module mail_mod
_EOF

"$BIN/dbconvert" -d "$DATA/bench" -i smoke.flat -l > bench_import.log 2>&1 \
    && echo "bench.d ready (player #1 wizard, port 2999)" \
    || { echo "import failed"; cat bench_import.log; exit 1; }
