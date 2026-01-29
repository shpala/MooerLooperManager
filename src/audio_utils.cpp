#include "audio_utils.h"
#include <QAudioDecoder>
#include <QAudioBuffer>
#include <QAudioFormat>
#include <QEventLoop>
#include <QFileInfo>
#include <QUrl>
#include <fstream>
#include <iostream>
#include <cstring>
#include <cmath>
#include <vector>

#pragma pack(push, 1)
struct WavHeader {
    char riff[4];
    uint32_t overall_size;
    char wave[4];
    char fmt_chunk_marker[4];
    uint32_t length_of_fmt;
    uint16_t format_type;
    uint16_t channels;
    uint32_t sample_rate;
    uint32_t byterate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char data_chunk_header[4];
    uint32_t data_size;
};
#pragma pack(pop)

std::vector<int32_t> AudioUtils::loadAudioFile(const std::string& filename) {
    QString qFilename = QString::fromStdString(filename);
    
    // For WAV files, try the built-in parser first as it's faster and supports 44.1kHz directly
    if (qFilename.toLower().endsWith(".wav")) {
        try {
            return loadWavFile(filename);
        } catch (const std::exception& e) {
            // If it failed because of sample rate, the decoder will handle it
            std::cerr << "Native WAV loader failed: " << e.what() << ". Falling back to QAudioDecoder." << std::endl;
        }
    }

    QAudioDecoder decoder;
    QAudioFormat format;
    format.setSampleRate(44100);
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Int32);

    decoder.setAudioFormat(format);
    decoder.setSource(QUrl::fromLocalFile(qFilename));

    std::vector<int32_t> output;
    QEventLoop loop;
    bool errorOccurred = false;
    QString errorMsg;

    QObject::connect(&decoder, &QAudioDecoder::bufferReady, [&]() {
        QAudioBuffer buffer = decoder.read();
        QAudioFormat bufferFormat = buffer.format();
        int count = buffer.sampleCount();
        
        if (bufferFormat.sampleFormat() == QAudioFormat::Int32) {
            const int32_t* data = buffer.constData<int32_t>();
            size_t currentSize = output.size();
            output.resize(currentSize + count);
            std::memcpy(output.data() + currentSize, data, count * sizeof(int32_t));
        } else if (bufferFormat.sampleFormat() == QAudioFormat::Int16) {
            const int16_t* data = buffer.constData<int16_t>();
            for (int i = 0; i < count; ++i) {
                // Scale 16-bit to 32-bit
                output.push_back(static_cast<int32_t>(data[i]) << 16);
            }
        } else if (bufferFormat.sampleFormat() == QAudioFormat::Float) {
            const float* data = buffer.constData<float>();
            for (int i = 0; i < count; ++i) {
                // Scale float to 32-bit
                float val = data[i];
                if (val > 1.0f) val = 1.0f;
                if (val < -1.0f) val = -1.0f;
                output.push_back(static_cast<int32_t>(val * 2147483647.0f));
            }
        }
    });

    QObject::connect(&decoder, &QAudioDecoder::finished, &loop, &QEventLoop::quit);
    QObject::connect(&decoder, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), [&](QAudioDecoder::Error error) {
        errorOccurred = true;
        errorMsg = decoder.errorString();
        loop.quit();
    });

    decoder.start();
    loop.exec();

    if (errorOccurred) {
        throw std::runtime_error("Decoding failed: " + errorMsg.toStdString());
    }

    if (output.empty()) {
        throw std::runtime_error("Decoding failed: No data produced. Make sure you have the necessary codecs installed.");
    }

    return output;
}

