#include "ure/gpu_multi_driver.hpp"
#include "ure/mlt.hpp"
#include "ure/gpu_structs.hpp"
#include "ure/gpu_context.hpp"
#include <cuda_runtime.h>
#include <ure/log.hpp>
#include <ure/check_cuda.hpp>
#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace ure::gpu {

__global__ void merge_accum_kernel(GpuVec3* dst, const GpuVec3* src, int count) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < count) {
        dst[idx] = dst[idx] + src[idx];
    }
}

__global__ void merge_counts_kernel(int* dst, const int* src, int count) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < count) {
        dst[idx] += src[idx];
    }
}

__global__ void merge_path_guiding_delta_kernel(float* dst,
                                                 const float* src,
                                                 const float* baseline,
                                                 size_t count,
                                                 float baseline_factor) {
    const size_t idx = static_cast<size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (idx < count) dst[idx] += src[idx] - baseline_factor * baseline[idx];
}

static void allocate_path_guiding_merge_buffers(MultiGpuContext* ctx) {
    if (ctx->num_gpus <= 1) return;
    GpuContext* first = ctx->contexts[0];
    if (!first->d_path_guiding_light_weights) return;
    if (first->path_guiding_required_bytes > std::numeric_limits<size_t>::max() / 3 ||
        first->path_guiding_required_bytes * 3 > first->path_guiding_budget_bytes) {
        throw std::runtime_error(
            "Multi-GPU path guiding merge requires three resident guide-cache footprints on device 0");
    }
    ctx->path_guiding_light_count = static_cast<size_t>(first->light_count);
    ctx->path_guiding_spatial_count =
        ctx->path_guiding_light_count *
        static_cast<size_t>(first->path_guiding_spatial_cell_count) *
        static_cast<size_t>(first->path_guiding_directional_bin_count);
    const size_t light_bytes = ctx->path_guiding_light_count * sizeof(float);
    const size_t spatial_bytes = ctx->path_guiding_spatial_count * sizeof(float);
    UR_CUDA_CHECK(cudaMalloc(&ctx->d_path_guiding_baseline_light, light_bytes));
    UR_CUDA_CHECK(cudaMalloc(&ctx->d_path_guiding_baseline_spatial, spatial_bytes));
    UR_CUDA_CHECK(cudaMalloc(&ctx->d_path_guiding_temp_light, light_bytes));
    UR_CUDA_CHECK(cudaMalloc(&ctx->d_path_guiding_temp_spatial, spatial_bytes));
}

static void validate_path_guiding_merge_state(const MultiGpuContext* ctx) {
    if (ctx->path_guiding_light_count == 0) return;
    const GpuContext* first = ctx->contexts[0];
    for (int i = 1; i < ctx->num_gpus; ++i) {
        const GpuContext* current = ctx->contexts[i];
        const size_t spatial_count =
            static_cast<size_t>(current->light_count) *
            static_cast<size_t>(current->path_guiding_spatial_cell_count) *
            static_cast<size_t>(current->path_guiding_directional_bin_count);
        if (!current->d_path_guiding_light_weights ||
            current->light_count != first->light_count ||
            spatial_count != ctx->path_guiding_spatial_count ||
            current->path_guiding_epoch != first->path_guiding_epoch ||
            current->path_guiding_passes_since_decay != first->path_guiding_passes_since_decay) {
            throw std::runtime_error("Multi-GPU path guiding state is incompatible across devices");
        }
    }
}

