FROM ubuntu:22.04

# Avoid prompts from apt
ENV DEBIAN_FRONTEND=noninteractive

# Install dependencies
RUN apt-get update && apt-get install -y software-properties-common \
    && add-apt-repository ppa:flatpak/stable \
    && apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    wget \
    file \
    libusb-1.0-0-dev \
    portaudio19-dev \
    qt6-base-dev \
    qt6-multimedia-dev \
    libgl1-mesa-dev \
    libxkbcommon-x11-0 \
    libxkbcommon-dev \
    libxkbcommon-x11-dev \
    libvulkan-dev \
    appstream \
    appstream-util \
    flatpak \
    flatpak-builder \
    elfutils \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Install linuxdeploy and Qt plugin
RUN wget https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage -O /usr/local/bin/linuxdeploy \
    && chmod +x /usr/local/bin/linuxdeploy \
    && wget https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage -O /usr/local/bin/linuxdeploy-plugin-qt \
    && chmod +x /usr/local/bin/linuxdeploy-plugin-qt

# Configure Flathub remote (system-wide)
RUN flatpak remote-add --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo

# Set env var to allow running AppImages inside Docker (for linuxdeploy)
ENV APPIMAGE_EXTRACT_AND_RUN=1

# Create build directory
WORKDIR /build

# Copy entrypoint script
COPY packaging/docker/docker-entrypoint.sh /usr/local/bin/
RUN chmod +x /usr/local/bin/docker-entrypoint.sh

ENTRYPOINT ["docker-entrypoint.sh"]

