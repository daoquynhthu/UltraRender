#pragma once

#include "ure/core/vector.hpp"
#include "ure/core/quaternion.hpp"
#include <vector>
#include <string>
#include <memory>
#include <array>

namespace ure {

namespace scene_ir {
struct SceneIR;
}

// Enums
enum class MaterialType {
    Lambertian,
    Metal,
    Dielectric,
    Light
};

// Data Structures
struct Vertex {
    core::Vec3f position;
    core::Vec3f normal;
    core::Vec2f uv;
    core::Vec3f tangent = {1.0f, 0.0f, 0.0f};
};

struct Texture {
    int width;
    int height;
    std::vector<float> data; // RGBA float or RGB float
};

struct Material {
    MaterialType type = MaterialType::Lambertian;
    core::Vec3f albedo = {0.8f, 0.8f, 0.8f};
    float roughness = 0.5f;
    float ior = 1.45f;
    float dispersion = 0.0f;
    core::Vec3f metal_eta = {0.0f, 0.0f, 0.0f};
    core::Vec3f extinction = {0.0f, 0.0f, 0.0f}; // For metals (Conductors)
    float thin_film_thickness = 0.0f;    // In nanometers
    float thin_film_ior = 1.0f;
    core::Vec3f emission = {0.0f, 0.0f, 0.0f};
    
    // Phase 3: Volume / SSS (Physical parameters)
    float medium_density = 0.0f;
    float medium_anisotropy = 0.0f; // g
    core::Vec3f medium_scattering = {0.0f, 0.0f, 0.0f};
    core::Vec3f medium_absorption = {0.0f, 0.0f, 0.0f};
    
    std::shared_ptr<Texture> albedo_texture;
};

// Physics Configuration
struct RigidBodyConfig {
    bool enabled = false;
    float mass = 0.0f; // 0 = static
    float friction = 0.5f;
    float restitution = 0.5f;
    float linear_damping = 0.01f;
    float angular_damping = 0.01f;
    core::Vec3f velocity = {0,0,0}; // Initial velocity
    std::string collider_type = "none"; // box, sphere, plane, mesh
    core::Vec3f collider_size = {1,1,1}; // For box
    float collider_radius = 0.5f; // For sphere
    int material_id = 0; // 0=Default, 1=Metal, 2=Wood, 3=Glass
};

struct FluidConfig {
    bool enabled = false;
    core::Vec3f bounds_min = {-5,-5,-5};
    core::Vec3f bounds_max = {5,5,5};
    float particle_spacing = 0.1f;
    // Simple fill volume for demo
    core::Vec3f fill_min = {-1,-1,-1};
    core::Vec3f fill_max = {1,0,1};
};

struct PhysicsConfig {
    bool enabled = false;
    float dt = 1.0f / 60.0f;
    int total_frames = 180;
    int spp_per_frame = 32;
    FluidConfig fluid;
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<int> indices;
};

struct RenderEntity {
    std::shared_ptr<Mesh> mesh;
    std::shared_ptr<Material> material;
    core::Vec3f position = {0,0,0};
    core::Vec3f scale = {1,1,1};
    core::Quat rotation = {}; // Quaternion
    RigidBodyConfig rigid_body; // Physics properties
};

struct Camera {
    core::Vec3f position = {0, 0, 10};
    core::Vec3f look_at = {0, 0, 0};
    core::Vec3f up = {0, 1, 0};
    float fov = 45.0f;
    float aspect_ratio = 16.0f / 9.0f;
    float aperture = 0.0f;
    float focus_dist = 10.0f;
};

struct SphereEntity {
    core::Vec3f center;
    float radius;
    std::shared_ptr<Material> material;
};

struct Scene {
    std::vector<RenderEntity> entities;
    std::vector<SphereEntity> spheres;
    Camera camera;
    PhysicsConfig physics; // Physics Settings
    core::Vec3f background_color = {0,0,0};
    float medium_density = 0.0f;
    float medium_anisotropy = 0.0f; // g
    core::Vec3f medium_scattering = {0.0f, 0.0f, 0.0f};
    core::Vec3f medium_absorption = {0.0f, 0.0f, 0.0f};
    float medium_max_distance = 50.0f;
    int width = 0;  // 0 means use default or CLI override
    int height = 0;
    int spp = 0; // 0 means use default or CLI override
};

struct RenderSettings {
    int width = 1920;
    int height = 1080;
    int spp = 100;
    std::string output_path;
};

} // namespace ure
