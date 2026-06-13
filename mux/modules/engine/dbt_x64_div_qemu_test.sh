#!/bin/sh
# dbt_x64_div_qemu_test.sh — verify the x86-64 DBT idiv/rem guards (#811)
# by executing the real emitted machine code under qemu-x86_64.
#
# The x86-64 idiv traps (#DE -> SIGFPE) on divide-by-zero and INT_MIN/-1;
# RV64 defines both without trapping, so the emitter guards them.  Those
# guards can only be *executed* on x86-64 — on an AArch64 dev box the JIT
# uses the a64 backend, so this script cross-compiles a tiny harness and
# runs the exact emitted bytes under qemu.  See dbt_x64_div_gen.cpp and
# dbt_x64_div_harness.c.
#
# Exit: 0 = pass (or skipped — toolchain absent), 1 = failure/trap.

set -eu
here=$(cd "$(dirname "$0")" && pwd)
inc="$here/../../include"

host_cxx=${CXX:-g++}
cross_cc=$(command -v x86_64-linux-gnu-gcc || true)
qemu=$(command -v qemu-x86_64-static || command -v qemu-x86_64 || true)

if [ -z "$cross_cc" ] || [ -z "$qemu" ]; then
    echo "SKIP: need x86_64-linux-gnu-gcc and qemu-x86_64(-static)" \
         "(apt install gcc-x86-64-linux-gnu qemu-user-static)"
    exit 0
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

echo "Generating x86-64 emitter byte blobs (native)..."
"$host_cxx" -std=c++17 -O2 -I"$inc" -o "$work/gen" "$here/dbt_x64_div_gen.cpp"
"$work/gen" "$work"

echo "Cross-compiling harness for x86-64..."
"$cross_cc" -O0 -static -o "$work/harness" "$here/dbt_x64_div_harness.c"

echo "Running under $(basename "$qemu")..."
"$qemu" "$work/harness" "$work"
