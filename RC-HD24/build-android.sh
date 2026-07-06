#!/bin/bash
# ============================================================================
# Compila RC-HD24 para Android (APK) usando el kit Qt para Android.
#
# Requisitos (ya detectados en esta Mac):
#   - Kit Qt Android   : ~/Qt/6.9.3/android_arm64_v8a  (aqt install-qt ...)
#   - Qt host (macOS)  : /opt/homebrew/opt/qt          (misma version 6.9.3)
#   - Android SDK      : ~/Library/Android/sdk         (de Android Studio)
#   - NDK              : 27.0.12077973
#   - JDK              : el que trae Android Studio (jbr)
#
# Uso:
#   ./build-android.sh            # genera el APK debug
#   ./build-android.sh install    # ademas lo instala por adb en el device
# ============================================================================
set -e

QT_ANDROID="${QT_ANDROID:-$HOME/Qt/6.9.3/android_arm64_v8a}"
QT_HOST="${QT_HOST:-/opt/homebrew/opt/qt}"
ANDROID_SDK="${ANDROID_SDK_ROOT:-$HOME/Library/Android/sdk}"
ANDROID_NDK="${ANDROID_NDK_ROOT:-$ANDROID_SDK/ndk/27.0.12077973}"
JDK="${JAVA_HOME:-/Applications/Android Studio.app/Contents/jbr/Contents/Home}"

BUILD_DIR=build-android

echo "=== RC-HD24 build Android ==="
echo "Qt Android : $QT_ANDROID"
echo "Qt host    : $QT_HOST"
echo "SDK        : $ANDROID_SDK"
echo "NDK        : $ANDROID_NDK"
echo "JDK        : $JDK"
echo ""

for p in "$QT_ANDROID/bin/qt-cmake" "$ANDROID_SDK/platform-tools" "$ANDROID_NDK" "$JDK/bin/java"; do
    if [ ! -e "$p" ]; then
        echo "ERROR: no existe $p"
        exit 1
    fi
done

export JAVA_HOME="$JDK"
export ANDROID_SDK_ROOT="$ANDROID_SDK"

mkdir -p "$BUILD_DIR"

"$QT_ANDROID/bin/qt-cmake" -S . -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DQT_HOST_PATH="$QT_HOST" \
    -DANDROID_SDK_ROOT="$ANDROID_SDK" \
    -DANDROID_NDK_ROOT="$ANDROID_NDK" \
    -DQT_ANDROID_BUILD_ALL_ABIS=OFF

cmake --build "$BUILD_DIR" --target apk

APK="$BUILD_DIR/android-build/build/outputs/apk/debug/android-build-debug.apk"
echo ""
echo "=== APK generado ==="
ls -lh "$APK"

if [ "$1" = "install" ]; then
    echo ""
    echo "=== Instalando en el dispositivo (adb) ==="
    "$ANDROID_SDK/platform-tools/adb" install -r "$APK"
fi

echo ""
echo "Para instalar a mano:"
echo "  adb install -r $APK"
echo ""
echo "El proyecto Gradle queda en $BUILD_DIR/android-build/"
echo "(se puede abrir en Android Studio: File > Open > esa carpeta)"
