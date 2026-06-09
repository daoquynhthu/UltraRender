#include "ure/wav_saver.hpp"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cmath>

namespace ure {
namespace io {

bool WavSaver::save(const std::string& filename, const std::vector<float>& samples, int sample_rate, int num_channels) {
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[WavSaver] Error: Could not open file " << filename << " for writing." << std::endl;
        return false;
    }

    // PCM Data conversion
    std::vector<int16_t> pcm_data;
    pcm_data.reserve(samples.size());
    for (float s : samples) {
        // Clamp to [-1, 1]
        float val = std::max(-1.0f, std::min(1.0f, s));
        // Scale to int16 range
        pcm_data.push_back(static_cast<int16_t>(val * 32767.0f));
    }

    int bits_per_sample = 16;
    int byte_rate = sample_rate * num_channels * bits_per_sample / 8;
    int block_align = num_channels * bits_per_sample / 8;
    int data_chunk_size = static_cast<int>(pcm_data.size() * sizeof(int16_t));
    int file_size = 36 + data_chunk_size;

    // Header
    file.write("RIFF", 4);
    file.write(reinterpret_cast<const char*>(&file_size), 4);
    file.write("WAVE", 4);

    // Format Chunk
    file.write("fmt ", 4);
    int fmt_chunk_size = 16;
    file.write(reinterpret_cast<const char*>(&fmt_chunk_size), 4);
    int16_t audio_format = 1; // PCM
    file.write(reinterpret_cast<const char*>(&audio_format), 2);
    int16_t channels = static_cast<int16_t>(num_channels);
    file.write(reinterpret_cast<const char*>(&channels), 2);
    file.write(reinterpret_cast<const char*>(&sample_rate), 4);
    file.write(reinterpret_cast<const char*>(&byte_rate), 4);
    file.write(reinterpret_cast<const char*>(&block_align), 2);
    int16_t bits = static_cast<int16_t>(bits_per_sample);
    file.write(reinterpret_cast<const char*>(&bits), 2);

    // Data Chunk
    file.write("data", 4);
    file.write(reinterpret_cast<const char*>(&data_chunk_size), 4);
    file.write(reinterpret_cast<const char*>(pcm_data.data()), data_chunk_size);

    file.close();
    return true;
}

} // namespace io
} // namespace ure
