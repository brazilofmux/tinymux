#!/usr/bin/env bash
# Extract M_() / MN_() / N_() msgids into tinymux.pot (#1473 / #1419 Phase 2a).
# T() and S_() are intentionally not keywords — catalogs stay opt-in.
#
# Translators: mux_vsnprintf supports POSIX %N$ positional arguments (#1623).
# Reorder multi-conversion msgids with %1$s / %2$d as needed; keep the same
# conversion type at each argument index as the msgid.
#
set -euo pipefail

PO_DIR=$(cd "$(dirname "$0")" && pwd)
MUX_DIR=$(cd "$PO_DIR/.." && pwd)
OUT="$PO_DIR/tinymux.pot"

if ! command -v xgettext >/dev/null 2>&1; then
    echo "update-pot: xgettext not found (install gettext)" >&2
    exit 1
fi

# Source trees that may hold player/staff prose. Add paths as modules grow.
mapfile -t SOURCES < <(
    find "$MUX_DIR/include" "$MUX_DIR/lib" "$MUX_DIR/src" "$MUX_DIR/modules" \
        \( -name '*.cpp' -o -name '*.c' -o -name '*.h' \) \
        -print | sort
)

if [[ ${#SOURCES[@]} -eq 0 ]]; then
    echo "update-pot: no sources under $MUX_DIR" >&2
    exit 1
fi

# Paths in the pot are relative to mux/ so they match the tree layout.
cd "$MUX_DIR"
REL_SOURCES=()
for f in "${SOURCES[@]}"; do
    REL_SOURCES+=("${f#"$MUX_DIR"/}")
done

tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT

xgettext \
    --language=C++ \
    --from-code=UTF-8 \
    --keyword=M_ \
    --keyword=MN_:1,2 \
    --keyword=N_ \
    --add-comments=TRANSLATORS \
    --package-name=TinyMUX \
    --package-version=2.14 \
    --msgid-bugs-address=tinymux@googlegroups.com \
    --copyright-holder="TinyMUX contributors" \
    --output="$tmp" \
    --directory="$MUX_DIR" \
    "${REL_SOURCES[@]}"

# Stable header notes for humans (xgettext overwrites COMMENT lines).
{
    echo '# TinyMUX server-message template (#1419 / #1473).'
    echo '# Opt-in M_()/N_() only — not T() casts or S_() softcode ABI.'
    echo '# Regenerate:  make -C mux/po pot   or   ./update-pot.sh'
    echo '#'
    # Drop the first two auto comment lines if present, keep the rest.
    sed '1{/^# /d;}; 2{/^# /d;}' "$tmp"
} > "$OUT"

echo "update-pot: wrote $OUT ($(grep -c '^msgid ' "$OUT" || true) msgid entries)"
