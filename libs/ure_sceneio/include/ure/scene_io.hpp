#pragma once

#include "ure/ure_api.hpp"
#include "ure/scene_ir.hpp"
#include "ure/render_config.hpp"
#include <string>
#include <vector>

namespace ure::gpu { struct HostTexture; }

namespace ure::scene_io {

// Load glTF scene directly into SceneIR
scene_ir::SceneIR load_gltf(const std::string& path);

// Load an image file into host texture (float RGB)
bool load_image(const std::string& path, gpu::HostTexture& out_tex);

// Save the frame buffer to a BMP file
bool save_bmp(const std::string& path,
              const std::vector<core::Vec3f>& pixels,
              int width, int height);

// SPD file loading
std::vector<float> load_spd(const std::string& path, int num_wavelengths);
std::vector<float> load_spd(const std::string& path, const RenderConfig& config);

} // namespace ure::scene_io
