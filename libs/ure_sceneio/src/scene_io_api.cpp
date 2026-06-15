#include "ure/scene_io.hpp"
#include "ure/gltf_scene_frontend.hpp"
#include "ure/image_loader.hpp"
#include "ure/image_saver.hpp"
#include "ure/spd_loader.hpp"

namespace ure::scene_io {

scene_ir::SceneIR load_gltf(const std::string& path) {
    return GltfSceneFrontend::parse_file_to_ir(path);
}

bool load_image(const std::string& path, gpu::HostTexture& out_tex) {
    return io::load_image_rgb32f(path, out_tex);
}

bool save_bmp(const std::string& path,
              const std::vector<core::Vec3f>& pixels,
              int width, int height) {
    return io::ImageSaver::save_bmp(path, width, height, pixels);
}

std::vector<float> load_spd(const std::string& path, int num_wavelengths) {
    auto raw = spectral::load_spd_file(path);
    if (raw.lambdas.empty()) return {};
    return spectral::resample_uniform(raw, num_wavelengths);
}

std::vector<float> load_spd(const std::string& path, const RenderConfig& config) {
    return load_spd(path, spectral_packet_lanes(config));
}

} // namespace ure::scene_io
