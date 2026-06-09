#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <chrono>
#include "ure/core/vector.hpp"
#include "ure/ure_api.hpp"
#include "ure/procedural.hpp"
#include "ure/scene_parser.hpp"
#include "ure/image_saver.hpp" 
#include "ure/physics/physics_world.hpp"
#include "ure/physics/collider.hpp"
#include "ure/physics/rigid_body.hpp"
#include "ure/physics/physics_events.hpp"
#include "ure/physics/marching_cubes.hpp"
#include "ure/physics/acoustic/acoustic_system.hpp"
#include "ure/wav_saver.hpp"
#include "ure/core/quaternion.hpp"

using namespace ure;

#include <iomanip>
#include <sstream>

// Helper to convert Quaternion to Euler Angles (Degrees)
ure::core::Vec3<float> quat_to_euler(const ure::core::Quat& q) {
    ure::core::Vec3<float> euler;
    const float PI = 3.14159265359f;

    // Roll (x-axis rotation)
    float sinr_cosp = 2 * (q.w * q.x + q.y * q.z);
    float cosr_cosp = 1 - 2 * (q.x * q.x + q.y * q.y);
    euler.x = std::atan2(sinr_cosp, cosr_cosp);

    // Pitch (y-axis rotation)
    float sinp = 2 * (q.w * q.y - q.z * q.x);
    if (std::abs(sinp) >= 1)
        euler.y = std::copysign(PI / 2, sinp); // use 90 degrees if out of range
    else
        euler.y = std::asin(sinp);

    // Yaw (z-axis rotation)
    float siny_cosp = 2 * (q.w * q.z + q.x * q.y);
    float cosy_cosp = 1 - 2 * (q.y * q.y + q.z * q.z);
    euler.z = std::atan2(siny_cosp, cosy_cosp);

    // Convert to degrees
    euler.x = euler.x * 180.0f / PI;
    euler.y = euler.y * 180.0f / PI;
    euler.z = euler.z * 180.0f / PI;

    return euler;
}

// Helper to convert Euler Angles (Degrees) to Quaternion
ure::core::Quat euler_to_quat(const ure::core::Vec3<float>& euler) {
    const float PI = 3.14159265359f;
    float yaw = euler.z * PI / 180.0f;
    float pitch = euler.y * PI / 180.0f;
    float roll = euler.x * PI / 180.0f;
    
    float cy = cos(yaw * 0.5f);
    float sy = sin(yaw * 0.5f);
    float cp = cos(pitch * 0.5f);
    float sp = sin(pitch * 0.5f);
    float cr = cos(roll * 0.5f);
    float sr = sin(roll * 0.5f);

    ure::core::Quat q;
    q.w = cr * cp * cy + sr * sp * sy;
    q.x = sr * cp * cy - cr * sp * sy;
    q.y = cr * sp * cy + sr * cp * sy;
    q.z = cr * cp * sy - sr * sp * cy;
    return q;
}

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

#include "ure/wav_saver.hpp"
#include <vector>

