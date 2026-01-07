#!/bin/bash
set -e

cd "$(dirname "$0")"

# Add Flathub remote if not exists
flatpak remote-add --if-not-exists --user flathub https://dl.flathub.org/repo/flathub.flatpakrepo

# Install Runtime and SDK
echo "Installing/Updating Runtime and SDK..."
flatpak install --user -y flathub org.kde.Platform//6.10 org.kde.Sdk//6.10

# Build the Flatpak
echo "Building Flatpak..."
# We use --force-clean to clean the build dir, --user to install to user installation
# --install-deps-from=flathub allows builder to install missing deps if configured
flatpak-builder --force-clean --user --install-deps-from=flathub build_dir io.github.shpala.MooerLooperManager.yml

echo "Build complete. To run:"
echo "flatpak run io.github.shpala.MooerLooperManager"

# Export to a repo (repo dir) and bundle (for distribution/testing)
echo "Creating bundle..."
mkdir -p output
flatpak-builder --user --repo=repo --force-clean build_dir io.github.shpala.MooerLooperManager.yml
flatpak build-bundle repo output/MooerLooperManager.flatpak io.github.shpala.MooerLooperManager
echo "Bundle created at output/MooerLooperManager.flatpak"
