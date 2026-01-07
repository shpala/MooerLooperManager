#!/bin/bash
set -e

# Check if local repository is mounted
if [ -d "/work/repo" ]; then
    echo "Using local repository from /work/repo..."
    # Copy to workspace to avoid polluting host directory with build artifacts
    # and to ensure we have write permissions if the mount is read-only or root-owned
    cp -r /work/repo ./MooerLooperManager
    cd MooerLooperManager
else
    # Clone the repository
    echo "Cloning repository..."
    git clone https://github.com/shpala/MooerLooperManager.git
    cd MooerLooperManager
fi

# Ensure the build script is executable
chmod +x packaging/appimage/build.sh
if [ -f "packaging/flatpak/internal-build-script.sh" ]; then
    chmod +x packaging/flatpak/internal-build-script.sh
fi

echo "Starting build process..."
# Set QMAKE and other environment variables if needed
export QMAKE=/usr/bin/qmake6

# Bypass AppStream validation issues
export APPSTREAM_CLI=/bin/true

if [ "$#" -gt 0 ]; then
    echo "Executing custom command: $@"
    exec "$@"
fi

./packaging/appimage/build.sh

# Find the resulting AppImage
APPIMAGE=$(find . -name "MooerLooperManager*.AppImage" | head -n 1)

if [ -f "$APPIMAGE" ]; then
    echo "AppImage created: $APPIMAGE"
    # Copy to the mounted output directory
    cp "$APPIMAGE" /output/
    # Also copy the latest one with a fixed name for convenience
    cp "$APPIMAGE" /output/MooerLooperManager-latest-x86_64.AppImage
    
    echo "Build artifacts copied to host directory."
    
    # Try to adjust permissions of the output file to match the directory
    # This is a best-effort attempt
    chown $(stat -c '%u:%g' /output) /output/$(basename "$APPIMAGE") || true
    chown $(stat -c '%u:%g' /output) /output/MooerLooperManager-latest-x86_64.AppImage || true
else
    echo "Error: AppImage not found!"
    exit 1
fi
