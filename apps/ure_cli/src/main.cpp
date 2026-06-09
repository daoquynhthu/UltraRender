#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>
#include "ure/core/vector.hpp"
#include "ure/core/quaternion.hpp"
#include "ure/ure_api.hpp"
#include "ure/render.hpp"
#include "ure/physics.hpp"
#include "ure/scene_io.hpp"
#include "ure/config.hpp"
#include "ure/gpu_scene_compiler.hpp"
#include "ure/world_scene_builder.hpp"
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

using namespace ure;

// ── helper: flush engine framebuffer to disk via public API ──
static void save_frame(IRenderEngine* engine, int width, int height, const std::string& path) {
    const auto& buffer = engine->get_framebuffer();
    if (buffer.empty()) return;
    std::vector<core::Vec3f> pixels(width * height);
    for (size_t i = 0; i < pixels.size(); ++i)
        pixels[i] = {buffer[i*3], buffer[i*3+1], buffer[i*3+2]};

    std::string tmp = path + ".tmp";
    ure::io::ImageSaver::save_bmp(tmp, width, height, pixels, ure::io::ToneMapType::ACES, 1.0f);
    if (std::filesystem::exists(path)) std::filesystem::remove(path);
    try { std::filesystem::rename(tmp, path); } catch (...) {}
}

// ── helper: dynamic body list ──
struct DynamicBody {
    int entity_index;
    std::shared_ptr<physics::RigidBody> body;
};

// ================================================================

