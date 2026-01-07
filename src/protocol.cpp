#include "protocol.h"
#include <QDataStream>
#include <QtEndian>

const uint16_t Protocol::CRC_TABLE[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
    0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
    0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
    0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
    0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
    0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
    0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
    0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
    0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
    0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
    0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
    0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
    0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
    0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
    0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
    0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
    0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
    0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
    0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
    0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
    0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
    0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0
};

uint16_t Protocol::calculateCRC16(const QByteArray& data) {
    uint16_t chk = 0;
    for (char byte : data) {
        chk = CRC_TABLE[(chk >> 8) ^ (uint8_t)byte] ^ (chk << 8);
    }
    return (~chk) & 0xFFFF;
}

QByteArray Protocol::createDeleteCommand(int slot) {
    QByteArray cmd(64, 0);
    cmd[0] = 0x3F; cmd[1] = 0xAA; cmd[2] = 0x55;
    cmd[3] = 0x03;
    cmd[4] = 0x00;
    cmd[5] = 0x88;
    
    // Slot at offset 6 (Little Endian)
    qToLittleEndian<uint16_t>(slot, reinterpret_cast<uchar*>(cmd.data() + 6));

    // CRC on bytes 3-7 (5 bytes)
    uint16_t crc = calculateCRC16(cmd.mid(3, 5));
    cmd[8] = (crc >> 8) & 0xFF;
    cmd[9] = crc & 0xFF;

    return cmd;
}

QByteArray Protocol::createDownloadCommand(int slot, uint16_t chunk) {
    QByteArray cmd(64, 0);
    cmd[0] = 0x3F; cmd[1] = 0xAA; cmd[2] = 0x55;
    cmd[3] = 0x07;
    cmd[4] = 0x00;
    cmd[5] = 0x82; // SUBCMD_DOWNLOAD
    cmd[6] = (uint8_t)slot;
    cmd[7] = 0x00;
    
    // Chunk at offset 8 (Little Endian)
    qToLittleEndian<uint16_t>(chunk, reinterpret_cast<uchar*>(cmd.data() + 8));

    // CRC on bytes 3-11 (9 bytes)
    uint16_t crc = calculateCRC16(cmd.mid(3, 9));
    cmd[12] = (crc >> 8) & 0xFF;
    cmd[13] = crc & 0xFF;

    return cmd;
}

QByteArray Protocol::createUploadCommand(int slot, uint16_t chunk) {
    QByteArray cmd(64, 0);
    cmd[0] = 0x3F; cmd[1] = 0xAA; cmd[2] = 0x55;
    cmd[3] = 0x07;
    cmd[4] = 0x00;
    cmd[5] = 0x84; // SUBCMD_UPLOAD
    cmd[6] = (uint8_t)slot;
    cmd[7] = 0x00;
    
    qToLittleEndian<uint16_t>(chunk, reinterpret_cast<uchar*>(cmd.data() + 8));

    uint16_t crc = calculateCRC16(cmd.mid(3, 9));
    cmd[12] = (crc >> 8) & 0xFF;
    cmd[13] = crc & 0xFF;

    return cmd;
}

QByteArray Protocol::createInitUploadCommand() {
    QByteArray cmd(64, 0);
    cmd[0] = 0x3F; cmd[1] = 0xAA; cmd[2] = 0x55;
    cmd[3] = 0x01;
    cmd[4] = 0x00;
    cmd[5] = 0x86;

    uint16_t crc = calculateCRC16(cmd.mid(3, 3));
    cmd[6] = (crc >> 8) & 0xFF;
    cmd[7] = crc & 0xFF;

    return cmd;
}

