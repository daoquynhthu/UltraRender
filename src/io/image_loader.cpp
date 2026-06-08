#include "io/image_loader.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <cmath>
#include <vector>

namespace ure::io {

namespace {

template <typename T>
bool read_value(std::ifstream& file, T& value) {
    file.read(reinterpret_cast<char*>(&value), sizeof(T));
    return static_cast<bool>(file);
}

}

bool load_image_rgb32f(const std::string& file_path, gpu::HostTexture& out_texture) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    std::uint16_t file_type = 0;
    if (!read_value(file, file_type) || file_type != 0x4D42) {
        return false;
    }

    std::uint32_t file_size = 0;
    std::uint16_t reserved1 = 0;
    std::uint16_t reserved2 = 0;
    std::uint32_t pixel_offset = 0;
    read_value(file, file_size);
    read_value(file, reserved1);
    read_value(file, reserved2);
    read_value(file, pixel_offset);

    std::uint32_t dib_size = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::uint16_t planes = 0;
    std::uint16_t bits_per_pixel = 0;
    std::uint32_t compression = 0;
    std::uint32_t image_size = 0;
    std::int32_t x_ppm = 0;
    std::int32_t y_ppm = 0;
    std::uint32_t colors_used = 0;
    std::uint32_t important_colors = 0;

    if (!read_value(file, dib_size) || dib_size < 40) {
        return false;
    }

    read_value(file, width);
    read_value(file, height);
    read_value(file, planes);
    read_value(file, bits_per_pixel);
    read_value(file, compression);
    read_value(file, image_size);
    read_value(file, x_ppm);
    read_value(file, y_ppm);
    read_value(file, colors_used);
    read_value(file, important_colors);

    if (!file || planes != 1 || (bits_per_pixel != 24 && bits_per_pixel != 32) || compression != 0 || width <= 0 || height == 0) {
        return false;
    }

    const int abs_height = height > 0 ? height : -height;
    const bool is_bottom_up = height > 0;
    const int bytes_per_pixel = bits_per_pixel / 8;
    const int row_stride = ((width * bytes_per_pixel + 3) / 4) * 4;

    std::vector<std::uint8_t> row_buffer(static_cast<std::size_t>(row_stride) * abs_height);
    file.seekg(pixel_offset, std::ios::beg);
    file.read(reinterpret_cast<char*>(row_buffer.data()), row_buffer.size());
    if (!file) {
        return false;
    }

    out_texture.width = width;
    out_texture.height = abs_height;
    out_texture.data.assign(static_cast<std::size_t>(width) * abs_height * 3, 0.0f);

    for (int y = 0; y < abs_height; ++y) {
        const int src_y = is_bottom_up ? (abs_height - 1 - y) : y;
        const std::uint8_t* src_row = row_buffer.data() + static_cast<std::size_t>(src_y) * row_stride;
        for (int x = 0; x < width; ++x) {
            const std::uint8_t* px = src_row + x * bytes_per_pixel;
            const std::size_t dst = static_cast<std::size_t>(y * width + x) * 3;
            out_texture.data[dst + 0] = px[2] / 255.0f;
            out_texture.data[dst + 1] = px[1] / 255.0f;
            out_texture.data[dst + 2] = px[0] / 255.0f;
        }
    }

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
