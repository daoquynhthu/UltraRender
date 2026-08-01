#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <ure/config.hpp>
#include <ure/backend.hpp>
#include <ure/image_saver.hpp>
#include <ure/log.hpp>
#include <ure/native_adapter.hpp>
#include <ure/native_scene_tooling.hpp>
#include <ure/render.hpp>
#include <ure/scene_frontend.hpp>
#include <ure/spectral_limits.hpp>
#include <ure/wave_optics.hpp>

namespace {

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string output_format_from_path_or_config(const std::string& path, const std::string& configured_format) {
    const std::string extension = lowercase(std::filesystem::path(path).extension().string());
    if (extension == ".hdr" || extension == ".rgbe") return "hdr";
    if (extension == ".ppm") return "ppm";
    if (extension == ".bmp") return "bmp";
    return lowercase(configured_format.empty() ? "hdr" : configured_format);
}

std::string output_extension_for_format(const std::string& format) {
    const std::string normalized = lowercase(format);
    if (normalized == "hdr" || normalized == "rgbe") return ".hdr";
    if (normalized == "ppm") return ".ppm";
    if (normalized == "bmp") return ".bmp";
    return ".hdr";
}

bool save_frame(ure::IRenderEngine& engine,
                int width,
                int height,
                const std::string& path,
                const std::string& configured_format) {
    const std::vector<float>& buffer = engine.get_framebuffer();
    if (buffer.empty()) return false;

    std::vector<ure::core::Vec3f> pixels(static_cast<size_t>(width) * static_cast<size_t>(height));
    for (size_t i = 0; i < pixels.size(); ++i) {
        pixels[i] = {buffer[i * 3], buffer[i * 3 + 1], buffer[i * 3 + 2]};
    }

    const std::string tmp = path + ".tmp";
    const std::string format = output_format_from_path_or_config(path, configured_format);
    bool saved = false;
    if (format == "hdr" || format == "rgbe") {
        saved = ure::io::ImageSaver::save_hdr(tmp, width, height, pixels, 1.0f);
    } else if (format == "ppm") {
        saved = ure::io::ImageSaver::save_ppm(tmp, width, height, pixels, ure::io::ToneMapType::ACES, 1.0f);
    } else if (format == "bmp") {
        saved = ure::io::ImageSaver::save_bmp(tmp, width, height, pixels, ure::io::ToneMapType::ACES, 1.0f);
    } else {
        std::cerr << "Error: unsupported output format '" << format << "'\n";
        return false;
    }

    if (!saved) return false;
    if (std::filesystem::exists(path)) {
        std::filesystem::remove(path);
    }
    std::filesystem::rename(tmp, path);
    return std::filesystem::exists(path);
}

bool check_scene_path(const std::string& scene_path) {
    if (scene_path.empty()) {
        std::cerr << "Error: render requires an explicit URE native or adapter scene path\n";
        return false;
    }
    if (!std::filesystem::exists(scene_path)) {
        std::cerr << "Error: file not found: " << scene_path << "\n";
        return false;
    }
    return true;
}

ure::SpectralSamplingMode parse_spectral_sampling_mode(const std::string& mode) {
    const std::string value = lowercase(mode);
    if (value == "uniform_sampled") return ure::SpectralSamplingMode::UniformSampled;
    if (value == "stratified") return ure::SpectralSamplingMode::Stratified;
    if (value == "importance") return ure::SpectralSamplingMode::Importance;
    if (value == "farm_shard") return ure::SpectralSamplingMode::FarmShard;
    return ure::SpectralSamplingMode::PacketUniform;
}

bool parse_wave_optics_mode(const std::string& mode, ure::WaveOpticsMode& out) {
    const std::string value = lowercase(mode);
    if (value == "radiometric") {
        out = ure::WaveOpticsMode::Radiometric;
        return true;
    }
    if (value == "camera_diffraction") {
        out = ure::WaveOpticsMode::CameraDiffraction;
        return true;
    }
    if (value == "coherent_field") {
        out = ure::WaveOpticsMode::CoherentField;
        return true;
    }
    if (value == "partial_coherence") {
        out = ure::WaveOpticsMode::PartialCoherence;
        return true;
    }
    return false;
}

bool parse_integrator_mode(const std::string& mode, ure::IntegratorMode& out) {
    const std::string value = lowercase(mode);
    if (value == "automatic" || value == "auto" || value.empty()) {
        out = ure::IntegratorMode::Automatic;
        return true;
    }
    if (value == "wavefront") {
        out = ure::IntegratorMode::Wavefront;
        return true;
    }
    if (value == "path_guided") {
        out = ure::IntegratorMode::PathGuided;
        return true;
    }
    if (value == "restir_di") {
        out = ure::IntegratorMode::RestirDI;
        return true;
    }
    if (value == "restir_pt") {
        out = ure::IntegratorMode::RestirPT;
        return true;
    }
    if (value == "bdpt") {
        out = ure::IntegratorMode::BDPT;
        return true;
    }
    if (value == "vcm") {
        out = ure::IntegratorMode::VCM;
        return true;
    }
    if (value == "specular_manifold") {
        out = ure::IntegratorMode::SpecularManifold;
        return true;
    }
    if (value == "mlt") {
        out = ure::IntegratorMode::MLT;
        return true;
    }
    return false;
}

const char* integrator_mode_name(ure::IntegratorMode mode) {
    switch (mode) {
    case ure::IntegratorMode::Wavefront: return "wavefront";
    case ure::IntegratorMode::PathGuided: return "path_guided";
    case ure::IntegratorMode::RestirDI: return "restir_di";
    case ure::IntegratorMode::SpecularManifold: return "specular_manifold";
    case ure::IntegratorMode::MLT: return "mlt";
    case ure::IntegratorMode::RestirPT: return "restir_pt";
    case ure::IntegratorMode::BDPT: return "bdpt";
    case ure::IntegratorMode::VCM: return "vcm";
    case ure::IntegratorMode::Automatic: return "automatic";
    }
    return "unknown";
}

bool parse_integrator_sampler(const std::string& sampler, ure::IntegratorSampler& out) {
    const std::string value = lowercase(sampler);
    if (value == "default" || value.empty()) {
        out = ure::IntegratorSampler::Default;
        return true;
    }
    if (value == "low_discrepancy") {
        out = ure::IntegratorSampler::LowDiscrepancy;
        return true;
    }
    if (value == "primary_sample_space") {
        out = ure::IntegratorSampler::PrimarySampleSpace;
        return true;
    }
    return false;
}

bool parse_integrator_quality_preset(const std::string& preset, ure::IntegratorQualityPreset& out) {
    const std::string value = lowercase(preset);
    if (value == "default" || value.empty()) {
        out = ure::IntegratorQualityPreset::Default;
        return true;
    }
    if (value == "preview") {
        out = ure::IntegratorQualityPreset::Preview;
        return true;
    }
    if (value == "final") {
        out = ure::IntegratorQualityPreset::Final;
        return true;
    }
    if (value == "research") {
        out = ure::IntegratorQualityPreset::Research;
        return true;
    }
    return false;
}

bool make_backend_config(
    const ure::config::BackendConfig& app_config,
    ure::BackendSelectionConfig& config) {
    const auto kind = ure::parse_backend_kind(lowercase(app_config.kind));
    if (!kind) {
        std::cerr << "Error: unsupported backend '" << app_config.kind << "'\n";
        return false;
    }
    if (app_config.memory_budget_mb >
        std::numeric_limits<std::uint64_t>::max() / (1024ull * 1024ull)) {
        std::cerr << "Error: backend memory budget overflows bytes\n";
        return false;
    }
    config.kind = *kind;
    config.adapter_id = app_config.adapter_id;
    config.adapter_ordinal = app_config.adapter_ordinal;
    config.memory_budget_bytes =
        app_config.memory_budget_mb * 1024ull * 1024ull;
    config.required_features = 0;
    for (const auto& name : app_config.required_features) {
        const auto feature = ure::parse_backend_feature(lowercase(name));
        if (!feature) {
            std::cerr << "Error: unsupported backend feature '" << name << "'\n";
            return false;
        }
        config.required_features |= ure::backend_feature_bit(*feature);
    }
    return true;
}

bool make_acceleration_config(
    const ure::config::AccelerationConfig& app_config,
    ure::AccelerationConfig& config) {
    const auto provider =
        ure::parse_acceleration_provider(lowercase(app_config.provider));
    const auto quality =
        ure::parse_acceleration_quality(lowercase(app_config.quality));
    const auto update_policy = ure::parse_acceleration_update_policy(
        lowercase(app_config.update_policy));
    if (!provider || !quality || !update_policy) {
        std::cerr << "Error: invalid acceleration configuration\n";
        return false;
    }
    if (app_config.scratch_budget_mb >
        std::numeric_limits<std::uint64_t>::max() / (1024ull * 1024ull)) {
        std::cerr << "Error: acceleration scratch budget overflows bytes\n";
        return false;
    }
    config.provider = *provider;
    config.quality = *quality;
    config.update_policy = *update_policy;
    config.clustered_geometry_enabled =
        app_config.clustered_geometry_enabled;
    config.collect_stats = app_config.collect_stats;
    config.scratch_budget_bytes =
        app_config.scratch_budget_mb * 1024ull * 1024ull;
    return true;
}

bool make_wave_optics_config(const ure::config::WaveOpticsConfig& app_config, ure::WaveOpticsConfig& cfg) {
    if (!parse_wave_optics_mode(app_config.mode, cfg.mode)) {
        std::cerr << "Error: unsupported wave optics mode '" << app_config.mode << "'\n";
        return false;
    }
    cfg.camera_diffraction_enabled = app_config.camera_diffraction_enabled;
    cfg.coherent_field_enabled = app_config.coherent_field_enabled;
    cfg.partial_coherence_enabled = app_config.partial_coherence_enabled;
    cfg.diffractive_materials_enabled = app_config.diffractive_materials_enabled;
    cfg.fluorescence_enabled = app_config.fluorescence_enabled;
    cfg.specular_manifold_enabled = app_config.specular_manifold_enabled;
    cfg.local_fullwave_enabled = app_config.local_fullwave_enabled;
    cfg.experimental_allow_preview_degradation = app_config.experimental_allow_preview_degradation;
    cfg.camera_aperture_diameter_m = app_config.camera_aperture_diameter_m;
    cfg.camera_focal_length_m = app_config.camera_focal_length_m;
    cfg.sensor_pixel_pitch_m = app_config.sensor_pixel_pitch_m;
    cfg.camera_defocus_waves_at_edge = app_config.camera_defocus_waves_at_edge;
    cfg.camera_aperture_rotation_rad = app_config.camera_aperture_rotation_rad;
    cfg.camera_aperture_blade_count = app_config.camera_aperture_blade_count;
    cfg.camera_psf_radius_pixels = app_config.camera_psf_radius_pixels;
    cfg.camera_wavelength_bin_count = app_config.camera_wavelength_bin_count;
    cfg.camera_pupil_sample_count = app_config.camera_pupil_sample_count;
    return true;
}

bool make_integrator_config(const ure::config::IntegratorConfig& app_config, ure::RenderConfig& cfg) {
    if (!parse_integrator_mode(app_config.mode, cfg.integrator.mode)) {
        std::cerr << "Error: unsupported integrator mode '" << app_config.mode << "'\n";
        return false;
    }
    if (!parse_integrator_sampler(app_config.sampler, cfg.integrator.sampler)) {
        std::cerr << "Error: unsupported integrator sampler '" << app_config.sampler << "'\n";
        return false;
    }
    if (!parse_integrator_quality_preset(app_config.quality_preset, cfg.integrator.quality_preset)) {
        std::cerr << "Error: unsupported integrator quality preset '" << app_config.quality_preset << "'\n";
        return false;
    }
    cfg.integrator.allow_biased_reuse = app_config.allow_biased_reuse;
    cfg.automatic_integrator.enabled =
        cfg.integrator.mode == ure::IntegratorMode::Automatic;
    cfg.automatic_integrator.target_relative_standard_error =
        app_config.target_relative_standard_error;
    cfg.automatic_integrator.time_budget_milliseconds =
        app_config.time_budget_milliseconds;
    cfg.automatic_integrator.memory_budget_mb = app_config.memory_budget_mb;
    cfg.automatic_integrator.pilot_spp = app_config.pilot_spp;
    cfg.automatic_integrator.maximum_techniques =
        app_config.maximum_techniques;
    cfg.automatic_integrator.minimum_wavefront_fraction =
        static_cast<float>(app_config.minimum_wavefront_fraction);
    cfg.automatic_integrator.allow_experimental =
        app_config.allow_experimental;
    cfg.sample_index_offset = app_config.sample_index_offset;
    if (cfg.integrator.mode == ure::IntegratorMode::Automatic &&
        (!(app_config.target_relative_standard_error > 0.0) ||
         app_config.memory_budget_mb < 0 ||
         app_config.pilot_spp < 2 ||
         app_config.maximum_techniques < 1 ||
         !(app_config.minimum_wavefront_fraction > 0.0) ||
         app_config.minimum_wavefront_fraction > 1.0)) {
        std::cerr << "Error: invalid automatic integrator objective\n";
        return false;
    }
    if (cfg.integrator.mode == ure::IntegratorMode::PathGuided) {
        cfg.path_guiding.enabled = true;
    } else if (cfg.integrator.mode == ure::IntegratorMode::RestirDI) {
        cfg.restir_di.enabled = true;
        cfg.restir_di.temporal_reuse = true;
    } else if (cfg.integrator.mode == ure::IntegratorMode::RestirPT) {
        cfg.restir_pt.enabled = true;
    } else if (cfg.integrator.mode == ure::IntegratorMode::SpecularManifold) {
        cfg.specular_manifold.enabled = true;
    } else if (cfg.integrator.mode == ure::IntegratorMode::BDPT) {
        cfg.bidirectional.enabled = true;
    } else if (cfg.integrator.mode == ure::IntegratorMode::VCM) {
        cfg.bidirectional.enabled = true;
        cfg.vcm.enabled = true;
    } else if (cfg.integrator.mode == ure::IntegratorMode::MLT) {
        cfg.mlt.enabled = true;
        cfg.integrator.sampler = ure::IntegratorSampler::PrimarySampleSpace;
    }
    return true;
}

bool validate_supported_wave_optics(
    const ure::RenderConfig& config) {
    if (config.integrator.mode == ure::IntegratorMode::Automatic) {
        auto baseline = config;
        baseline.integrator.mode = ure::IntegratorMode::Wavefront;
        baseline.automatic_integrator.enabled = false;
        return validate_supported_wave_optics(baseline);
    }
    const auto& cfg = config.wave_optics;
    if (ure::wave_optics_is_radiometric_only(cfg)) return true;
    if (ure::wave::
            is_supported_diffractive_material_config(
                config)) {
        return true;
    }
    if (ure::wave::
            is_supported_fluorescence_config(
                config)) {
        return true;
    }
    if (ure::wave::is_valid_diffraction_camera_config(cfg) &&
        !cfg.coherent_field_enabled &&
        !cfg.partial_coherence_enabled &&
        !cfg.diffractive_materials_enabled &&
        !cfg.fluorescence_enabled &&
        !cfg.specular_manifold_enabled &&
        !cfg.local_fullwave_enabled &&
        config.integrator.mode ==
            ure::IntegratorMode::Wavefront &&
        !config.path_guiding.enabled &&
        !config.restir_di.enabled &&
        !config.restir_pt.enabled &&
        !config.specular_manifold.enabled &&
        !config.bidirectional.enabled &&
        !config.vcm.enabled &&
        !config.mlt.enabled) {
        return true;
    }
    std::cerr << "Error: unsupported wave-optics feature combination or invalid camera diffraction optics.\n";
    return false;
}

int cmd_render(const ure::config::CliResult& cli) {
    const auto& app_config = cli.config;

    ure::RenderConfig gpu_config;
    const std::uint64_t domain_bins = app_config.spectral.domain_bins > 0
        ? app_config.spectral.domain_bins
        : static_cast<std::uint64_t>(std::max(app_config.spectral.bands, 0));
    const int packet_lanes = app_config.spectral.packet_lanes > 0
        ? app_config.spectral.packet_lanes
        : std::min(
              std::max(
                  app_config.spectral.bands,
                  ure::kMinSpectralPacketLanes),
              ure::kMaxSpectralPacketLanes);
    gpu_config.spectral_domain_bins = domain_bins;
    gpu_config.spectral_packet_lanes = packet_lanes;
    gpu_config.spectral_max_resident_mb = app_config.spectral.max_resident_mb;
    gpu_config.spectral_sampling_mode = parse_spectral_sampling_mode(app_config.spectral.sampling_mode);
    if (!make_backend_config(app_config.backend, gpu_config.backend)) {
        return 1;
    }
    if (!make_acceleration_config(
            app_config.acceleration, gpu_config.acceleration)) {
        return 1;
    }
    gpu_config.path_guiding.enabled = app_config.path_guiding.enabled;
    gpu_config.path_guiding.light_mixture = static_cast<float>(app_config.path_guiding.light_mixture);
    gpu_config.path_guiding.learning_rate = static_cast<float>(app_config.path_guiding.learning_rate);
    gpu_config.path_guiding.min_weight = static_cast<float>(app_config.path_guiding.min_weight);
    gpu_config.path_guiding.decay = static_cast<float>(app_config.path_guiding.decay);
    gpu_config.path_guiding.decay_interval = app_config.path_guiding.decay_interval;
    gpu_config.path_guiding.spatial_cell_count = app_config.path_guiding.spatial_cell_count;
    gpu_config.path_guiding.directional_bin_count = app_config.path_guiding.directional_bin_count;
    gpu_config.path_guiding.memory_budget_mb = app_config.path_guiding.memory_budget_mb;
    gpu_config.environment_light.direct_sampling = app_config.environment_light.direct_sampling;
    gpu_config.environment_light.intensity = static_cast<float>(app_config.environment_light.intensity);
    gpu_config.restir_di.enabled = app_config.restir_di.enabled;
    gpu_config.restir_di.temporal_reuse = app_config.restir_di.temporal_reuse;
    gpu_config.restir_di.spatial_reuse = app_config.restir_di.spatial_reuse;
    gpu_config.restir_di.unbiased = app_config.restir_di.unbiased;
    gpu_config.restir_di.max_history = app_config.restir_di.max_history;
    gpu_config.restir_di.spatial_candidate_count = app_config.restir_di.spatial_candidate_count;
    gpu_config.restir_di.spatial_radius = app_config.restir_di.spatial_radius;
    gpu_config.restir_di.min_target = static_cast<float>(app_config.restir_di.min_target);
    gpu_config.restir_di.position_threshold = static_cast<float>(app_config.restir_di.position_threshold);
    gpu_config.restir_di.normal_threshold = static_cast<float>(app_config.restir_di.normal_threshold);
    gpu_config.restir_pt.enabled = app_config.restir_pt.enabled;
    gpu_config.restir_pt.temporal_reuse = app_config.restir_pt.temporal_reuse;
    gpu_config.restir_pt.spatial_reuse = app_config.restir_pt.spatial_reuse;
    gpu_config.restir_pt.max_reuse_depth = app_config.restir_pt.max_reuse_depth;
    gpu_config.restir_pt.candidate_count = app_config.restir_pt.candidate_count;
    gpu_config.restir_pt.max_history = app_config.restir_pt.max_history;
    gpu_config.restir_pt.position_threshold = static_cast<float>(app_config.restir_pt.position_threshold);
    gpu_config.restir_pt.normal_threshold = static_cast<float>(app_config.restir_pt.normal_threshold);
    gpu_config.specular_manifold.enabled = app_config.integrator.specular_manifold.enabled;
    gpu_config.specular_manifold.max_specular_events = app_config.integrator.specular_manifold.max_specular_events;
    gpu_config.specular_manifold.solver_tolerance = static_cast<float>(app_config.integrator.specular_manifold.solver_tolerance);
    gpu_config.specular_manifold.max_newton_iterations = app_config.integrator.specular_manifold.max_newton_iterations;
    gpu_config.bidirectional.enabled = app_config.integrator.bidirectional.enabled;
    gpu_config.bidirectional.max_camera_vertices = app_config.integrator.bidirectional.max_camera_vertices;
    gpu_config.bidirectional.max_light_vertices = app_config.integrator.bidirectional.max_light_vertices;
    gpu_config.bidirectional.connections_per_pixel = app_config.integrator.bidirectional.connections_per_pixel;
    gpu_config.bidirectional.memory_budget_mb = app_config.integrator.bidirectional.memory_budget_mb;
    gpu_config.bidirectional.light_tracing = app_config.integrator.bidirectional.light_tracing;
    gpu_config.vcm.enabled = app_config.integrator.vcm.enabled;
    gpu_config.vcm.initial_radius = static_cast<float>(app_config.integrator.vcm.initial_radius);
    gpu_config.vcm.alpha = static_cast<float>(app_config.integrator.vcm.alpha);
    gpu_config.vcm.grid_capacity = app_config.integrator.vcm.grid_capacity;
    gpu_config.vcm.merge_surfaces = app_config.integrator.vcm.merge_surfaces;
    gpu_config.vcm.merge_volumes = app_config.integrator.vcm.merge_volumes;
    gpu_config.mlt.enabled = app_config.integrator.mlt.enabled;
    gpu_config.mlt.chain_count = app_config.integrator.mlt.chain_count;
    gpu_config.mlt.mutations_per_chain = app_config.integrator.mlt.mutations_per_chain;
    gpu_config.mlt.large_step_probability = static_cast<float>(app_config.integrator.mlt.large_step_probability);
    gpu_config.mlt.small_step_sigma = static_cast<float>(app_config.integrator.mlt.small_step_sigma);
    gpu_config.mlt.seed = app_config.integrator.mlt.seed;
    if (!make_integrator_config(app_config.integrator, gpu_config)) {
        return 1;
    }
    if (!make_wave_optics_config(app_config.wave_optics, gpu_config.wave_optics)) {
        return 1;
    }
    gpu_config.num_wavelengths = packet_lanes;
    gpu_config.queue_capacity = app_config.gpu.wavefront_capacity;
    gpu_config.max_trace_depth = app_config.renderer.max_depth;
    if (!ure::valid_spectral_packet_lane_count(
            gpu_config.spectral_packet_lanes)) {
        std::cerr << "Error: spectral packet lanes must be 1 or in [8, "
                  << ure::kMaxSpectralPacketLanes << "], got "
                  << gpu_config.spectral_packet_lanes << "\n";
        return 1;
    }
    if (gpu_config.spectral_domain_bins < static_cast<std::uint64_t>(gpu_config.spectral_packet_lanes)) {
        std::cerr << "Error: spectral domain bins must be >= packet lanes\n";
        return 1;
    }
    if (!validate_supported_wave_optics(gpu_config)) {
        return 1;
    }
    if (!check_scene_path(app_config.scene_path)) {
        return 1;
    }

    ure::scene_ir::SceneIR scene_ir;
    try {
        const std::string extension = lowercase(std::filesystem::path(app_config.scene_path).extension().string());
        if (extension == ".ure" || extension == ".urescene" || extension == ".urepkg") {
            auto loaded = ure::native_scene::load_native_asset(app_config.scene_path);
            if (!loaded.ok()) throw std::runtime_error(loaded.diagnostics.empty()
                ? "Native scene load failed" : loaded.diagnostics.front().message);
            scene_ir = std::move(loaded.value->scene);
        } else {
            auto imported = ure::native_scene::import_gltf_native(app_config.scene_path);
            if (!imported.ok()) throw std::runtime_error(imported.diagnostics.empty()
                ? "Adapter import failed" : imported.diagnostics.front().message);
            scene_ir = std::move(imported.archive.scene);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error parsing scene: " << e.what() << "\n";
        return 1;
    }

    if (scene_ir.width <= 0) scene_ir.width = app_config.width > 0 ? app_config.width : 1600;
    if (scene_ir.height <= 0) scene_ir.height = app_config.height > 0 ? app_config.height : 900;
    const int spp = app_config.renderer.spp > 0 ? app_config.renderer.spp : (scene_ir.spp > 0 ? scene_ir.spp : 100);

    std::unique_ptr<ure::IRenderEngine> engine;
    try {
        engine = ure::RenderEngineFactory::create_gpu_renderer(gpu_config);
    } catch (const std::exception& e) {
        std::cerr << "Error selecting backend: " << e.what() << "\n";
        return 1;
    }
    engine->load_scene_ir(scene_ir);
    engine->reset_accumulation();

    const std::filesystem::path output_dir = std::filesystem::current_path() / "output";
    std::filesystem::create_directories(output_dir);
    std::string output_filename = app_config.output.file;
    if (output_filename.empty()) {
        output_filename = std::filesystem::path(app_config.scene_path).stem().string() +
                          output_extension_for_format(app_config.output.format);
    }
    const std::string output_path = (output_dir / output_filename).string();

    if (gpu_config.integrator.mode == ure::IntegratorMode::Automatic) {
        ure::RenderSettings settings;
        settings.width = scene_ir.width;
        settings.height = scene_ir.height;
        settings.spp = spp;
        settings.output_path = output_path;
        engine->render(settings);
        if (!save_frame(*engine, scene_ir.width, scene_ir.height,
                        output_path, app_config.output.format)) {
            std::cerr << "Error: failed to save output: "
                      << output_path << "\n";
            return 1;
        }
        const auto report = engine->get_automatic_integrator_report();
        std::cout << "Automatic portfolio: "
                  << report.total_allocated_spp << " samples, relative SE "
                  << report.estimated_relative_standard_error
                  << ", quality/time/memory targets "
                  << (report.quality_target_met ? "met" : "missed") << "/"
                  << (report.time_budget_met ? "met" : "missed") << "/"
                  << (report.memory_budget_met ? "met" : "missed") << "\n";
        for (const auto& technique : report.techniques) {
            std::cout << "  " << integrator_mode_name(technique.mode)
                      << ": " << technique.reason;
            if (technique.selected) {
                std::cout << ", " << technique.allocated_spp
                          << " samples, weight "
                          << technique.aggregation_weight;
            }
            std::cout << "\n";
        }
        UR_LOG_INFO(CLI, "Render Finished!");
        UR_LOG_INFO(CLI, "Output: {}", output_path);
        return 0;
    }

    auto last_save_time = std::chrono::steady_clock::now();
    int current_spp = 0;
    while (current_spp < spp) {
        current_spp = engine->render_pass();
        if (current_spp % 10 == 0 || current_spp >= spp) {
            std::cout << "\r[Main] Progress: " << current_spp << "/" << spp << " SPP" << std::flush;
        }

        const auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_save_time).count() >= 1 ||
            current_spp == 1 ||
            current_spp == 10 ||
            current_spp >= spp) {
            if (!save_frame(*engine, scene_ir.width, scene_ir.height, output_path, app_config.output.format)) {
                std::cerr << "Error: failed to save output: " << output_path << "\n";
                return 1;
            }
            last_save_time = now;
        }
    }

