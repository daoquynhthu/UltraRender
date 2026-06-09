#pragma once
#include "ure/ure_api.hpp"
#include <cmath>
#include <numbers>
#include <memory>
#include <vector>

namespace ure {

class SceneBuilder {
public:
    SceneBuilder& add_entity(std::shared_ptr<Mesh> mesh, std::shared_ptr<Material> mat, 
                            core::Vec3f pos = {0,0,0}, core::Vec3f scale = {1,1,1}, core::Quat rot = {},
                            RigidBodyConfig rb_config = {});
    
    SceneBuilder& add_sphere(core::Vec3f center, float radius, std::shared_ptr<Material> mat, RigidBodyConfig rb_config = {});

    SceneBuilder& set_camera(core::Vec3f pos, core::Vec3f look_at, float fov);
    SceneBuilder& set_resolution(int width, int height);
    SceneBuilder& set_spp(int spp);
    SceneBuilder& set_medium(float density, core::Vec3f scattering, core::Vec3f absorption, float max_dist, float anisotropy = 0.0f);
    
    // Physics
    SceneBuilder& set_physics_enabled(bool enabled, float dt = 1.0f/60.0f, int frames = 180, int spp_per_frame = 32);
    SceneBuilder& set_fluid_config(const FluidConfig& config);

    // Helper
    int get_entity_count() const;

    // Primitive Generators
    static std::shared_ptr<Mesh> create_quad();
    static std::shared_ptr<Mesh> create_cube(float size = 1.0f);
    static std::shared_ptr<Mesh> create_sphere(float radius = 0.5f, int slices = 32, int stacks = 16);
    static std::shared_ptr<Mesh> create_cylinder(float radius = 0.5f, float height = 1.0f, int segments = 32);
    static std::shared_ptr<Mesh> create_cup(float radius = 0.5f, float height = 1.0f, float thickness = 0.05f, int segments = 32);
    static std::shared_ptr<Mesh> create_torus(float major_radius = 0.8f, float minor_radius = 0.1f, int major_segments = 48, int minor_segments = 24);

    Scene build();

private:
    Scene scene_;
};

}
