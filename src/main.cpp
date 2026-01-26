#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include "api/ure_api.hpp"
#include "api/procedural.hpp"
#include "api/scene_parser.hpp"
#include "io/image_saver.hpp" 

using namespace ure;

// Helper to save image
void save_current_frame(IRenderEngine* engine, int width, int height, const std::string& path) {
    const auto& buffer = engine->get_frame_buffer();
    std::vector<ure::core::Vec3f> pixels(width * height);
    
    // Safety check
    if (buffer.size() < pixels.size() * 3) return;

    for (size_t i = 0; i < pixels.size(); ++i) {
        pixels[i].x = buffer[i*3 + 0];
        pixels[i].y = buffer[i*3 + 1];
        pixels[i].z = buffer[i*3 + 2];
    }
    
    // Use temp file for atomic-like write to avoid partial reads by frontend
    std::string temp_path = path + ".tmp";
    ure::io::ImageSaver::save_bmp(temp_path, width, height, pixels, ure::io::ToneMapType::ACES, 1.0f);
    
    try {
        // On Windows, std::filesystem::rename might fail if target exists, so we remove it first
        if (std::filesystem::exists(path)) {
            std::filesystem::remove(path);
        }
        std::filesystem::rename(temp_path, path);
    } catch (const std::exception& e) {
        std::cerr << "[Main] Error updating output file: " << e.what() << std::endl;
    }
}

