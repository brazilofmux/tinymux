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

# #1879: SetNumFlagsAndName used a fixed 64 KiB stack buffer; the lexer
# accepts names of up to 65535 bytes, so "1:" + name + NUL overflowed.
# Inject a max-length attribute name into a TinyMUX fixture and run the
# conversion paths that re-encode names (Latin-1 / cross-family).
echo "# long attribute name re-encode (#1879)"
python3 - "$fix/t5x-v5.flat" "$tmp/longname-t5x.flat" <<'PY'
import sys
src, dst = sys.argv[1], sys.argv[2]
# 65534 + "1:" + NUL would overflow char[65536]; stay under lexer 65535.
name = "A" * 65534
with open(src, "r", encoding="latin-1") as f:
    lines = f.readlines()
out = []
for line in lines:
    if line.startswith("+N"):
        out.append("+N452\n")
        continue
    if line.startswith("!0"):
        out.append("+A451\n")
        out.append('"%s"\n' % ("1:" + name))
    out.append(line)
with open(dst, "w", encoding="latin-1") as f:
    f.writelines(out)
PY
check "long attr name t5x -> tinymush" -o tinymush "$tmp/longname-t5x.flat"
check "long attr name t5x -> pennmush"  -o pennmush  "$tmp/longname-t5x.flat"
check "long attr name t5x -> rhostmush" -o rhostmush "$tmp/longname-t5x.flat"
check "long attr name t5x -> tinymux v2" -o tinymux -v 2 "$tmp/longname-t5x.flat"

echo "# ---"
echo "# $pass passed, $fail failed"
[ "$fail" -eq 0 ]
