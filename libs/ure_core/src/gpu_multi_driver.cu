#include "ure/gpu_multi_driver.hpp"
#include "ure/gpu_structs.hpp"
#include "ure/gpu_context.hpp"
#include <cuda_runtime.h>
#include <ure/log.hpp>
#include <algorithm>
#include <cstring>

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

MultiGpuContext* init_multi_gpu_renderer(int width, int height,
                                         const std::vector<RenderMesh>& meshes,
                                         const std::vector<GpuInstance>& instances,
                                         const std::vector<GpuSphere>& spheres,
                                         const std::vector<GpuMaterialData>& materials,
                                         const std::vector<HostTexture>& textures,
                                         const ure::RenderConfig& config) {
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
        ctx->contexts[i] = init_gpu_renderer(width, height, meshes, instances, spheres, materials, textures, config);
    }

    cudaSetDevice(0);
    size_t fb_size = width * height * sizeof(GpuVec3);
    cudaMalloc(&ctx->d_merged_accum, fb_size);
    cudaMemset(ctx->d_merged_accum, 0, fb_size);
    cudaMalloc(&ctx->d_merged_counts, width * height * sizeof(int));
    cudaMemset(ctx->d_merged_counts, 0, width * height * sizeof(int));

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
    delete[] ctx->contexts;
    delete ctx;
}

int render_pass_multi_gpu(MultiGpuContext* ctx, int samples_per_pass) {
    // Each GPU processes a disjoint slice of the sample space.
    // GPU i: samples [base + i*spp, base + (i+1)*spp)
    // After the pass, all GPUs share the same new base.
    int current_base = ctx->contexts[0]->current_spp;

    for (int i = 0; i < ctx->num_gpus; ++i) {
        cudaSetDevice(i);
        ctx->contexts[i]->current_spp = current_base + i * samples_per_pass;
        render_pass_gpu(ctx->contexts[i], samples_per_pass);
    }

    int new_base = current_base + ctx->num_gpus * samples_per_pass;
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
