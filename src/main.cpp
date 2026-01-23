#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include "api/ure_api.hpp"
#include "api/procedural.hpp"
#include "api/scene_parser.hpp"
#include "io/image_saver.hpp" 

using namespace ure;

int main(int argc, char* argv[]) {
    std::cout << "========================================" << std::endl;
    std::cout << "   UltraRender Engine - Procedural MVP  " << std::endl;
    std::cout << "========================================" << std::endl;

    // 1. Parse Arguments
    std::string scene_path_or_name = "procedural_demo";
    int spp = 100; 
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-s" && i + 1 < argc) {
            spp = std::stoi(argv[++i]);
        } else if (arg[0] != '-') {
            scene_path_or_name = arg;
        }
    }

    std::cout << "[Main] Target: " << scene_path_or_name << ", SPP: " << spp << std::endl;

    // 2. Build Scene
    Scene scene;
    
    if (std::filesystem::exists(scene_path_or_name)) {
        std::cout << "[Main] Parsing scene file..." << std::endl;
        scene = SceneParser::parse_file(scene_path_or_name);
    } else {
        std::cout << "[Main] No file found, using internal procedural fallback." << std::endl;
        SceneBuilder builder;
        // Camera matching the default scene description
        builder.set_camera({0, 4, 18}, {0, 1, 0}, 45.0f);
        scene = builder.build();
    }

    // 3. Initialize Engine
    std::cout << "[Main] Initializing GPU Engine..." << std::endl;
    auto engine = RenderEngineFactory::create_gpu_engine();
    
    // 4. Load Scene
    std::cout << "[Main] Loading Scene Data..." << std::endl;
    engine->load_scene(scene);
    
    // 5. Render
    RenderSettings settings;
    // Prioritize CLI overrides (if they existed) but here we use scene values if set, otherwise default
    if (scene.width > 0 && scene.height > 0) {
        settings.width = scene.width;
        settings.height = scene.height;
    } else {
        settings.width = 1280;
        settings.height = 720;
    }
    settings.spp = spp;
    
    std::cout << "[Main] Rendering..." << std::endl;
    engine->render(settings);
    
    // 6. Save Output
    std::filesystem::path output_dir = std::filesystem::current_path() / "output";
    if (!std::filesystem::exists(output_dir)) {
        std::filesystem::create_directory(output_dir);
    }
    
    std::string filename_base = "output_procedural";
    if (std::filesystem::exists(scene_path_or_name)) {
        filename_base = std::filesystem::path(scene_path_or_name).stem().string();
    }
    std::string output_filename = filename_base + ".bmp";
    std::filesystem::path output_path = output_dir / output_filename;
    
    std::cout << "[Main] Saving output to: " << output_path.string() << std::endl;

    const auto& buffer = engine->get_frame_buffer();
    
    // Convert to internal Vec3f for ImageSaver
    std::vector<ure::core::Vec3f> pixels(settings.width * settings.height);
    for (size_t i = 0; i < pixels.size(); ++i) {
        pixels[i].x = buffer[i*3 + 0];
        pixels[i].y = buffer[i*3 + 1];
        pixels[i].z = buffer[i*3 + 2];
    }
    
    ure::io::ImageSaver::save_bmp(output_path.string(), settings.width, settings.height, pixels);
    std::cout << "[Main] Done!" << std::endl;

    return 0;
}
