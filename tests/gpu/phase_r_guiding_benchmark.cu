#include <cuda_runtime.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "ure/gpu_context.hpp"
#include "ure/gpu_driver.hpp"

using namespace ure::gpu;

static GpuMaterialData make_material(MaterialType type, float value, float roughness = 0.5f) {
    GpuMaterialData material = {};
    material.header.type = type;
    material.header.roughness = roughness;
    material.header.ior = type == MaterialType::Dielectric ? 1.5f : 1.0f;
    material.albedo = SpectralPacket(value);
    material.metal_eta = SpectralPacket(type == MaterialType::Metal ? 0.25f : 1.0f);
    material.extinction = SpectralPacket(type == MaterialType::Metal ? 3.0f : 0.0f);
    return material;
}

static void add_sphere(std::vector<GpuSphere>& spheres, const GpuVec3& center, float radius, int material) {
    GpuSphere sphere = {};
    sphere.center = center;
    sphere.radius = radius;
    sphere.material_index = material;
    spheres.push_back(sphere);
}

static void add_light(std::vector<GpuSphere>& spheres,
                      std::vector<GpuMaterialData>& materials,
                      const GpuVec3& center,
                      float radius,
                      float emission) {
    GpuMaterialData light = make_material(MaterialType::Light, 0.0f);
    light.emission = SpectralPacket(emission);
    const int material_index = 7 + static_cast<int>(materials.size());
    materials.push_back(light);
    add_sphere(spheres, center, radius, material_index);
}

static void build_scene(const std::string& name,
                        std::vector<GpuSphere>& spheres,
                        std::vector<GpuMaterialData>& materials,
                        bool& volume) {
    materials.push_back(make_material(MaterialType::Lambertian, 0.72f, 0.9f));
    add_sphere(spheres, GpuVec3(0.0f, -1001.0f, 0.0f), 1000.0f, 7);
    volume = false;
    if (name == "cornell") {
        materials.push_back(make_material(MaterialType::Lambertian, 0.55f, 0.8f));
        add_sphere(spheres, GpuVec3(0.0f, 0.0f, -1005.0f), 1000.0f, 8);
        add_sphere(spheres, GpuVec3(-1.1f, 0.0f, 0.0f), 1.0f, 7);
        add_sphere(spheres, GpuVec3(1.2f, -0.25f, -0.6f), 0.75f, 8);
        add_light(spheres, materials, GpuVec3(-2.0f, 4.0f, 1.0f), 0.35f, 10.0f);
        add_light(spheres, materials, GpuVec3(2.0f, 4.5f, -1.0f), 0.45f, 5.0f);
        add_light(spheres, materials, GpuVec3(0.0f, 5.0f, -3.0f), 0.30f, 16.0f);
    } else if (name == "multi_light") {
        add_sphere(spheres, GpuVec3(0.0f, 0.0f, 0.0f), 1.0f, 7);
        for (int i = 0; i < 8; ++i) {
            const float x = -3.5f + static_cast<float>(i);
            add_light(spheres, materials, GpuVec3(x, 2.0f + 0.3f * (i % 3), -1.0f), 0.22f,
                      i == 6 ? 24.0f : 1.5f + static_cast<float>(i));
        }
    } else if (name == "complex_material") {
        materials.push_back(make_material(MaterialType::Metal, 0.9f, 0.08f));
        materials.push_back(make_material(MaterialType::Dielectric, 1.0f, 0.03f));
        materials.push_back(make_material(MaterialType::Cloth, 0.65f, 0.7f));
        add_sphere(spheres, GpuVec3(-1.7f, 0.0f, 0.0f), 1.0f, 8);
        add_sphere(spheres, GpuVec3(0.0f, 0.0f, -0.5f), 1.0f, 9);
        add_sphere(spheres, GpuVec3(1.7f, 0.0f, 0.0f), 1.0f, 10);
        add_light(spheres, materials, GpuVec3(-3.0f, 4.0f, 2.0f), 0.4f, 14.0f);
        add_light(spheres, materials, GpuVec3(3.0f, 3.0f, 0.0f), 0.3f, 7.0f);
        add_light(spheres, materials, GpuVec3(0.0f, 5.0f, -3.0f), 0.5f, 4.0f);
    } else if (name == "volume") {
        volume = true;
        add_sphere(spheres, GpuVec3(0.0f, 0.0f, 0.0f), 1.0f, 7);
        add_light(spheres, materials, GpuVec3(-2.5f, 3.5f, 0.5f), 0.35f, 12.0f);
        add_light(spheres, materials, GpuVec3(2.5f, 3.0f, -1.0f), 0.35f, 6.0f);
        add_light(spheres, materials, GpuVec3(0.0f, 4.5f, -3.0f), 0.45f, 18.0f);
    } else {
        throw std::runtime_error("unknown scene preset: " + name);
    }
}

int main(int argc, char** argv) {
    if (argc != 7) return 2;
    const std::string scene_name = argv[1];
    const bool guided = std::stoi(argv[2]) != 0;
    const int width = std::stoi(argv[3]);
    const int height = std::stoi(argv[4]);
    const int spp = std::stoi(argv[5]);
    const std::string output_path = argv[6];
    if (width <= 0 || height <= 0 || spp <= 0) return 3;

    ure::RenderConfig config;
    config.num_wavelengths = 8;
    config.spectral_packet_lanes = 8;
    config.queue_capacity = std::max(width * height, 64);
    config.max_trace_depth = 8;
    config.path_guiding.enabled = guided;
    config.path_guiding.spatial_cell_count = 32;
    config.path_guiding.directional_bin_count = 16;
    config.path_guiding.memory_budget_mb = 64;

    std::vector<GpuSphere> spheres;
    std::vector<GpuMaterialData> materials;
    bool volume = false;
    build_scene(scene_name, spheres, materials, volume);
    GpuContext* context = init_gpu_renderer(width, height, {}, {}, spheres, materials, {}, config);
    try {
        const float camera_position[3] = {0.0f, 1.5f, 8.0f};
        const float camera_target[3] = {0.0f, 0.3f, 0.0f};
        update_camera_gpu(context, camera_position, camera_target, 42.0f);
        if (volume) {
            update_medium_gpu(context, 0.08f, 0.35f, SpectralPacket(0.35f), SpectralPacket(0.015f), 20.0f);
        }
        for (int i = 0; i < spp; ++i) render_pass_gpu(context, 1);
        std::vector<float> framebuffer(static_cast<size_t>(width) * height * 3);
        copy_frame_buffer_gpu(context, framebuffer.data());
        std::ofstream output(output_path, std::ios::binary);
        const std::int32_t dimensions[2] = {width, height};
        output.write(reinterpret_cast<const char*>(dimensions), sizeof(dimensions));
        output.write(reinterpret_cast<const char*>(framebuffer.data()),
                     static_cast<std::streamsize>(framebuffer.size() * sizeof(float)));
        if (!output) throw std::runtime_error("failed to write benchmark output");
    } catch (...) {
        free_gpu_renderer(context);
        throw;
    }
    free_gpu_renderer(context);
    return 0;
}
