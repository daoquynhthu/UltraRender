#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <math.h>
#include "ure/detail/cuda_structs.cuh"

using namespace ure::gpu;

namespace ure::gpu {

static __device__ __forceinline__ float luma(GpuVec3 rgb) {
    return rgb.x * 0.299f + rgb.y * 0.587f + rgb.z * 0.114f;
}

__global__ __launch_bounds__(256) void suppress_dark_outliers_kernel(
    GpuVec3* output_buffer,
    const GpuVec3* input_buffer,
    const GpuVec3* normal_buffer,
    const GpuVec3* albedo_buffer,
    int width,
    int height,
    float k_sigma,
    float min_luma,
    float normal_phi,
    float albedo_phi
) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    
    if (x >= width || y >= height) return;
    
    int idx = y * width + x;
    
    GpuVec3 c0 = input_buffer[idx];
    float l0 = luma(c0);
    GpuVec3 n0 = normal_buffer[idx];
    float n0_len = n0.length_sq();
    GpuVec3 a0 = albedo_buffer[idx];
    
    float sum_w = 0.0f;
    float sum_l = 0.0f;
    float sum_l2 = 0.0f;
    GpuVec3 sum_c(0, 0, 0);
    
    for (int j = -1; j <= 1; ++j) {
        for (int i = -1; i <= 1; ++i) {
            int nx = x + i;
            int ny = y + j;
            
            if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;
            
            int n_idx = ny * width + nx;
            
            GpuVec3 c1 = input_buffer[n_idx];
            float l1 = luma(c1);
            float w = 1.0f;
            
            if (n0_len > 1e-6f) {
                GpuVec3 n1 = normal_buffer[n_idx];
                float n1_len = n1.length_sq();
                if (n1_len > 1e-6f) {
                    float nd = 1.0f - fmaxf(-1.0f, fminf(1.0f, n0.dot(n1)));
                    w *= __expf(-nd / normal_phi);
                }
            }
            
            GpuVec3 a1 = albedo_buffer[n_idx];
            GpuVec3 da = a0 - a1;
            float da2 = da.dot(da);
            w *= __expf(-da2 / albedo_phi);
            
            sum_w += w;
            sum_l += l1 * w;
            sum_l2 += l1 * l1 * w;
            sum_c = sum_c + c1 * w;
        }
    }
    
    if (sum_w <= 1e-6f) {
        output_buffer[idx] = c0;
        return;
    }
    
    float mean = sum_l / sum_w;
    float var = fmaxf(0.0f, sum_l2 / sum_w - mean * mean);
    float sigma = sqrtf(var);
    
    if (l0 < mean - k_sigma * sigma && l0 < min_luma) {
        output_buffer[idx] = sum_c * (1.0f / sum_w);
    } else {
        output_buffer[idx] = c0;
    }
}

__global__ __launch_bounds__(256) void atrous_filter_kernel(
    GpuVec3* output_buffer,
    const GpuVec3* input_buffer,
    const GpuVec3* normal_buffer,
    const GpuVec3* albedo_buffer,
    int width,
    int height,
    int step_size,
    float c_phi, 
    float n_phi, 
    float p_phi
) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    
    if (x >= width || y >= height) return;
    
    int idx = y * width + x;
    
    GpuVec3 c_val = input_buffer[idx];
    GpuVec3 n_val = normal_buffer[idx];
    GpuVec3 p_val = albedo_buffer[idx];
    const float kernel[5] = { 1.0f/16.0f, 1.0f/4.0f, 3.0f/8.0f, 1.0f/4.0f, 1.0f/16.0f };

    GpuVec3 sum_color(0,0,0);
    float sum_weight = 0.0f;
    
    for (int j = -2; j <= 2; ++j) {
        for (int i = -2; i <= 2; ++i) {
            int nx = x + i * step_size;
            int ny = y + j * step_size;
            
            if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;
            
            int n_idx = ny * width + nx;
            
            GpuVec3 c_tmp = input_buffer[n_idx];
            GpuVec3 n_tmp = normal_buffer[n_idx];
            GpuVec3 p_tmp = albedo_buffer[n_idx];
            
            GpuVec3 t = c_val - c_tmp;
            float dist2 = t.dot(t);
            float w_c = __expf(-dist2 / c_phi);
            
            GpuVec3 t_n = n_val - n_tmp;
            float dist2_n = t_n.dot(t_n);
            float w_n = __expf(-dist2_n / n_phi);
            
            GpuVec3 t_p = p_val - p_tmp;
            float dist2_p = t_p.dot(t_p);
            float w_p = __expf(-dist2_p / p_phi);
            
            float weight = w_c * w_n * w_p * kernel[i+2] * kernel[j+2];
            
            sum_color = sum_color + c_tmp * weight;
            sum_weight += weight;
        }
    }
    
    if (sum_weight > 1e-6f) {
        output_buffer[idx] = sum_color * (1.0f / sum_weight);
    } else {
        output_buffer[idx] = c_val;
    }
}

} // namespace ure::gpu
