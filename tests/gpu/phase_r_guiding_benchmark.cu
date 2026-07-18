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
    } else if (name == "occlusion") {
        add_sphere(spheres, GpuVec3(0.0f, 0.0f, 0.0f), 1.0f, 7);
        add_sphere(spheres, GpuVec3(-1.25f, 0.35f, 1.2f), 0.7f, 7);
        add_sphere(spheres, GpuVec3(1.25f, 0.15f, -0.9f), 0.9f, 7);
        add_light(spheres, materials, GpuVec3(-2.8f, 3.8f, 1.5f), 0.3f, 18.0f);
        add_light(spheres, materials, GpuVec3(2.8f, 3.4f, -1.5f), 0.4f, 9.0f);
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
    } else if (name == "glass_caustic") {
        materials.push_back(make_material(MaterialType::Dielectric, 1.0f, 0.0f));
        add_sphere(spheres, GpuVec3(0.0f, -3.0f, 0.0f), 1.0f, 7);
        add_sphere(spheres, GpuVec3(0.0f, 0.0f, 0.0f), 1.0f, 8);
        add_light(spheres, materials, GpuVec3(0.3f, 0.3f, 0.0f), 0.05f, 320.0f);
    } else if (name == "sds") {
        materials.push_back(make_material(MaterialType::Metal, 0.92f, 0.0f));
        add_sphere(spheres, GpuVec3(2.7071068f, 1.7071068f, 0.0f), 1.0f, 7);
        add_sphere(spheres, GpuVec3(0.0f, 0.0f, 0.0f), 1.0f, 8);
        add_light(spheres, materials, GpuVec3(2.0707107f, -1.0707107f, 0.0f), 0.1f, 80.0f);
    } else if (name == "small_emitter") {
        materials.push_back(make_material(MaterialType::Metal, 0.95f, 0.0f));
        add_sphere(spheres, GpuVec3(2.7071068f, 1.7071068f, 0.0f), 1.0f, 7);
        add_sphere(spheres, GpuVec3(0.0f, 0.0f, 0.0f), 1.0f, 8);
        add_light(spheres, materials, GpuVec3(2.0141421f, -1.0141421f, 0.0f), 0.02f, 2000.0f);
    } else if (name == "mixed_specular") {
        materials.push_back(make_material(MaterialType::Metal, 0.92f, 0.0f));
        materials.push_back(make_material(MaterialType::Metal, 0.78f, 0.24f));
        add_sphere(spheres, GpuVec3(2.7071068f, 1.7071068f, 0.0f), 1.0f, 7);
        add_sphere(spheres, GpuVec3(0.0f, 0.0f, 0.0f), 1.0f, 8);
        add_sphere(spheres, GpuVec3(-1.7f, 0.2f, -0.4f), 0.65f, 9);
        add_light(spheres, materials, GpuVec3(2.0707107f, -1.0707107f, 0.0f), 0.1f, 80.0f);
    } else {
        throw std::runtime_error("unknown scene preset: " + name);
    }
}

