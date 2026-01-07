#!/bin/bash
set -e

# Navigate to the build directory
cd packaging/flatpak

# Add Flathub remote (system-wide)
flatpak remote-add --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo

# Install Runtime and SDK (system-wide)
echo "Installing/Updating Runtime and SDK..."
# We use system install because we mounted /var/lib/flatpak
flatpak install -y flathub org.kde.Platform//6.10 org.kde.Sdk//6.10

# Build the Flatpak and export to repo
echo "Building Flatpak..."
mkdir -p output
# We build directly to the repo. --install-deps-from=flathub handles missing deps.
# Note: we use system installation so no --user.
flatpak-builder --repo=repo --force-clean --install-deps-from=flathub --disable-rofiles-fuse build_dir io.github.shpala.MooerLooperManager.yml

# Debug: Check linking and execution
echo "Checking binary dependencies..."
flatpak-builder --run build_dir io.github.shpala.MooerLooperManager.yml ldd /app/bin/MooerLooperManager

echo "Trying to run binary (help)..."
flatpak-builder --run build_dir io.github.shpala.MooerLooperManager.yml /app/bin/MooerLooperManager --help || echo "Run failed (might be expected for GUI app without display)"

# Export to a repo (repo dir) and bundle
flatpak build-bundle repo output/MooerLooperManager.flatpak io.github.shpala.MooerLooperManager
echo "Bundle created at build_flatpak/output/MooerLooperManager.flatpak"

# Move to the mounted output directory
cp output/MooerLooperManager.flatpak /output/
echo "Bundle copied to /output/"
