#include "ure/config.hpp"
#include <cstring>
#include <cstdlib>

namespace ure::config {

std::optional<RenderConfig> load_config(const std::string& path) {
    // TODO: Phase I — real JSON config parser
    (void)path;
    return std::nullopt; // Not implemented yet
}

RenderConfig parse_cli(int argc, char** argv, const RenderConfig& base) {
    RenderConfig cfg = base;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 < argc) return argv[++i];
            return "";
        };
        if (arg == "-s" || arg == "--spp") {
            cfg.spp = std::stoi(next());
        } else if (arg == "-o" || arg == "--output") {
            cfg.output_path = next();
        } else if (arg == "--width") {
            cfg.width = std::stoi(next());
        } else if (arg == "--height") {
            cfg.height = std::stoi(next());
        } else if (arg == "--scene") {
            cfg.scene_path = next();
        } else if (arg == "--fov") {
            cfg.fov = std::stof(next());
        } else if (arg == "--physics") {
            cfg.physics_enabled = true;
        } else if (arg == "--audio") {
            cfg.enable_audio = true;
        } else if (arg == "--cam-pos") {
            if (i + 3 < argc) {
                cfg.camera_pos.x = std::stof(argv[++i]);
                cfg.camera_pos.y = std::stof(argv[++i]);
                cfg.camera_pos.z = std::stof(argv[++i]);
            }
        } else if (arg == "--cam-look") {
            if (i + 3 < argc) {
                cfg.camera_look.x = std::stof(argv[++i]);
                cfg.camera_look.y = std::stof(argv[++i]);
                cfg.camera_look.z = std::stof(argv[++i]);
            }
        }
    }
    return cfg;
}

} // namespace ure::config