int main(int argc, char** argv) {
    if (argc != 7) return 2;
    const std::string scene_name = argv[1];
    const int mode = std::stoi(argv[2]);
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
    config.path_guiding.enabled = mode == 1;
    config.path_guiding.spatial_cell_count = 32;
    config.path_guiding.directional_bin_count = 16;
    config.path_guiding.memory_budget_mb = 64;
    if (mode == 2) {
        config.integrator.mode = ure::IntegratorMode::RestirPT;
        config.restir_pt.enabled = true;
        config.restir_pt.temporal_reuse = true;
        config.restir_pt.spatial_reuse = true;
        config.restir_pt.max_reuse_depth = 3;
        config.restir_pt.candidate_count = 3;
        config.restir_pt.max_history = 8;
        config.restir_pt.position_threshold = 2.0f;
        config.restir_pt.normal_threshold = 0.25f;
    } else if (mode == 3) {
        config.integrator.mode = ure::IntegratorMode::SpecularManifold;
        config.bidirectional.max_camera_vertices = 8;
        config.bidirectional.max_light_vertices = 8;
        config.bidirectional.connections_per_pixel = 64;
        config.bidirectional.memory_budget_mb = 256;
        config.specular_manifold.enabled = true;
        config.specular_manifold.max_specular_events = 4;
        config.specular_manifold.solver_tolerance = 1e-5f;
        config.specular_manifold.max_newton_iterations = 48;
    }

    std::vector<GpuSphere> spheres;
    std::vector<GpuMaterialData> materials;
    bool volume = false;
    build_scene(scene_name, spheres, materials, volume);
    GpuContext* context = init_gpu_renderer(width, height, {}, {}, spheres, materials, {}, config);
    try {
        float camera_position[3] = {0.0f, 1.5f, 8.0f};
        float camera_target[3] = {0.0f, 0.3f, 0.0f};
        if (scene_name == "glass_caustic") {
            camera_position[0] = 3.0f;
            camera_position[1] = 0.0f;
            camera_position[2] = 3.0f;
            camera_target[0] = 0.0f;
            camera_target[1] = -2.0f;
            camera_target[2] = 0.0f;
        } else if (scene_name == "sds" || scene_name == "small_emitter" ||
                   scene_name == "mixed_specular") {
            camera_position[0] = 0.0f;
            camera_position[1] = -1.0f;
            camera_position[2] = 3.0f;
            camera_target[0] = 2.0f;
            camera_target[1] = 1.0f;
            camera_target[2] = 0.0f;
        }
        update_camera_gpu(context, camera_position, camera_target, 42.0f);
        if (volume) {
            update_medium_gpu(context, 0.08f, 0.35f, SpectralPacket(0.35f), SpectralPacket(0.015f), 20.0f);
        }
        GpuManifoldTelemetry manifold_total = {};
        double manifold_rgb_sum = 0.0;
        std::vector<GpuVec3> manifold_sample(
            static_cast<size_t>(width) * height);
        std::vector<double> manifold_image(
            static_cast<size_t>(width) * height * 3, 0.0);
        std::vector<GpuVec3> specular_emitter_sample(
            static_cast<size_t>(width) * height);
        std::vector<double> specular_emitter_image(
            static_cast<size_t>(width) * height * 3, 0.0);
        for (int i = 0; i < spp; ++i) {
            render_pass_gpu(context, 1);
            const cudaError_t reference_copy_status = cudaMemcpy(
                specular_emitter_sample.data(),
                context->d_specular_emitter_accum,
                specular_emitter_sample.size() * sizeof(GpuVec3),
                cudaMemcpyDeviceToHost);
            if (reference_copy_status != cudaSuccess) {
                throw std::runtime_error(
                    "failed to copy specular-emitter benchmark AOV");
            }
            for (size_t pixel = 0; pixel < specular_emitter_sample.size();
                 ++pixel) {
                const GpuVec3 value = specular_emitter_sample[pixel];
                specular_emitter_image[pixel * 3] += value.x;
                specular_emitter_image[pixel * 3 + 1] += value.y;
                specular_emitter_image[pixel * 3 + 2] += value.z;
            }
            if (mode == 3) {
                const cudaError_t copy_status = cudaMemcpy(
                    manifold_sample.data(), context->d_manifold_accum,
                    manifold_sample.size() * sizeof(GpuVec3),
                    cudaMemcpyDeviceToHost);
                if (copy_status != cudaSuccess) {
                    throw std::runtime_error(
                        "failed to copy manifold benchmark accumulation");
                }
                for (size_t pixel = 0; pixel < manifold_sample.size();
                     ++pixel) {
                    const GpuVec3 value = manifold_sample[pixel];
                    manifold_rgb_sum += std::abs(value.x) +
                        std::abs(value.y) + std::abs(value.z);
                    manifold_image[pixel * 3] += value.x;
                    manifold_image[pixel * 3 + 1] += value.y;
                    manifold_image[pixel * 3 + 2] += value.z;
                }
            }
            const GpuManifoldTelemetry current =
                context->last_manifold_telemetry;
            manifold_total.attempted += current.attempted;
            manifold_total.converged += current.converged;
            manifold_total.root_matches += current.root_matches;
            manifold_total.total_root_trials += current.total_root_trials;
            manifold_total.rejected_material += current.rejected_material;
            manifold_total.rejected_primitive += current.rejected_primitive;
            manifold_total.rejected_singular += current.rejected_singular;
            manifold_total.rejected_tir += current.rejected_tir;
            manifold_total.rejected_residual += current.rejected_residual;
            manifold_total.rejected_differential +=
                current.rejected_differential;
            manifold_total.rejected_non_delta += current.rejected_non_delta;
            manifold_total.rejected_occluded += current.rejected_occluded;
            manifold_total.rejected_response += current.rejected_response;
        }
        std::vector<float> framebuffer(static_cast<size_t>(width) * height * 3);
        copy_frame_buffer_gpu(context, framebuffer.data());
        std::ofstream output(output_path, std::ios::binary);
        const std::int32_t dimensions[2] = {width, height};
        output.write(reinterpret_cast<const char*>(dimensions), sizeof(dimensions));
        output.write(reinterpret_cast<const char*>(framebuffer.data()),
                     static_cast<std::streamsize>(framebuffer.size() * sizeof(float)));
        if (!output) throw std::runtime_error("failed to write benchmark output");
        std::ofstream manifold_output(output_path + ".manifold", std::ios::binary);
        manifold_output.write(
            reinterpret_cast<const char*>(dimensions), sizeof(dimensions));
        std::vector<float> normalized_manifold(manifold_image.size());
        for (size_t index = 0; index < manifold_image.size(); ++index) {
            normalized_manifold[index] = static_cast<float>(
                manifold_image[index] / double(spp));
        }
        manifold_output.write(
            reinterpret_cast<const char*>(normalized_manifold.data()),
            static_cast<std::streamsize>(
                normalized_manifold.size() * sizeof(float)));
        if (!manifold_output) {
            throw std::runtime_error("failed to write manifold benchmark AOV");
        }
        std::ofstream specular_emitter_output(
            output_path + ".specular_emitter", std::ios::binary);
        specular_emitter_output.write(
            reinterpret_cast<const char*>(dimensions), sizeof(dimensions));
        std::vector<float> normalized_specular_emitter(
            specular_emitter_image.size());
        for (size_t index = 0; index < specular_emitter_image.size(); ++index) {
            normalized_specular_emitter[index] = static_cast<float>(
                specular_emitter_image[index] / double(spp));
        }
        specular_emitter_output.write(
            reinterpret_cast<const char*>(normalized_specular_emitter.data()),
            static_cast<std::streamsize>(
                normalized_specular_emitter.size() * sizeof(float)));
        if (!specular_emitter_output) {
            throw std::runtime_error(
                "failed to write specular-emitter benchmark AOV");
        }
        std::ofstream telemetry(output_path + ".telemetry");
        telemetry << "surface_suffixes=" << context->last_restir_pt_telemetry.surface_suffixes << '\n'
                  << "volume_suffixes=" << context->last_restir_pt_telemetry.volume_suffixes << '\n'
                  << "temporal_candidates=" << context->last_restir_pt_telemetry.temporal_candidates << '\n'
                  << "spatial_candidates=" << context->last_restir_pt_telemetry.spatial_candidates << '\n'
                  << "accepted_reconnections=" << context->last_restir_pt_telemetry.accepted_reconnections << '\n'
                  << "rejected_stale=" << context->last_restir_pt_telemetry.rejected_stale << '\n'
                  << "rejected_geometry=" << context->last_restir_pt_telemetry.rejected_geometry << '\n'
                  << "rejected_specular=" << context->last_restir_pt_telemetry.rejected_specular << '\n'
                  << "rejected_volume=" << context->last_restir_pt_telemetry.rejected_volume << '\n'
                  << "manifold_attempted=" << manifold_total.attempted << '\n'
                  << "manifold_converged=" << manifold_total.converged << '\n'
                  << "manifold_root_matches=" << manifold_total.root_matches << '\n'
                  << "manifold_root_trials=" << manifold_total.total_root_trials << '\n'
                  << "manifold_rejected_material=" << manifold_total.rejected_material << '\n'
                  << "manifold_rejected_primitive=" << manifold_total.rejected_primitive << '\n'
                  << "manifold_rejected_singular=" << manifold_total.rejected_singular << '\n'
                  << "manifold_rejected_tir=" << manifold_total.rejected_tir << '\n'
                  << "manifold_rejected_residual=" << manifold_total.rejected_residual << '\n'
                  << "manifold_rejected_differential=" << manifold_total.rejected_differential << '\n'
                  << "manifold_rejected_non_delta=" << manifold_total.rejected_non_delta << '\n'
                  << "manifold_rejected_occluded=" << manifold_total.rejected_occluded << '\n'
                  << "manifold_rejected_response=" << manifold_total.rejected_response << '\n'
                  << "manifold_rgb_sum=" << manifold_rgb_sum << '\n';
        if (!telemetry) throw std::runtime_error("failed to write benchmark telemetry");
    } catch (...) {
        free_gpu_renderer(context);
        throw;
    }
    free_gpu_renderer(context);
    return 0;
}
