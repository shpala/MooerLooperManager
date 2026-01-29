#ifndef USB_DEVICE_H
#define USB_DEVICE_H

#include <libusb-1.0/libusb.h>
#include <vector>
#include <string>
#include <QByteArray>
#include "protocol.h"

struct DeviceInfo {
    uint16_t vid;
    uint16_t pid;
    uint8_t bus;
    uint8_t address;
    std::string name;
    std::string serial;
    bool hasPermission;
};

class USBDevice {
public:
    USBDevice();
    ~USBDevice();

    static std::vector<DeviceInfo> enumerateDevices();
    static bool installUdevRule();
    static bool needsUdevRule();

    bool connect(uint8_t bus = 0, uint8_t address = 0);
    void disconnect();
    bool isConnected() const;
    uint8_t getBus() const { return connectedBus; }
    uint8_t getAddress() const { return connectedAddress; }

    // High level operations
    std::vector<TrackInfo> listTracks();
    void deleteTrack(int slot);
    void playTrack(int slot);
    void stopPlayback(int slot);

    // Callbacks for progress
    typedef void (*ProgressCallback)(size_t current, size_t total, void* userData);

    // Download/Upload
    std::vector<int32_t> downloadTrack(int slot, ProgressCallback callback = nullptr, void* userData = nullptr);
    void uploadTrack(int slot, const std::vector<int32_t>& audio, ProgressCallback callback = nullptr, void* userData = nullptr);

    // Streaming
    // This needs a specialized loop
    void startStreaming(int slot, std::function<void(const std::vector<int32_t>&)> audioCallback, std::atomic<bool>& stopFlag,
                        ProgressCallback progressCallback = nullptr, void* progressUserData = nullptr, int startChunk = 1);

private:
    libusb_context* ctx;
    libusb_device_handle* dev_handle;
    bool connected;
    uint8_t connectedBus;
    uint8_t connectedAddress;

    int write(const QByteArray& data, int endpoint = Protocol::EP_OUT, int timeout = 5000);
    QByteArray read(int size, int endpoint = Protocol::EP_IN_DATA, int timeout = 5000);
};

#endif // USB_DEVICE_H