    std::cout << "\n";
    UR_LOG_INFO(CLI, "Render Finished!");
    UR_LOG_INFO(CLI, "Output: {}", output_path);
    return 0;
}

int cmd_info(const std::string& scene_path) {
    if (!check_scene_path(scene_path)) {
        return 1;
    }
    try {
        const auto imported = ure::native_scene::import_gltf_native(scene_path);
        if (!imported.ok()) throw std::runtime_error(imported.diagnostics.empty()
            ? "Adapter import failed" : imported.diagnostics.front().message);
        const auto& ir = imported.archive.scene;
        std::cout << "Scene: " << scene_path << "\n";
        std::cout << "  Meshes:     " << ir.meshes.size() << "\n";
        std::cout << "  Materials:  " << ir.materials.size() << "\n";
        std::cout << "  Instances:  " << ir.instances.size() << "\n";
        std::cout << "  Spheres:    " << ir.spheres.size() << "\n";
        std::cout << "  Textures:   " << ir.textures.size() << "\n";
        std::cout << "  Images:     " << ir.images.size() << "\n";
        std::cout << "  Width:      " << ir.width << "\n";
        std::cout << "  Height:     " << ir.height << "\n";
        std::cout << "  SPP:        " << ir.spp << "\n";
        std::cout << "  Physics:    " << (ir.physics.enabled ? "enabled" : "disabled") << "\n";
    } catch (const std::exception& e) {
        std::cerr << "Error parsing scene: " << e.what() << "\n";
        return 1;
    }
    return 0;
}