std::vector<int32_t> AudioUtils::loadWavFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) throw std::runtime_error("Cannot open file");

    WavHeader header;
    file.read((char*)&header, sizeof(WavHeader));

    if (strncmp(header.riff, "RIFF", 4) != 0 || strncmp(header.wave, "WAVE", 4) != 0) {
        throw std::runtime_error("Invalid WAV file");
    }

    if (header.sample_rate != 44100) {
        throw std::runtime_error("Only 44100 Hz supported in this version");
    }

    // Read data
    std::vector<uint8_t> rawData(header.data_size);
    file.read((char*)rawData.data(), header.data_size);

    std::vector<int32_t> output;
    int numSamples = header.data_size / header.block_align;
    output.reserve(numSamples * 2);

    int bytesPerSample = header.bits_per_sample / 8;

    for (int i = 0; i < numSamples; i++) {
        int offset = i * header.block_align;
        int32_t left = 0, right = 0;

        // Read Left
        if (bytesPerSample == 2) {
            int16_t val = *(int16_t*)(rawData.data() + offset);
            // MATCH PYTHON: Shift 8 bits, not 16.
            // 16-bit 0x7FFF -> 32-bit 0x007FFF00 (24-bit max)
            left = (int32_t)val << 8; 
        } else if (bytesPerSample == 3) {
            uint8_t* p = rawData.data() + offset;
            left = (p[0] | (p[1] << 8) | (p[2] << 16));
            left = (left << 8) >> 8; // Sign extend
            // Already 24-bit.
        } else if (bytesPerSample == 4) {
             if (header.format_type == 3) { // Float
                 float val = *(float*)(rawData.data() + offset);
                 // Float 1.0 -> 0x007FFFFF (24-bit max)
                 left = (int32_t)(val * 8388607.0f);
             } else {
                 int32_t val = *(int32_t*)(rawData.data() + offset);
                 // 32-bit -> Keep as 32-bit (Protocol::encodeAudioData will handle the >> 8)
                 left = val;
             }
        }

        // Handle Channels
        if (header.channels == 1) {
            // Mono -> Stereo (-3dB)
            left = (int32_t)(left * 0.70710678f);
            right = left;
        } else {
            // Read Right
            int chOffset = bytesPerSample;
            if (bytesPerSample == 2) {
                int16_t val = *(int16_t*)(rawData.data() + offset + chOffset);
                right = (int32_t)val << 8;
            } else if (bytesPerSample == 3) {
                uint8_t* p = rawData.data() + offset + chOffset;
                right = (p[0] | (p[1] << 8) | (p[2] << 16));
                right = (right << 8) >> 8;
            } else if (bytesPerSample == 4) {
                 if (header.format_type == 3) { // Float
                     float val = *(float*)(rawData.data() + offset + chOffset);
                     right = (int32_t)(val * 8388607.0f);
                 } else {
                     int32_t val = *(int32_t*)(rawData.data() + offset + chOffset);
                     right = val;
                 }
            }
        }

        output.push_back(left);
        output.push_back(right);
    }
    return output;
}

bool AudioUtils::saveWavFile(const std::string& filename, const std::vector<int32_t>& samples) {
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) return false;

    WavHeader header;
    memcpy(header.riff, "RIFF", 4);
    memcpy(header.wave, "WAVE", 4);
    memcpy(header.fmt_chunk_marker, "fmt ", 4);
    header.length_of_fmt = 16;
    header.format_type = 1; // PCM
    header.channels = 2;
    header.sample_rate = 44100;

    // Save as 32-bit PCM to match Python behavior
    header.bits_per_sample = 32; 
    header.block_align = 4 * 2; // 32-bit * 2 channels
    header.byterate = 44100 * header.block_align;
    memcpy(header.data_chunk_header, "data", 4);

    // 32-bit stereo frames
    int numSamples = samples.size() / 2;
    header.data_size = numSamples * 8; // 8 bytes per frame (4 bytes * 2 channels)
    header.overall_size = header.data_size + 36;

    file.write((char*)&header, sizeof(WavHeader));

    // Write samples directly
    // The samples from device/internal logic are like 0xXXXXXX00 (24-bit in 32-bit container, shifted)
    // Python saves these int32 values directly.
    file.write((const char*)samples.data(), samples.size() * sizeof(int32_t));

    return true;
}
