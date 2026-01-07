#!/bin/bash

# Navigate to repo root
cd "$(dirname "$0")/../../"

# Exit on error
set -e

# Configuration
APP_NAME="MooerLooperManager"
BUILD_DIR="build_appimage"
APPDIR="AppDir"

# Setup linuxdeploy
if command -v linuxdeploy &> /dev/null; then
    LINUXDEPLOY="linuxdeploy"
elif [ -f linuxdeploy-x86_64.AppImage ]; then
    LINUXDEPLOY="./linuxdeploy-x86_64.AppImage"
else
    wget https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
    chmod +x linuxdeploy-x86_64.AppImage
    LINUXDEPLOY="./linuxdeploy-x86_64.AppImage"
fi

if command -v linuxdeploy-plugin-qt &> /dev/null; then
    LINUXDEPLOY_PLUGIN_QT="linuxdeploy-plugin-qt"
elif [ -f linuxdeploy-plugin-qt-x86_64.AppImage ]; then
    LINUXDEPLOY_PLUGIN_QT="./linuxdeploy-plugin-qt-x86_64.AppImage"
else
    wget https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
    chmod +x linuxdeploy-plugin-qt-x86_64.AppImage
    LINUXDEPLOY_PLUGIN_QT="./linuxdeploy-plugin-qt-x86_64.AppImage"
fi

# Build project
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
make -j$(nproc)
make install DESTDIR="$APPDIR"

# Ensure assets are in the right place for linuxdeploy
mkdir -p "$APPDIR/usr/share/pixmaps"
cp ../assets/MooerLooperManager.png "$APPDIR/usr/share/pixmaps/MooerLooperManager.png"

# Link appdata to the name linuxdeploy-plugin-appimage expects
ln -sf io.github.shpala.MooerLooperManager.metainfo.xml "$APPDIR/usr/share/metainfo/io.github.shpala.MooerLooperManager.appdata.xml"

# Create AppImage
cd ..
if [[ "$*" == *""* ]]; then
    export NO_STRIP=true
fi
export LINUXDEPLOY_OUTPUT_VERSION=$(git rev-parse --short HEAD 2>/dev/null || echo "latest")
export LINUXDEPLOY_OUTPUT_APPNAME="MooerLooperManager"
# Force output name
export OUTPUT="MooerLooperManager-x86_64.AppImage"

# Bypass AppStream validation issues during build
export APPSTREAM_CLI=/bin/true

# Find libraries (compatible with both Arch and Ubuntu)
LIBUSB_PATH=$(find /usr/lib /lib -name "libusb-1.0.so.0" 2>/dev/null | head -n 1)
LIBPORTAUDIO_PATH=$(find /usr/lib /lib -name "libportaudio.so.2" 2>/dev/null | head -n 1)

echo "DEBUG: LIBUSB_PATH='$LIBUSB_PATH'"
echo "DEBUG: LIBPORTAUDIO_PATH='$LIBPORTAUDIO_PATH'"

$LINUXDEPLOY --appdir "$BUILD_DIR/$APPDIR" \
    --plugin qt \
    --output appimage \
    --desktop-file resources/MooerLooperManager.desktop \
    --icon-file assets/MooerLooperManager.png \
    --library "$LIBUSB_PATH" \
    --library "$LIBPORTAUDIO_PATH" 

# If linuxdeploy ignored $OUTPUT, rename it manually
if [ -f "Mooer_Looper_Manager-a27ad0e-x86_64.AppImage" ]; then
    mv "Mooer_Looper_Manager-a27ad0e-x86_64.AppImage" "$OUTPUT"
fi
# Generic fallback rename
find . -maxdepth 1 -name "Mooer_Looper_Manager*.AppImage" -exec mv {} "$OUTPUT" \;

echo "AppImage created successfully!"