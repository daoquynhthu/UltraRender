#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <ure/config.hpp>
#include <ure/gpu_structs.hpp>
#include <ure/image_saver.hpp>
#include <ure/log.hpp>
#include <ure/render.hpp>
#include <ure/scene_frontend.hpp>

#ifdef USE_CUDA
#include <cuda_runtime.h>
#endif

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
        std::cerr << "Error: render requires an explicit glTF/GLB scene path\n";
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
    if (value == "wavefront" || value.empty()) {
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
    if (cfg.integrator.mode == ure::IntegratorMode::PathGuided) {
        cfg.path_guiding.enabled = true;
    } else if (cfg.integrator.mode == ure::IntegratorMode::RestirDI) {
        cfg.restir_di.enabled = true;
        cfg.restir_di.temporal_reuse = true;
    } else if (cfg.integrator.mode == ure::IntegratorMode::SpecularManifold) {
        cfg.specular_manifold.enabled = true;
    } else if (cfg.integrator.mode == ure::IntegratorMode::MLT) {
        cfg.mlt.enabled = true;
        cfg.integrator.sampler = ure::IntegratorSampler::PrimarySampleSpace;
    }
    return true;
}

bool validate_supported_wave_optics(const ure::WaveOpticsConfig& cfg) {
    if (ure::wave_optics_is_radiometric_only(cfg)) return true;
    std::cerr << "Error: Phase W wave-optics modes are configuration/API gated but not implemented yet; "
              << "only radiometric mode is currently supported.\n";
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
        : std::min(std::max(app_config.spectral.bands, ure::gpu::kMinPacketLanes),
                   ure::gpu::kMaxPacketLanes);
    gpu_config.spectral_domain_bins = domain_bins;
    gpu_config.spectral_packet_lanes = packet_lanes;
    gpu_config.spectral_max_resident_mb = app_config.spectral.max_resident_mb;
    gpu_config.spectral_sampling_mode = parse_spectral_sampling_mode(app_config.spectral.sampling_mode);
    gpu_config.path_guiding.enabled = app_config.path_guiding.enabled;
    gpu_config.path_guiding.light_mixture = static_cast<float>(app_config.path_guiding.light_mixture);
    gpu_config.path_guiding.learning_rate = static_cast<float>(app_config.path_guiding.learning_rate);
    gpu_config.path_guiding.min_weight = static_cast<float>(app_config.path_guiding.min_weight);
    gpu_config.environment_light.direct_sampling = app_config.environment_light.direct_sampling;
    gpu_config.environment_light.intensity = static_cast<float>(app_config.environment_light.intensity);
    gpu_config.restir_di.enabled = app_config.restir_di.enabled;
    gpu_config.restir_di.temporal_reuse = app_config.restir_di.temporal_reuse;
    gpu_config.restir_di.spatial_reuse = app_config.restir_di.spatial_reuse;
    gpu_config.restir_di.unbiased = app_config.restir_di.unbiased;
    gpu_config.restir_di.max_history = app_config.restir_di.max_history;
    gpu_config.specular_manifold.enabled = app_config.integrator.specular_manifold.enabled;
    gpu_config.specular_manifold.max_specular_events = app_config.integrator.specular_manifold.max_specular_events;
    gpu_config.specular_manifold.solver_tolerance = static_cast<float>(app_config.integrator.specular_manifold.solver_tolerance);
    gpu_config.specular_manifold.max_newton_iterations = app_config.integrator.specular_manifold.max_newton_iterations;
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
    if (!ure::gpu::valid_packet_lane_count(gpu_config.spectral_packet_lanes)) {
        std::cerr << "Error: spectral packet lanes must be 1 or in [8, "
                  << ure::gpu::kMaxPacketLanes << "], got "
                  << gpu_config.spectral_packet_lanes << "\n";
        return 1;
    }
    if (gpu_config.spectral_domain_bins < static_cast<std::uint64_t>(gpu_config.spectral_packet_lanes)) {
        std::cerr << "Error: spectral domain bins must be >= packet lanes\n";
        return 1;
    }
    if (!validate_supported_wave_optics(gpu_config.wave_optics)) {
        return 1;
    }
    if (!check_scene_path(app_config.scene_path)) {
        return 1;
    }

    ure::scene_ir::SceneIR scene_ir;
    try {
        scene_ir = ure::SceneFrontend::parse_file_to_ir(app_config.scene_path);
    } catch (const std::exception& e) {
        std::cerr << "Error parsing scene: " << e.what() << "\n";
        return 1;
    }

    if (scene_ir.width <= 0) scene_ir.width = app_config.width > 0 ? app_config.width : 1600;
    if (scene_ir.height <= 0) scene_ir.height = app_config.height > 0 ? app_config.height : 900;
    const int spp = app_config.renderer.spp > 0 ? app_config.renderer.spp : (scene_ir.spp > 0 ? scene_ir.spp : 100);

    auto engine = ure::RenderEngineFactory::create_gpu_renderer(gpu_config);
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

    auto last_save_time = std::chrono::steady_clock::now();
    int current_spp = 0;
    while (current_spp < spp) {
        current_spp = engine->render_pass();
        if (current_spp % 10 == 0 || current_spp == spp) {
            std::cout << "\r[Main] Progress: " << current_spp << "/" << spp << " SPP" << std::flush;
        }

        const auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_save_time).count() >= 1 ||
            current_spp == 1 ||
            current_spp == 10 ||
            current_spp == spp) {
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
        const auto ir = ure::SceneFrontend::parse_file_to_ir(scene_path);
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
#ifdef USE_CUDA
    int count = 0;
    const cudaError_t err = cudaGetDeviceCount(&count);
    if (err != cudaSuccess) {
        std::cerr << "CUDA error: " << cudaGetErrorString(err) << "\n";
        return 1;
    }
    std::cout << "Found " << count << " CUDA device(s):\n";
    for (int i = 0; i < count; ++i) {
        cudaDeviceProp props{};
        if (cudaGetDeviceProperties(&props, i) == cudaSuccess) {
            std::cout << "  [" << i << "] " << props.name
                      << "  CC " << props.major << "." << props.minor
                      << "  " << (props.totalGlobalMem >> 20) << " MB\n";
        }
    }
    return 0;
#else
    std::cout << "CUDA not available (compiled without USE_CUDA)\n";
    return 0;
#endif
}

int cmd_validate(const std::string& scene_path) {
    if (!check_scene_path(scene_path)) {
        return 1;
    }
    try {
        const auto ir = ure::SceneFrontend::parse_file_to_ir(scene_path);
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
        return ok ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "Validation FAILED: " << e.what() << "\n";
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
    }
    return 0;
}
