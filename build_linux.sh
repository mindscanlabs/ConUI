#!/bin/bash
# Linux build: configure + compile, run the self-test, then stage a
# ready-to-run folder under dist/.
#
#     ./build_linux.sh                       the playground    -> dist/linux/
#     CONUI_APP=skeleton ./build_linux.sh    the fork template -> dist/linux-skeleton/
#     CONUI_ABOUT_CAT=OFF ./build_linux.sh   the playground without the ABOUT cat
#     ./build_linux.sh run                   build, then launch
#
# Requirements: a C++20 compiler (GCC 10+ or Clang 12+), CMake 3.15+, Ninja
# (optional, Make is used without it), GLFW 3.3+ and the OpenGL development
# files. For example:
#     Debian/Ubuntu  sudo apt install build-essential cmake ninja-build libglfw3-dev libgl1-mesa-dev
#     Fedora         sudo dnf install gcc-c++ cmake ninja-build glfw-devel mesa-libGL-devel
#     Arch           sudo pacman -S base-devel cmake ninja glfw
#
# UNVERIFIED: written for Linux but not yet run there; only the Windows path
# (build.sh) has actually been run. The self-test opens a hidden window, so
# it needs a display (an X11 or Wayland session, or xvfb-run). Set
# CONUI_SKIP_SELFTEST=1 to build without it.
#
# The window keeps the desktop's own title bar on Linux (src/Core/Platform_other.cpp);
# ConUI draws its content below it.
set -e

APP="${CONUI_APP:-playground}"
if [ "$APP" = "skeleton" ]; then
    BUILD_NAME=build_linux-skeleton; DIST_NAME=linux-skeleton
else
    BUILD_NAME=build_linux; DIST_NAME=linux
fi
echo "--- Application: $APP ---"

ROOT_DIR=$(pwd)
BUILD_DIR="$ROOT_DIR/$BUILD_NAME"
DIST_DIR="$ROOT_DIR/dist/$DIST_NAME"
mkdir -p "$BUILD_DIR"

GENERATOR="Unix Makefiles"
command -v ninja >/dev/null 2>&1 && GENERATOR="Ninja"

echo "--- Configuring project ($GENERATOR) ---"
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -G "$GENERATOR" -DCMAKE_BUILD_TYPE=Release \
    -DCONUI_APP="$APP" -DCONUI_ABOUT_CAT="${CONUI_ABOUT_CAT:-ON}" \
    || { echo "CMake configuration failed"; exit 1; }

echo "--- Compiling ---"
cmake --build "$BUILD_DIR" --parallel || { echo "Compilation failed"; exit 1; }

if [ ! -x "$BUILD_DIR/ConUIApp" ]; then
    echo "--- Error: Build Failed! ($BUILD_DIR/ConUIApp not found) ---"
    exit 1
fi

if [ "${CONUI_SKIP_SELFTEST:-0}" != "1" ]; then
    echo "--- Self-test ---"
    # The binary sits next to resources.dat and themes/ in the build folder;
    # CONUI_APP_ROOT keeps the test's data.bin there too.
    (cd "$BUILD_DIR" && CONUI_APP_ROOT="$BUILD_DIR" CONUI_SNAPSHOT_DIR="$ROOT_DIR/tests/snapshots" \
        ./ConUIApp --selftest) \
        || { echo "Self-test FAILED"; exit 1; }
fi

echo "--- Staging dist/$DIST_NAME ---"
rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR/themes"
cp -f "$BUILD_DIR/ConUIApp" "$DIST_DIR/"
cp -f "$BUILD_DIR/resources.dat" "$DIST_DIR/"
cp -f "$ROOT_DIR"/themes/*.json "$DIST_DIR/themes/"

echo "--- Build Successful! ---"
echo "Ready-to-run copy: $DIST_DIR (start ./ConUIApp there)"

if [ "$1" == "run" ]; then
    echo "--- Launching App ---"
    cd "$DIST_DIR"
    ./ConUIApp &
fi
