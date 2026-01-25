#pragma once
#include "ure_api.hpp"
#include <cmath>
#include <numbers>
#include <memory>
#include <vector>

namespace ure {

class SceneBuilder {
public:
    SceneBuilder& add_entity(std::shared_ptr<Mesh> mesh, std::shared_ptr<Material> mat, 
                            Vec3 pos = {0,0,0}, Vec3 scale = {1,1,1}, Vec3 rot = {0,0,0});
    
    SceneBuilder& add_sphere(Vec3 center, float radius, std::shared_ptr<Material> mat);

    SceneBuilder& set_camera(Vec3 pos, Vec3 look_at, float fov);
    SceneBuilder& set_resolution(int width, int height);
    SceneBuilder& set_medium(float density, Vec3 scattering, Vec3 absorption, float max_dist, float anisotropy = 0.0f);
    
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
