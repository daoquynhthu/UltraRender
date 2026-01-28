#pragma once
#include <string>
#include <vector>

namespace ure {
namespace io {

class WavSaver {
public:
    // Save floating point samples to a 16-bit PCM WAV file
    // samples: Interleaved samples if num_channels > 1
    // num_channels: 1 for Mono, 2 for Stereo
    static bool save(const std::string& filename, const std::vector<float>& samples, int sample_rate = 44100, int num_channels = 1);
};

}
}
