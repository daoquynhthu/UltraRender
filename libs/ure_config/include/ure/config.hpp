#pragma once

#include "ure/ure_api.hpp"
#include <string>
#include <vector>
#include <optional>

namespace ure::config {

// Render configuration loaded from a JSON file (or defaults)
struct RenderConfig {
    int width = 1280;
    int height = 720;
    int spp = 64;
    float fov = 45.0f;
    std::string output_path = "output.bmp";
    bool physics_enabled = false;
    float physics_dt = 1.0f / 60.0f;
    int physics_frames = 180;
    int physics_spp_per_frame = 32;
    std::string scene_path;        // empty = procedural default
    bool enable_audio = false;
    core::Vec3f camera_pos = {0.0f, 5.0f, 15.0f};
    core::Vec3f camera_look = {0.0f, 0.0f, 0.0f};
};

// Load config from a JSON file. Returns empty optional if file not found.
std::optional<RenderConfig> load_config(const std::string& path);

// Parse CLI arguments (argc/argv) into a RenderConfig.
// Overrides any previously loaded config values.
RenderConfig parse_cli(int argc, char** argv, const RenderConfig& base = {});

// Convert RenderConfig → RenderSettings for use with IRenderEngine
inline RenderSettings make_render_config(const RenderConfig& cfg) {
    RenderSettings s;
    s.width = cfg.width;
    s.height = cfg.height;
    s.spp = cfg.spp;
    s.output_path = cfg.output_path;
    return s;
}

// Convert RenderConfig → Scene (for procedural default)
inline Scene make_scene(const RenderConfig& cfg) {
    Scene scene;
    scene.width = cfg.width;
    scene.height = cfg.height;
    scene.spp = cfg.spp;
    scene.camera.position = cfg.camera_pos;
    scene.camera.look_at = cfg.camera_look;
    scene.camera.fov = cfg.fov;
    return scene;
}

} // namespace ure::config
