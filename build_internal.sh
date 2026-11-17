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

# AI denoise models (RawNIND Bayer + Linear, GPL-3.0): staged by
# cmake/AiDenoiseModel.cmake in build_dir/models; the app looks for them in
# <executable dir>/models. linuxdeploy only copies the executable, so bundle
# them explicitly.
models_ok=1
for v in bayer linear; do
    if [ ! -f "build_dir/models/rawnind_${v}.ncnn.param" ] || [ ! -f "build_dir/models/rawnind_${v}.ncnn.bin" ]; then
        models_ok=0
    fi
done
if [ "$models_ok" = 1 ]; then
    mkdir -p AppDir/usr/bin/models
    cp build_dir/models/* AppDir/usr/bin/models/
    echo "[ build_internal.sh ] - Bundled AI denoise models"
else
    echo "[ build_internal.sh ] - ERROR: AI denoise models missing in build_dir/models (configure step failed to fetch them?)"
    exit 1
fi

# Run linuxdeploy
# The Vulkan loader must come from the host: it has to match the installed GPU
# drivers. Ubuntu 22.04's old loader returned null entry points with current
# NVIDIA drivers and crashed GPU initialisation (AI denoise).
linuxdeploy --appdir AppDir -e build_dir/Photon -d Photon.desktop -i assets/icons/photon.png \
    --exclude-library "libvulkan.so*" --plugin qt --output appimage

# Move and rename output
mkdir -p /app/dist/linux
mv Photon-*.AppImage /app/dist/linux/Photon-Linux.AppImage
