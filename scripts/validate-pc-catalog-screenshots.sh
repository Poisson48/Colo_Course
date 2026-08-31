#!/usr/bin/env bash
# Validation visuelle PC du catalogue recettes (captures d'écran + assertions QML).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build}"
BIN="$BUILD/tests/tst_qml"
SHOT_DIR="${COLO_SHOT_DIR:-$ROOT/tmp/catalog-shots}"

[ -x "$BIN" ] || { echo "FAIL: $BIN absent — cmake --build build --target tst_qml"; exit 1; }

rm -rf "$SHOT_DIR"
mkdir -p "$SHOT_DIR"

echo "== Captures catalogue → $SHOT_DIR =="
set +e
QT_QPA_PLATFORM=offscreen COLO_SHOT_DIR="$SHOT_DIR" \
  "$BIN" test_recipesCatalogScreenshots
RC=$?
set -e
if [ "$RC" -ne 0 ]; then
  echo "FAIL: test_recipesCatalogScreenshots (code $RC)"
  exit 1
fi

echo ""
echo "Captures générées :"
ls -la "$SHOT_DIR"/*.png

echo ""
echo "OK — catalogue validé par capture d'écran PC"
