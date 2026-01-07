#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <vector>
#include <cstdint>
#include <QString>
#include <QByteArray>

struct TrackInfo {
    int slot;
    bool has_track;
    double duration;
    uint32_t size;
};

class Protocol {
public:
    static const uint16_t VENDOR_ID = 0x34DB;
    static const uint16_t PRODUCT_ID = 0x0008;
    static const int EP_OUT = 0x02;
    static const int EP_IN_STATUS = 0x81;
    static const int EP_IN_DATA = 0x83;
    static const int MAX_TRACKS = 100;

    static QByteArray createDeleteCommand(int slot);
    static QByteArray createDownloadCommand(int slot, uint16_t chunk = 0);
    static QByteArray createUploadCommand(int slot, uint16_t chunk);
    static QByteArray createInitUploadCommand();
    static QByteArray createPlayCommand(int slot);
    static QByteArray createPlayStreamCommand(int slot, uint8_t chunk);

    static std::vector<TrackInfo> parseTrackList(const QByteArray& data);
    static bool parseTrackInfoHeader(const QByteArray& data, uint32_t& size);
    
    // Audio conversion helpers
    static QByteArray encodeAudioData(const std::vector<int32_t>& samples, bool stereo = true);
    static std::vector<int32_t> parseAudioData(const QByteArray& data, bool skipHeader = true);

private:
    static uint16_t calculateCRC16(const QByteArray& data);
    static const uint16_t CRC_TABLE[256];
};

#endif // PROTOCOL_H
