#include "ure/scene_frontend.hpp"

#include "ure/gltf_scene_frontend.hpp"
#include "ure/scene_ir_frontend.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace ure {

scene_ir::SceneIR SceneFrontend::parse_file_to_ir(const std::string& filepath) {
    std::string extension = std::filesystem::path(filepath).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (extension == ".gltf" || extension == ".glb") {
        return GltfSceneFrontend::parse_file_to_ir(filepath);
    }

    if (extension == ".scene") {
        return LegacySceneFrontend::parse_file_to_ir(filepath);
    }

    throw std::runtime_error("Unsupported scene format: " + extension + " (expected .gltf, .glb, or .scene)");
}

} // namespace ure
