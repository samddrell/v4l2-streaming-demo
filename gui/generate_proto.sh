#!/usr/bin/env bash
# Generates gui/telemetry_pb2.py from proto/telemetry.proto.
#
# Requires `protoc` on PATH, version-matched to the `protobuf` pip package
# installed from requirements.txt (design.md §2 reproducibility requirement —
# a protoc/runtime version mismatch is a known failure mode, test-plan.md
# TC-WIRE-01). The generated file is intentionally not checked in
# (design.md §8) — run this after every fresh checkout/proto change.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROTO_DIR="$SCRIPT_DIR/../proto"
PROTO_FILE="$PROTO_DIR/telemetry.proto"

if ! command -v protoc >/dev/null 2>&1; then
  echo "protoc not found on PATH. Install it (matching your 'protobuf' pip package version) before running this script." >&2
  exit 1
fi

protoc --python_out="$SCRIPT_DIR" -I "$PROTO_DIR" "$PROTO_FILE"
echo "Generated $SCRIPT_DIR/telemetry_pb2.py"
