#include "usb_device.h"
#include <QtEndian>
#include <iostream>
#include <thread>
#include <chrono>
#include <cmath>

USBDevice::USBDevice() : ctx(nullptr), dev_handle(nullptr), connected(false) {
    libusb_init(&ctx);
}

USBDevice::~USBDevice() {
    disconnect();
    if (ctx) libusb_exit(ctx);
}

bool USBDevice::connect() {
    if (connected) return true;

    dev_handle = libusb_open_device_with_vid_pid(ctx, Protocol::VENDOR_ID, Protocol::PRODUCT_ID);
    if (!dev_handle) {
        std::cerr << "Device not found" << std::endl;
        return false;
    }

    if (libusb_kernel_driver_active(dev_handle, 0) == 1) {
        libusb_detach_kernel_driver(dev_handle, 0);
    }
    if (libusb_kernel_driver_active(dev_handle, 1) == 1) {
        libusb_detach_kernel_driver(dev_handle, 1);
    }

    if (libusb_claim_interface(dev_handle, 0) < 0) {
        std::cerr << "Cannot claim interface 0" << std::endl;
        disconnect();
        return false;
    }
    if (libusb_claim_interface(dev_handle, 1) < 0) {
        std::cerr << "Cannot claim interface 1" << std::endl;
        disconnect();
        return false;
    }

    connected = true;
    return true;
}

void USBDevice::disconnect() {
    if (dev_handle) {
        libusb_release_interface(dev_handle, 0);
        libusb_release_interface(dev_handle, 1);
        libusb_close(dev_handle);
        dev_handle = nullptr;
    }
    connected = false;
}

bool USBDevice::isConnected() const {
    return connected;
}

int USBDevice::write(const QByteArray& data, int endpoint, int timeout) {
    if (!connected) return -1;
    int transferred;
    // Changed to interrupt transfer based on Python implementation/packet capture
    int r = libusb_interrupt_transfer(dev_handle, endpoint, (unsigned char*)data.data(), data.size(), &transferred, timeout);
    if (r < 0) {
        std::cerr << "Write error to ep " << std::hex << endpoint << ": " << libusb_error_name(r) << std::dec << std::endl;
    }
    return transferred;
}

QByteArray USBDevice::read(int size, int endpoint, int timeout) {
    if (!connected) return QByteArray();
    QByteArray buffer(size, 0);
    int transferred;
    // Changed to interrupt transfer based on Python implementation/packet capture
    int r = libusb_interrupt_transfer(dev_handle, endpoint, (unsigned char*)buffer.data(), size, &transferred, timeout);
    if (r < 0 && r != LIBUSB_ERROR_TIMEOUT) {
        std::cerr << "Read error from ep " << std::hex << endpoint << ": " << libusb_error_name(r) << std::dec << std::endl;
        return QByteArray();
    }
    buffer.resize(transferred);
    return buffer;
}

std::vector<TrackInfo> USBDevice::listTracks() {
    std::vector<TrackInfo> tracks;
    for (int i = 0; i < Protocol::MAX_TRACKS; i++) {
        QByteArray cmd = Protocol::createDownloadCommand(i, 0); // Query command is same as download chunk 0
        write(cmd);
        QByteArray resp = read(1024);
        
        bool hasTrack = false;
        uint32_t size = 0;
        double duration = 0;

        if (Protocol::parseTrackInfoHeader(resp, size)) {
            hasTrack = true;
            // Matches Python logic: DEVICE_SIZE_MULTIPLIER = 1.0
            // duration = size / (44100 * 2ch * 3bytes)
            duration = (double)size / (6.0 * 44100.0);
        }
        tracks.push_back({i, hasTrack, duration, size});
    }
    return tracks;
}

void USBDevice::deleteTrack(int slot) {
    write(Protocol::createDeleteCommand(slot));
    read(64, Protocol::EP_IN_STATUS); // Ack
}

void USBDevice::playTrack(int slot) {
    write(Protocol::createPlayCommand(slot));
    read(1024, Protocol::EP_IN_DATA); // Response?
}

