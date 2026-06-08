#include "api/scene_frontend.hpp"

#include "api/gltf_scene_frontend.hpp"
#include "api/scene_ir_frontend.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
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

    return LegacySceneFrontend::parse_file_to_ir(filepath);
}

} // namespace ure
