#pragma once

#include "ure/ure_api.hpp"
#include "ure/detail/cuda_driver.cuh"
#include "ure/detail/cuda_scene_loader.cuh"
#include "ure/render_config.hpp"
#include "ure/scene_ir.hpp"

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4324)
#endif

namespace ure {

struct CompiledGpuScene {
    std::vector<ure::gpu::RenderMesh> meshes;
    std::vector<ure::gpu::GpuInstance> instances;
    std::vector<ure::gpu::GpuSphere> spheres;
    std::vector<ure::gpu::GpuMaterialData> materials;
    std::vector<ure::gpu::HostTexture> textures;
    Camera camera;
    float medium_density = 0.0f;
    float medium_anisotropy = 0.0f;
    scene_ir::VolumePhaseFunction medium_phase = scene_ir::VolumePhaseFunction::HenyeyGreenstein;
    int medium_phase_resource_index = -1;
    std::vector<scene_ir::MiePhaseResource> mie_phase_resources;
    ure::gpu::SpectralPacket medium_scattering = ure::gpu::SpectralPacket(0.0f);
    ure::gpu::SpectralPacket medium_absorption = ure::gpu::SpectralPacket(0.0f);
    float medium_max_distance = 0.0f;
    int width = 1920;
    int height = 1080;
};

class GpuSceneCompiler {
public:
    static CompiledGpuScene compile(const scene_ir::SceneIR& scene_ir);
    static CompiledGpuScene compile(const scene_ir::SceneIR& scene_ir, const RenderConfig& config);

    static void build_instance_transform(const core::Vec3f& position,
                                         const core::Vec3f& scale,
                                         const core::Quat& rotation,
                                         const std::shared_ptr<Mesh>& mesh,
                                         gpu::GpuInstanceTransform& out);
};

} // namespace ure

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
