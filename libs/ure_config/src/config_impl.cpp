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
        if (j.contains("wave_optics")) {
            auto& w = j["wave_optics"];
            if (w.contains("mode")) cfg.wave_optics.mode = w["mode"].get<std::string>();
            if (w.contains("camera_diffraction") && w["camera_diffraction"].contains("enabled"))
                cfg.wave_optics.camera_diffraction_enabled = w["camera_diffraction"]["enabled"].get<bool>();
            if (w.contains("coherent_field") && w["coherent_field"].contains("enabled"))
                cfg.wave_optics.coherent_field_enabled = w["coherent_field"]["enabled"].get<bool>();
            if (w.contains("partial_coherence") && w["partial_coherence"].contains("enabled"))
                cfg.wave_optics.partial_coherence_enabled = w["partial_coherence"]["enabled"].get<bool>();
            if (w.contains("diffractive_materials") && w["diffractive_materials"].contains("enabled"))
                cfg.wave_optics.diffractive_materials_enabled = w["diffractive_materials"]["enabled"].get<bool>();
            if (w.contains("fluorescence") && w["fluorescence"].contains("enabled"))
                cfg.wave_optics.fluorescence_enabled = w["fluorescence"]["enabled"].get<bool>();
            if (w.contains("specular_manifold") && w["specular_manifold"].contains("enabled"))
                cfg.wave_optics.specular_manifold_enabled = w["specular_manifold"]["enabled"].get<bool>();
            if (w.contains("local_fullwave") && w["local_fullwave"].contains("enabled"))
                cfg.wave_optics.local_fullwave_enabled = w["local_fullwave"]["enabled"].get<bool>();
            if (w.contains("experimental_allow_preview_degradation"))
                cfg.wave_optics.experimental_allow_preview_degradation =
                    w["experimental_allow_preview_degradation"].get<bool>();
        }
        if (j.contains("path_guiding")) {
            auto& p = j["path_guiding"];
            if (p.contains("enabled")) cfg.path_guiding.enabled = p["enabled"].get<bool>();
            if (p.contains("light_mixture")) cfg.path_guiding.light_mixture = p["light_mixture"].get<double>();
            if (p.contains("learning_rate")) cfg.path_guiding.learning_rate = p["learning_rate"].get<double>();
        }
        if (j.contains("restir_di")) {
            auto& r = j["restir_di"];
            if (r.contains("enabled")) cfg.restir_di.enabled = r["enabled"].get<bool>();
            if (r.contains("temporal_reuse")) cfg.restir_di.temporal_reuse = r["temporal_reuse"].get<bool>();
            if (r.contains("spatial_reuse")) cfg.restir_di.spatial_reuse = r["spatial_reuse"].get<bool>();
            if (r.contains("unbiased")) cfg.restir_di.unbiased = r["unbiased"].get<bool>();
            if (r.contains("max_history")) cfg.restir_di.max_history = r["max_history"].get<int>();
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
    std::string wave_optics_mode;
    bool wave_camera_diffraction = false;
    bool wave_coherent_field = false;
    bool wave_partial_coherence = false;
    bool wave_diffractive_materials = false;
    bool wave_fluorescence = false;
    bool wave_specular_manifold = false;
    bool wave_local_fullwave = false;
    bool wave_preview_degradation = false;
    bool path_guiding = false;
    double path_guiding_light_mixture = -1.0;
    double path_guiding_learning_rate = -1.0;
    bool restir_di = false;
    bool restir_di_temporal_reuse = true;
    bool restir_di_spatial_reuse = false;
    bool restir_di_unbiased = false;
    int restir_di_max_history = -1;
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
    render_cmd->add_option("--wave-optics-mode", wave_optics_mode, "Wave optics mode: radiometric, camera_diffraction, coherent_field, partial_coherence");
    render_cmd->add_flag("--enable-camera-diffraction", wave_camera_diffraction, "Enable wave-optics camera diffraction");
    render_cmd->add_flag("--enable-coherent-field", wave_coherent_field, "Enable coherent complex-field transport");
    render_cmd->add_flag("--enable-partial-coherence", wave_partial_coherence, "Enable partial-coherence transport");
    render_cmd->add_flag("--enable-diffractive-materials", wave_diffractive_materials, "Enable diffractive material operators");
    render_cmd->add_flag("--enable-fluorescence", wave_fluorescence, "Enable fluorescence wavelength conversion");
    render_cmd->add_flag("--enable-specular-manifold", wave_specular_manifold, "Enable specular manifold/refractive direct-light paths");
    render_cmd->add_flag("--enable-local-fullwave", wave_local_fullwave, "Enable local full-wave solver coupling");
    render_cmd->add_flag("--allow-wave-preview-degradation", wave_preview_degradation, "Allow explicit non-physical preview degradation for unsupported wave nodes");
    render_cmd->add_flag("--enable-path-guiding", path_guiding, "Enable progressive path guiding for supported radiometric samplers");
    render_cmd->add_option("--path-guiding-light-mixture", path_guiding_light_mixture, "Mixture weight for guided direct-light sampling");
    render_cmd->add_option("--path-guiding-learning-rate", path_guiding_learning_rate, "Progressive path guiding light-weight update scale");
    render_cmd->add_flag("--enable-restir-di", restir_di, "Enable ReSTIR direct-light reservoir reuse");
    render_cmd->add_flag("--restir-di-temporal-reuse,!--no-restir-di-temporal-reuse", restir_di_temporal_reuse, "Enable temporal ReSTIR DI candidate reuse");
    render_cmd->add_flag("--restir-di-spatial-reuse", restir_di_spatial_reuse, "Request spatial ReSTIR DI reuse");
    render_cmd->add_flag("--restir-di-unbiased", restir_di_unbiased, "Request unbiased ReSTIR DI mode");
    render_cmd->add_option("--restir-di-max-history", restir_di_max_history, "Maximum temporal history carried by ReSTIR DI");
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
        if (!wave_optics_mode.empty()) cfg.wave_optics.mode = wave_optics_mode;
        if (wave_camera_diffraction) cfg.wave_optics.camera_diffraction_enabled = true;
        if (wave_coherent_field) cfg.wave_optics.coherent_field_enabled = true;
        if (wave_partial_coherence) cfg.wave_optics.partial_coherence_enabled = true;
        if (wave_diffractive_materials) cfg.wave_optics.diffractive_materials_enabled = true;
        if (wave_fluorescence) cfg.wave_optics.fluorescence_enabled = true;
        if (wave_specular_manifold) cfg.wave_optics.specular_manifold_enabled = true;
        if (wave_local_fullwave) cfg.wave_optics.local_fullwave_enabled = true;
        if (wave_preview_degradation) cfg.wave_optics.experimental_allow_preview_degradation = true;
        if (path_guiding) cfg.path_guiding.enabled = true;
        if (path_guiding_light_mixture >= 0.0) cfg.path_guiding.light_mixture = path_guiding_light_mixture;
        if (path_guiding_learning_rate >= 0.0) cfg.path_guiding.learning_rate = path_guiding_learning_rate;
        if (restir_di) cfg.restir_di.enabled = true;
        cfg.restir_di.temporal_reuse = restir_di_temporal_reuse;
        if (restir_di_spatial_reuse) cfg.restir_di.spatial_reuse = true;
        if (restir_di_unbiased) cfg.restir_di.unbiased = true;
        if (restir_di_max_history > 0) cfg.restir_di.max_history = restir_di_max_history;
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
