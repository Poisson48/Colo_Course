#!/usr/bin/env bash
# Installe le kit de build Android pour Colo_Course (sans sudo) :
#   - Android SDK (cmdline-tools, platform-tools, platform 35, build-tools, NDK r26b)
#   - Qt pour Android + Qt hôte (via aqtinstall dans un venv)
#   - libsodium et libsecp256k1 cross-compilées pour arm64-v8a
# Usage : bash scripts/setup-android.sh   (~7 Go de téléchargements)
set -euo pipefail

SDK_ROOT="$HOME/Android/Sdk"
NDK_VER="26.1.10909125"
QT_VER="6.8.2"
QT_ROOT="$HOME/Qt"
PREFIX="$HOME/android-libs/arm64-v8a"
API=24
JOBS="$(nproc)"

echo "== 1/5 Android cmdline-tools =="
if [ ! -x "$SDK_ROOT/cmdline-tools/latest/bin/sdkmanager" ]; then
  mkdir -p "$SDK_ROOT/cmdline-tools"
  cd /tmp
  curl -fLO https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip
  # python3 -m zipfile : pas besoin d'unzip, mais il perd les bits d'exécution
  python3 -m zipfile -e commandlinetools-linux-11076708_latest.zip "$SDK_ROOT/cmdline-tools"
  rm -f commandlinetools-linux-11076708_latest.zip
  mv "$SDK_ROOT/cmdline-tools/cmdline-tools" "$SDK_ROOT/cmdline-tools/latest"
  chmod +x "$SDK_ROOT/cmdline-tools/latest/bin/"*
fi
SDKMANAGER="$SDK_ROOT/cmdline-tools/latest/bin/sdkmanager"

echo "== 2/5 SDK + NDK =="
yes | "$SDKMANAGER" --licenses >/dev/null || true
"$SDKMANAGER" --install \
  "platform-tools" \
  "platforms;android-35" \
  "build-tools;35.0.0" \
  "ndk;$NDK_VER" >/dev/null
NDK="$SDK_ROOT/ndk/$NDK_VER"

echo "== 3/5 Qt $QT_VER (hôte + android_arm64_v8a) =="
if [ ! -d "$HOME/.venvs/aqt" ]; then
  python3 -m venv "$HOME/.venvs/aqt"
  "$HOME/.venvs/aqt/bin/pip" -q install aqtinstall
fi
AQT="$HOME/.venvs/aqt/bin/aqt"
HOST_ARCH="$($AQT list-qt linux desktop --arch $QT_VER | tr ' ' '\n' | grep -m1 gcc_64)"
[ -d "$QT_ROOT/$QT_VER/gcc_64" ] || "$AQT" install-qt linux desktop "$QT_VER" "$HOST_ARCH" -O "$QT_ROOT"
[ -d "$QT_ROOT/$QT_VER/android_arm64_v8a" ] || "$AQT" install-qt linux android "$QT_VER" android_arm64_v8a -O "$QT_ROOT"

TC="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"

echo "== 4/5 libsodium (arm64) =="
if [ ! -f "$PREFIX/lib/libsodium.a" ]; then
  cd /tmp
  curl -fLO https://download.libsodium.org/libsodium/releases/libsodium-1.0.20.tar.gz
  tar xzf libsodium-1.0.20.tar.gz && cd libsodium-1.0.20
  ./configure --host=aarch64-linux-android --prefix="$PREFIX" \
    --disable-shared --enable-static --with-pic \
    CC="$TC/aarch64-linux-android${API}-clang" >/dev/null
  make -j"$JOBS" >/dev/null && make install >/dev/null
fi

echo "== 5/5 libsecp256k1 (arm64) =="
if [ ! -f "$PREFIX/lib/libsecp256k1.a" ]; then
  cd /tmp
  curl -fL -o secp256k1-0.7.0.tar.gz https://github.com/bitcoin-core/secp256k1/archive/refs/tags/v0.7.0.tar.gz
  tar xzf secp256k1-0.7.0.tar.gz && cd secp256k1-0.7.0
  cmake -B build-android -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=$API \
    -DSECP256K1_ENABLE_MODULE_SCHNORRSIG=ON -DSECP256K1_ENABLE_MODULE_EXTRAKEYS=ON \
    -DSECP256K1_BUILD_TESTS=OFF -DSECP256K1_BUILD_EXHAUSTIVE_TESTS=OFF \
    -DSECP256K1_BUILD_BENCHMARK=OFF -DSECP256K1_BUILD_CTIME_TESTS=OFF \
    -DBUILD_SHARED_LIBS=OFF -DCMAKE_INSTALL_PREFIX="$PREFIX" >/dev/null
  cmake --build build-android -j"$JOBS" >/dev/null
  cmake --install build-android >/dev/null
fi

echo
echo "OK — kit Android prêt :"
echo "  SDK   : $SDK_ROOT"
echo "  NDK   : $NDK"
echo "  Qt    : $QT_ROOT/$QT_VER/{gcc_64,android_arm64_v8a}"
echo "  Libs  : $PREFIX"
echo "Build de l'APK : bash scripts/build-android.sh"