int cmd_list_devices() {
    try {
        const auto adapters = ure::enumerate_backend_adapters();
        std::cout << "Found " << adapters.size()
                  << " backend adapter(s):\n";
        for (const auto& adapter : adapters) {
            std::cout << "  [" << adapter.ordinal << "] "
                      << ure::backend_kind_name(adapter.kind) << " "
                      << adapter.name << "\n"
                      << "      id: " << adapter.adapter_id << "\n"
                      << "      memory: "
                      << (adapter.memory.total_bytes >> 20) << " MiB total, "
                      << (adapter.memory.available_bytes >> 20)
                      << " MiB available\n"
                      << "      driver: " << adapter.driver_identity << "\n"
                      << "      compiler: " << adapter.compiler_identity
                      << "\n";
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Backend enumeration failed: " << e.what() << "\n";
        return 1;
    }
}

int cmd_validate(const std::string& scene_path) {
    if (!check_scene_path(scene_path)) {
        return 1;
    }
    try {
        const std::string extension = lowercase(std::filesystem::path(scene_path).extension().string());
        if (extension == ".ure" || extension == ".urescene" || extension == ".urepkg") {
            const auto inspection = ure::native_scene::inspect_native_asset(scene_path);
            std::cout << "Schema: " << inspection.version.major << "." << inspection.version.minor << "\n";
            std::cout << "Features: validated against native capability registry\n";
            std::cout << "Resources: " << inspection.resource_count << " declared\n";
            std::cout << "Budget: " << inspection.stored_bytes << " stored bytes, "
                      << inspection.resident_bytes << " resident bytes\n";
            std::cout << "Adapter loss: none (native source)\n";
            for (const auto& diagnostic : inspection.diagnostics) {
                std::ostream& stream = diagnostic.severity == ure::native_scene::DiagnosticSeverity::Error
                    ? std::cerr : std::cout;
                stream << diagnostic.code << " [" << diagnostic.path << "]: " << diagnostic.message << "\n";
            }
            if (inspection.ok()) std::cout << "Valid: " << scene_path << "\n";
            return inspection.ok() ? 0 : 1;
        }
        const auto imported = ure::native_scene::import_gltf_native(scene_path);
        if (!imported.ok()) throw std::runtime_error(imported.diagnostics.empty()
            ? "Adapter import failed" : imported.diagnostics.front().message);
        const auto& ir = imported.archive.scene;
        std::cout << "Valid: " << scene_path << "\n";
        std::cout << "  " << ir.meshes.size() << " meshes, "
                  << ir.materials.size() << " materials, "
                  << ir.instances.size() << " instances, "
                  << ir.spheres.size() << " spheres\n";
        bool ok = true;
        for (size_t i = 0; i < ir.meshes.size(); ++i) {
            const auto& mesh = ir.meshes[i];
            if (!mesh->mesh || mesh->mesh->vertices.empty()) {
                std::cerr << "  Warning: mesh " << i << " (" << mesh->name << ") has no positions\n";
                ok = false;
            }
        }
        if (ok) {
            std::cout << "  No issues found.\n";
        }
        std::cout << ure::native_scene::write_adapter_loss_report(imported.loss_report);
        return ok ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "Validation FAILED: " << e.what() << "\n";
        return 1;
    }
}

int cmd_native_tool(const ure::config::CliResult& cli) {
    try {
        switch (cli.command) {
        case ure::config::CliCommand::Build:
            ure::native_scene::build_native_scene(cli.scene_path, cli.output_path);
            break;
        case ure::config::CliCommand::Pack: {
            std::vector<std::filesystem::path> inputs;
            inputs.reserve(cli.input_paths.size());
            for (const auto& input : cli.input_paths) inputs.emplace_back(input);
            ure::native_scene::pack_native_scenes(cli.output_path, inputs);
            break;
        }
        case ure::config::CliCommand::Unpack:
            ure::native_scene::unpack_native_package(cli.scene_path, cli.output_path);
            break;
        case ure::config::CliCommand::Migrate:
            ure::native_scene::migrate_native_scene(cli.scene_path, cli.output_path);
            break;
        case ure::config::CliCommand::Inspect: {
            const auto inspection = ure::native_scene::inspect_native_asset(cli.scene_path);
            std::cout << "ID: " << inspection.id << "\n"
                      << "Kind: " << (inspection.kind == ure::native_scene::ContainerKind::Package ? "package" : "scene") << "\n"
                      << "Version: " << inspection.version.major << "." << inspection.version.minor << "\n"
                      << "Semantic hash: " << inspection.semantic_hash << "\n"
                      << "Scenes: " << inspection.scene_count << "\n"
                      << "Resources: " << inspection.resource_count << "\n"
                      << "Stored bytes: " << inspection.stored_bytes << "\n"
                      << "Resident bytes: " << inspection.resident_bytes << "\n";
            if (!inspection.ok()) return 1;
            break;
        }
        case ure::config::CliCommand::Export:
            ure::native_scene::export_native_scene_usda(
                cli.scene_path,
                cli.output_path,
                cli.allow_lossy
                    ? ure::native_scene::
                          UsdExportPolicy::
                              AllowDocumentedLoss
                    : ure::native_scene::
                          UsdExportPolicy::Strict,
                cli.loss_report_path,
                cli.scene_id);
            break;
        default:
            return 1;
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Native tooling FAILED: " << error.what() << "\n";
        return 1;
    }
}

} // namespace

int main(int argc, char* argv[]) {
    const auto cli = ure::config::parse_cli(argc, argv);

    ure::log::set_min_level(
        cli.verbose ? ure::log::Level::Debug :
        cli.quiet ? ure::log::Level::Error :
                    ure::log::Level::Info);

    switch (cli.command) {
    case ure::config::CliCommand::Render:
        return cmd_render(cli);
    case ure::config::CliCommand::Info:
        return cmd_info(cli.scene_path);
    case ure::config::CliCommand::ListDevices:
        return cmd_list_devices();
    case ure::config::CliCommand::Validate:
        return cmd_validate(cli.scene_path);
    case ure::config::CliCommand::Build:
    case ure::config::CliCommand::Pack:
    case ure::config::CliCommand::Unpack:
    case ure::config::CliCommand::Inspect:
    case ure::config::CliCommand::Migrate:
    case ure::config::CliCommand::Export:
        return cmd_native_tool(cli);
    }
    return 0;
}
