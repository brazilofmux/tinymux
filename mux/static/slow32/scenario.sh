#!/bin/bash
# Run tests/scenario/run.sh -- live sessions over a socket, against netmux
# serving its port from inside the emulator -- with the SLOW-32 netmux
# (build.sh's output) under the SLOW-32 DBT.
#
#   OUT=/some/dir mux/static/slow32/scenario.sh   # the same OUT as build.sh
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-$HERE/out}
R=${SLOW32_ROOT:-$HOME/slow-32}
SCEN=$(cd "$HERE/../../../tests/scenario" && pwd)
EMU=${S32_EMU:-$R/tools/dbt/slow32-dbt}

mkdir -p "$OUT/scenario-bin"
printf '#!/bin/bash\nexec %q %q "$@"\n' "$EMU" "$OUT/netmux.s32x" > "$OUT/scenario-bin/netmux"
chmod +x "$OUT/scenario-bin/netmux"
sed -e "s|^BIN=.*|BIN=$OUT/scenario-bin|" -e "s|^SCRIPT_DIR=.*|SCRIPT_DIR=$SCEN|" "$SCEN/run.sh" > "$OUT/scenario-run.sh"
cd "$SCEN/../.." && exec bash "$OUT/scenario-run.sh" "$@"
