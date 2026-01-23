#include "api/scene_parser.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <unordered_map>

namespace ure {

Scene SceneParser::parse_file(const std::string& filepath) {
    SceneBuilder builder;
    std::ifstream file(filepath);
    
    if (!file.is_open()) {
        std::cerr << "[SceneParser] Error: Could not open file " << filepath << std::endl;
        return builder.build();
    }

    std::unordered_map<std::string, std::shared_ptr<Material>> materials;
    std::string line;

    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::stringstream ss(line);
        std::string command;
        ss >> command;

        if (command == "camera") {
            // Supports two formats:
            // 1. camera <px> <py> <pz> <lx> <ly> <lz> <fov>
            // 2. camera pos <px> <py> <pz> lookat <lx> <ly> <lz> fov <f>
            
            Vec3 pos = {0, 0, 10}, lookat = {0, 0, 0};
            float fov = 45.0f;

            // Peek at next token to decide format
            std::string first_token;
            if (ss >> first_token) {
                if (first_token == "pos") {
                    // Tokenized format
                    while (true) {
                        if (first_token == "pos") ss >> pos.x >> pos.y >> pos.z;
                        else if (first_token == "lookat") ss >> lookat.x >> lookat.y >> lookat.z;
                        else if (first_token == "fov") ss >> fov;
                        
                        if (!(ss >> first_token)) break;
                    }
                } else {
                    // Numeric format
                    try {
                        pos.x = std::stof(first_token);
                        ss >> pos.y >> pos.z >> lookat.x >> lookat.y >> lookat.z >> fov;
                    } catch (...) {
                        // Fallback or error
                    }
                }
            }
            builder.set_camera(pos, lookat, fov);
        }
        else if (command == "resolution") {
            int w, h;
            ss >> w >> h;
            builder.set_resolution(w, h);
        }
        else if (command == "define_material") {
            // define_material <name> <type> [params...]
            std::string name, type_str;
            ss >> name >> type_str;
            
            auto mat = std::make_shared<Material>();
            
            if (type_str == "lambertian") {
                mat->type = MaterialType::Lambertian;
                ss >> mat->albedo.x >> mat->albedo.y >> mat->albedo.z;
                if (ss >> mat->roughness) { /* optional */ }
            }
            else if (type_str == "metal") {
                mat->type = MaterialType::Metal;
                ss >> mat->albedo.x >> mat->albedo.y >> mat->albedo.z;
                ss >> mat->roughness;
                if (ss >> mat->ior) {
                    ss >> mat->extinction.x >> mat->extinction.y >> mat->extinction.z;
                }
            }
            else if (type_str == "dielectric") {
                mat->type = MaterialType::Dielectric;
                ss >> mat->albedo.x >> mat->albedo.y >> mat->albedo.z;
                ss >> mat->roughness;
                ss >> mat->ior;
                if (ss >> mat->dispersion) { /* optional */ }
            }
            else if (type_str == "light") {
                mat->type = MaterialType::Light;
                ss >> mat->emission.x >> mat->emission.y >> mat->emission.z;
            }

            // Check for optional thin film parameters at the end of any material definition
            ss.clear(); // Clear any failbits from optional parameters
            std::string extra_token;
            while (ss >> extra_token) {
                if (extra_token == "thin_film") {
                    ss >> mat->thin_film_thickness >> mat->thin_film_ior;
                }
            }
            
            materials[name] = mat;
        }
        else if (command == "add_entity") {
            // add_entity <type> <mat_name> <pos> <scale> [rot]
            std::string type, mat_name;
            ss >> type >> mat_name;
            
            Vec3 pos = {0,0,0}, scale = {1,1,1}, rot = {0,0,0};
            
            // Read optional transforms if available
            // Assuming strict order: pos x y z [scale x y z] [rot x y z]
            // But let's check stream state
            if (ss >> pos.x >> pos.y >> pos.z) {
                if (ss >> scale.x >> scale.y >> scale.z) {
                    if (!(ss >> rot.x >> rot.y >> rot.z)) {
                        ss.clear(); // Clear failbit if rot is missing
                    }
                } else {
                    ss.clear(); // Clear failbit if scale is missing
                }
            }

            // Clear stream state before reading specific parameters to ensure robustness
            ss.clear();

            std::shared_ptr<Mesh> mesh;
            
            if (type == "sphere") {
                 // Use analytical sphere optimization
                 // Base radius is 0.5, so radius = 0.5 * scale.x
                 if (materials.count(mat_name)) {
                     builder.add_sphere(pos, 0.5f * scale.x, materials[mat_name]);
                 }
                 continue; // Skip standard entity add
            }
            else if (type == "quad") mesh = SceneBuilder::create_quad();
            else if (type == "cube") mesh = SceneBuilder::create_cube();
            else if (type == "mesh_sphere") mesh = SceneBuilder::create_sphere(); // Fallback for mesh sphere
            else if (type == "highpoly_sphere") {
                int slices = 200;
                int stacks = 100;
                // Try to read extra args if available
                if (ss >> slices) {
                    ss >> stacks;
                }
                mesh = SceneBuilder::create_sphere(0.5f, slices, stacks);
            }
            else if (type == "mesh_cylinder") {
                float r = 0.5f, h = 1.0f;
                int segments = 32;
                if (ss >> r >> h >> segments) {}
                mesh = SceneBuilder::create_cylinder(r, h, segments);
            }
            else if (type == "mesh_cup") {
                float r = 0.5f, h = 1.0f, th = 0.05f;
                int segments = 32;
                if (ss >> r >> h >> th >> segments) {}
                mesh = SceneBuilder::create_cup(r, h, th, segments);
            }
            else if (type == "mesh_torus") {
                float major_r = 0.8f, minor_r = 0.1f;
                int major_seg = 48, minor_seg = 24;
                if (ss >> major_r >> minor_r >> major_seg >> minor_seg) {}
                mesh = SceneBuilder::create_torus(major_r, minor_r, major_seg, minor_seg);
            }

            if (mesh && materials.count(mat_name)) {
                builder.add_entity(mesh, materials[mat_name], pos, scale, rot);
            } else {
                if (!materials.count(mat_name)) {
                    std::cerr << "[SceneParser] Warning: Material '" << mat_name << "' not found." << std::endl;
                }
            }
        }
    }

    return builder.build();
}

}