static void merge_and_broadcast_path_guiding(MultiGpuContext* ctx, float baseline_factor) {
    if (ctx->path_guiding_light_count == 0) return;
    validate_path_guiding_merge_state(ctx);
    constexpr int kBlockSize = 256;
    const size_t light_bytes = ctx->path_guiding_light_count * sizeof(float);
    const size_t spatial_bytes = ctx->path_guiding_spatial_count * sizeof(float);
    cudaSetDevice(0);
    for (int i = 1; i < ctx->num_gpus; ++i) {
        UR_CUDA_CHECK(cudaMemcpyPeer(ctx->d_path_guiding_temp_light, 0,
                                     ctx->contexts[i]->d_path_guiding_light_weights, i,
                                     light_bytes));
        UR_CUDA_CHECK(cudaMemcpyPeer(ctx->d_path_guiding_temp_spatial, 0,
                                     ctx->contexts[i]->d_path_guiding_spatial_directional_weights, i,
                                     spatial_bytes));
        merge_path_guiding_delta_kernel<<<
            static_cast<unsigned int>((ctx->path_guiding_light_count + kBlockSize - 1) / kBlockSize),
            kBlockSize>>>(ctx->contexts[0]->d_path_guiding_light_weights,
                          ctx->d_path_guiding_temp_light,
                          ctx->d_path_guiding_baseline_light,
                          ctx->path_guiding_light_count,
                          baseline_factor);
        UR_CUDA_CHECK(cudaGetLastError());
        merge_path_guiding_delta_kernel<<<
            static_cast<unsigned int>((ctx->path_guiding_spatial_count + kBlockSize - 1) / kBlockSize),
            kBlockSize>>>(ctx->contexts[0]->d_path_guiding_spatial_directional_weights,
                          ctx->d_path_guiding_temp_spatial,
                          ctx->d_path_guiding_baseline_spatial,
                          ctx->path_guiding_spatial_count,
                          baseline_factor);
        UR_CUDA_CHECK(cudaGetLastError());
    }
    UR_CUDA_CHECK(cudaDeviceSynchronize());
    for (int i = 1; i < ctx->num_gpus; ++i) {
        UR_CUDA_CHECK(cudaMemcpyPeer(ctx->contexts[i]->d_path_guiding_light_weights, i,
                                     ctx->contexts[0]->d_path_guiding_light_weights, 0,
                                     light_bytes));
        UR_CUDA_CHECK(cudaMemcpyPeer(ctx->contexts[i]->d_path_guiding_spatial_directional_weights, i,
                                     ctx->contexts[0]->d_path_guiding_spatial_directional_weights, 0,
                                     spatial_bytes));
    }
}

MultiGpuContext* init_multi_gpu_renderer(int width, int height,
                                         const std::vector<RenderMesh>& meshes,
                                         const std::vector<GpuInstance>& instances,
                                         const std::vector<GpuSphere>& spheres,
                                         const std::vector<GpuMaterialData>& materials,
                                         const std::vector<HostTexture>& textures,
                                         const ure::RenderConfig& config,
                                         const std::vector<scene_ir::MiePhaseResource>& mie_phase_resources) {
    int device_count = 0;
    cudaGetDeviceCount(&device_count);
    int num_gpus = std::min(device_count, config.num_gpus_to_use);
    if (num_gpus < 1) num_gpus = 1;

    MultiGpuContext* ctx = new MultiGpuContext();
    ctx->num_gpus = num_gpus;
    ctx->width = width;
    ctx->height = height;
    ctx->contexts = new GpuContext*[num_gpus];

    UR_LOG_INFO(GPU, "Initializing {} GPU(s) for multi-GPU rendering ({}x{})", num_gpus, width, height);

    for (int i = 0; i < num_gpus; ++i) {
        cudaSetDevice(i);
        cudaDeviceProp prop;
        cudaGetDeviceProperties(&prop, i);
        UR_LOG_INFO(GPU, "  GPU[{}]: {}", i, prop.name);
        ure::RenderConfig device_config = config;
        if (device_config.mlt.enabled) {
            device_config.mlt.chain_id_offset =
                ure::integrator::make_mlt_chain_shard(
                    i, num_gpus, config.mlt.chain_count,
                    config.mlt.chain_id_offset).global_chain_offset;
        }
        ctx->contexts[i] = init_gpu_renderer(width, height, meshes, instances, spheres, materials,
                                             textures, device_config, mie_phase_resources);
    }

    cudaSetDevice(0);
    size_t fb_size = width * height * sizeof(GpuVec3);
    cudaMalloc(&ctx->d_merged_accum, fb_size);
    cudaMemset(ctx->d_merged_accum, 0, fb_size);
    cudaMalloc(&ctx->d_merged_counts, width * height * sizeof(int));
    cudaMemset(ctx->d_merged_counts, 0, width * height * sizeof(int));
    allocate_path_guiding_merge_buffers(ctx);

    return ctx;
}

void free_multi_gpu_renderer(MultiGpuContext* ctx) {
    for (int i = 0; i < ctx->num_gpus; ++i) {
        cudaSetDevice(i);
        free_gpu_renderer(ctx->contexts[i]);
    }
    cudaSetDevice(0);
    cudaFree(ctx->d_merged_accum);
    cudaFree(ctx->d_merged_counts);
    cudaFree(ctx->d_path_guiding_baseline_light);
    cudaFree(ctx->d_path_guiding_baseline_spatial);
    cudaFree(ctx->d_path_guiding_temp_light);
    cudaFree(ctx->d_path_guiding_temp_spatial);
    delete[] ctx->contexts;
    delete ctx;
}