int main(int argc, char* argv[]) {
    srand(static_cast<unsigned int>(time(0)));
    std::cout << "========================================\n"
              << "   UltraRender Engine - Modular MVP\n"
              << "========================================\n";

    // 1. Parse arguments via public API
    ure::config::RenderConfig cfg;
    if (argc > 1) {
        cfg = ure::config::parse_cli(argc, argv);
    }
    bool enable_physics = cfg.physics_enabled;
    std::string scene_path = cfg.scene_path.empty() ? "procedural_demo" : cfg.scene_path;
    int cli_spp = cfg.spp;
    int cli_width = cfg.width;
    int cli_height = cfg.height;
    std::string output_filename_override = cfg.output_path;
    std::string output_dir_str;

    // Legacy fallback: positional arg as scene path
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg[0] != '-' && scene_path == "procedural_demo")
            scene_path = arg;
        if ((arg == "-d" || arg == "--output-dir") && i + 1 < argc)
            output_dir_str = argv[++i];
    }

    std::cout << "[Main] Target: " << scene_path << ", SPP: " << cli_spp << "\n";

    // 2. Build Scene
    Scene scene;
    ure::scene_ir::SceneIR scene_ir;
    bool has_scene_ir = false;
    std::shared_ptr<physics::PhysicsWorld> physics_world;
    auto acoustic_system = std::make_shared<acoustic::AcousticSystem>();
    std::vector<DynamicBody> dynamic_bodies;
    int fluid_entity_index = -1;

    // Legacy rigid body pointers (procedural fallback)
    std::shared_ptr<physics::RigidBody> sphere1_body, sphere2_body, box_body;

    if (std::filesystem::exists(scene_path)) {
        std::cout << "[Main] Parsing scene file: " << scene_path << "\n";
        scene_ir = SceneParser::parse_file_to_ir(scene_path);
        has_scene_ir = true;
        scene = ure::scene_ir::to_legacy_scene(scene_ir);

        if (scene.physics.enabled) {
            enable_physics = true;
            std::cout << "[Main] Scene Physics Enabled.\n";
            physics_world = std::make_shared<physics::PhysicsWorld>();
            physics_world->register_listener(acoustic_system.get());
            acoustic_system->set_spatial_query(physics_world.get());

            if (scene.physics.fluid.enabled) {
                auto fluid_system = physics_world->get_fluid_system();
                if (fluid_system) {
                    std::cout << "[Main] Initializing Fluid System from Scene...\n";
                    fluid_system->clear_particles();
                    core::Vec3f fbmin(scene.physics.fluid.bounds_min.x, scene.physics.fluid.bounds_min.y, scene.physics.fluid.bounds_min.z);
                    core::Vec3f fbmax(scene.physics.fluid.bounds_max.x, scene.physics.fluid.bounds_max.y, scene.physics.fluid.bounds_max.z);
                    fluid_system->configure_rest_state(scene.physics.fluid.particle_spacing, fbmin, fbmax);
                    fluid_system->target_density = 1000.0f;
                    fluid_system->pressure_stiffness = 500.0f;
                    fluid_system->viscosity_coefficient = 10.0f;
                    fluid_system->surface_tension_coefficient = 0.05f;
                    fluid_system->enable_particle_shifting = false;
                    fluid_system->enable_two_way_coupling = false;
                    core::Vec3f ffill_min(scene.physics.fluid.fill_min.x, scene.physics.fluid.fill_min.y, scene.physics.fluid.fill_min.z);
                    core::Vec3f ffill_max(scene.physics.fluid.fill_max.x, scene.physics.fluid.fill_max.y, scene.physics.fluid.fill_max.z);
                    int p_count = fluid_system->seed_box_volume(ffill_min, ffill_max, scene.physics.fluid.particle_spacing, 0.1f, 1337u);
                    std::cout << "[Main] Created " << p_count << " fluid particles.\n";
                    auto mesh_fluid = std::make_shared<Mesh>();
                    auto mat_fluid = std::make_shared<Material>();
                    mat_fluid->type = MaterialType::Dielectric;
                    mat_fluid->albedo = {0.96f, 0.98f, 0.99f};
                    mat_fluid->roughness = 0.02f;
                    mat_fluid->ior = 1.33f;
                    RenderEntity fluid_ent;
                    fluid_ent.mesh = mesh_fluid;
                    fluid_ent.material = mat_fluid;
                    fluid_ent.scale = {1,1,1};
                    fluid_ent.position = {};
                    scene.entities.push_back(fluid_ent);
                    fluid_entity_index = (int)scene.entities.size() - 1;
                }
            }

            using namespace ure::physics;
            for (size_t i = 0; i < scene.entities.size(); ++i) {
                const auto& entity = scene.entities[i];
                if (!entity.rigid_body.enabled) continue;
                auto rb = std::make_shared<RigidBody>();
                rb->set_mass(entity.rigid_body.mass);
                rb->position = core::Vec3f(entity.position.x, entity.position.y, entity.position.z);
                rb->orientation = core::Quat::from_euler_zyx(core::Vec3f(entity.rotation.x, entity.rotation.y, entity.rotation.z));
                rb->velocity = core::Vec3f(entity.rigid_body.velocity.x, entity.rigid_body.velocity.y, entity.rigid_body.velocity.z);
                rb->friction = entity.rigid_body.friction;
                rb->restitution = entity.rigid_body.restitution;
                rb->linear_damping = entity.rigid_body.linear_damping;
                rb->material_id = entity.rigid_body.material_id;
                if (entity.material) {
                    if (entity.material->type == MaterialType::Metal) rb->material_id = 1;
                    else if (entity.material->type == MaterialType::Dielectric) rb->material_id = 3;
                    else if (entity.material->type == MaterialType::Lambertian) rb->material_id = 2;
                }
                physics_world->add_body(rb);
                if (entity.rigid_body.collider_type == "box")
                    physics_world->add_collider(std::make_shared<BoxCollider>(rb.get(), core::Vec3f(entity.rigid_body.collider_size.x, entity.rigid_body.collider_size.y, entity.rigid_body.collider_size.z), core::Vec3f(0)));
                else if (entity.rigid_body.collider_type == "sphere")
                    physics_world->add_collider(std::make_shared<SphereCollider>(rb.get(), entity.rigid_body.collider_radius, core::Vec3f(0)));
                else if (entity.rigid_body.collider_type == "plane")
                    physics_world->add_collider(std::make_shared<PlaneCollider>(rb.get(), core::Vec3f(0,1,0), entity.position.y));

                acoustic::AcousticMaterial mat_props;
                int mid = entity.rigid_body.material_id;
                if (mid == 3) mat_props = {"Glass", 2500.0f, 70e9f, 0.2f, 0.003f, 0.05f, 0.05f, 0.9f};
                else if (mid == 1) mat_props = {"Metal", 7800.0f, 200e9f, 0.33f, 0.005f, 0.02f, 0.1f, 0.0f};
                else if (mid == 2) mat_props = {"Wood", 700.0f, 10e9f, 0.4f, 0.05f, 0.4f, 0.5f, 0.0f};
                else if (mid == 4) mat_props = {"Concrete", 2400.0f, 30e9f, 0.2f, 0.01f, 0.1f, 0.3f, 0.0f};
                else if (mid == 5) mat_props = {"Carpet", 100.0f, 0.1e9f, 0.4f, 0.5f, 0.8f, 0.6f, 0.0f};
                else mat_props = {"Generic", 1000.0f, 1e9f, 0.3f, 0.1f, 0.2f, 0.2f, 0.0f};
                acoustic_system->register_material(mid, mat_props);
                if (entity.rigid_body.mass > 0.0f) {
                    dynamic_bodies.push_back({(int)i, rb});
                    core::Vec3f dims = {1,1,1};
                    if (entity.rigid_body.collider_type == "box")
                        dims = {entity.rigid_body.collider_size.x, entity.rigid_body.collider_size.y, entity.rigid_body.collider_size.z};
                    else if (entity.rigid_body.collider_type == "sphere") {
                        float d = entity.rigid_body.collider_radius * 2.0f;
                        dims = {d, d, d};
                    }
                    acoustic_system->register_body(mid, mat_props, dims);
                }
            }
            if (physics_world) physics_world->register_listener(acoustic_system.get());
        }
    } else {
        if (scene_path != "procedural_demo")
            std::cerr << "[Main] Warning: File '" << scene_path << "' not found. Falling back to procedural demo.\n";
        else
            std::cout << "[Main] No file specified, using internal procedural fallback.\n";
        SceneBuilder builder;

        if (enable_physics) {
            std::cout << "[Main] Physics Demo Enabled. Preparing...\n";
            using namespace ure::physics;
            physics_world = std::make_shared<PhysicsWorld>();
            physics_world->register_listener(acoustic_system.get());
            acoustic_system->register_body(1, {"Metal", 7800.0f, 200e9f, 0.33f, 0.005f}, {1,1,1});
            acoustic_system->register_body(2, {"Wood", 700.0f, 10e9f, 0.4f, 0.05f, 0.2f, 0.5f, 0.0f}, {1,1,1});
            acoustic_system->register_body(3, {"Glass", 2500.0f, 70e9f, 0.24f, 0.002f, 0.05f, 0.1f, 0.8f}, {1,1,1});

            auto floor_body = std::make_shared<RigidBody>();
            floor_body->set_mass(0);
            floor_body->position = core::Vec3f(0, -1.05f, 0);
            floor_body->restitution = 0.5f;
            physics_world->add_body(floor_body);
            physics_world->add_collider(std::make_shared<PlaneCollider>(floor_body.get(), core::Vec3f(0,1,0), -1.05f));

            auto make_wall = [&](core::Vec3f pos, core::Vec3f half_ext) {
                auto body = std::make_shared<RigidBody>();
                body->set_mass(0);
                body->position = pos;
                physics_world->add_body(body);
                physics_world->add_collider(std::make_shared<BoxCollider>(body.get(), half_ext, core::Vec3f(0)));
            };
            make_wall({0, -1.05f, 0}, {1.1f, 0.05f, 1.1f});
            make_wall({-1.05f, 0, 0}, {0.05f, 1.0f, 1.1f});
            make_wall({1.05f, 0, 0}, {0.05f, 1.0f, 1.1f});
            make_wall({0, 0, 1.05f}, {1.0f, 1.0f, 0.05f});
            make_wall({0, 0, -1.05f}, {1.0f, 1.0f, 0.05f});

            sphere1_body = std::make_shared<RigidBody>();
            sphere1_body->set_mass(2.0f);
            sphere1_body->position = core::Vec3f(0.1f, 2.0f, 0.1f);
            sphere1_body->restitution = 0.6f;
            sphere1_body->friction = 0.2f;
            sphere1_body->linear_damping = 0.99f;
            sphere1_body->angular_damping = 0.01f;
            sphere1_body->material_id = 1;
            physics_world->add_body(sphere1_body);
            physics_world->add_collider(std::make_shared<SphereCollider>(sphere1_body.get(), 0.4f, core::Vec3f(0)));

            box_body = std::make_shared<RigidBody>();
            box_body->set_mass(5.0f);
            box_body->position = core::Vec3f(-0.5f, -0.5f, -0.5f);
            box_body->restitution = 0.2f;
            box_body->friction = 0.5f;
            box_body->linear_damping = 0.99f;
            box_body->angular_damping = 0.1f;
            box_body->material_id = 3;
            physics_world->add_body(box_body);
            physics_world->add_collider(std::make_shared<BoxCollider>(box_body.get(), core::Vec3f(0.3f,0.3f,0.3f), core::Vec3f(0)));

            sphere2_body = std::make_shared<RigidBody>();
            sphere2_body->set_mass(1.0f);
            sphere2_body->position = core::Vec3f(0.5f, 0.0f, 0.5f);
            sphere2_body->restitution = 0.8f;
            sphere2_body->friction = 0.1f;
            sphere2_body->material_id = 2;
            physics_world->add_body(sphere2_body);
            physics_world->add_collider(std::make_shared<SphereCollider>(sphere2_body.get(), 0.4f, core::Vec3f(0)));

            auto fluid_system = physics_world->get_fluid_system();
            if (fluid_system) {
                std::cout << "[Main] Initializing Fluid System (Cup)...\n";
                fluid_system->clear_particles();
                float spacing = 0.035f;
                fluid_system->configure_rest_state(spacing, {-5,-5,-5}, {5,5,5});
                fluid_system->target_density = 1000.0f;
                fluid_system->pressure_stiffness = 3000.0f;
                fluid_system->viscosity_coefficient = 0.002f;
                fluid_system->surface_tension_coefficient = 0.05f;
                fluid_system->enable_particle_shifting = false;
                fluid_system->enable_two_way_coupling = false;
                fluid_system->seed_box_volume({-0.9f,-0.9f,-0.9f}, {0.9f,0.0f,0.9f}, spacing, 0.1f, 2026u);
            }

            auto mesh_sphere = SceneBuilder::create_sphere(0.4f);
            auto mesh_cube = SceneBuilder::create_cube(1.0f);
            auto mesh_quad = SceneBuilder::create_quad();
            auto mat_floor = std::make_shared<Material>();
            mat_floor->albedo = {0.5f,0.5f,0.5f}; mat_floor->roughness = 0.8f;
            auto mat_glass = std::make_shared<Material>();
            mat_glass->type = MaterialType::Dielectric; mat_glass->ior = 1.5f; mat_glass->roughness = 0.0f; mat_glass->albedo = {1,1,1};
            auto mat_box = std::make_shared<Material>();
            mat_box->albedo = {0.8f,0.6f,0.2f}; mat_box->roughness = 0.6f;
            auto mat_sphere1 = std::make_shared<Material>();
            mat_sphere1->albedo = {0.8f,0.2f,0.2f}; mat_sphere1->roughness = 0.2f;
            auto mat_sphere2 = std::make_shared<Material>();
            mat_sphere2->albedo = {0.2f,0.2f,0.8f}; mat_sphere2->roughness = 0.2f;
            auto water_mat = std::make_shared<Material>();
            water_mat->type = MaterialType::Dielectric; water_mat->albedo = {0.6f,0.85f,0.98f}; water_mat->roughness = 0.02f; water_mat->ior = 1.33f;
            auto mat_light = std::make_shared<Material>();
            mat_light->type = MaterialType::Light; mat_light->emission = {20,20,20};

            builder.add_entity(mesh_quad, mat_floor, {0,-1.05f,0}, {20,1,20}, {});
            builder.add_entity(mesh_cube, mat_glass, {0,-1.05f,0}, {2.2f,0.1f,2.2f}, {});
            builder.add_entity(mesh_cube, mat_glass, {-1.05f,0,0}, {0.1f,2.0f,2.2f}, {});
            builder.add_entity(mesh_cube, mat_glass, {1.05f,0,0}, {0.1f,2.0f,2.2f}, {});
            builder.add_entity(mesh_cube, mat_glass, {0,0,1.05f}, {2.0f,2.0f,0.1f}, {});
            builder.add_entity(mesh_cube, mat_glass, {0,0,-1.05f}, {2.0f,2.0f,0.1f}, {});
            builder.add_entity(mesh_cube, mat_box, {box_body->position.x,box_body->position.y,box_body->position.z}, {0.6f,0.6f,0.6f}, {});
            builder.add_entity(mesh_sphere, mat_sphere1, {sphere1_body->position.x,sphere1_body->position.y,sphere1_body->position.z}, {1,1,1}, {});
            builder.add_entity(mesh_sphere, mat_sphere2, {sphere2_body->position.x,sphere2_body->position.y,sphere2_body->position.z}, {1,1,1}, {});
            auto mesh_fluid = std::make_shared<Mesh>();
            fluid_entity_index = builder.get_entity_count();
            builder.add_entity(mesh_fluid, water_mat, {}, {1,1,1}, {});
            builder.add_sphere({0,10,0}, 1.0f, mat_light);
            builder.set_camera({0,2,6}, {0,0,0}, 45.0f);
        } else {
            auto mesh_cube = SceneBuilder::create_cube(1.0f);
            auto mesh_sphere = SceneBuilder::create_sphere(1.0f);
            auto mesh_quad = SceneBuilder::create_quad();
            auto mat_red = std::make_shared<Material>();
            mat_red->albedo = {0.8f,0.1f,0.1f}; mat_red->roughness = 0.3f;
            auto mat_glass = std::make_shared<Material>();
            mat_glass->type = MaterialType::Dielectric; mat_glass->albedo = {1,1,1}; mat_glass->ior = 1.5f; mat_glass->roughness = 0.0f;
            auto mat_floor = std::make_shared<Material>();
            mat_floor->albedo = {0.8f,0.8f,0.8f}; mat_floor->roughness = 0.5f;
            auto mat_light = std::make_shared<Material>();
            mat_light->type = MaterialType::Light; mat_light->emission = {10,10,10};
            builder.add_entity(mesh_quad, mat_floor, {0,-1,0}, {20,20,1}, core::Quat::from_euler_zyx(0, -90, 0));
            for (int i = -3; i <= 3; ++i) {
                float x = i * 2.5f;
                builder.add_entity(mesh_cube, mat_red, {x,0,0}, {1,1,1}, core::Quat::from_euler_zyx(0, i*15.0f, 0));
            }
            builder.add_entity(mesh_sphere, mat_glass, {0,1.5f,3}, {1.5f,1.5f,1.5f}, {});
            builder.add_entity(mesh_sphere, mat_glass, {-3,1,3}, {1,1,1}, {});
            builder.add_entity(mesh_sphere, mat_glass, {3,1,3}, {1,1,1}, {});
            builder.add_sphere({0,8,0}, 1.0f, mat_light);
            builder.set_camera({0,5,15}, {0,0,0}, 35.0f);
        }
        scene = builder.build();
    }

    // 3. Initialize Engine
    std::cout << "[Main] Initializing GPU Engine...\n";
    auto engine = RenderEngineFactory::create_gpu_renderer();

    if (scene.physics.fluid.enabled && physics_world) {
        auto fluid_system = physics_world->get_fluid_system();
        std::cout << "[Main] Relaxing initial fluid particle distribution...\n";
        fluid_system->relax_initial_distribution(physics_world->get_colliders(), 8);
    }

    // Resolution priority: Scene > CLI > Default
    if (scene.width <= 0) scene.width = (cli_width > 0) ? cli_width : 1600;
    if (scene.height <= 0) scene.height = (cli_height > 0) ? cli_height : 900;
    int spp = (cli_spp > 0) ? cli_spp : ((scene.spp > 0) ? scene.spp : 100);

    // 4. Load Scene (first-time = full GPU upload)
    std::cout << "[Main] Loading Scene Data...\n";
    bool use_scene_ir = has_scene_ir && !enable_physics;
    if (use_scene_ir) {
        engine->load_scene_ir(scene_ir);
    } else {
        engine->load_scene(scene);
    }

    // 4b. Build World (ECS runtime bus) from Scene
    World world;
    WorldSceneBuilder::from_scene(scene, world);

    // Spatial Audio Listener
    {
        core::Vec3f cam_pos(scene.camera.position.x, scene.camera.position.y, scene.camera.position.z);
        core::Vec3f cam_look(scene.camera.look_at.x, scene.camera.look_at.y, scene.camera.look_at.z);
        core::Vec3f cam_up(scene.camera.up.x, scene.camera.up.y, scene.camera.up.z);
        core::Vec3f cam_forward(cam_look.x - cam_pos.x, cam_look.y - cam_pos.y, cam_look.z - cam_pos.z);
        acoustic_system->set_listener(cam_pos, cam_forward, cam_up);
        if (physics_world) acoustic_system->set_spatial_query(physics_world.get());
        std::cout << "[Main] Spatial Audio Listener set.\n";
    }

    // 5. Render Loop
    std::cout << "[Main] Starting: " << scene.width << "x" << scene.height << " @ " << spp << " SPP\n";

    std::filesystem::path output_dir;
    if (!output_dir_str.empty()) {
        output_dir = output_dir_str;
    } else if (enable_physics && std::filesystem::exists(scene_path)) {
        output_dir = std::filesystem::current_path() / "output" / std::filesystem::path(scene_path).stem().string();
    } else {
        output_dir = std::filesystem::current_path() / "output";
    }
    std::filesystem::create_directories(output_dir);

    std::string output_filename = output_filename_override;
    if (output_filename.empty()) {
        std::string base = std::filesystem::exists(scene_path) ? std::filesystem::path(scene_path).stem().string() : "output_procedural";
        output_filename = base + ".bmp";
    }
    std::string output_path = (output_dir / output_filename).string();

    engine->reset_accumulation();

    // Pre-allocate transform buffer (reused in physics loop)
    std::vector<gpu::GpuInstanceTransform> transforms;

    // ── Physics Render Loop ──
    if (enable_physics && physics_world) {
        float dt = (scene.physics.dt > 0) ? scene.physics.dt : (1.0f / 60.0f);
        int total_frames = (scene.physics.total_frames > 0) ? scene.physics.total_frames : 180;
        int spp_per_frame = (scene.physics.spp_per_frame > 0) ? scene.physics.spp_per_frame : 64;
        std::vector<float> audio_buffer;
        int sample_rate = 44100;

        // Determine if fluid is active (needs full scene reload each frame)
        bool has_fluid = (fluid_entity_index >= 0 && physics_world && physics_world->get_fluid_system());

        auto start_time = std::chrono::high_resolution_clock::now();
        for (int frame = 0; frame < total_frames; ++frame) {
            auto now = std::chrono::high_resolution_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
            float avg = (frame > 0) ? (float)elapsed / frame : 0;
            float eta = (avg > 0) ? (total_frames - frame) * avg : 0;
            std::cout << "[Progress] Frame " << frame+1 << "/" << total_frames
                      << " (" << std::fixed << std::setprecision(1) << (float)(frame+1)/total_frames*100 << "%)"
                      << " Elapsed: " << elapsed << "s ETA: " << (int)eta << "s\n";

            // 1. Step Physics
            physics_world->step(dt);

            // 2. Synthesize Audio
            std::vector<float> frame_audio = acoustic_system->generate_samples(dt, sample_rate);
            audio_buffer.insert(audio_buffer.end(), frame_audio.begin(), frame_audio.end());

            // 3. Update Fluid Visuals
            bool fluid_changed = false;
            auto fs = physics_world->get_fluid_system();
            if (fs) {
                core::Vec3f mc_min = fs->bounds_max, mc_max = fs->bounds_min;
                const auto& particles = fs->get_particles();
                if (!particles.empty()) {
                    mc_min = mc_max = particles[0].position;
                    for (const auto& p : particles) {
                        if (p.position.x < mc_min.x) mc_min.x = p.position.x;
                        if (p.position.y < mc_min.y) mc_min.y = p.position.y;
                        if (p.position.z < mc_min.z) mc_min.z = p.position.z;
                        if (p.position.x > mc_max.x) mc_max.x = p.position.x;
                        if (p.position.y > mc_max.y) mc_max.y = p.position.y;
                        if (p.position.z > mc_max.z) mc_max.z = p.position.z;
                    }
                    float pad = 0.2f;
                    mc_min = mc_min - core::Vec3f(pad, pad, pad);
                    mc_max = mc_max + core::Vec3f(pad, pad, pad);
                }
                float isolevel = fs->target_density * 0.5f;
                auto fluid_mesh = physics::MarchingCubes::generate(*fs, mc_min, mc_max, 128, isolevel);
                if (fluid_entity_index >= 0 && (size_t)fluid_entity_index < world.entity_count()) {
                    world.geometries[fluid_entity_index].mesh = fluid_mesh;
                }
                fluid_changed = true;
                std::cout << "  [Fluid] Triangles: " << fluid_mesh->indices.size()/3 << "\n";
            }

            // 4. Update entity transforms from physics
            for (const auto& db : dynamic_bodies) {
                if ((size_t)db.entity_index < world.entity_count()) {
                    world.transforms[db.entity_index].position = {db.body->position.x, db.body->position.y, db.body->position.z};
                    world.transforms[db.entity_index].rotation = db.body->orientation;
                }
            }
            if (dynamic_bodies.empty() && box_body && sphere1_body && sphere2_body && world.entity_count() > 8) {
                world.transforms[6].position = {box_body->position.x, box_body->position.y, box_body->position.z};
                world.transforms[6].rotation = box_body->orientation;
                world.transforms[7].position = {sphere1_body->position.x, sphere1_body->position.y, sphere1_body->position.z};
                world.transforms[7].rotation = sphere1_body->orientation;
                world.transforms[8].position = {sphere2_body->position.x, sphere2_body->position.y, sphere2_body->position.z};
                world.transforms[8].rotation = sphere2_body->orientation;
            }

            // 5. Upload to GPU
            if (has_fluid && fluid_changed) {
                // Fluid mesh changed: need full scene reload
                std::cout << "  [GPU] Full scene reload (fluid mesh changed)...\n";
                Scene updated_scene = WorldSceneBuilder::build_scene(world);
                engine->load_scene(updated_scene);
            } else {
                // Hot-update: only transforms
                WorldSceneBuilder::build_transforms(world, transforms);
                engine->update_transforms(transforms.data(), (int)transforms.size());
            }
            engine->reset_accumulation();

            // 6. Render
            std::cout << "  [GPU] Rendering " << spp_per_frame << " SPP: " << std::flush;
            for (int s = 0; s < spp_per_frame; ++s) {
                engine->render_pass();
                std::cout << "." << std::flush;
            }
            std::cout << " Done.\n";

            // 7. Save Frame
            std::stringstream ss;
            ss << "frame_" << std::setw(3) << std::setfill('0') << frame << ".bmp";
            save_frame(engine.get(), scene.width, scene.height, (output_dir / ss.str()).string());
            std::cout << "  [Output] Frame " << frame+1 << " saved.\n";
        }

        // Save Audio
        std::filesystem::path audio_path = output_dir / "physics_demo.wav";
        if (std::filesystem::exists(scene_path))
            audio_path = output_dir / (std::filesystem::path(scene_path).stem().string() + ".wav");
        ure::io::WavSaver::save(audio_path.string(), audio_buffer, sample_rate, 2);
        std::cout << "\n[Main] Physics Simulation Complete.\n";
        std::cout << "[Main] Frames: " << output_dir.string() << "\n";
        std::cout << "[Main] Audio: " << audio_path.string() << "\n";
        return 0;
    }

    // ── Standard Render Loop ──
    auto start_time = std::chrono::steady_clock::now();
    auto last_save_time = start_time;
    int current_spp = 0;
    while (current_spp < spp) {
        current_spp = engine->render_pass();
        if (current_spp % 10 == 0 || current_spp == spp)
            std::cout << "\r[Main] Progress: " << current_spp << "/" << spp << " SPP" << std::flush;
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_save_time).count() >= 1
            || current_spp == 1 || current_spp == 10 || current_spp == spp) {
            save_frame(engine.get(), scene.width, scene.height, output_path);
            last_save_time = now;
        }
    }
    std::cout << "\n[Main] Render Finished!\n";
    std::cout << "[Main] Output: " << output_path << "\n";
    return 0;
}
