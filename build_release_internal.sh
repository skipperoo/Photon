#!/bin/bash
set -euo pipefail

export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig

BUILD_DIR=/tmp/photon-build
APPDIR=/tmp/Photon.AppDir

rm -rf "$BUILD_DIR" "$APPDIR"
mkdir -p "$BUILD_DIR" "$APPDIR/usr/share/fonts"

cmake -S /app -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DPHOTON_GIT_COMMIT="${PHOTON_GIT_COMMIT:-unknown}"
cmake --build "$BUILD_DIR" -j"$(nproc)"

export QMAKE="$(command -v qmake)"
export VERSION="$(git -C /app describe --tags --always 2>/dev/null || echo latest)"
export QML_SOURCES_PATHS="/app/content"
export EXTRA_QT_MODULES="quick;quickcontrols2;qml"
export EXTRA_PLATFORM_PLUGINS="libqwayland.so"

cp -r /usr/share/fonts/truetype "$APPDIR/usr/share/fonts/" 2>/dev/null || true
cp -r /usr/share/fonts/opentype "$APPDIR/usr/share/fonts/" 2>/dev/null || true

cat > "$APPDIR/AppRun" <<'EOF'
#!/bin/bash
APPDIR="$(dirname "$(readlink -f "$0")")"

export QT_QPA_FONTDIR="$APPDIR/usr/share/fonts"
export FONTCONFIG_PATH="$APPDIR/usr/share/fonts"
export QT_QUICK_CONTROLS_STYLE="${QT_QUICK_CONTROLS_STYLE:-Basic}"
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-wayland;xcb}"

exec "$APPDIR/usr/bin/Photon" "$@"
EOF
chmod +x "$APPDIR/AppRun"

cd /tmp
linuxdeploy --appdir "$APPDIR" \
    -e "$BUILD_DIR/Photon" \
    -d /app/Photon.desktop \
    -i /app/assets/icons/photon.png \
    --plugin qt \
    --output appimage

mkdir -p /app/dist/linux
mv Photon-*.AppImage /app/dist/linux/Photon-Linux-x86_64.AppImage
