#!/bin/bash
# macOS build: configure + compile with Ninja, bundle the .dylib dependencies
# into the .app, then stage the result under dist/macos/.
#
#     ./build_macos.sh                       the playground   -> dist/macos/
#     CONUI_APP=skeleton ./build_macos.sh    the fork template -> dist/macos-skeleton/
#     CONUI_ABOUT_CAT=OFF ./build_macos.sh   the playground without the ABOUT cat
#
# UNVERIFIED: written for Homebrew + dylibbundler + iconutil, and never run
# on real hardware.
# Only the Windows path (build.sh) has actually been run.
# Treat this as a documented starting point, not a tested pipeline.
#
# Where things live in the bundle and outside it:
#   ConUIApp.app/Contents/Resources/resources.dat   read-only, from CMake
#   ~/Library/Application Support/ConUI/data.bin    written by the app
#   ~/Library/Application Support/ConUI/themes/     written at first run from
#                                                   the compiled-in themes
# themes/ is deliberately NOT copied into the bundle: a signed bundle must not
# be written to, and the theme editor saves themes. The built-ins are compiled
# into the binary (docs/DESIGN_SYSTEM.md §15), so nothing is lost.
set -e

APP="${CONUI_APP:-playground}"
if [ "$APP" = "skeleton" ]; then
    BUILD_NAME=build_macos-skeleton; DIST_NAME=macos-skeleton
else
    BUILD_NAME=build_macos; DIST_NAME=macos
fi

check_dependency() {
    if ! brew list "$1" &>/dev/null; then
        echo "Installing $1 via Homebrew..."
        brew install "$1"
    fi
}

echo "--- Checking macOS dependencies ---"
if ! command -v brew &> /dev/null; then
    echo "Error: Homebrew is required on macOS but was not found."
    echo "Please install it from https://brew.sh/"
    exit 1
fi

# GLEW is gone (GL entry points are loaded by hand, src/Render/GLLoader.cpp):
# GLFW is the only third-party library.
check_dependency glfw
check_dependency cmake
check_dependency ninja
check_dependency imagemagick
if ! command -v dylibbundler &> /dev/null; then
    echo "Installing dylibbundler (used for packaging dylibs)..."
    brew install dylibbundler
fi

ROOT_DIR=$(pwd)
BUILD_DIR="$ROOT_DIR/$BUILD_NAME"
DIST_DIR="$ROOT_DIR/dist/$DIST_NAME"
mkdir -p "$BUILD_DIR" "$DIST_DIR"

cd "$BUILD_DIR"
echo "--- Configuring project for macOS ($APP) ---"
cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DCONUI_APP="$APP" \
    -DCONUI_ABOUT_CAT="${CONUI_ABOUT_CAT:-ON}"

echo "--- Compiling ---"
ninja -j "$(sysctl -n hw.ncpu)"
cd "$ROOT_DIR"

APP_DIR="$BUILD_DIR/ConUIApp.app"
if [ ! -d "$APP_DIR" ]; then
    echo "--- Error: Build Failed! ($APP_DIR not found) ---"
    exit 1
fi

MACOS_DIR="$APP_DIR/Contents/MacOS"
FRAMEWORKS_DIR="$APP_DIR/Contents/Frameworks"
RESOURCES_DIR="$APP_DIR/Contents/Resources"
mkdir -p "$FRAMEWORKS_DIR" "$RESOURCES_DIR"

echo "--- Self-test ---"
# CONUI_APP_ROOT points both the resources and the data at the build folder,
# so the test neither needs the bundle layout nor touches the user's settings.
(cd "$BUILD_DIR" && CONUI_APP_ROOT="$BUILD_DIR" CONUI_SNAPSHOT_DIR="$ROOT_DIR/tests/snapshots" \
    "$MACOS_DIR/ConUIApp" --selftest) \
    || { echo "Self-test FAILED"; exit 1; }

if [ -f "$ROOT_DIR/src/icon.ico" ]; then
    echo "--- Generating macOS icon ---"
    ICONSET_DIR="$RESOURCES_DIR/AppIcon.iconset"
    mkdir -p "$ICONSET_DIR"
    magick -size 1024x1024 xc:"#062038" \
        \( "$ROOT_DIR/src/icon.ico[0]" -resize 800x800 -background none \) \
        -gravity center -composite -alpha set -type TrueColorAlpha \
        PNG32:"$ICONSET_DIR/icon_512x512@2x.png"
    cp "$ICONSET_DIR/icon_512x512@2x.png" "$ICONSET_DIR/icon_512x512.png"
    iconutil -c icns "$ICONSET_DIR" -o "$RESOURCES_DIR/AppIcon.icns"
    rm -rf "$ICONSET_DIR"
fi

echo "--- Bundling .dylib dependencies ---"
dylibbundler -od -b -x "$MACOS_DIR/ConUIApp" -d "$FRAMEWORKS_DIR" -p "@executable_path/../Frameworks"

# Rewriting the dylib paths invalidates the signatures, and Apple Silicon
# refuses to run an unsigned binary: sign the bundle ad hoc.
echo "--- Signing (ad hoc) ---"
codesign --force --deep --sign - "$APP_DIR"

echo "--- Staging dist/$DIST_NAME ---"
rm -rf "$DIST_DIR/ConUIApp.app"
cp -R "$APP_DIR" "$DIST_DIR/"

echo "--- Build Successful! ---"
echo "App bundle: $DIST_DIR/ConUIApp.app"
