#pragma once

#include <vector>
#include <string>
#include <memory>
#include <array>

namespace ure {

namespace scene_ir {
struct SceneIR;
}

// Basic Math Types
struct Vec3 { 
    float x, y, z; 
    Vec3(float _x=0, float _y=0, float _z=0) : x(_x), y(_y), z(_z) {}
};
struct Vec2 { 
    float u, v; 
    Vec2(float _u=0, float _v=0) : u(_u), v(_v) {}
};

// Enums
enum class MaterialType {
    Lambertian,
    Metal,
    Dielectric,
    Light
};

// Data Structures
struct Vertex {
    Vec3 position;
    Vec3 normal;
    Vec2 uv;
};

struct Texture {
    int width;
    int height;
    std::vector<float> data; // RGBA float or RGB float
};

struct Material {
    MaterialType type = MaterialType::Lambertian;
    Vec3 albedo = {0.8f, 0.8f, 0.8f};
    float roughness = 0.5f;
    float ior = 1.45f;
    float dispersion = 0.0f;
    Vec3 metal_eta = {0.0f, 0.0f, 0.0f};
    Vec3 extinction = {0.0f, 0.0f, 0.0f}; // For metals (Conductors)
    float thin_film_thickness = 0.0f;    // In nanometers
    float thin_film_ior = 1.0f;
    Vec3 emission = {0.0f, 0.0f, 0.0f};
    
    // Phase 3: Volume / SSS (Physical parameters)
    float medium_density = 0.0f;
    float medium_anisotropy = 0.0f; // g
    Vec3 medium_scattering = {0.0f, 0.0f, 0.0f};
    Vec3 medium_absorption = {0.0f, 0.0f, 0.0f};
    
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
    Vec3 velocity = {0,0,0}; // Initial velocity
    std::string collider_type = "none"; // box, sphere, plane, mesh
    Vec3 collider_size = {1,1,1}; // For box
    float collider_radius = 0.5f; // For sphere
    int material_id = 0; // 0=Default, 1=Metal, 2=Wood, 3=Glass
};

struct FluidConfig {
    bool enabled = false;
    Vec3 bounds_min = {-5,-5,-5};
    Vec3 bounds_max = {5,5,5};
    float particle_spacing = 0.1f;
    // Simple fill volume for demo
    Vec3 fill_min = {-1,-1,-1};
    Vec3 fill_max = {1,0,1};
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
    Vec3 position = {0,0,0};
    Vec3 scale = {1,1,1};
    Vec3 rotation = {0,0,0}; // Euler angles in degrees
    RigidBodyConfig rigid_body; // Physics properties
};

struct Camera {
    Vec3 position = {0, 0, 10};
    Vec3 look_at = {0, 0, 0};
    Vec3 up = {0, 1, 0};
    float fov = 45.0f;
    float aspect_ratio = 16.0f / 9.0f;
    float aperture = 0.0f;
    float focus_dist = 10.0f;
};

struct SphereEntity {
    Vec3 center;
    float radius;
    std::shared_ptr<Material> material;
};

struct Scene {
    std::vector<RenderEntity> entities;
    std::vector<SphereEntity> spheres;
    Camera camera;
    PhysicsConfig physics; // Physics Settings
    Vec3 background_color = {0,0,0};
    float medium_density = 0.0f;
    float medium_anisotropy = 0.0f; // g
    Vec3 medium_scattering = {0.0f, 0.0f, 0.0f};
    Vec3 medium_absorption = {0.0f, 0.0f, 0.0f};
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

// Interface
class IRenderEngine {
public:
    virtual ~IRenderEngine() = default;
    
    // Load scene data into internal engine representation (e.g. upload to GPU)
    virtual void load_scene(const Scene& scene) = 0;
    virtual void load_scene_ir(const scene_ir::SceneIR& scene_ir) = 0;
    
    // Execute rendering
    // Legacy blocking render, should be implemented using render_pass loop
    virtual void render(const RenderSettings& settings) = 0;
    
    // Interactive API
    
    // Render one pass (or a batch of samples) and accumulate to the frame buffer.
    // Returns the current accumulated sample count (SPP).
    virtual int render_pass() = 0;
    
    // Reset accumulation buffer (clear to black, reset SPP to 0).
    // Should be called when scene/camera changes.
    virtual void reset_accumulation() = 0;
    
    // Update camera parameters without reloading the entire scene.
    // Automatically triggers reset_accumulation().
    virtual void update_camera(const Camera& camera) = 0;
    
    // Get current sample count
    virtual int get_current_spp() const = 0;
    
    // Get raw frame buffer (Linear RGB float)
    virtual const std::vector<float>& get_frame_buffer() const = 0;
};

// Factory
class RenderEngineFactory {
public:
    static std::unique_ptr<IRenderEngine> create_gpu_engine();
    static std::unique_ptr<IRenderEngine> create_cpu_engine();
};

} // namespace ure
