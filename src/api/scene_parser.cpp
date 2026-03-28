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
        else if (command == "spp") {
            int spp;
            ss >> spp;
            builder.set_spp(spp);
        }
        else if (command == "medium") {
             // medium density <d> scatter <r> <g> <b> absorb <r> <g> <b> max_dist <dist> g <g>
             float density = 0.0f;
             Vec3 scattering = {0,0,0}, absorption = {0,0,0};
             float max_dist = 0.0f;
             float anisotropy = 0.0f;
             
             std::string token;
             while (ss >> token) {
                 if (token == "density") ss >> density;
                 else if (token == "scatter") ss >> scattering.x >> scattering.y >> scattering.z;
                 else if (token == "absorb") ss >> absorption.x >> absorption.y >> absorption.z;
                 else if (token == "max_dist") ss >> max_dist;
                 else if (token == "g" || token == "anisotropy") ss >> anisotropy;
             }
             builder.set_medium(density, scattering, absorption, max_dist, anisotropy);
        }
        else if (command == "physics") {
            // physics enabled <0/1> dt <float> frames <int> spp <int>
            bool enabled = false;
            float dt = 1.0f/60.0f;
            int frames = 180;
            int spp_per_frame = 32;
            
            std::string token;
            while (ss >> token) {
                if (token == "enabled") ss >> enabled;
                else if (token == "dt") ss >> dt;
                else if (token == "frames") ss >> frames;
                else if (token == "spp") ss >> spp_per_frame;
            }
            builder.set_physics_enabled(enabled, dt, frames, spp_per_frame);
        }
        else if (command == "fluid") {
            // fluid enabled <0/1> spacing <float> bounds <min> <max> fill <min> <max>
            FluidConfig config;
            std::string token;
            while (ss >> token) {
                if (token == "enabled") ss >> config.enabled;
                else if (token == "spacing") ss >> config.particle_spacing;
                else if (token == "bounds") {
                    ss >> config.bounds_min.x >> config.bounds_min.y >> config.bounds_min.z;
                    ss >> config.bounds_max.x >> config.bounds_max.y >> config.bounds_max.z;
                }
                else if (token == "fill") {
                    ss >> config.fill_min.x >> config.fill_min.y >> config.fill_min.z;
                    ss >> config.fill_max.x >> config.fill_max.y >> config.fill_max.z;
                }
            }
            builder.set_fluid_config(config);
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

            // Check for optional parameters (thin film, medium/SSS) at the end of any material definition
            ss.clear(); // Clear any failbits from optional parameters
            std::string extra_token;
            while (ss >> extra_token) {
                if (extra_token == "thin_film") {
                    ss >> mat->thin_film_thickness >> mat->thin_film_ior;
                }
                else if (extra_token == "eta_rgb") {
                    ss >> mat->metal_eta.x >> mat->metal_eta.y >> mat->metal_eta.z;
                }
                else if (extra_token == "density") {
                    ss >> mat->medium_density;
                }
                else if (extra_token == "scatter") {
                    ss >> mat->medium_scattering.x >> mat->medium_scattering.y >> mat->medium_scattering.z;
                }
                else if (extra_token == "absorb") {
                    ss >> mat->medium_absorption.x >> mat->medium_absorption.y >> mat->medium_absorption.z;
                }
                else if (extra_token == "g" || extra_token == "anisotropy") {
                    ss >> mat->medium_anisotropy;
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
            bool is_analytical_sphere = false;
            
            if (type == "sphere") {
                 is_analytical_sphere = true;
                 // Don't continue yet, wait for physics parsing
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

            // Parse optional physics parameters
            ss.clear(); // Clear any failbits
            RigidBodyConfig rb_config;
            std::string extra_token;
            while (ss >> extra_token) {
                 if (extra_token == "mass") ss >> rb_config.mass;
                 else if (extra_token == "friction") ss >> rb_config.friction;
                 else if (extra_token == "restitution") ss >> rb_config.restitution;
                 else if (extra_token == "damping") ss >> rb_config.linear_damping;
                 else if (extra_token == "velocity") ss >> rb_config.velocity.x >> rb_config.velocity.y >> rb_config.velocity.z;
                 else if (extra_token == "material_id") ss >> rb_config.material_id;
                 else if (extra_token == "collider") {
                     ss >> rb_config.collider_type;
                     if (rb_config.collider_type == "box") {
                         ss >> rb_config.collider_size.x >> rb_config.collider_size.y >> rb_config.collider_size.z;
                     } else if (rb_config.collider_type == "sphere") {
                         ss >> rb_config.collider_radius;
                     }
                 }
            }
            
            if (rb_config.mass > 0.0f || rb_config.collider_type != "none") {
                rb_config.enabled = true;
            }

            if (is_analytical_sphere) {
                if (rb_config.enabled) {
                    // Fallback to Mesh Sphere for Physics Entities
                    // Default mesh sphere has radius 0.5, which matches analytical base radius.
                    // So we can preserve the scale.
                    mesh = SceneBuilder::create_sphere(); 
                } else {
                    // Use optimized analytical sphere for static non-physics objects
                    if (materials.count(mat_name)) {
                        builder.add_sphere(pos, 0.5f * scale.x, materials[mat_name]);
                    }
                    continue;
                }
            }

            if (mesh && materials.count(mat_name)) {
                builder.add_entity(mesh, materials[mat_name], pos, scale, rot, rb_config);
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