int main(int argc, char* argv[]) {
    std::cout << "========================================" << std::endl;
    std::cout << "   UltraRender Engine - Procedural MVP  " << std::endl;
    std::cout << "========================================" << std::endl;

    // 1. Parse Arguments
    std::string scene_path_or_name = "procedural_demo";
    std::string output_filename_override = "";
    std::string output_dir_str = "";
    int cli_spp = 0; 
    int cli_width = 0;
    int cli_height = 0;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-s" || arg == "--spp") && i + 1 < argc) {
            cli_spp = std::stoi(argv[++i]);
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            output_filename_override = argv[++i];
        } else if ((arg == "-d" || arg == "--output-dir") && i + 1 < argc) {
            output_dir_str = argv[++i];
        } else if (arg == "--scene" && i + 1 < argc) {
            scene_path_or_name = argv[++i];
        } else if (arg == "--width" && i + 1 < argc) {
            cli_width = std::stoi(argv[++i]);
        } else if (arg == "--height" && i + 1 < argc) {
            cli_height = std::stoi(argv[++i]);
        } else if (arg[0] != '-') {
            // Only treat as scene path if we haven't set it via --scene yet, 
            // OR if we want to support legacy behavior (last arg wins).
            // Let's stick to: if it looks like a file and we haven't explicitly used --scene.
            if (scene_path_or_name == "procedural_demo" || scene_path_or_name.find('.') == std::string::npos) {
                 scene_path_or_name = arg;
            }
        }
    }

    std::cout << "[Main] Target: " << scene_path_or_name << ", SPP (CLI): " << cli_spp << std::endl;

    // 2. Build Scene
    Scene scene;
    
    if (std::filesystem::exists(scene_path_or_name)) {
        std::cout << "[Main] Parsing scene file: " << scene_path_or_name << std::endl;
        scene = SceneParser::parse_file(scene_path_or_name);
    } else {
        if (scene_path_or_name != "procedural_demo") {
             std::cerr << "[Main] Warning: File '" << scene_path_or_name << "' not found. Falling back to procedural demo." << std::endl;
        } else {
             std::cout << "[Main] No file specified, using internal procedural fallback." << std::endl;
        }
        SceneBuilder builder;
        // Camera matching the default scene description
        builder.set_camera({0, 4, 18}, {0, 1, 0}, 45.0f);
        scene = builder.build();
    }

    // 3. Initialize Engine
    std::cout << "[Main] Initializing GPU Engine..." << std::endl;
    auto engine = RenderEngineFactory::create_gpu_engine();

    // Resolution Priority: Scene File > CLI > Default
    // 1. Determine Width
    if (scene.width > 0) {
        if (cli_width > 0 && cli_width != scene.width) {
            std::cerr << "[Main] Warning: Resolution width conflict! Scene (" << scene.width 
                      << ") != CLI (" << cli_width << "). Using Scene value." << std::endl;
        }
    } else if (cli_width > 0) {
        scene.width = cli_width;
    } else {
        scene.width = 1280;
    }

    // 2. Determine Height
    if (scene.height > 0) {
        if (cli_height > 0 && cli_height != scene.height) {
            std::cerr << "[Main] Warning: Resolution height conflict! Scene (" << scene.height 
                      << ") != CLI (" << cli_height << "). Using Scene value." << std::endl;
        }
    } else if (cli_height > 0) {
        scene.height = cli_height;
    } else {
        scene.height = 720;
    }
    
    // 3. Determine SPP (Priority: CLI > Scene > Default)
    int spp = 100; // Default
    if (cli_spp > 0) {
        spp = cli_spp;
        if (scene.spp > 0 && scene.spp != cli_spp) {
            std::cerr << "[Main] Warning: SPP conflict! CLI (" << cli_spp 
                      << ") overrides Scene (" << scene.spp << ")." << std::endl;
        }
    } else if (scene.spp > 0) {
        spp = scene.spp;
    }

    // 4. Load Scene
    std::cout << "[Main] Loading Scene Data..." << std::endl;
    engine->load_scene(scene);
    
    // 5. Render Loop (Interactive / Progressive)
    RenderSettings settings;
    settings.width = scene.width;
    settings.height = scene.height;
    settings.spp = spp;

    std::cout << "[Main] Starting Render Loop: " << settings.width << "x" << settings.height << " @ " << settings.spp << " SPP" << std::endl;
    
    // Prepare output path
    std::filesystem::path output_dir;
    if (!output_dir_str.empty()) {
        output_dir = output_dir_str;
    } else {
        output_dir = std::filesystem::current_path() / "output";
    }

    if (!std::filesystem::exists(output_dir)) {
        std::filesystem::create_directories(output_dir);
    }
    
    std::string output_filename;
    if (!output_filename_override.empty()) {
        output_filename = output_filename_override;
    } else {
        std::string filename_base = "output_procedural";
        if (std::filesystem::exists(scene_path_or_name)) {
            filename_base = std::filesystem::path(scene_path_or_name).stem().string();
        }
        output_filename = filename_base + ".bmp";
    }
    std::filesystem::path output_path = output_dir / output_filename;
    std::string output_path_str = output_path.string();

    // Reset accumulation before starting
    engine->reset_accumulation();

    auto start_time = std::chrono::steady_clock::now();
    auto last_save_time = std::chrono::steady_clock::now();
    int current_spp = 0;

    while (current_spp < spp) {
        current_spp = engine->render_pass();
        
        // Console Progress Update
        if (current_spp % 10 == 0 || current_spp == spp) {
             std::cout << "\r[Main] Progress: " << current_spp << "/" << spp << " SPP" << std::flush;
        }

        // Periodic Save (e.g., every 2 seconds or at specific milestones)
        auto now = std::chrono::steady_clock::now();
        auto elapsed_seconds = std::chrono::duration_cast<std::chrono::seconds>(now - last_save_time).count();
        
        // Save if: 
        // 1. More than 1 second passed since last save AND we have made progress
        // 2. OR it's the very first few samples (for quick feedback)
        // 3. OR it's the final sample
        if (elapsed_seconds >= 1 || current_spp == 1 || current_spp == 10 || current_spp == spp) {
            save_current_frame(engine.get(), settings.width, settings.height, output_path_str);
            last_save_time = now;
        }
    }
    
    std::cout << std::endl << "[Main] Render Finished!" << std::endl;
    std::cout << "[Main] Final Output saved to: " << output_path_str << std::endl;

    return 0;
}
