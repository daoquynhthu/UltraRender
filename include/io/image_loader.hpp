#pragma once

#include "gpu/gpu_scene_loader.hpp"
#include "scene/scene_ir.hpp"
#include <string>

namespace ure::io {

bool load_image_rgb32f(const std::string& file_path, gpu::HostTexture& out_texture);
void apply_image_color_space(gpu::HostTexture& texture, scene_ir::ImageColorSpace color_space);

} // namespace ure::io
