#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SDK=${1:?Usage: scripts/import_sdk.sh /path/to/HCNetSDK}
[[ -f "$SDK/include/HCNetSDK.h" && -f "$SDK/lib/libhcnetsdk.so" ]] || { echo "HCNetSDK not found: $SDK" >&2; exit 1; }
mkdir -p "$ROOT/third_party/HCNetSDK"
cp -a "$SDK/include" "$SDK/lib" "$ROOT/third_party/HCNetSDK/"
echo "SDK imported."
