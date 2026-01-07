#!/bin/bash
set -e

# Navigate to repo root
cd "$(dirname "$0")/../../"

# Build the docker image if it doesn't exist (optional, mostly to ensure we have it)
# ./packaging/docker/rebuild_image.sh # Uncomment if you want to force rebuild image

# Ensure output and cache directories exist
mkdir -p output_docker
mkdir -p .flatpak-cache

echo "Running Flatpak build in Docker..."
# We mount .flatpak-cache to /var/lib/flatpak to persist system installations
# We rely on the Dockerfile having done 'flatpak remote-add --if-not-exists flathub ...' (system wide)
sudo docker run --rm --privileged \
    -v "$(pwd):/work/repo" \
    -v "$(pwd)/output_docker:/output" \
    -v "$(pwd)/.flatpak-cache:/var/lib/flatpak" \
    mooer-builder \
    ./packaging/flatpak/internal-build-script.sh

# Fix permissions
if [ -f "output_docker/MooerLooperManager.flatpak" ]; then
    sudo chown $(id -u):$(id -g) output_docker/MooerLooperManager.flatpak
fi

echo "Build finished. Check output_docker/ for results."
