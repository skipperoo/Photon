#!/bin/bash
set -euo pipefail

IMAGE=photon-release-builder
OUTPUT=dist/linux/Photon-Linux-x86_64.AppImage

mkdir -p dist/linux

echo "[ build_release.sh ] - Building Docker image ${IMAGE}..."
docker build -t "$IMAGE" -f Dockerfile.release .

echo "[ build_release.sh ] - Building Photon in container and packaging AppImage..."
docker run --rm \
    --user "$(id -u):$(id -g)" \
    -e HOME=/tmp \
    -v "$(pwd)/dist:/app/dist" \
    "$IMAGE"

echo "[ build_release.sh ] - Done: ${OUTPUT}"
