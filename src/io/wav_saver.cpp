#include "io/wav_saver.hpp"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cmath>

namespace ure {
namespace io {

bool WavSaver::save(const std::string& filename, const std::vector<float>& samples, int sample_rate, int num_channels) {
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[WavSaver] Failed to open file: " << filename << std::endl;
        return false;
    }

    // Parameters
    int bits_per_sample = 16;
    int block_align = num_channels * bits_per_sample / 8;
    int byte_rate = sample_rate * block_align;
    int data_chunk_size = static_cast<int>(samples.size() * (bits_per_sample / 8)); // samples contains all channels interleaved
    int riff_chunk_size = 36 + data_chunk_size;

    // Write Header
    file.write("RIFF", 4);
    file.write(reinterpret_cast<const char*>(&riff_chunk_size), 4);
    file.write("WAVE", 4);
    file.write("fmt ", 4);
    
    int sub_chunk1_size = 16;
    short audio_format = 1; // PCM
    short num_channels_short = (short)num_channels;
    short bits_per_sample_short = (short)bits_per_sample;

    file.write(reinterpret_cast<const char*>(&sub_chunk1_size), 4);
    file.write(reinterpret_cast<const char*>(&audio_format), 2);
    file.write(reinterpret_cast<const char*>(&num_channels_short), 2);
    file.write(reinterpret_cast<const char*>(&sample_rate), 4);
    file.write(reinterpret_cast<const char*>(&byte_rate), 4);
    file.write(reinterpret_cast<const char*>(&block_align), 2);
    file.write(reinterpret_cast<const char*>(&bits_per_sample_short), 2);

    file.write("data", 4);
    file.write(reinterpret_cast<const char*>(&data_chunk_size), 4);

    // Write Data
    for (float sample : samples) {
        // Clamp to [-1, 1]
        float s = std::max(-1.0f, std::min(1.0f, sample));
        // Convert to 16-bit PCM
        short pcm = (short)(s * 32767.0f);
        file.write(reinterpret_cast<const char*>(&pcm), 2);
    }

    file.close();
    std::cout << "[WavSaver] Saved " << samples.size() << " samples to " << filename << std::endl;
    return true;
}

}
}
