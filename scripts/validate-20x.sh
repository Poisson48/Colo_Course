#!/usr/bin/env bash
# 20 passes bureau + 20 passes Android avant release.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PASSES="${1:-20}"
AVD="${AVD:-Qt_API35}"
EMULATOR_PID=""

cleanup() {
  [ -n "$EMULATOR_PID" ] && kill "$EMULATOR_PID" 2>/dev/null || true
}
trap cleanup EXIT

echo "========== $PASSES passes PC =========="
export GUI_TIMEOUT="${GUI_TIMEOUT:-60}"
for i in $(seq 1 "$PASSES"); do
  echo "--- PC passe $i/$PASSES ---"
  "$ROOT/scripts/validate-pc.sh" || { echo "ÉCHEC PC passe $i"; exit 1; }
done
echo "PC: $PASSES/$PASSES OK"

echo "========== Build APK =========="
if [ ! -f "$ROOT/colocourse-arm64.apk" ] || [ "${SKIP_APK_BUILD:-0}" != 1 ]; then
  VERSION_NAME="${VERSION_NAME:-0.27.24-test}" VERSION_CODE="${VERSION_CODE:-999}" \
    bash "$ROOT/scripts/build-android.sh"
fi

echo "========== Émulateur $AVD =========="
adb start-server
if ! adb devices | grep -q 'emulator.*device'; then
  "$HOME/Android/Sdk/emulator/emulator" -avd "$AVD" \
    -no-audio -no-boot-anim -gpu swiftshader_indirect &
  EMULATOR_PID=$!
  adb wait-for-device
  for _ in $(seq 1 120); do
    adb shell getprop sys.boot_completed 2>/dev/null | grep -q 1 && break
    sleep 2
  done
fi

echo "========== $PASSES passes Android =========="
for i in $(seq 1 "$PASSES"); do
  echo "--- Android passe $i/$PASSES ---"
  "$ROOT/scripts/validate-android.sh" || { echo "ÉCHEC Android passe $i"; exit 1; }
done
echo "Android: $PASSES/$PASSES OK"
echo "========== VALIDATION COMPLÈTE =========="
