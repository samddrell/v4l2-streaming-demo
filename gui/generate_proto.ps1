# Generates gui/telemetry_pb2.py from proto/telemetry.proto.
#
# Requires `protoc` on PATH, version-matched to the `protobuf` pip package
# installed from requirements.txt (design.md §2 reproducibility requirement —
# a protoc/runtime version mismatch is a known failure mode, test-plan.md
# TC-WIRE-01). The generated file is intentionally not checked in
# (design.md §8) — run this after every fresh checkout/proto change.

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProtoDir = Join-Path $ScriptDir "..\proto"
$ProtoFile = Join-Path $ProtoDir "telemetry.proto"

if (-not (Get-Command protoc -ErrorAction SilentlyContinue)) {
    Write-Error "protoc not found on PATH. Install it (matching your 'protobuf' pip package version) before running this script."
    exit 1
}

protoc --python_out=$ScriptDir -I $ProtoDir $ProtoFile
Write-Host "Generated $ScriptDir\telemetry_pb2.py"
