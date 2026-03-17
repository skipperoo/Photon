#!/bin/bash
# Internal build script for Photon AppImage

mkdir -p build_dir && cd build_dir
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
cd ..

export QMAKE=$(which qmake)
export VERSION=$(git describe --tags --always || echo "latest")
export QML_SOURCES_PATHS="/app/content"
export EXTRA_QT_PLUGINS="platforms,wayland-graphics-integration-client,wayland-shell-integration,imageformats,styles,controls,quickcontrols2"

# Run linuxdeploy
linuxdeploy --appdir AppDir -e build_dir/Photon -d Photon.desktop -i assets/icons/photon.png --plugin qt --output appimage

# Move and rename output
mkdir -p /app/dist/linux
mv Photon-*.AppImage /app/dist/linux/Photon-Linux.AppImage
