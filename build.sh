#!/bin/bash
# Windows/ucrt64 build: configure + compile with Ninja, then stage a
# ready-to-run copy under dist/ (launcher + resources.dat + themes/ + bin/
# with the app exe and its runtime DLLs — see CMakeLists.txt section 6 for
# why the launcher/bin split exists).
#
#     ./build.sh                       the playground  -> dist/win/
#     CONUI_APP=skeleton ./build.sh    the fork template -> dist/win-skeleton/
#     ./build.sh run                   build, then launch
#     ./build.sh package               build, then pack dist/win into release/ConUI-win-x64.zip
#     CONUI_ABOUT_CAT=OFF ./build.sh   the playground without the ABOUT cat
#
# Each application has its own build directory, so switching between them
# never forces a full rebuild of the other.
set -e

APP="${CONUI_APP:-playground}"
if [ "$APP" = "skeleton" ]; then
    BUILD_DIR=build-skeleton
    DIST_NAME=win-skeleton
else
    BUILD_DIR=build
    DIST_NAME=win
fi
echo "--- Application: $APP ---"

echo "--- Checking for running instances ---"
taskkill.exe /F /IM ConUI.exe 2>/dev/null || true
taskkill.exe /F /IM ConUIApp.exe 2>/dev/null || true

ROOT_DIR=$(pwd)
mkdir -p "$BUILD_DIR"

cd "$BUILD_DIR"
echo "--- Configuring project ---"
cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DCONUI_APP="$APP" \
    -DCONUI_ABOUT_CAT="${CONUI_ABOUT_CAT:-ON}" \
    || { echo "CMake configuration failed"; exit 1; }

echo "--- Compiling ---"
ninja || { echo "Compilation failed"; exit 1; }
cd "$ROOT_DIR"

if [ ! -f "$BUILD_DIR/ConUI.exe" ]; then
    echo "--- Error: Build Failed! ($BUILD_DIR/ConUI.exe not found) ---"
    exit 1
fi

echo "--- Self-test ---"
# The app lives in bin/ but its resources sit one level up, exactly as they do
# in dist/ — so point it at the root the same way the launcher does.
# CONUI_SNAPSHOT_DIR points at the screen baselines kept in the repository;
# run once with CONUI_SNAPSHOT_UPDATE=1 to rewrite them after a change that
# was meant to move things.
(cd "$BUILD_DIR" && CONUI_APP_ROOT="$(pwd)" CONUI_SNAPSHOT_DIR="$ROOT_DIR/tests/snapshots" \
    ./bin/ConUIApp.exe --selftest) \
    || { echo "Self-test FAILED"; exit 1; }

echo "--- Staging dist/$DIST_NAME ---"
DIST_DIR="$ROOT_DIR/dist/$DIST_NAME"
rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR/bin" "$DIST_DIR/themes"

cp -f "$BUILD_DIR/ConUI.exe" "$DIST_DIR/"
cp -f "$BUILD_DIR/resources.dat" "$DIST_DIR/"
cp -f themes/*.json "$DIST_DIR/themes/"
cp -f "$BUILD_DIR/bin/ConUIApp.exe" "$DIST_DIR/bin/"
cp -f "$BUILD_DIR"/bin/*.dll "$DIST_DIR/bin/" 2>/dev/null || true

# Licences travel with every copy: ConUI's own, the font's (OFL requires it),
# and those of the runtime DLLs in bin/ (GLFW: zlib; winpthreads: MIT and BSD,
# whose binary redistribution clause asks for the notice).
MINGW_ROOT="${MINGW_PREFIX:-/ucrt64}"
mkdir -p "$DIST_DIR/licenses"
cp -f LICENSE "$DIST_DIR/LICENSE.txt"
cp -f src/fonts/OFL.txt "$DIST_DIR/licenses/JetBrainsMono-OFL.txt"
cp -f "$MINGW_ROOT/share/licenses/glfw/LICENSE" "$DIST_DIR/licenses/GLFW-LICENSE.txt" 2>/dev/null \
    || echo "  note: GLFW licence not found under $MINGW_ROOT/share/licenses"
cp -f "$MINGW_ROOT/share/licenses/winpthreads/COPYING" "$DIST_DIR/licenses/winpthreads-COPYING.txt" 2>/dev/null \
    || echo "  note: winpthreads licence not found under $MINGW_ROOT/share/licenses"

echo "--- Runtime DLL closure ---"
# Every DLL imported by anything in the distribution must either ship in bin/
# or be part of Windows itself. A new dependency then fails HERE, at build
# time, instead of on a user's machine with "glfw3.dll was not found". The
# api-ms-win-crt-* set is the Universal CRT, part of Windows 10 and later.
SYSTEM_RE='^(kernel32|user32|gdi32|shell32|opengl32|dwmapi|advapi32|ole32|oleaut32|imm32|winmm|version|shlwapi|ntdll|ucrtbase|api-ms-win-.*|ext-ms-win-.*)\.dll$'
missing=0
for f in "$DIST_DIR"/ConUI.exe "$DIST_DIR"/bin/*.exe "$DIST_DIR"/bin/*.dll; do
    [ -f "$f" ] || continue
    for dll in $(objdump -p "$f" | awk '/DLL Name:/ {print $3}'); do
        low=$(echo "$dll" | tr 'A-Z' 'a-z')
        echo "$low" | grep -Eq "$SYSTEM_RE" && continue
        if ! ls "$DIST_DIR/bin" | grep -qix "$dll"; then
            echo "  MISSING: $dll (imported by $(basename "$f"))"
            missing=1
        fi
    done
done
[ "$missing" = 0 ] || { echo "Runtime DLL check FAILED"; exit 1; }
echo "  bin/ ships: $(ls "$DIST_DIR/bin" | grep -i '\.dll$' | tr '\n' ' ')— nothing else is needed"

echo "--- Build Successful! ---"
echo "Ready-to-run copy: $DIST_DIR"

if [ "$1" == "package" ]; then
    # The release archive: dist/<name> packed as a "ConUI" folder. Windows'
    # own tar.exe (bsdtar) writes zip; MSYS's GNU tar cannot. That bsdtar has
    # no -s to rename on the fly, so the folder is copied under its final name.
    echo "--- Packaging ---"
    ZIP_NAME="ConUI-$DIST_NAME-x64.zip"
    WINTAR="$(cygpath -u "${SYSTEMROOT:-C:\\Windows}")/System32/tar.exe"
    PKG_DIR="$ROOT_DIR/dist/.package"
    rm -rf "$PKG_DIR"
    mkdir -p "$PKG_DIR" "$ROOT_DIR/release"
    cp -R "$DIST_DIR" "$PKG_DIR/ConUI"
    rm -f "$ROOT_DIR/release/$ZIP_NAME"
    (cd "$PKG_DIR" && "$WINTAR" -a -c -f "$(cygpath -w "$ROOT_DIR/release/$ZIP_NAME")" ConUI) \
        || { echo "Packaging FAILED"; exit 1; }
    rm -rf "$PKG_DIR"
    echo "Release archive: release/$ZIP_NAME"
fi

if [ "$1" == "run" ]; then
    echo "--- Launching App ---"
    cd "$DIST_DIR"
    ./ConUI.exe &
fi
