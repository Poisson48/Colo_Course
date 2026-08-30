#!/usr/bin/env bash
# Une passe de validation bureau : tests unitaires + fumée GUI (Xvfb).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build}"
BIN="$BUILD/src/colocourse"
LOG="$(mktemp -t colo-pc-smoke.XXXXXX.log)"
GUI_TIMEOUT="${GUI_TIMEOUT:-60}"

cleanup() { rm -f "$LOG"; }
trap cleanup EXIT

[ -x "$BIN" ] || { echo "FAIL: binaire absent ($BIN)"; exit 1; }

echo "== ctest =="
ctest --test-dir "$BUILD" --output-on-failure -j4

echo "== GUI Xvfb (${GUI_TIMEOUT}s) =="
QT_LOGGING_RULES="*.warning=true" xvfb-run -a -s "-screen 0 400x780x24" \
  timeout "$GUI_TIMEOUT" "$BIN" >"$LOG" 2>&1 || true

grep -q 'SIGSEGV\|ASSERT\|Fatal' "$LOG" && {
  echo "FAIL: crash dans les logs"
  tail -30 "$LOG"
  exit 1
}

COUNT=$(grep -o 'catalogue prêt : [0-9]*' "$LOG" | tail -1 | grep -o '[0-9]*$' || true)
if [ -z "${COUNT:-}" ] || [ "$COUNT" -lt 1000 ]; then
  echo "FAIL: catalogue non chargé (count=${COUNT:-0})"
  tail -20 "$LOG"
  exit 1
fi

grep -q 'RelayClient.*connected' "$LOG" || echo "WARN: relais non connecté (réseau ?)"

echo "OK PC — catalogue $COUNT recettes"
