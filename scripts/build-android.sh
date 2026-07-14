#!/usr/bin/env bash
# Build de l'APK debug arm64. Prérequis : bash scripts/setup-android.sh
set -euo pipefail

SDK_ROOT="$HOME/Android/Sdk"
NDK_VER="26.1.10909125"
QT_VER="6.8.2"
QT_ROOT="$HOME/Qt"
PREFIX="$HOME/android-libs/arm64-v8a"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

export ANDROID_SDK_ROOT="$SDK_ROOT"
export ANDROID_NDK_ROOT="$SDK_ROOT/ndk/$NDK_VER"
export PKG_CONFIG_LIBDIR="$PREFIX/lib/pkgconfig"
export JAVA_HOME="${JAVA_HOME:-/usr/lib/jvm/java-17-openjdk-amd64}"

"$QT_ROOT/$QT_VER/android_arm64_v8a/bin/qt-cmake" \
  -S "$ROOT" -B "$ROOT/build-android" -G Ninja \
  -DQT_HOST_PATH="$QT_ROOT/$QT_VER/gcc_64" \
  -DANDROID_SDK_ROOT="$SDK_ROOT" \
  -DANDROID_NDK_ROOT="$ANDROID_NDK_ROOT" \
  -DCMAKE_FIND_ROOT_PATH="$PREFIX" \
  -DCMAKE_BUILD_TYPE=Release

cmake --build "$ROOT/build-android" --target apk -j"$(nproc)"

APK="$(find "$ROOT/build-android" -name '*.apk' | head -1)"
echo
echo "APK : $APK"