std::vector<int32_t> USBDevice::downloadTrack(int slot, ProgressCallback callback, void* userData) {
    // Get info
    write(Protocol::createDownloadCommand(slot, 0));
    QByteArray firstChunk = read(1024);
    
    uint32_t trackSize = 0;
    if (!Protocol::parseTrackInfoHeader(firstChunk, trackSize)) {
        throw std::runtime_error("Track does not exist");
    }

    std::vector<int32_t> fullAudio;
    fullAudio.reserve(trackSize / 3); // Approx

    QByteArray buffer;
    int chunks = (trackSize + 1023) / 1024;
    
    // Start from Chunk 1
    for (int i = 1; i <= chunks; i++) {
        write(Protocol::createDownloadCommand(slot, i));
        QByteArray data = read(1024);
        if (data.isEmpty()) break;
        
        buffer.append(data);
        
        int numFrames = buffer.size() / 6;
        int bytesToProcess = numFrames * 6;
        
        if (bytesToProcess > 0) {
            std::vector<int32_t> samples = Protocol::parseAudioData(buffer.left(bytesToProcess), false);
            fullAudio.insert(fullAudio.end(), samples.begin(), samples.end());
            buffer.remove(0, bytesToProcess);
        }
        
        if (callback && (i % 10 == 0)) callback(fullAudio.size() * 3, trackSize, userData);
    }
    if (callback) callback(trackSize, trackSize, userData);
    
    // Trim if necessary to match trackSize frames
    // trackSize is bytes. 1 frame = 6 bytes. 2 samples per frame.
    // Total samples = (trackSize / 6) * 2 = trackSize / 3.
    size_t expectedSamples = trackSize / 3;
    if (fullAudio.size() > expectedSamples) {
        fullAudio.resize(expectedSamples);
    }

    return fullAudio;
}

void USBDevice::uploadTrack(int slot, const std::vector<int32_t>& audio, ProgressCallback callback, void* userData) {
    // 1. Init
    write(Protocol::createInitUploadCommand());
    read(64, Protocol::EP_IN_STATUS);
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // 2. Prepare Data
    QByteArray audioData = Protocol::encodeAudioData(audio);
    uint32_t size = audioData.size();
    
    QByteArray metaChunk(1024, 0);
    qToLittleEndian<uint32_t>(size, reinterpret_cast<uchar*>(metaChunk.data()));
    
    // Send Chunk 0 (Meta)
    write(Protocol::createUploadCommand(slot, 0));
    read(64, Protocol::EP_IN_STATUS);
    write(metaChunk, 0x03); // EP_OUT_DATA
    read(64, Protocol::EP_IN_STATUS);

    // Send chunks 1+
    int totalChunks = (audioData.size() + 1023) / 1024;
    for (int i = 0; i < totalChunks; i++) {
        int offset = i * 1024;
        QByteArray chunk = audioData.mid(offset, 1024);
        if (chunk.size() < 1024) chunk.resize(1024); // Zero pad
        
        write(Protocol::createUploadCommand(slot, i + 1));
        read(64, Protocol::EP_IN_STATUS);
        
        write(chunk, 0x03);
        read(64, Protocol::EP_IN_STATUS);
        
        if (callback && (i % 10 == 0)) callback(offset, size, userData);
    }
    if (callback) callback(size, size, userData);
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    // Finalize/Verify
    write(Protocol::createDownloadCommand(slot, 0)); // Query
    read(1024);
}

void USBDevice::startStreaming(int slot, std::function<void(const std::vector<int32_t>&)> audioCallback, std::atomic<bool>& stopFlag) {
    // Get info
    write(Protocol::createDownloadCommand(slot, 0));
    QByteArray firstChunk = read(1024);
    uint32_t size;
    if (!Protocol::parseTrackInfoHeader(firstChunk, size)) return;
    
    int chunks = (size + 1023) / 1024;
    QByteArray remainderBuffer;
    
    for (int i = 1; i <= chunks; i++) {
        if (stopFlag) break;
        
        write(Protocol::createDownloadCommand(slot, i));
        QByteArray data = read(1024);
        if (data.isEmpty()) break;
        
        remainderBuffer.append(data);
        
        int numFrames = remainderBuffer.size() / 6;
        int bytesToProcess = numFrames * 6;
        
        if (bytesToProcess > 0) {
            std::vector<int32_t> samples = Protocol::parseAudioData(remainderBuffer.left(bytesToProcess), false);
            audioCallback(samples);
            remainderBuffer.remove(0, bytesToProcess);
        }
    }
}
