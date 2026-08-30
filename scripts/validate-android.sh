#!/usr/bin/env bash
# Une passe de validation Android (émulateur déjà démarré).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APK="${APK:-$ROOT/colocourse-arm64.apk}"
PKG=org.colocourse.app
ACT="$PKG/org.qtproject.qt.android.bindings.QtActivity"
LOG_TAG="ColoAndroidSmoke"

adb devices | grep -q 'device$' || { echo "FAIL: aucun appareil/émulateur"; exit 1; }
[ -f "$APK" ] || { echo "FAIL: APK absent ($APK)"; exit 1; }

adb logcat -c
adb install -r -d "$APK" >/dev/null
adb shell am force-stop "$PKG"
adb shell am start -n "$ACT" >/dev/null

# Attendre chargement catalogue (parse async + réseau).
DEADLINE=$((SECONDS + 90))
COUNT=0
while [ "$SECONDS" -lt "$DEADLINE" ]; do
  sleep 2
  if adb logcat -d -s qt.qpa.*:* RecipeLibrary:* RelayClient:* DEBUG:* 2>/dev/null | grep -q 'SIGSEGV\|Fatal signal'; then
    echo "FAIL: crash Android"
    adb logcat -d | tail -40
    exit 1
  fi
  COUNT=$(adb logcat -d 2>/dev/null | grep 'catalogue prêt' | tail -1 | grep -o '[0-9]* recettes' | grep -o '^[0-9]*' || true)
  [ -n "${COUNT:-}" ] && [ "$COUNT" -ge 1000 ] && break
done

if [ -z "${COUNT:-}" ] || [ "$COUNT" -lt 1000 ]; then
  echo "FAIL: catalogue Android count=${COUNT:-0}"
  adb logcat -d | grep -iE 'RecipeLibrary|recipe|error|fatal' | tail -30
  exit 1
fi

# Navigation Recettes (tap zone header « Recettes » ~200,180 sur 400x780).
adb shell input tap 200 175
sleep 2
adb shell input keyevent KEYCODE_BACK
sleep 1

if adb logcat -d 2>/dev/null | grep -q 'SIGSEGV\|Fatal signal'; then
  echo "FAIL: crash après navigation Recettes"
  exit 1
fi

echo "OK Android — catalogue $COUNT recettes"
