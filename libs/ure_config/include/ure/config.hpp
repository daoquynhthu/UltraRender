#pragma once

#include "ure/ure_api.hpp"
#include <string>
#include <vector>
#include <optional>

namespace ure::config {

struct SpectralConfig {
    int bands = 64;
    std::vector<std::string> spd_search_paths = {"./spds/"};
};

struct RendererConfig {
    int max_depth = 50;
    double rr_min_prob = 0.05;
    int spp = 64;
};

struct OutputConfig {
    std::string file = "output.bmp";
    std::string tonemap = "aces";
    std::string format = "bmp";
};

struct GpuConfig {
    std::vector<int> device_ids = {0};
    int wavefront_capacity = 0;
};

struct RenderConfig {
    int width = 1280;
    int height = 720;
    float fov = 45.0f;
    core::Vec3f camera_pos = {0.0f, 5.0f, 15.0f};
    core::Vec3f camera_look = {0.0f, 0.0f, 0.0f};
    std::string scene_path;
    bool physics_enabled = false;
    float physics_dt = 1.0f / 60.0f;
    int physics_frames = 180;
    int physics_spp_per_frame = 32;
    bool enable_audio = false;
    SpectralConfig spectral;
    RendererConfig renderer;
    OutputConfig output;
    GpuConfig gpu;
};

enum class CliCommand {
    Render,
    Info,
    ListDevices,
    Validate
};

struct CliResult {
    CliCommand command = CliCommand::Render;
    RenderConfig config;
    std::string scene_path;
    std::string config_path;
    bool verbose = false;
    bool quiet = false;
};

CliResult parse_cli(int argc, char** argv);

RenderConfig load_config(const std::string& path);

inline RenderSettings make_render_config(const RenderConfig& cfg) {
    RenderSettings s;
    s.width = cfg.width;
    s.height = cfg.height;
    s.spp = cfg.renderer.spp;
    s.output_path = cfg.output.file;
    return s;
}

inline Scene make_scene(const RenderConfig& cfg) {
    Scene scene;
    scene.width = cfg.width;
    scene.height = cfg.height;
    scene.spp = cfg.renderer.spp;
    scene.camera.position = cfg.camera_pos;
    scene.camera.look_at = cfg.camera_look;
    scene.camera.fov = cfg.fov;
    return scene;
}

} // namespace ure::config
