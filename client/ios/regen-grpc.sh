#!/usr/bin/env bash
# Regenerate Swift protobuf + gRPC stubs for Titan from mux/proxy/hydra.proto.
#
# Output: client/ios/Titan/Generated/hydra.pb.swift and hydra.grpc.swift
#
# Requires (brew install):
#   protobuf                — protoc
#   swift-protobuf          — protoc-gen-swift (1.x)
#   protoc-gen-grpc-swift   — grpc-swift v2 plugin (binary: protoc-gen-grpc-swift-2)

set -euo pipefail

repo_root=$(cd "$(dirname "$0")/../.." && pwd)
proto_dir="$repo_root/mux/proxy"
out_dir="$repo_root/client/ios/Titan/Generated"

if ! command -v protoc >/dev/null 2>&1; then
    echo "error: protoc not found — brew install protobuf" >&2
    exit 1
fi
if ! command -v protoc-gen-swift >/dev/null 2>&1; then
    echo "error: protoc-gen-swift not found — brew install swift-protobuf" >&2
    exit 1
fi

# brew installs the v2 plugin as protoc-gen-grpc-swift-2; protoc looks for
# protoc-gen-grpc-swift, so we point it at the v2 binary explicitly.
plugin_path=$(command -v protoc-gen-grpc-swift-2 || true)
if [ -z "$plugin_path" ]; then
    echo "error: protoc-gen-grpc-swift-2 not found — brew install protoc-gen-grpc-swift" >&2
    exit 1
fi

mkdir -p "$out_dir"

protoc \
    --proto_path="$proto_dir" \
    --swift_out="$out_dir" \
    --plugin=protoc-gen-grpc-swift="$plugin_path" \
    --grpc-swift_out="$out_dir" \
    "$proto_dir/hydra.proto"

echo "Regenerated:"
echo "  $out_dir/hydra.pb.swift"
echo "  $out_dir/hydra.grpc.swift"
