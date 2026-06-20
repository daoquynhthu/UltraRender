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
            if (p.contains("min_weight")) cfg.path_guiding.min_weight = p["min_weight"].get<double>();
            if (p.contains("decay")) cfg.path_guiding.decay = p["decay"].get<double>();
            if (p.contains("decay_interval")) cfg.path_guiding.decay_interval = p["decay_interval"].get<int>();
            if (p.contains("spatial_cell_count")) cfg.path_guiding.spatial_cell_count = p["spatial_cell_count"].get<int>();
            if (p.contains("directional_bin_count")) cfg.path_guiding.directional_bin_count = p["directional_bin_count"].get<int>();
        }
        if (j.contains("environment_light")) {
            auto& e = j["environment_light"];
            if (e.contains("direct_sampling")) cfg.environment_light.direct_sampling = e["direct_sampling"].get<bool>();
            if (e.contains("intensity")) cfg.environment_light.intensity = e["intensity"].get<double>();
        }
        if (j.contains("restir_di")) {
            auto& r = j["restir_di"];
            if (r.contains("enabled")) cfg.restir_di.enabled = r["enabled"].get<bool>();
            if (r.contains("temporal_reuse")) cfg.restir_di.temporal_reuse = r["temporal_reuse"].get<bool>();
            if (r.contains("spatial_reuse")) cfg.restir_di.spatial_reuse = r["spatial_reuse"].get<bool>();
            if (r.contains("unbiased")) cfg.restir_di.unbiased = r["unbiased"].get<bool>();
            if (r.contains("max_history")) cfg.restir_di.max_history = r["max_history"].get<int>();
        }
        if (j.contains("integrator")) {
            auto& i = j["integrator"];
            if (i.contains("mode")) cfg.integrator.mode = i["mode"].get<std::string>();
            if (i.contains("sampler")) cfg.integrator.sampler = i["sampler"].get<std::string>();
            if (i.contains("quality_preset")) cfg.integrator.quality_preset = i["quality_preset"].get<std::string>();
            if (i.contains("allow_biased_reuse")) cfg.integrator.allow_biased_reuse = i["allow_biased_reuse"].get<bool>();
            if (i.contains("specular_manifold")) {
                auto& s = i["specular_manifold"];
                if (s.contains("enabled")) cfg.integrator.specular_manifold.enabled = s["enabled"].get<bool>();
                if (s.contains("max_specular_events")) cfg.integrator.specular_manifold.max_specular_events = s["max_specular_events"].get<int>();
                if (s.contains("solver_tolerance")) cfg.integrator.specular_manifold.solver_tolerance = s["solver_tolerance"].get<double>();
                if (s.contains("max_newton_iterations")) cfg.integrator.specular_manifold.max_newton_iterations = s["max_newton_iterations"].get<int>();
            }
            if (i.contains("mlt")) {
                auto& m = i["mlt"];
                if (m.contains("enabled")) cfg.integrator.mlt.enabled = m["enabled"].get<bool>();
                if (m.contains("chain_count")) cfg.integrator.mlt.chain_count = m["chain_count"].get<int>();
                if (m.contains("mutations_per_chain")) cfg.integrator.mlt.mutations_per_chain = m["mutations_per_chain"].get<int>();
                if (m.contains("large_step_probability")) cfg.integrator.mlt.large_step_probability = m["large_step_probability"].get<double>();
                if (m.contains("small_step_sigma")) cfg.integrator.mlt.small_step_sigma = m["small_step_sigma"].get<double>();
                if (m.contains("seed")) cfg.integrator.mlt.seed = m["seed"].get<std::uint32_t>();
            }
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
    double path_guiding_min_weight = -1.0;
    double path_guiding_decay = -1.0;
    int path_guiding_decay_interval = -1;
    int path_guiding_spatial_cell_count = -1;
    int path_guiding_directional_bin_count = -1;
    bool environment_light_direct_sampling = false;
    double environment_light_intensity = -1.0;
    bool restir_di = false;
    bool restir_di_temporal_reuse = true;
    bool restir_di_spatial_reuse = false;
    bool restir_di_unbiased = false;
    int restir_di_max_history = -1;
    bool integrator_specular_manifold = false;
    std::string integrator_mode;
    std::string integrator_sampler;
    std::string integrator_quality_preset;
    bool integrator_allow_biased_reuse = false;
    int integrator_specular_max_events = -1;
    double integrator_specular_tolerance = -1.0;
    int integrator_specular_newton_iterations = -1;
    bool integrator_mlt = false;
    int integrator_mlt_chain_count = -1;
    int integrator_mlt_mutations_per_chain = -1;
    double integrator_mlt_large_step_probability = -1.0;
    double integrator_mlt_small_step_sigma = -1.0;
    std::uint32_t integrator_mlt_seed = 0;
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
    render_cmd->add_option("--path-guiding-min-weight", path_guiding_min_weight, "Minimum visible contribution recorded by path guiding");
    render_cmd->add_option("--path-guiding-decay", path_guiding_decay, "Multiplicative path guiding history decay");
    render_cmd->add_option("--path-guiding-decay-interval", path_guiding_decay_interval, "Render passes between path guiding decay epochs");
    render_cmd->add_option("--path-guiding-spatial-cells", path_guiding_spatial_cell_count, "Spatial cell count for GPU path guiding cache");
    render_cmd->add_option("--path-guiding-directional-bins", path_guiding_directional_bin_count, "Direction bin count per spatial cell/light for GPU path guiding cache");
    render_cmd->add_flag("--enable-environment-light-sampling", environment_light_direct_sampling, "Add the radiometric sky/background to explicit direct-light sampling");
    render_cmd->add_option("--environment-light-intensity", environment_light_intensity, "Radiometric sky/background intensity scale for environment direct sampling");
    render_cmd->add_flag("--enable-restir-di", restir_di, "Enable ReSTIR direct-light reservoir reuse");
    render_cmd->add_flag("--restir-di-temporal-reuse,!--no-restir-di-temporal-reuse", restir_di_temporal_reuse, "Enable temporal ReSTIR DI candidate reuse");
    render_cmd->add_flag("--restir-di-spatial-reuse", restir_di_spatial_reuse, "Request spatial ReSTIR DI reuse");
    render_cmd->add_flag("--restir-di-unbiased", restir_di_unbiased, "Request unbiased ReSTIR DI mode");
    render_cmd->add_option("--restir-di-max-history", restir_di_max_history, "Maximum temporal history carried by ReSTIR DI");
    render_cmd->add_flag("--enable-integrator-specular-manifold", integrator_specular_manifold, "Request radiometric specular manifold integration");
    render_cmd->add_option("--integrator-mode", integrator_mode, "Integrator mode: wavefront, path_guided, restir_di, specular_manifold, mlt");
    render_cmd->add_option("--integrator-sampler", integrator_sampler, "Integrator sampler: default, low_discrepancy, primary_sample_space");
    render_cmd->add_option("--integrator-quality-preset", integrator_quality_preset, "Integrator quality preset: default, preview, final, research");
    render_cmd->add_flag("--allow-biased-integrator-reuse", integrator_allow_biased_reuse, "Allow explicitly biased reuse integrators such as current ReSTIR DI baseline");
    render_cmd->add_option("--specular-manifold-max-events", integrator_specular_max_events, "Maximum specular events in a manifold connection");
    render_cmd->add_option("--specular-manifold-tolerance", integrator_specular_tolerance, "Specular manifold solver tolerance");
    render_cmd->add_option("--specular-manifold-newton-iterations", integrator_specular_newton_iterations, "Specular manifold Newton iteration cap");
    render_cmd->add_flag("--enable-mlt", integrator_mlt, "Request primary-sample-space MLT integration");
    render_cmd->add_option("--mlt-chain-count", integrator_mlt_chain_count, "Number of independent MLT chains");
    render_cmd->add_option("--mlt-mutations-per-chain", integrator_mlt_mutations_per_chain, "Mutations per MLT chain");
    render_cmd->add_option("--mlt-large-step-probability", integrator_mlt_large_step_probability, "MLT large-step probability");
    render_cmd->add_option("--mlt-small-step-sigma", integrator_mlt_small_step_sigma, "MLT small-step mutation sigma");
    render_cmd->add_option("--mlt-seed", integrator_mlt_seed, "MLT primary-sample-space seed");
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
        if (path_guiding_min_weight >= 0.0) cfg.path_guiding.min_weight = path_guiding_min_weight;
        if (path_guiding_decay >= 0.0) cfg.path_guiding.decay = path_guiding_decay;
        if (path_guiding_decay_interval >= 0) cfg.path_guiding.decay_interval = path_guiding_decay_interval;
        if (path_guiding_spatial_cell_count >= 0) cfg.path_guiding.spatial_cell_count = path_guiding_spatial_cell_count;
        if (path_guiding_directional_bin_count >= 0) cfg.path_guiding.directional_bin_count = path_guiding_directional_bin_count;
        if (environment_light_direct_sampling) cfg.environment_light.direct_sampling = true;
        if (environment_light_intensity >= 0.0) cfg.environment_light.intensity = environment_light_intensity;
        if (restir_di) cfg.restir_di.enabled = true;
        cfg.restir_di.temporal_reuse = restir_di_temporal_reuse;
        if (restir_di_spatial_reuse) cfg.restir_di.spatial_reuse = true;
        if (restir_di_unbiased) cfg.restir_di.unbiased = true;
        if (restir_di_max_history > 0) cfg.restir_di.max_history = restir_di_max_history;
        if (integrator_specular_manifold) cfg.integrator.specular_manifold.enabled = true;
        if (!integrator_mode.empty()) cfg.integrator.mode = integrator_mode;
        if (!integrator_sampler.empty()) cfg.integrator.sampler = integrator_sampler;
        if (!integrator_quality_preset.empty()) cfg.integrator.quality_preset = integrator_quality_preset;
        if (integrator_allow_biased_reuse) cfg.integrator.allow_biased_reuse = true;
        if (integrator_specular_max_events > 0) cfg.integrator.specular_manifold.max_specular_events = integrator_specular_max_events;
        if (integrator_specular_tolerance > 0.0) cfg.integrator.specular_manifold.solver_tolerance = integrator_specular_tolerance;
        if (integrator_specular_newton_iterations > 0) cfg.integrator.specular_manifold.max_newton_iterations = integrator_specular_newton_iterations;
        if (integrator_mlt) cfg.integrator.mlt.enabled = true;
        if (integrator_mlt_chain_count > 0) cfg.integrator.mlt.chain_count = integrator_mlt_chain_count;
        if (integrator_mlt_mutations_per_chain > 0) cfg.integrator.mlt.mutations_per_chain = integrator_mlt_mutations_per_chain;
        if (integrator_mlt_large_step_probability >= 0.0) cfg.integrator.mlt.large_step_probability = integrator_mlt_large_step_probability;
        if (integrator_mlt_small_step_sigma > 0.0) cfg.integrator.mlt.small_step_sigma = integrator_mlt_small_step_sigma;
        if (integrator_mlt_seed > 0) cfg.integrator.mlt.seed = integrator_mlt_seed;
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
