#!/bin/sh
# websocket_test.sh — build and run the WebSocket frame-parser unit test
# (websocket_test.cpp) against the real compiled parser.
#
# Verifies the zero-payload-CLOSE-at-read-boundary fix and basic framing.
# Links netmux-websocket.o + libmux and stubs the netmux-layer callbacks.
# SKIPs cleanly (exit 0) if the build artifacts aren't present — run
# `make install` from the repo root first.
#
# Exit: 0 = pass (or skipped), 1 = failure.

set -eu
here=$(cd "$(dirname "$0")" && pwd)
cd "$here"

obj=netmux-websocket.o
if [ ! -f "$obj" ] || [ ! -f ../lib/libmux.so ]; then
    echo "SKIP: build the tree first (make install) — need $obj and libmux"
    exit 0
fi

cxx=${CXX:-g++}
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT

libdir=$(cd "$here/../lib" && pwd)
"$cxx" -std=c++17 -DHAVE_CONFIG_H -I. -I../include -I../modules -I../announce \
    -I../ganl/include -I../sqlite -DTINYMUX_JIT -DSSL_ENABLED \
    -o "$out/websocket_test" websocket_test.cpp "$obj" \
    -L"$libdir" -Wl,-rpath,"$libdir" -lmux -lcrypto -lssl -lm -lcrypt -lpcre2-8

# Prefer the tree we linked — ambient LD_LIBRARY_PATH to another libmux
# (common on multi-tree developer machines) yields missing mux_snprintf.
LD_LIBRARY_PATH="$libdir${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    "$out/websocket_test"
