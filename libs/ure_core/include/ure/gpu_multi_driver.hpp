#pragma once

#include <cstddef>
#include <vector>
#include "ure/gpu_driver.hpp"
#include "ure/render_config.hpp"

namespace ure::gpu {

struct MultiGpuContext;

MultiGpuContext* init_multi_gpu_renderer(int width, int height,
                                         const std::vector<RenderMesh>& meshes,
                                         const std::vector<GpuInstance>& instances,
                                         const std::vector<GpuSphere>& spheres,
                                         const std::vector<GpuMaterialData>& materials,
                                         const std::vector<HostTexture>& textures = {},
                                         const ure::RenderConfig& config = ure::RenderConfig{},
                                         const std::vector<scene_ir::MiePhaseResource>& mie_phase_resources = {});

void free_multi_gpu_renderer(MultiGpuContext* ctx);

int render_pass_multi_gpu(MultiGpuContext* ctx, int samples_per_pass);

void copy_frame_buffer_multi_gpu(MultiGpuContext* ctx, float* host_buffer);

} // namespace ure::gpu