int render_pass_multi_gpu(MultiGpuContext* ctx, int samples_per_pass) {
    // Each GPU processes a disjoint slice of the sample space.
    // GPU i: samples [base + i*spp, base + (i+1)*spp)
    // After the pass, all GPUs share the same new base.
    int current_base = ctx->contexts[0]->current_spp;
    const bool mlt_mode =
        ctx->contexts[0]->render_config.integrator.mode ==
        ure::IntegratorMode::MLT;
    float path_guiding_baseline_factor = 1.0f;
    if (ctx->path_guiding_light_count > 0) {
        validate_path_guiding_merge_state(ctx);
        GpuContext* first = ctx->contexts[0];
        const bool decay_this_pass =
            first->path_guiding_passes_since_decay + 1 >= first->render_config.path_guiding.decay_interval;
        path_guiding_baseline_factor = decay_this_pass ? first->render_config.path_guiding.decay : 1.0f;
        cudaSetDevice(0);
        UR_CUDA_CHECK(cudaMemcpy(ctx->d_path_guiding_baseline_light,
                                 first->d_path_guiding_light_weights,
                                 ctx->path_guiding_light_count * sizeof(float),
                                 cudaMemcpyDeviceToDevice));
        UR_CUDA_CHECK(cudaMemcpy(ctx->d_path_guiding_baseline_spatial,
                                 first->d_path_guiding_spatial_directional_weights,
                                 ctx->path_guiding_spatial_count * sizeof(float),
                                 cudaMemcpyDeviceToDevice));
    }

    for (int i = 0; i < ctx->num_gpus; ++i) {
        cudaSetDevice(i);
        if (!mlt_mode) {
            ctx->contexts[i]->current_spp = current_base + i * samples_per_pass;
        }
        render_pass_gpu(ctx->contexts[i], samples_per_pass);
        UR_CUDA_CHECK(cudaDeviceSynchronize());
    }

    merge_and_broadcast_path_guiding(ctx, path_guiding_baseline_factor);

    int new_base = mlt_mode
        ? current_base +
            ctx->contexts[0]->render_config.mlt.mutations_per_chain *
                samples_per_pass
        : current_base + ctx->num_gpus * samples_per_pass;
    for (int i = 0; i < ctx->num_gpus; ++i) {
        ctx->contexts[i]->current_spp = new_base;
    }

    return new_base;
}

static void ensure_merged(MultiGpuContext* ctx) {
    // If already merged, skip
    int n_pixels = ctx->width * ctx->height;
    int block = 256;
    int grid = (n_pixels + block - 1) / block;
    size_t fb_size = n_pixels * sizeof(GpuVec3);
    size_t cnt_size = n_pixels * sizeof(int);

    cudaSetDevice(0);

    // Reset merged buffers
    cudaMemset(ctx->d_merged_accum, 0, fb_size);
    cudaMemset(ctx->d_merged_counts, 0, cnt_size);

    GpuVec3* temp_accum = nullptr;
    int* temp_counts = nullptr;
    cudaMalloc(&temp_accum, fb_size);
    cudaMalloc(&temp_counts, cnt_size);

    for (int i = 0; i < ctx->num_gpus; ++i) {
        if (i == 0) {
            cudaMemcpy(temp_accum, ctx->contexts[0]->d_accum_buffer, fb_size, cudaMemcpyDeviceToDevice);
            cudaMemcpy(temp_counts, ctx->contexts[0]->d_sample_counts, cnt_size, cudaMemcpyDeviceToDevice);
        } else {
            cudaMemcpyPeer(temp_accum, 0, ctx->contexts[i]->d_accum_buffer, i, fb_size);
            cudaMemcpyPeer(temp_counts, 0, ctx->contexts[i]->d_sample_counts, i, cnt_size);
        }
        merge_accum_kernel<<<grid, block>>>(ctx->d_merged_accum, temp_accum, n_pixels);
        merge_counts_kernel<<<grid, block>>>(ctx->d_merged_counts, temp_counts, n_pixels);
        cudaDeviceSynchronize();
    }

    cudaFree(temp_accum);
    cudaFree(temp_counts);
}

void copy_frame_buffer_multi_gpu(MultiGpuContext* ctx, float* host_buffer) {
    ensure_merged(ctx);

    cudaSetDevice(0);
    size_t fb_size = ctx->width * ctx->height * sizeof(GpuVec3);

    // Write merged accum/counts into device 0 context for the copy helper
    cudaMemcpy(ctx->contexts[0]->d_accum_buffer, ctx->d_merged_accum, fb_size, cudaMemcpyDeviceToDevice);
    cudaMemcpy(ctx->contexts[0]->d_sample_counts, ctx->d_merged_counts, ctx->width * ctx->height * sizeof(int), cudaMemcpyDeviceToDevice);

    copy_frame_buffer_gpu(ctx->contexts[0], host_buffer);
}

} // namespace ure::gpu
