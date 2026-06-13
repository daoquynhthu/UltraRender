#pragma once

#include <vector>
#include <cuda_runtime.h>
#include "ure/gpu_structs.hpp"
#include "ure/render_config.hpp"

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4324)
#endif

namespace ure::gpu {

struct GpuContext {
    int width;
    int height;
    int current_spp;

    GpuVec3* d_output;
    GpuVec3* d_accum_buffer;
    GpuVec3* d_accum_sq_buffer;
    int* d_sample_counts;

    GpuVec3* d_normal_buffer;
    GpuVec3* d_albedo_buffer;
    float* d_depth_buffer;
    GpuVec2* d_uv_buffer;
    GpuVec2* d_motion_vector_buffer;

    RayQueue queueA, queueB;
    HitQueue hitQueue;
    ShadowQueue shadowQueue;

    GpuMaterial* d_materials;
    // Phase E: SoA spectral arrays (allocated alongside d_materials)
    float* d_mat_albedo;
    float* d_mat_metal_eta;
    float* d_mat_extinction;
    float* d_mat_medium_scattering;
    float* d_mat_medium_absorption;
    float* d_mat_emission;
    int num_spectral_channels;
    GpuSphere* d_spheres;
    GpuMesh* d_meshes;
    GpuInstance* d_instances;
    GpuInstanceDesc* d_instance_descs;
    GpuInstanceTransform* d_instance_transforms;
    GpuInstanceTransform* d_previous_instance_transforms;
    GpuTexture* d_textures;
    int* d_light_indices;

    int material_count;
    int sphere_count;
    int mesh_count;
    int instance_count;
    int texture_count;
    int light_count;

    GpuCamera camera;
    GpuCamera previous_camera;
    bool has_previous_camera;

    float medium_density;
    float medium_anisotropy;
    GpuSpectrum medium_scattering;
    GpuSpectrum medium_absorption;
    float medium_max_distance;

    ure::RenderConfig render_config;

    std::vector<void*> pointers_to_free;
    std::vector<cudaArray_t> arrays_to_free;
    std::vector<cudaTextureObject_t> tex_objs_to_free;
};

} // namespace ure::gpu

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
