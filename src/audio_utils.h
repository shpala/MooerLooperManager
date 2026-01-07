#ifndef AUDIO_UTILS_H
#define AUDIO_UTILS_H

#include <vector>
#include <string>
#include <cstdint>

class AudioUtils {
public:
    // Reads WAV file and returns stereo interleaved 32-bit samples (scaled)
    // Handles resampling to 44100 Hz if possible (simple decimation/interpolation) or throws
    // Handles Mono -> Stereo conversion
    static std::vector<int32_t> loadWavFile(const std::string& filename);
    
    static bool saveWavFile(const std::string& filename, const std::vector<int32_t>& samples);
};

#endif // AUDIO_UTILS_H
