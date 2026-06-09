#pragma once

#include "ure/ure_api.hpp"
#include "ure/gpu_driver.hpp"
#include "ure/gpu_scene_loader.hpp"
#include "ure/scene_ir.hpp"

namespace ure {

struct CompiledGpuScene {
    std::vector<ure::gpu::RenderMesh> meshes;
    std::vector<ure::gpu::GpuInstance> instances;
    std::vector<ure::gpu::GpuSphere> spheres;
    std::vector<ure::gpu::GpuMaterial> materials;
    std::vector<ure::gpu::HostTexture> textures;
    Camera camera;
    float medium_density = 0.0f;
    float medium_anisotropy = 0.0f;
    ure::gpu::GpuSpectrum medium_scattering = ure::gpu::GpuSpectrum(0.0f);
    ure::gpu::GpuSpectrum medium_absorption = ure::gpu::GpuSpectrum(0.0f);
    float medium_max_distance = 0.0f;
    int width = 1920;
    int height = 1080;
};

class GpuSceneCompiler {
public:
    static CompiledGpuScene compile_legacy(const Scene& scene);
    static CompiledGpuScene compile(const scene_ir::SceneIR& scene_ir);
};

} // namespace ure