int main(int argc, char* argv[]) {
    // ... (CLI parsing) ...
    // ...
    // After parsing:
    
    // Audio Buffer
    std::vector<float> audio_buffer;
    int sample_rate = 44100;

    // ... (Scene Setup) ...
    srand(static_cast<unsigned int>(time(0)));
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
    bool enable_physics = false;
    
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
        } else if (arg == "--physics") {
            enable_physics = true;
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
    ure::scene_ir::SceneIR scene_ir;
    bool has_scene_ir = false;
    
    // Physics World (Shared Pointer to keep it alive)
    std::shared_ptr<ure::physics::PhysicsWorld> physics_world;
    
    // Acoustic System
    auto acoustic_system = std::make_shared<ure::acoustic::AcousticSystem>();

    // Map to track dynamic entities for visual updates
    struct DynamicBody {
        int entity_index;
        std::shared_ptr<ure::physics::RigidBody> body;
    };
    std::vector<DynamicBody> dynamic_bodies;
    int fluid_entity_index = -1;

    // Legacy hardcoded pointers (kept for procedural fallback)
    std::shared_ptr<ure::physics::RigidBody> sphere1_body;
    std::shared_ptr<ure::physics::RigidBody> sphere2_body;
    std::shared_ptr<ure::physics::RigidBody> box_body;
    
    if (std::filesystem::exists(scene_path_or_name)) {
        std::cout << "[Main] Parsing scene file: " << scene_path_or_name << std::endl;
        scene_ir = SceneParser::parse_file_to_ir(scene_path_or_name);
        has_scene_ir = true;
        scene = ure::scene_ir::to_legacy_scene(scene_ir);

        // Check for physics configuration in scene
        if (scene.physics.enabled) {
            enable_physics = true;
            std::cout << "[Main] Scene Physics Enabled." << std::endl;
            physics_world = std::make_shared<ure::physics::PhysicsWorld>();
            
            // Register Acoustic System
            physics_world->register_listener(acoustic_system.get());
            
            // Link Acoustic System to Physics World (for Ray Tracing)
            acoustic_system->set_physics_world(physics_world.get());
            
            // Define some acoustic materials (IDs must match scene/logic)
            // ID 1: Metal, ID 2: Wood, ID 3: Glass
            // Removed hardcoded registration here. Will be handled per-entity.

            // Initialize Fluid System
            if (scene.physics.fluid.enabled) {
                 auto fluid_system = physics_world->get_fluid_system();
                 if (fluid_system) {
                     std::cout << "[Main] Initializing Fluid System from Scene..." << std::endl;
                    fluid_system->clear_particles();
                    ure::core::Vec3<float> fluid_bounds_min(scene.physics.fluid.bounds_min.x, scene.physics.fluid.bounds_min.y, scene.physics.fluid.bounds_min.z);
                    ure::core::Vec3<float> fluid_bounds_max(scene.physics.fluid.bounds_max.x, scene.physics.fluid.bounds_max.y, scene.physics.fluid.bounds_max.z);
                    float spacing = scene.physics.fluid.particle_spacing;
                    fluid_system->configure_rest_state(spacing, fluid_bounds_min, fluid_bounds_max);
                     
                     std::cout << "[Main] Systemic Calibration (Scene):" << std::endl;
                     std::cout << "  - Spacing: " << spacing << std::endl;
                    std::cout << "  - Calibrated Mass: " << fluid_system->particle_mass << std::endl;
                    
                    // High-Fidelity Params
                    fluid_system->target_density = 1000.0f;
                    fluid_system->pressure_stiffness = 500.0f;
                    fluid_system->viscosity_coefficient = 10.0f;
                    fluid_system->surface_tension_coefficient = 0.05f;
                    fluid_system->enable_particle_shifting = false;
                    fluid_system->enable_two_way_coupling = false;
                    
                    std::cout << "[Main] High-Res Fluid Config (Scene): Spacing=" << spacing 
                              << " Mass=" << fluid_system->particle_mass 
                              << " H=" << fluid_system->smoothing_radius << std::endl;
                    
                    std::cout << "[Main] Initializing particles with deterministic jittered volume fill..." << std::endl;
                    int p_count = fluid_system->seed_box_volume(
                        ure::core::Vec3<float>(scene.physics.fluid.fill_min.x, scene.physics.fluid.fill_min.y, scene.physics.fluid.fill_min.z),
                        ure::core::Vec3<float>(scene.physics.fluid.fill_max.x, scene.physics.fluid.fill_max.y, scene.physics.fluid.fill_max.z),
                        spacing,
                        0.1f,
                        1337u
                    );
                    std::cout << "[Main] Created " << p_count << " fluid particles." << std::endl;
                     
                     // Create Fluid Entity for Visualization
                     auto mesh_fluid = std::make_shared<Mesh>();
                     auto mat_fluid = std::make_shared<Material>();
                     mat_fluid->type = MaterialType::Dielectric; // Water
                     mat_fluid->albedo = {0.96f, 0.98f, 0.99f};
                    mat_fluid->roughness = 0.02f;
                    mat_fluid->ior = 1.33f;
                     
                     RenderEntity fluid_ent;
                     fluid_ent.mesh = mesh_fluid;
                     fluid_ent.material = mat_fluid;
                     fluid_ent.scale = {1,1,1};
                     fluid_ent.position = {0,0,0};
                     
                     scene.entities.push_back(fluid_ent);
                     fluid_entity_index = (int)scene.entities.size() - 1;
                     std::cout << "[Main] Fluid Entity Index assigned: " << fluid_entity_index << std::endl;
                 }
            }
            
            // Initialize Rigid Bodies from Entities
            using namespace ure::physics;
            for (size_t i = 0; i < scene.entities.size(); ++i) {
                const auto& entity = scene.entities[i];
                if (entity.rigid_body.enabled) {
                    auto rb = std::make_shared<RigidBody>();
                    rb->set_mass(entity.rigid_body.mass);
                    rb->position = ure::core::Vec3<float>(entity.position.x, entity.position.y, entity.position.z);
                    rb->orientation = euler_to_quat(ure::core::Vec3<float>(entity.rotation.x, entity.rotation.y, entity.rotation.z));
                    rb->velocity = ure::core::Vec3<float>(entity.rigid_body.velocity.x, entity.rigid_body.velocity.y, entity.rigid_body.velocity.z);
                    rb->friction = entity.rigid_body.friction;
                    rb->restitution = entity.rigid_body.restitution;
                    rb->linear_damping = entity.rigid_body.linear_damping;
                    rb->material_id = entity.rigid_body.material_id;
                    rb->angular_damping = entity.rigid_body.angular_damping;
                    
                    // Assign Acoustic Material ID based on visual material
                    if (entity.material) {
                        if (entity.material->type == MaterialType::Metal) {
                            rb->material_id = 1; // Metal
                        } else if (entity.material->type == MaterialType::Dielectric) {
                            rb->material_id = 3; // Glass
                        } else if (entity.material->type == MaterialType::Lambertian) {
                            rb->material_id = 2; // Wood
                        }
                    }

                    physics_world->add_body(rb);
                    
                    // Add Collider
                    if (entity.rigid_body.collider_type == "box") {
                        physics_world->add_collider(std::make_shared<BoxCollider>(
                            rb.get(), ure::core::Vec3<float>(entity.rigid_body.collider_size.x, entity.rigid_body.collider_size.y, entity.rigid_body.collider_size.z), ure::core::Vec3<float>(0)));
                    } else if (entity.rigid_body.collider_type == "sphere") {
                        physics_world->add_collider(std::make_shared<SphereCollider>(
                            rb.get(), entity.rigid_body.collider_radius, ure::core::Vec3<float>(0)));
                    } else if (entity.rigid_body.collider_type == "plane") {
                         // Assume plane is facing UP at position.y
                        physics_world->add_collider(std::make_shared<PlaneCollider>(
                            rb.get(), ure::core::Vec3<float>(0, 1, 0), entity.position.y));
                    }
                    
                    // Track dynamic bodies
                    // Define Acoustic Props first
                    ure::acoustic::AcousticMaterial mat_props;
                    int mid = entity.rigid_body.material_id;
                    
                    if (mid == 3 || (mid == 0 && entity.material && entity.material->type == MaterialType::Dielectric)) {
                        // Glass: Low absorption, High transmission
                        mat_props = {"Glass", 2500.0f, 70e9f, 0.2f, 0.003f, 0.05f, 0.05f, 0.9f};
                    } else if (mid == 1 || (mid == 0 && entity.material && entity.material->type == MaterialType::Metal)) {
                        // Metal: Very low absorption
                        // Increased damping from 0.001 to 0.005 to reduce ringing
                        mat_props = {"Metal", 7800.0f, 200e9f, 0.33f, 0.005f, 0.02f, 0.1f, 0.0f};
                    } else if (mid == 2 || (mid == 0 && entity.material && entity.material->type == MaterialType::Lambertian)) {
                        // Wood
                        mat_props = {"Wood", 700.0f, 10e9f, 0.4f, 0.05f, 0.4f, 0.5f, 0.0f};
                    } else if (mid == 4) {
                        // Concrete Wall (Hard, reflective)
                        mat_props = {"Concrete", 2400.0f, 30e9f, 0.2f, 0.01f, 0.1f, 0.3f, 0.0f};
                    } else if (mid == 5) {
                        // Carpet/Ground (Soft, absorptive)
                        mat_props = {"Carpet", 100.0f, 0.1e9f, 0.4f, 0.5f, 0.8f, 0.6f, 0.0f};
                    } else {
                        mat_props = {"Generic", 1000.0f, 1e9f, 0.3f, 0.1f, 0.2f, 0.2f, 0.0f};
                    }

                    // Register Material for Ray Tracer (Static & Dynamic)
                    acoustic_system->register_material(mid, mat_props);

                    if (entity.rigid_body.mass > 0.0f) {
                        dynamic_bodies.push_back({(int)i, rb});
                        std::cout << "[Main] Registered Dynamic Body for Entity " << i 
                                  << " (Mass: " << entity.rigid_body.mass << ")" << std::endl;
                        
                        // Register for Acoustic System (Modal Synthesis)
                        ure::core::Vec3<float> dims = {1.0f, 1.0f, 1.0f};
                        if (entity.rigid_body.collider_type == "box") {
                            dims = {entity.rigid_body.collider_size.x, entity.rigid_body.collider_size.y, entity.rigid_body.collider_size.z};
                        } else if (entity.rigid_body.collider_type == "sphere") {
                            float d = entity.rigid_body.collider_radius * 2.0f;
                            dims = {d, d, d};
                        }
                        
                        // Use material_id as body_id (simplified)
                        acoustic_system->register_body(mid, mat_props, dims);
                    }
            }
            }
            
            // Also register Acoustic System listener here for loaded scenes!
            if (physics_world) {
                physics_world->register_listener(acoustic_system.get());
            }
        }
    } else {
        if (scene_path_or_name != "procedural_demo") {
             std::cerr << "[Main] Warning: File '" << scene_path_or_name << "' not found. Falling back to procedural demo." << std::endl;
        } else {
             std::cout << "[Main] No file specified, using internal procedural fallback." << std::endl;
        }
        SceneBuilder builder;
        
        if (enable_physics) {
            std::cout << "[Main] Physics Demo Enabled. Preparing..." << std::endl;
            
            using namespace ure::physics;
            physics_world = std::make_shared<PhysicsWorld>();

            // Register Acoustic System
            physics_world->register_listener(acoustic_system.get());
            
            // Define default materials
            // Metal: Low damping (long ring), High stiffness
            // Increased damping from 0.001 to 0.005
            acoustic_system->register_body(1, {"Metal", 7800.0f, 200e9f, 0.33f, 0.005f}, {1.0f, 1.0f, 1.0f});
            // Wood: High damping (thud), Low stiffness
            acoustic_system->register_body(2, {"Wood", 700.0f, 10e9f, 0.4f, 0.05f, 0.2f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f});
            // Glass: High stiffness, Low damping, Transparent to sound (Refraction test)
            acoustic_system->register_body(3, {"Glass", 2500.0f, 70e9f, 0.24f, 0.002f, 0.05f, 0.1f, 0.8f}, {1.0f, 1.0f, 1.0f});
        
        // 1. Setup Physics World (Glass Cup Demo)
        // Floor (Static Plane)
        auto floor_body = std::make_shared<RigidBody>();
        floor_body->set_mass(0); // Static
        floor_body->position = ure::core::Vec3<float>(0, -1.05f, 0); // Slightly below cup
        floor_body->restitution = 0.5f;
        physics_world->add_body(floor_body);
        
        auto floor_collider = std::make_shared<PlaneCollider>(floor_body.get(), ure::core::Vec3<float>(0, 1, 0), -1.05f);
        physics_world->add_collider(floor_collider);
        
        // Glass Cup Colliders (5 Walls)
        // Cup Interior: [-1, 1] x [-1, 1] x [-1, 1] (roughly)
        // Bottom Wall
        auto cup_bottom = std::make_shared<RigidBody>();
        cup_bottom->set_mass(0);
        cup_bottom->position = ure::core::Vec3<float>(0, -1.05f, 0);
        physics_world->add_body(cup_bottom);
        physics_world->add_collider(std::make_shared<BoxCollider>(cup_bottom.get(), ure::core::Vec3<float>(1.1f, 0.05f, 1.1f), ure::core::Vec3<float>(0)));

        // Left Wall (-x)
        auto cup_left = std::make_shared<RigidBody>();
        cup_left->set_mass(0);
        cup_left->position = ure::core::Vec3<float>(-1.05f, 0.0f, 0);
        physics_world->add_body(cup_left);
        physics_world->add_collider(std::make_shared<BoxCollider>(cup_left.get(), ure::core::Vec3<float>(0.05f, 1.0f, 1.1f), ure::core::Vec3<float>(0)));

        // Right Wall (+x)
        auto cup_right = std::make_shared<RigidBody>();
        cup_right->set_mass(0);
        cup_right->position = ure::core::Vec3<float>(1.05f, 0.0f, 0);
        physics_world->add_body(cup_right);
        physics_world->add_collider(std::make_shared<BoxCollider>(cup_right.get(), ure::core::Vec3<float>(0.05f, 1.0f, 1.1f), ure::core::Vec3<float>(0)));

        // Front Wall (+z)
        auto cup_front = std::make_shared<RigidBody>();
        cup_front->set_mass(0);
        cup_front->position = ure::core::Vec3<float>(0, 0.0f, 1.05f);
        physics_world->add_body(cup_front);
        physics_world->add_collider(std::make_shared<BoxCollider>(cup_front.get(), ure::core::Vec3<float>(1.0f, 1.0f, 0.05f), ure::core::Vec3<float>(0)));

        // Back Wall (-z)
        auto cup_back = std::make_shared<RigidBody>();
        cup_back->set_mass(0);
        cup_back->position = ure::core::Vec3<float>(0, 0.0f, -1.05f);
        physics_world->add_body(cup_back);
        physics_world->add_collider(std::make_shared<BoxCollider>(cup_back.get(), ure::core::Vec3<float>(1.0f, 1.0f, 0.05f), ure::core::Vec3<float>(0)));

        // Sphere 1 (Falling into cup)
        sphere1_body = std::make_shared<RigidBody>();
        sphere1_body->set_mass(2.0f);
        sphere1_body->position = ure::core::Vec3<float>(0.1f, 2.0f, 0.1f); 
        sphere1_body->restitution = 0.6f;
        sphere1_body->friction = 0.2f; 
        sphere1_body->linear_damping = 0.99f; 
        sphere1_body->angular_damping = 0.01f; 
        sphere1_body->material_id = 1; // Metal
        physics_world->add_body(sphere1_body);
        
        auto sphere1_collider = std::make_shared<SphereCollider>(sphere1_body.get(), 0.4f, ure::core::Vec3<float>(0)); 
        physics_world->add_collider(sphere1_collider);

        // Box (Inside cup, sinking)
        box_body = std::make_shared<RigidBody>();
        box_body->set_mass(5.0f);
        box_body->position = ure::core::Vec3<float>(-0.5f, -0.5f, -0.5f); 
        box_body->restitution = 0.2f; 
        box_body->friction = 0.5f;
        box_body->linear_damping = 0.99f;
        box_body->angular_damping = 0.1f;
        box_body->material_id = 3; // Wood
        physics_world->add_body(box_body);

        auto box_collider = std::make_shared<BoxCollider>(box_body.get(), ure::core::Vec3<float>(0.3f, 0.3f, 0.3f), ure::core::Vec3<float>(0));
        physics_world->add_collider(box_collider);

        // Sphere 2 (Static Obstacle in air/water?) -> Let's make it dynamic too
        sphere2_body = std::make_shared<RigidBody>();
        sphere2_body->set_mass(1.0f); 
        sphere2_body->position = ure::core::Vec3<float>(0.5f, 0.0f, 0.5f); 
        sphere2_body->restitution = 0.8f;
        sphere2_body->friction = 0.1f; 
        sphere2_body->material_id = 2; // Wood
        physics_world->add_body(sphere2_body);
        
        auto sphere2_collider = std::make_shared<SphereCollider>(sphere2_body.get(), 0.4f, ure::core::Vec3<float>(0)); 
        physics_world->add_collider(sphere2_collider);
        
        // Initialize Fluid System
        auto fluid_system = physics_world->get_fluid_system();
        if (fluid_system) {
            std::cout << "[Main] Initializing Fluid System (Cup)..." << std::endl;
            fluid_system->clear_particles();
            float spacing = 0.035f;
            fluid_system->configure_rest_state(spacing, {-5, -5, -5}, {5, 5, 5});

            std::cout << "[Main] Derived Fluid Calibration:" << std::endl;
            std::cout << "  - Spacing: " << spacing << std::endl;
            std::cout << "  - Calibrated Mass: " << fluid_system->particle_mass << std::endl;
            
            fluid_system->target_density = 1000.0f;
            fluid_system->pressure_stiffness = 3000.0f;
            fluid_system->viscosity_coefficient = 0.002f;
            fluid_system->surface_tension_coefficient = 0.05f;
            fluid_system->enable_particle_shifting = false;
            fluid_system->enable_two_way_coupling = false;

            std::cout << "[Main] High-Res Fluid Config: Spacing=" << spacing 
                      << " Mass=" << fluid_system->particle_mass 
                      << " H=" << fluid_system->smoothing_radius << std::endl;

            fluid_system->seed_box_volume({-0.9f, -0.9f, -0.9f}, {0.9f, 0.0f, 0.9f}, spacing, 0.1f, 2026u);
            std::cout << "[Main] Created " << fluid_system->get_particles().size() << " fluid particles." << std::endl;
        }

        // 2. Build Visual Scene (Initial State)
        auto mesh_sphere = SceneBuilder::create_sphere(0.4f); // Radius 0.4
        auto mesh_cube = SceneBuilder::create_cube(1.0f); // Default cube size 1.0 (extent 0.5)
        // We need custom sized cubes for walls? 
        // SceneBuilder::create_cube creates unit cube [-0.5, 0.5].
        // We can scale it via entity transform.
        
        auto mesh_quad = SceneBuilder::create_quad();
        
        auto mat_floor = std::make_shared<Material>();
        mat_floor->albedo = {0.5f, 0.5f, 0.5f};
        mat_floor->roughness = 0.8f;
        
        auto mat_glass = std::make_shared<Material>();
        mat_glass->type = MaterialType::Dielectric;
        mat_glass->ior = 1.5f;
        mat_glass->roughness = 0.0f;
        mat_glass->albedo = {1.0f, 1.0f, 1.0f};

        auto mat_box = std::make_shared<Material>();
        mat_box->albedo = {0.8f, 0.6f, 0.2f}; // Wood color
        mat_box->roughness = 0.6f;

        auto mat_sphere1 = std::make_shared<Material>();
        mat_sphere1->albedo = {0.8f, 0.2f, 0.2f}; // Red
        mat_sphere1->roughness = 0.2f;

        auto mat_sphere2 = std::make_shared<Material>();
        mat_sphere2->albedo = {0.2f, 0.2f, 0.8f}; // Blue
        mat_sphere2->roughness = 0.2f;

        auto water_mat = std::make_shared<Material>();
        // water_mat->type = MaterialType::Dielectric;
        // water_mat->ior = 1.33f;
        // water_mat->roughness = 0.0f;
        // water_mat->albedo = {1.0f, 1.0f, 1.0f};
        
        // Water Material
        water_mat->type = MaterialType::Dielectric;
        water_mat->albedo = {0.6f, 0.85f, 0.98f};
        water_mat->roughness = 0.02f;
        water_mat->ior = 1.33f;
        
        auto mat_light = std::make_shared<Material>();
        mat_light->type = MaterialType::Light;
        mat_light->emission = {20.0f, 20.0f, 20.0f};

        // Visual Floor (Entity 0)
        builder.add_entity(mesh_quad, mat_floor, {0, -1.05f, 0}, {20, 1, 20}, {0, 0, 0});
        
        // Visual Cup Walls (Entities 1-5)
        // Bottom
        builder.add_entity(mesh_cube, mat_glass, {0, -1.05f, 0}, {2.2f, 0.1f, 2.2f}, {0,0,0});
        // Left
        builder.add_entity(mesh_cube, mat_glass, {-1.05f, 0, 0}, {0.1f, 2.0f, 2.2f}, {0,0,0});
        // Right
        builder.add_entity(mesh_cube, mat_glass, {1.05f, 0, 0}, {0.1f, 2.0f, 2.2f}, {0,0,0});
        // Front
        builder.add_entity(mesh_cube, mat_glass, {0, 0, 1.05f}, {2.0f, 2.0f, 0.1f}, {0,0,0});
        // Back
        builder.add_entity(mesh_cube, mat_glass, {0, 0, -1.05f}, {2.0f, 2.0f, 0.1f}, {0,0,0});

        // Visual Box (Entity 6) -> Needs scaling to 0.6 (0.3 extents)
        ure::Vec3 box_pos(box_body->position.x, box_body->position.y, box_body->position.z);
        builder.add_entity(mesh_cube, mat_box, box_pos, {0.6f, 0.6f, 0.6f}, {0, 0, 0});

        // Visual Sphere 1 (Entity 7)
        ure::Vec3 s1_pos(sphere1_body->position.x, sphere1_body->position.y, sphere1_body->position.z);
        builder.add_entity(mesh_sphere, mat_sphere1, s1_pos, {1, 1, 1}, {0, 0, 0});

        // Visual Sphere 2 (Entity 8)
        ure::Vec3 s2_pos(sphere2_body->position.x, sphere2_body->position.y, sphere2_body->position.z);
        builder.add_entity(mesh_sphere, mat_sphere2, s2_pos, {1, 1, 1}, {0, 0, 0});
        
        // Fluid Entity (Dynamic Index)
        auto mesh_fluid = std::make_shared<Mesh>();
        fluid_entity_index = builder.get_entity_count();
        std::cout << "[Main] Fluid Entity Index assigned: " << fluid_entity_index << std::endl;
        builder.add_entity(mesh_fluid, water_mat, {0,0,0}, {1,1,1}, {0,0,0});

        // Light (Entity 10)
        builder.add_sphere({0, 10, 0}, 1.0f, mat_light);
        builder.set_camera({0, 2, 6}, {0, 0, 0}, 45.0f); // Closer camera
        } else {
            // Standard Procedural Demo
            // 1. Create Shared Assets (Meshes & Materials)
            auto mesh_cube = SceneBuilder::create_cube(1.0f);
            auto mesh_sphere = SceneBuilder::create_sphere(1.0f); // Analytical spheres are better, but testing mesh instancing here
            auto mesh_quad = SceneBuilder::create_quad();
    
            auto mat_red = std::make_shared<Material>();
            mat_red->albedo = {0.8f, 0.1f, 0.1f};
            mat_red->roughness = 0.3f;
    
            auto mat_glass = std::make_shared<Material>();
            mat_glass->type = MaterialType::Dielectric;
            mat_glass->albedo = {1.0f, 1.0f, 1.0f};
            mat_glass->ior = 1.5f;
            mat_glass->roughness = 0.0f;
    
            auto mat_floor = std::make_shared<Material>();
            mat_floor->albedo = {0.8f, 0.8f, 0.8f};
            mat_floor->roughness = 0.5f;
    
            auto mat_light = std::make_shared<Material>();
            mat_light->type = MaterialType::Light;
            mat_light->emission = {10.0f, 10.0f, 10.0f};
    
            // 2. Build Scene using Instances
            // Floor
            builder.add_entity(mesh_quad, mat_floor, {0, -1.0f, 0}, {20, 20, 1}, {-90, 0, 0});
    
            // Instanced Cubes (Same mesh, different transforms)
            for (int i = -3; i <= 3; ++i) {
                float x = i * 2.5f;
                float angle = i * 15.0f;
                // Alternating materials if desired, but here we test geometry reuse
                builder.add_entity(mesh_cube, mat_red, {x, 0.0f, 0.0f}, {1, 1, 1}, {0, angle, 0});
            }
    
            // Instanced Spheres (Glass)
            builder.add_entity(mesh_sphere, mat_glass, {0, 1.5f, 3.0f}, {1.5f, 1.5f, 1.5f}, {0, 0, 0});
            builder.add_entity(mesh_sphere, mat_glass, {-3.0f, 1.0f, 3.0f}, {1.0f, 1.0f, 1.0f}, {0, 0, 0});
            builder.add_entity(mesh_sphere, mat_glass, {3.0f, 1.0f, 3.0f}, {1.0f, 1.0f, 1.0f}, {0, 0, 0});
    
            // Light
            builder.add_sphere({0, 8, 0}, 1.0f, mat_light);
    
            // Camera matching the default scene description
            builder.set_camera({0, 5, 15}, {0, 0, 0}, 35.0f);
        }
        scene = builder.build();
    }

    // 3. Initialize Engine
    std::cout << "[Main] Initializing GPU Engine..." << std::endl;
    auto engine = RenderEngineFactory::create_gpu_engine();

    // Fluid Initialization Pass
    if (scene.physics.fluid.enabled && physics_world) {
        auto fluid_system = physics_world->get_fluid_system();
        std::cout << "[Main] Relaxing initial fluid particle distribution..." << std::endl;
        fluid_system->relax_initial_distribution(physics_world->get_colliders(), 8);
        std::cout << "[Main] Fluid initialization pass complete." << std::endl;
    }

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
        scene.width = 1600;
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
        scene.height = 900;
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
    bool use_scene_ir_for_engine = has_scene_ir && !enable_physics;
    if (use_scene_ir_for_engine) {
        engine->load_scene_ir(scene_ir);
    } else {
        engine->load_scene(scene);
    }
    
    // Set Spatial Audio Listener
    {
        ure::core::Vec3<float> cam_pos(scene.camera.position.x, scene.camera.position.y, scene.camera.position.z);
        ure::core::Vec3<float> cam_look(scene.camera.look_at.x, scene.camera.look_at.y, scene.camera.look_at.z);
        ure::core::Vec3<float> cam_up(scene.camera.up.x, scene.camera.up.y, scene.camera.up.z);
        // Manual subtraction to avoid operator lookup issues
        ure::core::Vec3<float> cam_forward(cam_look.x - cam_pos.x, cam_look.y - cam_pos.y, cam_look.z - cam_pos.z);
        acoustic_system->set_listener(cam_pos, cam_forward, cam_up);
        
        // Link Physics World for Ray Tracing
        if (physics_world) {
            acoustic_system->set_physics_world(physics_world.get());
        }
        
        std::cout << "[Main] Spatial Audio Listener set at: " << cam_pos.x << ", " << cam_pos.y << ", " << cam_pos.z << std::endl;
    }
    
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
        if (enable_physics && std::filesystem::exists(scene_path_or_name)) {
             std::string scene_name = std::filesystem::path(scene_path_or_name).stem().string();
             output_dir = std::filesystem::current_path() / "output" / scene_name;
        } else {
             output_dir = std::filesystem::current_path() / "output";
        }
    }

    if (!std::filesystem::exists(output_dir)) {
        std::filesystem::create_directories(output_dir);
    }
    // Removed physics_demo subdirectory creation to simplify structure
    
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
    
    // --- Physics Render Loop ---
    if (enable_physics) {
        float dt = (scene.physics.dt > 0) ? scene.physics.dt : (1.0f / 60.0f);
        int total_frames = (scene.physics.total_frames > 0) ? scene.physics.total_frames : 180;
        int spp_per_frame = (scene.physics.spp_per_frame > 0) ? scene.physics.spp_per_frame : 64;
        
        auto start_time = std::chrono::high_resolution_clock::now();

            try {
                for (int frame = 0; frame < total_frames; ++frame) {
                    // Log Progress
                    auto now = std::chrono::high_resolution_clock::now();
                    auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
                    float avg_time_per_frame = (frame > 0) ? (float)duration / frame : 0.0f;
                    float eta = (avg_time_per_frame > 0) ? (total_frames - frame) * avg_time_per_frame : 0.0f;

                    std::cout << "[Progress] Frame " << frame + 1 << "/" << total_frames 
                              << " (" << std::fixed << std::setprecision(1) << (float)(frame + 1) / total_frames * 100.0f << "%)"
                              << " Elapsed: " << duration << "s ETA: " << (int)eta << "s" << std::endl;

                    // 1. Step Physics (Events will trigger here)
                    std::cout << "  [Step] Physics..." << std::flush;
                    physics_world->step(dt);
                    
                    // 2. Synthesize Audio
                    std::vector<float> frame_audio = acoustic_system->generate_samples(dt, sample_rate);
                    audio_buffer.insert(audio_buffer.end(), frame_audio.begin(), frame_audio.end());
                    
                    std::cout << " Done. (Audio Samples: " << frame_audio.size() << ")" << std::endl;
                    
                    // Update Fluid Visuals
                    auto fs = physics_world->get_fluid_system();
                    if (fs) {
                        // Dynamic AABB for Marching Cubes to optimize resolution
                        ure::core::Vec3<float> mc_min = fs->bounds_max;
                        ure::core::Vec3<float> mc_max = fs->bounds_min;
                        const auto& particles = fs->get_particles();
                        
                        if (!particles.empty()) {
                            mc_min = particles[0].position;
                            mc_max = particles[0].position;
                            for (const auto& p : particles) {
                                if (p.position.x < mc_min.x) mc_min.x = p.position.x;
                                if (p.position.y < mc_min.y) mc_min.y = p.position.y;
                                if (p.position.z < mc_min.z) mc_min.z = p.position.z;
                                
                                if (p.position.x > mc_max.x) mc_max.x = p.position.x;
                                if (p.position.y > mc_max.y) mc_max.y = p.position.y;
                                if (p.position.z > mc_max.z) mc_max.z = p.position.z;
                            }
                            // Add padding
                            float padding = 0.2f;
                            mc_min = mc_min - ure::core::Vec3<float>(padding, padding, padding);
                            mc_max = mc_max + ure::core::Vec3<float>(padding, padding, padding);
                        } else {
                            mc_min = fs->bounds_min;
                            mc_max = fs->bounds_max;
                        }
                        
                        // Generate Mesh
        // Isolevel adjusted to reduce blob artifacts. Target density is 1000.
        // Higher isolevel = tighter surface, less blobby.
        float isolevel = fs->target_density * 0.5f;
        auto fluid_mesh = ure::physics::MarchingCubes::generate(*fs, mc_min, mc_max, 128, isolevel);
                        std::cout << " Done. (Triangles: " << fluid_mesh->indices.size() / 3 << ")" << std::endl;
                        
                        // Update the fluid entity (Dynamic Index)
                        if (fluid_entity_index >= 0 && scene.entities.size() > fluid_entity_index) {
                            scene.entities[fluid_entity_index].mesh = fluid_mesh;
                        }
                    }

                    // 2. Update Visuals
                    for (const auto& db : dynamic_bodies) {
                        if (db.entity_index < scene.entities.size()) {
                            ure::core::Vec3<float> pos = db.body->position;
                            ure::core::Quat rot = db.body->orientation;
                            scene.entities[db.entity_index].position = {pos.x, pos.y, pos.z};
                            ure::core::Vec3<float> euler = quat_to_euler(rot);
                            scene.entities[db.entity_index].rotation = {euler.x, euler.y, euler.z};
                        }
                    }

                    // Legacy Update for Procedural Demo
                    if (dynamic_bodies.empty() && box_body && sphere1_body && sphere2_body && scene.entities.size() > 8) {
                        // Update Box (Entity 6)
                        ure::core::Vec3<float> pos_box = box_body->position;
                        ure::core::Quat rot_box = box_body->orientation;
                        scene.entities[6].position = {pos_box.x, pos_box.y, pos_box.z};
                        ure::core::Vec3<float> euler_box = quat_to_euler(rot_box);
                        scene.entities[6].rotation = {euler_box.x, euler_box.y, euler_box.z};

                        // Update Sphere 1 (Entity 7)
                        ure::core::Vec3<float> pos1 = sphere1_body->position;
                        ure::core::Quat rot1 = sphere1_body->orientation;
                        scene.entities[7].position = {pos1.x, pos1.y, pos1.z};
                        ure::core::Vec3<float> euler1 = quat_to_euler(rot1);
                        scene.entities[7].rotation = {euler1.x, euler1.y, euler1.z};

                        // Update Sphere 2 (Entity 8)
                        ure::core::Vec3<float> pos2 = sphere2_body->position;
                        ure::core::Quat rot2 = sphere2_body->orientation;
                        scene.entities[8].position = {pos2.x, pos2.y, pos2.z};
                        ure::core::Vec3<float> euler2 = quat_to_euler(rot2);
                        scene.entities[8].rotation = {euler2.x, euler2.y, euler2.z};
                    }
                    
                    // 3. Update Scene in Engine
                    std::cout << "  [Step] Uploading Scene to GPU..." << std::flush;
                    engine->load_scene(scene);
                    engine->reset_accumulation();
                    std::cout << " Done." << std::endl;
                    
                    // 4. Render Frame
                    std::cout << "  [Step] Rendering " << spp_per_frame << " SPP: " << std::flush;
                    for (int s = 0; s < spp_per_frame; ++s) {
                        engine->render_pass();
                        std::cout << "." << std::flush;
                    }
                    std::cout << " Done." << std::endl;
                    
                    // 5. Save Frame
                    std::cout << "  [Step] Saving Frame..." << std::flush;
                    std::stringstream ss;
                    ss << "frame_" << std::setw(3) << std::setfill('0') << frame << ".bmp";
                    std::string frame_filename = ss.str();
                    std::string frame_path = (output_dir / frame_filename).string();
                    
                    save_current_frame(engine.get(), settings.width, settings.height, frame_path);
                    std::cout << " Done." << std::endl;
                    
                    std::cout << "[Main] Frame " << frame + 1 << "/" << total_frames << " saved." << std::endl;
                    
                    // Debug: Print Dynamic Bodies Status
                    if (frame % 10 == 0) {
                         for (const auto& db : dynamic_bodies) {
                             std::cout << "  [Debug] Body " << db.entity_index 
                                       << " Pos: " << db.body->position.x << "," << db.body->position.y << "," << db.body->position.z 
                                       << " Vel: " << db.body->velocity.y 
                                       << " Mass: " << db.body->mass
                                       << " Static: " << (db.body->is_static ? "YES" : "NO")
                                       << std::endl;
                         }
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "\n[CRITICAL ERROR] Simulation Loop Failed: " << e.what() << std::endl;
                return 1;
            } catch (...) {
                std::cerr << "\n[CRITICAL ERROR] Simulation Loop Failed with Unknown Error" << std::endl;
                return 1;
            }
        
        // Save Audio
        std::filesystem::path audio_path = output_dir / "physics_demo.wav";
        // If specific output dir is used, use scene name
        if (std::filesystem::exists(scene_path_or_name)) {
             std::string scene_name = std::filesystem::path(scene_path_or_name).stem().string();
             audio_path = output_dir / (scene_name + ".wav");
        }
        
        ure::io::WavSaver::save(audio_path.string(), audio_buffer, sample_rate, 2);

        std::cout << std::endl << "[Main] Physics Simulation & Rendering Complete." << std::endl;
        std::cout << "[Main] Frames saved to " << output_dir.string() << std::endl;
        std::cout << "[Main] Audio saved to " << audio_path.string() << std::endl;
        return 0;
    }

    // --- Standard Render Loop ---
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
