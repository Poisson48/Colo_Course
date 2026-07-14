#!/usr/bin/env bash
# Build + signature de l'APK arm64.
# Prérequis local : bash scripts/setup-android.sh   (en CI : job "android" de ci.yml)
#
# Variables utiles :
#   SDK_ROOT / QT_ROOT / QT_VER / NDK_VER / PREFIX   chemins du kit
#   VERSION_NAME / VERSION_CODE                      version de l'APK
#   KEYSTORE / KEYALIAS / STOREPASS                  clé de signature (défaut : debug)
#   OUT                                              chemin de l'APK final
set -euo pipefail

SDK_ROOT="${SDK_ROOT:-$HOME/Android/Sdk}"
NDK_VER="${NDK_VER:-26.1.10909125}"
QT_VER="${QT_VER:-6.8.2}"
QT_ROOT="${QT_ROOT:-$HOME/Qt}"
QT_ANDROID="${QT_ANDROID:-$QT_ROOT/$QT_VER/android_arm64_v8a}"
QT_HOST="${QT_HOST:-$QT_ROOT/$QT_VER/gcc_64}"
PREFIX="${PREFIX:-$HOME/android-libs/arm64-v8a}"
BUILD_TOOLS_VER="${BUILD_TOOLS_VER:-35.0.0}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${OUT:-$ROOT/colocourse-arm64.apk}"

export ANDROID_SDK_ROOT="$SDK_ROOT"
export ANDROID_NDK_ROOT="${ANDROID_NDK_ROOT:-$SDK_ROOT/ndk/$NDK_VER}"
export PKG_CONFIG_LIBDIR="$PREFIX/lib/pkgconfig"
export JAVA_HOME="${JAVA_HOME:-/usr/lib/jvm/java-17-openjdk-amd64}"

# La version doit être cohérente entre le manifest (lu par aapt) et les propriétés
# CMake (lues par androiddeployqt) : on aligne les deux sur VERSION_NAME/VERSION_CODE.
VERSION_NAME="${VERSION_NAME:-0.1.0}"
VERSION_CODE="${VERSION_CODE:-1}"
sed -i -E \
  -e "s/android:versionName=\"[^\"]*\"/android:versionName=\"$VERSION_NAME\"/" \
  -e "s/android:versionCode=\"[^\"]*\"/android:versionCode=\"$VERSION_CODE\"/" \
  "$ROOT/android/AndroidManifest.xml"

"$QT_ANDROID/bin/qt-cmake" \
  -S "$ROOT" -B "$ROOT/build-android" -G Ninja \
  -DQT_HOST_PATH="$QT_HOST" \
  -DANDROID_SDK_ROOT="$SDK_ROOT" \
  -DANDROID_NDK_ROOT="$ANDROID_NDK_ROOT" \
  -DCMAKE_FIND_ROOT_PATH="$PREFIX" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCOLO_VERSION_NAME="$VERSION_NAME" \
  -DCOLO_VERSION_CODE="$VERSION_CODE"

cmake --build "$ROOT/build-android" --target apk -j"$(nproc)"

UNSIGNED="$(find "$ROOT/build-android" -name '*-release-unsigned.apk' | head -1)"
[ -n "$UNSIGNED" ] || { echo "APK non signé introuvable dans build-android/" >&2; exit 1; }

# Android refuse d'installer un APK non signé. Le keystore de debug suffit pour
# installer sur son propre téléphone ; ce n'est PAS une clé de publication Play Store.
KEYSTORE="${KEYSTORE:-$HOME/.android/debug.keystore}"
KEYALIAS="${KEYALIAS:-androiddebugkey}"
STOREPASS="${STOREPASS:-android}"
if [ ! -f "$KEYSTORE" ]; then
  echo "== keystore de debug généré : $KEYSTORE =="
  mkdir -p "$(dirname "$KEYSTORE")"
  keytool -genkeypair -keystore "$KEYSTORE" -alias "$KEYALIAS" \
    -storepass "$STOREPASS" -keypass "$STOREPASS" \
    -keyalg RSA -keysize 2048 -validity 10000 \
    -dname "CN=Colo Course Debug, O=ColoCourse, C=FR" >/dev/null
fi

BT="$SDK_ROOT/build-tools/$BUILD_TOOLS_VER"
"$BT/zipalign" -f -p 4 "$UNSIGNED" "$ROOT/build-android/aligned.apk"
"$BT/apksigner" sign \
  --ks "$KEYSTORE" --ks-key-alias "$KEYALIAS" \
  --ks-pass "pass:$STOREPASS" --key-pass "pass:$STOREPASS" \
  --out "$OUT" "$ROOT/build-android/aligned.apk"
"$BT/apksigner" verify "$OUT"

echo
echo "APK signé : $OUT  (v$VERSION_NAME, code $VERSION_CODE)"
echo "Installation : adb install -r $OUT"
