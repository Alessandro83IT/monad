#!/bin/sh

set -eu

PROJECT_ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
BUILD_DIR="${ATHENA_APPIMAGE_BUILD_DIR:-$PROJECT_ROOT/build}"
APPDIR="$BUILD_DIR/AthenaPM.AppDir"

# Check the effective normalized bindir before deleting any existing AppDir.
cmake -P "$BUILD_DIR/check-appimage-bindir.cmake"

rm -rf "$APPDIR"

if [ -n "${ATHENA_APPIMAGE_CONFIG:-}" ]; then
    DESTDIR="$APPDIR" cmake --install "$BUILD_DIR" --prefix /usr --config "$ATHENA_APPIMAGE_CONFIG"
else
    DESTDIR="$APPDIR" cmake --install "$BUILD_DIR" --prefix /usr
fi

cp "$PROJECT_ROOT/packaging/AppDir/AppRun" \
   "$APPDIR/AppRun"

chmod +x "$APPDIR/AppRun"

cp "$PROJECT_ROOT/packaging/AppDir/athena-pm.desktop" \
   "$APPDIR/athena-pm.desktop"

cp "$PROJECT_ROOT/packaging/AppDir/athena.png" \
   "$APPDIR/athena.png"

echo "AppDir creata: $APPDIR"

APPIMAGETOOL="${APPIMAGETOOL:-$HOME/Applications/appimagetool/appimagetool-x86_64.AppImage}"
ATHENA_VERSION="$(sed -n 's/^[[:space:]]*VERSION[[:space:]]\+\([0-9][0-9.]*\).*/\1/p' "$PROJECT_ROOT/CMakeLists.txt" | head -n 1)"
DIST_DIR="${ATHENA_APPIMAGE_DIST_DIR:-$PROJECT_ROOT/dist}"
OUTPUT="$DIST_DIR/AthenaPM-${ATHENA_VERSION}-x86_64.AppImage"

mkdir -p "$DIST_DIR"

"$APPIMAGETOOL" \
    "$APPDIR" \
    "$OUTPUT"

echo "AppImage creata: $OUTPUT"
