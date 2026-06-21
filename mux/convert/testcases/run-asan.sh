#!/bin/sh
# Memory-safety harness for the Omega converter.
#
# Builds omega with AddressSanitizer and runs every conversion direction,
# extraction, and a maximally color-dense stress input, failing on any ASan
# report (heap/stack overflow, use-after-scope, allocator mismatch, ...).
# This complements run.sh (which checks functional correctness); it is what
# found the allocator-mismatch and stack-use-after-scope bugs in the conversion
# paths.  Requires a compiler with -fsanitize=address.
#
# The ASan binary is built as ../omega.asan so the normal ../omega is left in
# place for run.sh and installs.

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
conv=$here/..
fix=$here/fixtures
asan=$conv/omega.asan

echo "# building omega.asan with AddressSanitizer ..."
( cd "$conv" \
  && make clean >/dev/null 2>&1 \
  && make CXX="g++ -fsanitize=address -fno-omit-frame-pointer" >/tmp/omega-asan-build.log 2>&1 \
  && cp omega omega.asan \
  && make clean >/dev/null 2>&1 \
  && make >/dev/null 2>&1 ) \
  || { echo "Bail out! ASan build failed (see /tmp/omega-asan-build.log)"; exit 1; }

tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp" "$asan"' EXIT

# container-overflow needs an instrumented libstdc++; without one it false-
# positives, so disable just that check.  Everything else stays on.
export ASAN_OPTIONS=abort_on_error=0:detect_leaks=0:detect_container_overflow=0

pass=0
fail=0
check() { # desc, omega-args...
    desc=$1; shift
    "$asan" "$@" "$tmp/out" >/dev/null 2>"$tmp/err"
    if grep -q "ERROR: AddressSanitizer" "$tmp/err"; then
        fail=$((fail + 1)); echo "not ok $((pass + fail)) - $desc"
        grep -m1 "ERROR: AddressSanitizer:" "$tmp/err" | sed 's/^.*AddressSanitizer:/#   /'
    else
        pass=$((pass + 1)); echo "ok $((pass + fail)) - $desc"
    fi
}

echo "# conversions: every family fixture -> every family"
for from in t5x-v5-color p6h-new t6h-3.1 r7h-v7; do
    for to in pennmush tinymush rhostmush tinymux; do
        check "$from -> $to" -o "$to" "$fix/$from.flat"
    done
done

echo "# version migrations on 24-bit color"
for v in 1 2 3 4 5; do
    check "t5x-v5-color -v $v" -v "$v" "$fix/t5x-v5-color.flat"
done

echo "# extraction"
for f in t5x-v5-color p6h-new t6h-3.1; do
    check "extract #1 from $f" -x 1 "$fix/$f.flat"
done

echo "# color-dense stress through the restrict/ANSI/softcode buffers"
check "stress -> pennmush"  -o pennmush  "$fix/t5x-v5-stress.flat"
check "stress -> rhostmush" -o rhostmush "$fix/t5x-v5-stress.flat"
check "stress -> tinymux v2" -o tinymux -v 2 "$fix/t5x-v5-stress.flat"
check "stress extract #1"   -x 1 "$fix/t5x-v5-stress.flat"

echo "# ---"
echo "# $pass passed, $fail failed"
[ "$fail" -eq 0 ]
