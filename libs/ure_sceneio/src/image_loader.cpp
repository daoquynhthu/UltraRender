#include "ure/image_loader.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <new>
#include <string_view>
#include <vector>

#include <ure/log.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

namespace ure::io {
namespace {

constexpr std::size_t kMaxDecodedImageBytes = UINT64_C(268435456);

bool valid_image_extent(int width, int height) {
    if (width <= 0 || height <= 0)
        return false;
    const auto width_size = static_cast<std::size_t>(width);
    const auto height_size = static_cast<std::size_t>(height);
    return width_size <= std::numeric_limits<std::size_t>::max() / height_size / 3 /
                             sizeof(float) &&
           width_size * height_size * 3 * sizeof(float) <=
               kMaxDecodedImageBytes;
}

bool probe_image_extent(const std::string& file_path, int& width,
                        int& height, int& channels) {
    if (stbi_info(file_path.c_str(), &width, &height, &channels)) {
        if (height < 0 && height != std::numeric_limits<int>::min())
            height = -height;
        return valid_image_extent(width, height);
    }
    std::ifstream input(file_path, std::ios::binary);
    std::array<unsigned char, 26> header{};
    if (!input.read(reinterpret_cast<char*>(header.data()),
                    static_cast<std::streamsize>(header.size())) ||
        header[0] != 'B' || header[1] != 'M')
        return false;
    const auto signed_value = [&header](std::size_t offset) {
        const std::uint32_t value =
            static_cast<std::uint32_t>(header[offset]) |
            (static_cast<std::uint32_t>(header[offset + 1]) << 8U) |
            (static_cast<std::uint32_t>(header[offset + 2]) << 16U) |
            (static_cast<std::uint32_t>(header[offset + 3]) << 24U);
        return static_cast<std::int32_t>(value);
    };
    const auto raw_width = signed_value(18);
    const auto raw_height = signed_value(22);
    if (raw_width <= 0 || raw_height == 0 ||
        raw_height == std::numeric_limits<std::int32_t>::min())
        return false;
    width = raw_width;
    height = raw_height < 0 ? -raw_height : raw_height;
    channels = 3;
    return valid_image_extent(width, height);
}

bool read_ppm_token(std::istream& input, std::string& token) {
    token.clear();
    char value{};
    while (input.get(value)) {
        if (std::isspace(static_cast<unsigned char>(value)))
            continue;
        if (value == '#') {
            input.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            continue;
        }
        token.push_back(value);
        break;
    }
    if (token.empty())
        return false;
    while (input.get(value)) {
        if (std::isspace(static_cast<unsigned char>(value))) {
            if (value == '\r' && input.peek() == '\n')
                input.get();
            break;
        }
        if (value == '#') {
            input.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            break;
        }
        token.push_back(value);
    }
    return true;
}

bool parse_positive(std::string_view token, int& value) {
    value = 0;
    const auto parsed = std::from_chars(token.data(),
                                        token.data() + token.size(), value);
    return parsed.ec == std::errc{} &&
           parsed.ptr == token.data() + token.size() && value > 0;
}

bool load_ppm_rgb32f(const std::string& file_path,
                     gpu::HostTexture& out_texture) {
    std::ifstream input(file_path, std::ios::binary);
    std::string token;
    if (!input || !read_ppm_token(input, token) ||
        (token != "P3" && token != "P6"))
        return false;
    const bool binary = token == "P6";
    int width{};
    int height{};
    int maximum{};
    if (!read_ppm_token(input, token) || !parse_positive(token, width) ||
        !read_ppm_token(input, token) || !parse_positive(token, height) ||
        !read_ppm_token(input, token) || !parse_positive(token, maximum) ||
        maximum > 65535)
        return false;
    if (!valid_image_extent(width, height))
        return false;
    const auto width_size = static_cast<std::size_t>(width);
    const auto height_size = static_cast<std::size_t>(height);
    const auto sample_count = width_size * height_size * 3;
    std::vector<float> samples;
    try {
        samples.resize(sample_count);
    } catch (const std::bad_alloc&) {
        return false;
    }
    if (binary) {
        const std::size_t bytes_per_sample = maximum < 256 ? 1 : 2;
        if (sample_count > std::numeric_limits<std::size_t>::max() /
                               bytes_per_sample)
            return false;
        std::vector<unsigned char> bytes(sample_count * bytes_per_sample);
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        if (input.gcount() != static_cast<std::streamsize>(bytes.size()))
            return false;
        for (std::size_t index = 0; index < sample_count; ++index) {
            const int sample = bytes_per_sample == 1
                                   ? bytes[index]
                                   : (static_cast<int>(bytes[index * 2]) << 8) |
                                         bytes[index * 2 + 1];
            if (sample > maximum)
                return false;
            samples[index] = static_cast<float>(sample) /
                             static_cast<float>(maximum);
        }
    } else {
        for (std::size_t index = 0; index < sample_count; ++index) {
            int sample{};
            if (!read_ppm_token(input, token))
                return false;
            const auto parsed = std::from_chars(
                token.data(), token.data() + token.size(), sample);
            if (parsed.ec != std::errc{} ||
                parsed.ptr != token.data() + token.size() || sample < 0 ||
                sample > maximum)
                return false;
            samples[index] = static_cast<float>(sample) /
                             static_cast<float>(maximum);
        }
    }
    out_texture.width = width;
    out_texture.height = height;
    out_texture.channels = 3;
    out_texture.data = std::move(samples);
    return true;
}

}

bool load_image_rgb32f(const std::string& file_path, gpu::HostTexture& out_texture) {
    if (load_ppm_rgb32f(file_path, out_texture))
        return true;
    int w{};
    int h{};
    int channels{};
    if (!probe_image_extent(file_path, w, h, channels)) {
        UR_LOG_WARN(SceneIO, "image dimensions are invalid or exceed the decode budget: {}", file_path);
        return false;
    }
    float* data = stbi_loadf(file_path.c_str(), &w, &h, &channels, 3);
    if (!data) {
        UR_LOG_WARN(SceneIO, "could not load {}: {}", file_path, stbi_failure_reason());
        return false;
    }

    out_texture.width = w;
    out_texture.height = h;
    out_texture.channels = 3;
    try {
        out_texture.data.assign(
            data, data + static_cast<size_t>(w) * static_cast<size_t>(h) * 3);
    } catch (const std::bad_alloc&) {
        stbi_image_free(data);
        return false;
    }
    stbi_image_free(data);
    return true;
}

void apply_image_color_space(gpu::HostTexture& texture, scene_ir::ImageColorSpace color_space) {
    if (color_space == scene_ir::ImageColorSpace::Linear) {
        return;
    }

    auto to_linear = [](float value) {
        float clamped = std::max(0.0f, std::min(1.0f, value));
        if (clamped <= 0.04045f) {
            return clamped / 12.92f;
        }
        return std::pow((clamped + 0.055f) / 1.055f, 2.4f);
    };

    for (float& channel : texture.data) {
        channel = to_linear(channel);
    }
}

} // namespace ure::io
