#!/bin/bash
# Internal build script for Photon AppImage

mkdir -p build_dir && cd build_dir
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
cd ..

export QMAKE=$(which qmake)
export VERSION=$(git describe --tags --always || echo "latest")
export QML_SOURCES_PATHS="/app/content"
export EXTRA_QT_MODULES="quick,quickcontrols2,qml"
export EXTRA_QT_PLUGINS="platforms,wayland-graphics-integration-client,wayland-shell-integration,imageformats,styles,controls,quickcontrols2"
mkdir -p AppDir/usr/share/fonts
cp -r /usr/share/fonts/truetype AppDir/usr/share/fonts/ 2>/dev/null || true
cp -r /usr/share/fonts/opentype AppDir/usr/share/fonts/ 2>/dev/null || true
export QT_QPA_FONTDIR="$APPDIR/usr/share/fonts"
cat >AppDir/AppRun <<'EOF'
#!/bin/bash
APPDIR="$(dirname "$(readlink -f "$0")")"

export QT_QPA_FONTDIR="$APPDIR/usr/share/fonts"
export FONTCONFIG_PATH="$APPDIR/usr/share/fonts"
export QT_QUICK_CONTROLS_STYLE="${QT_QUICK_CONTROLS_STYLE:-Basic}"
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-wayland;xcb}"

exec "$APPDIR/usr/bin/Photon" "$@"
EOF
chmod +x AppDir/AppRun
# Run linuxdeploy
linuxdeploy --appdir AppDir -e build_dir/Photon -d Photon.desktop -i assets/icons/photon.png --plugin qt --output appimage

# Move and rename output
mkdir -p /app/dist/linux
mv Photon-*.AppImage /app/dist/linux/Photon-Linux.AppImage
