#include "ure/config.hpp"
#include <CLI11/CLI11.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

namespace ure::config {

using json = nlohmann::json;

RenderConfig load_config(const std::string& path) {
    RenderConfig cfg;
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "[config] Warning: could not open '" << path << "', using defaults.\n";
        return cfg;
    }
    try {
        json j;
        f >> j;
        if (j.contains("spectral")) {
            auto& s = j["spectral"];
            if (s.contains("bands")) cfg.spectral.bands = s["bands"].get<int>();
            if (s.contains("domain_bins")) cfg.spectral.domain_bins = s["domain_bins"].get<std::uint64_t>();
            if (s.contains("packet_lanes")) cfg.spectral.packet_lanes = s["packet_lanes"].get<int>();
            if (s.contains("max_resident_mb")) cfg.spectral.max_resident_mb = s["max_resident_mb"].get<int>();
            if (s.contains("sampling_mode")) cfg.spectral.sampling_mode = s["sampling_mode"].get<std::string>();
            if (s.contains("spd_search_paths"))
                cfg.spectral.spd_search_paths = s["spd_search_paths"].get<std::vector<std::string>>();
        }
        if (j.contains("renderer")) {
            auto& r = j["renderer"];
            if (r.contains("max_depth")) cfg.renderer.max_depth = r["max_depth"].get<int>();
            if (r.contains("rr_min_prob")) cfg.renderer.rr_min_prob = r["rr_min_prob"].get<double>();
            if (r.contains("spp")) cfg.renderer.spp = r["spp"].get<int>();
        }
        if (j.contains("output")) {
            auto& o = j["output"];
            if (o.contains("file")) cfg.output.file = o["file"].get<std::string>();
            if (o.contains("tonemap")) cfg.output.tonemap = o["tonemap"].get<std::string>();
            if (o.contains("format")) cfg.output.format = o["format"].get<std::string>();
        }
        if (j.contains("gpu")) {
            auto& g = j["gpu"];
            if (g.contains("device_ids"))
                cfg.gpu.device_ids = g["device_ids"].get<std::vector<int>>();
            if (g.contains("wavefront_capacity"))
                cfg.gpu.wavefront_capacity = g["wavefront_capacity"].get<int>();
        }
    } catch (const std::exception& e) {
        std::cerr << "[config] JSON parse error in '" << path << "': " << e.what() << "\n";
    }
    return cfg;
}

CliResult parse_cli(int argc, char** argv) {
    CliResult result;
    CLI::App app{"UltraRender Engine - GPU Path Tracer"};
    app.require_subcommand(1);

    bool verbose = false, quiet = false;
    app.add_flag("-v,--verbose", verbose, "Verbose output (Debug level)");
    app.add_flag("-q,--quiet", quiet, "Quiet output (Error+ only)");

    auto* render_cmd = app.add_subcommand("render", "Render a scene file");
    std::string scene_render, config_render, output_render, format_render;
    int spp_render = 0, width_render = 0, height_render = 0;
    std::uint64_t spectral_domain_bins = 0;
    int spectral_packet_lanes = 0, spectral_max_resident_mb = 0;
    std::string spectral_sampling_mode;
    bool physics = false, audio = false;
    render_cmd->add_option("scene", scene_render, "Path to scene file (glTF)")->required();
    render_cmd->add_option("-c,--config", config_render, "Path to JSON config file");
    render_cmd->add_option("--spp", spp_render, "Samples per pixel");
    render_cmd->add_option("--width", width_render, "Render width");
    render_cmd->add_option("--height", height_render, "Render height");
    render_cmd->add_option("-o,--output", output_render, "Output image path");
    render_cmd->add_option("--format", format_render, "Output format: bmp, ppm, hdr");
    render_cmd->add_option("--spectral-domain-bins", spectral_domain_bins, "Spectral resource/domain resolution");
    render_cmd->add_option("--spectral-packet-lanes", spectral_packet_lanes, "GPU spectral packet lanes");
    render_cmd->add_option("--spectral-max-resident-mb", spectral_max_resident_mb, "Resident spectral resource budget in MB");
    render_cmd->add_option("--spectral-sampling", spectral_sampling_mode, "Spectral sampling mode");
    render_cmd->add_flag("--physics", physics, "Enable physics simulation");
    render_cmd->add_flag("--audio", audio, "Enable audio rendering");

    auto* info_cmd = app.add_subcommand("info", "Print scene information");
    std::string scene_info;
    info_cmd->add_option("scene", scene_info, "Path to scene file")->required();

    app.add_subcommand("list-devices", "List available CUDA devices");

    auto* validate_cmd = app.add_subcommand("validate", "Validate a scene file");
    std::string scene_validate;
    validate_cmd->add_option("scene", scene_validate, "Path to scene file")->required();

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        std::exit(app.exit(e));
    }

    result.verbose = verbose;
    result.quiet = quiet;

    if (*render_cmd) {
        result.command = CliCommand::Render;
        result.scene_path = scene_render;
        result.config_path = config_render;
        RenderConfig cfg;
        if (!config_render.empty())
            cfg = load_config(config_render);
        if (spp_render > 0) cfg.renderer.spp = spp_render;
        if (width_render > 0) cfg.width = width_render;
        if (height_render > 0) cfg.height = height_render;
        if (!output_render.empty()) cfg.output.file = output_render;
        if (!format_render.empty()) cfg.output.format = format_render;
        if (spectral_domain_bins > 0) cfg.spectral.domain_bins = spectral_domain_bins;
        if (spectral_packet_lanes > 0) cfg.spectral.packet_lanes = spectral_packet_lanes;
        if (spectral_max_resident_mb > 0) cfg.spectral.max_resident_mb = spectral_max_resident_mb;
        if (!spectral_sampling_mode.empty()) cfg.spectral.sampling_mode = spectral_sampling_mode;
        cfg.physics_enabled = physics;
        cfg.enable_audio = audio;
        cfg.scene_path = scene_render;
        result.config = cfg;
    } else if (*info_cmd) {
        result.command = CliCommand::Info;
        result.scene_path = scene_info;
    } else if (*validate_cmd) {
        result.command = CliCommand::Validate;
        result.scene_path = scene_validate;
    } else {
        result.command = CliCommand::ListDevices;
    }
    return result;
}

} // namespace ure::config
