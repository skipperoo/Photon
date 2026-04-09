#!/bin/bash
# Script to build Photon AppImage using Docker

# Ensure dist/linux directory exists
mkdir -p dist/linux

# Build the Docker image (rebuilds everytime to collect changes)
echo "[ build_appimage.sh ] - Building Docker image..."
docker build -t photon-linux-builder -f Dockerfile.linux .

# Run the container and mount the dist directory to get the output
echo "[ build_appimage.sh ] - Running container to build AppImage..."
docker run --rm -v "$(pwd)/dist:/app/dist" photon-linux-builder

echo "[ build_appimage.sh ] - AppImage should be available at dist/linux/Photon-Linux.AppImage"
