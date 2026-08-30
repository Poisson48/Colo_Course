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

echo "========== Émulateur $AVD (Xvfb) =========="
export ANDROID_EMU_ENABLE_CRASH_REPORTING=0
adb start-server
if ! adb devices | grep -q 'emulator.*device'; then
  xvfb-run -a -s "-screen 0 1280x720x24" "$HOME/Android/Sdk/emulator/emulator" -avd "$AVD" \
    -no-window -no-audio -no-boot-anim -gpu swiftshader_indirect &
  for _ in $(seq 1 90); do
    adb devices | grep -q 'emulator.*device' && break
    sleep 2
  done
  for _ in $(seq 1 60); do
    adb shell getprop sys.boot_completed 2>/dev/null | grep -q 1 && break
    sleep 2
  done
fi

echo "========== $PASSES passes Android =========="
export ANDROID_DEADLINE="${ANDROID_DEADLINE:-420}"
for i in $(seq 1 "$PASSES"); do
  echo "--- Android passe $i/$PASSES ---"
  if [ "$i" -eq 1 ]; then
    ANDROID_CLEAR_DATA=1 "$ROOT/scripts/validate-android.sh" || { echo "ÉCHEC Android passe $i"; exit 1; }
  else
    ANDROID_CLEAR_DATA=0 "$ROOT/scripts/validate-android.sh" || { echo "ÉCHEC Android passe $i"; exit 1; }
  fi
done
echo "Android: $PASSES/$PASSES OK"
echo "========== VALIDATION COMPLÈTE =========="
