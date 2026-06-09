#include "ure/image_loader.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include <ure/log.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

namespace ure::io {

bool load_image_rgb32f(const std::string& file_path, gpu::HostTexture& out_texture) {
    int w, h, channels;
    float* data = stbi_loadf(file_path.c_str(), &w, &h, &channels, 3);
    if (!data) {
        UR_LOG_WARN(SceneIO, "could not load {}: {}", file_path, stbi_failure_reason());
        return false;
    }

    out_texture.width = w;
    out_texture.height = h;
    out_texture.data.assign(data, data + static_cast<size_t>(w) * static_cast<size_t>(h) * 3);
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
