#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <ure/config.hpp>
#include <ure/gpu_structs.hpp>
#include <ure/image_saver.hpp>
#include <ure/log.hpp>
#include <ure/render.hpp>
#include <ure/scene_parser.hpp>

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

int cmd_render(const ure::config::CliResult& cli) {
    const auto& app_config = cli.config;
    if (!check_scene_path(app_config.scene_path)) {
        return 1;
    }

    ure::RenderConfig gpu_config;
    gpu_config.num_wavelengths = app_config.spectral.bands;
    gpu_config.queue_capacity = app_config.gpu.wavefront_capacity;
    gpu_config.max_trace_depth = app_config.renderer.max_depth;
    if (gpu_config.num_wavelengths < ure::gpu::kMinSpectralChannels ||
        gpu_config.num_wavelengths > ure::gpu::kMaxSpectralChannels) {
        std::cerr << "Error: spectral bands must be in ["
                  << ure::gpu::kMinSpectralChannels << ", "
                  << ure::gpu::kMaxSpectralChannels << "], got "
                  << gpu_config.num_wavelengths << "\n";
        return 1;
    }

    ure::scene_ir::SceneIR scene_ir;
    try {
        scene_ir = ure::SceneParser::parse_file_to_ir(app_config.scene_path);
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
        const auto ir = ure::SceneParser::parse_file_to_ir(scene_path);
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
        const auto ir = ure::SceneParser::parse_file_to_ir(scene_path);
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
