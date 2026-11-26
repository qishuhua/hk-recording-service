#!/usr/bin/env bash
set -euo pipefail
BASE=${BASE_URL:-http://127.0.0.1:8181}
curl -fsS "$BASE/healthz"; echo
out=$(mktemp); trap 'rm -f "$out"' EXIT
status=$(curl -sS -o "$out" -w '%{http_code}' -H 'Content-Type: application/json' -d '{}' "$BASE/recordings")
[[ "$status" == 400 ]] || { cat "$out"; echo "Expected 400, got $status" >&2; exit 1; }
echo 'HTTP smoke test passed.'
