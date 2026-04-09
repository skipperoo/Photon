#!/bin/bash
# Script to build Photon for Windows using Docker
# This MUST be run on a Windows machine (e.g., via Git Bash) with Windows Containers enabled

# Check if we are likely on a Linux machine
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    echo "[ build_windows.sh ] - WARNING: You are on Linux. Windows containers require a Windows host."
    echo "This script will likely fail unless your Docker is configured for remote Windows build nodes."
fi

# Ensure dist/windows directory exists
mkdir -p dist/windows

# Build the Docker image (rebuilds everytime to collect changes)
echo "[ build_windows.sh ] - Building Docker image (this may take 30+ minutes the first time)..."
docker build -t photon-windows-builder -f Dockerfile.windows .

# Run the container and mount the dist directory to get the output
# Using PWD formatted for Windows if on Git Bash
echo "[ build_windows.sh ] - Running container to build Windows binaries..."
docker run --rm -v "$(pwd)/dist:C:/app/dist" photon-windows-builder

echo "[ build_windows.sh ] - Build complete. Outputs in dist/windows/Photon"
