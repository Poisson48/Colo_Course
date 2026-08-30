#!/usr/bin/env bash
# Une passe de validation Android (émulateur déjà démarré).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APK="${APK:-$ROOT/colocourse-arm64.apk}"
PKG=org.colocourse.app
ACT="$PKG/org.qtproject.qt.android.bindings.QtActivity"
DEADLINE="${ANDROID_DEADLINE:-420}"
CLEAR_DATA="${ANDROID_CLEAR_DATA:-0}"

adb devices | grep -q 'device$' || { echo "FAIL: aucun appareil/émulateur"; exit 1; }
[ -f "$APK" ] || { echo "FAIL: APK absent ($APK)"; exit 1; }

adb logcat -c
adb install -r -d "$APK" >/dev/null
if [ "$CLEAR_DATA" = "1" ]; then
  adb shell pm clear "$PKG" >/dev/null 2>&1 || true
fi
adb shell am start -n "$ACT" >/dev/null

# Premier lancement : nom + bandeau MAJ éventuel.
sleep 4
adb shell input text "Testeur" 2>/dev/null || true
adb shell input keyevent 66 2>/dev/null || true
sleep 1
adb shell input tap 375 72 2>/dev/null || true
sleep 2

START=$SECONDS
COUNT=0
while [ $((SECONDS - START)) -lt "$DEADLINE" ]; do
  sleep 5
  if adb logcat -d 2>/dev/null | grep -qE 'Fatal signal|SIGSEGV'; then
    echo "FAIL: crash Android"
    adb logcat -d | tail -30
    exit 1
  fi
  COUNT=$(adb logcat -d 2>/dev/null \
    | grep -oE 'catalogue prêt : [0-9]+' | tail -1 | grep -oE '[0-9]+$' || true)
  if [ -z "${COUNT:-}" ]; then
    COUNT=$(adb logcat -d 2>/dev/null \
      | grep 'default.*RecipeLibrary' | grep -oE '[0-9]+ recettes' | tail -1 | grep -oE '^[0-9]+' || true)
  fi
  if [ -n "${COUNT:-}" ] && [ "$COUNT" -ge 1000 ]; then
    break
  fi
done

if [ -z "${COUNT:-}" ] || [ "$COUNT" -lt 1000 ]; then
  # Repli : dump UI (onglet Catalogue avec un nombre).
  adb shell uiautomator dump /sdcard/colo-ui.xml >/dev/null 2>&1 || true
  UI=$(adb shell cat /sdcard/colo-ui.xml 2>/dev/null || true)
  if echo "$UI" | grep -qE 'Catalogue \([0-9]{3,}\)'; then
    COUNT=$(echo "$UI" | grep -oE 'Catalogue \([0-9]+' | head -1 | grep -oE '[0-9]+')
  fi
fi

if [ -z "${COUNT:-}" ] || [ "$COUNT" -lt 1000 ]; then
  echo "FAIL: catalogue Android count=${COUNT:-0} (${SECONDS}s)"
  adb logcat -d | grep -iE 'recipe|catalog|chargement|warning|error' | tail -25
  exit 1
fi

adb shell input tap 200 175
sleep 2
adb shell input keyevent 4
sleep 1

if adb logcat -d 2>/dev/null | grep -qE 'Fatal signal|SIGSEGV'; then
  echo "FAIL: crash après navigation Recettes"
  exit 1
fi

echo "OK Android — catalogue $COUNT recettes (${SECONDS}s)"
