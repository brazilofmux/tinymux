#!/bin/bash
# Run testcases/tools/Smoke against the SLOW-32 build (build.sh's output),
# each program under the SLOW-32 DBT.  netmux runs as dbconvert when its
# name says so, and the emulator hands the guest the image's path as
# argv[0], so the images are reached through links named for the part.
#
#   OUT=/some/dir mux/static/slow32/smoke.sh      # the same OUT as build.sh
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-$HERE/out}
R=${SLOW32_ROOT:-$HOME/slow-32}
TC=$(cd "$HERE/../../../testcases" && pwd)
EMU=${S32_EMU:-$R/tools/dbt/slow32-dbt}

mkdir -p "$OUT/img" "$OUT/bin" "$OUT/ws"
ln -sf "$OUT/netmux.s32x" "$OUT/img/dbconvert"
ln -sf "$OUT/muxscript.s32x" "$OUT/img/muxscript"
for p in dbconvert muxscript; do
    printf '#!/bin/bash\nexec %q %q "$@"\n' "$EMU" "$OUT/img/$p" > "$OUT/bin/$p"
    chmod +x "$OUT/bin/$p"
done
# Smoke with its BIN (and its testcases directory) pointed here
sed -e "s|^TC=.*|TC=$TC|" -e "s|^BIN=.*|BIN=$OUT/bin|" "$TC/tools/Smoke" > "$OUT/Smoke"
chmod +x "$OUT/Smoke"
cp "$TC/smoke.flat" "$OUT/ws/"
cd "$OUT/ws" && exec "$OUT/Smoke" --workspace "$OUT/ws" "$@"