QByteArray Protocol::createPlayCommand(int slot) {
    QByteArray cmd(64, 0);
    cmd[0] = 0x3F; cmd[1] = 0xAA; cmd[2] = 0x55;
    cmd[3] = 0x07;
    cmd[4] = 0x00;
    cmd[5] = 0x8A; // SUBCMD_PLAY
    cmd[6] = 0x01; // Play Action
    cmd[7] = 0x00;
    
    qToLittleEndian<uint16_t>(slot, reinterpret_cast<uchar*>(cmd.data() + 8));

    uint16_t crc = calculateCRC16(cmd.mid(3, 9));
    cmd[12] = (crc >> 8) & 0xFF;
    cmd[13] = crc & 0xFF;

    return cmd;
}

QByteArray Protocol::createPlayStreamCommand(int slot, uint8_t chunk) {
    QByteArray cmd(64, 0);
    cmd[0] = 0x3F; cmd[1] = 0xAA; cmd[2] = 0x55;
    cmd[3] = 0x07;
    cmd[4] = 0x00;
    cmd[5] = 0x8A;
    cmd[6] = 0x01;
    cmd[7] = 0x00;
    cmd[8] = (uint8_t)slot;
    cmd[9] = chunk;

    uint16_t crc = calculateCRC16(cmd.mid(3, 9));
    cmd[12] = (crc >> 8) & 0xFF;
    cmd[13] = crc & 0xFF;

    return cmd;
}

std::vector<TrackInfo> Protocol::parseTrackList(const QByteArray& data) {
    std::vector<TrackInfo> tracks;
    int offset = 16;
    
    for (int i = 0; i < MAX_TRACKS; i++) {
        if (offset + 8 > data.size()) break;
        
        bool hasTrack = data[offset] != 0;
        uint32_t size = qFromLittleEndian<uint32_t>(reinterpret_cast<const uchar*>(data.data() + offset + 4));
        
        double duration = 0;
        if (hasTrack) {
            duration = (double)size / (6.0 * 44100.0);
        }
        
        tracks.push_back({i, hasTrack, duration, size});
        offset += 8;
    }
    return tracks;
}

bool Protocol::parseTrackInfoHeader(const QByteArray& data, uint32_t& size) {
    if (data.size() < 12) return false;
    bool exists = data[0] == 0x01;
    size = qFromLittleEndian<uint32_t>(reinterpret_cast<const uchar*>(data.data() + 4));
    return exists;
}

std::vector<int32_t> Protocol::parseAudioData(const QByteArray& data, bool skipHeader) {
    int offset = skipHeader ? 18 : 0;
    if (offset >= data.size()) return {};
    
    int bytesAvailable = data.size() - offset;
    int frames = bytesAvailable / 6;
    
    std::vector<int32_t> samples;
    samples.reserve(frames * 2);
    
    const uchar* ptr = reinterpret_cast<const uchar*>(data.data() + offset);
    
    for (int i = 0; i < frames * 2; i++) {
        // 3 bytes per sample to int32
        int32_t val = ptr[0] | (ptr[1] << 8) | (ptr[2] << 16);
        // Sign extension from 24-bit
        val = (val << 8) >> 8;
        // Scale to 32-bit (optional, but matches python logic)
        val = val << 8;
        
        samples.push_back(val);
        ptr += 3;
    }
    
    return samples;
}

QByteArray Protocol::encodeAudioData(const std::vector<int32_t>& samples, bool stereo) {
    // Input is assumed to be interleaved int32 (scaled)
    // If not stereo (mono), caller must handle duplication before calling this or we do it here.
    // The Python implementation handles mono-to-stereo conversion.
    // Here we assume input is already appropriate stereo interleaved data for simplicity,
    // or we implement the mono check logic in the caller.
    
    QByteArray output;
    output.reserve(samples.size() * 3);
    
    for (int32_t sample : samples) {
        // Scale down from 32-bit to 24-bit
        int32_t val = sample >> 8;
        
        output.append((char)(val & 0xFF));
        output.append((char)((val >> 8) & 0xFF));
        output.append((char)((val >> 16) & 0xFF));
    }
    return output;
}
