#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <math.h>
#include "ure/detail/cuda_structs.cuh"
#include "path_tracer_api_decl.cuh"

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

static __device__ __forceinline__ float statistical_normal_dot(
    GpuVec3 left,
    GpuVec3 right) {
    const float left_length = sqrtf(left.length_sq());
    const float right_length = sqrtf(right.length_sq());
    if (!(left_length > 0.0f) || !(right_length > 0.0f)) return -1.0f;
    return fmaxf(-1.0f, fminf(1.0f,
        left.dot(right) / (left_length * right_length)));
}

static __device__ __forceinline__ float statistical_signal(
    const float* value,
    int component_count,
    int stokes_domain) {
    if (stokes_domain != 0) return value[0];
    float result = 0.0f;
    for (int component = 0; component < component_count; ++component) {
        result += value[component];
    }
    return result / static_cast<float>(component_count);
}

static __device__ __forceinline__ float statistical_median(
    float* values,
    int count) {
    if (count == 0) return 0.0f;
    for (int index = 1; index < count; ++index) {
        const float value = values[index];
        int position = index;
        while (position > 0 && values[position - 1] > value) {
            values[position] = values[position - 1];
            --position;
        }
        values[position] = value;
    }
    const int middle = count / 2;
    return (count & 1) != 0
        ? values[middle]
        : 0.5f * (values[middle - 1] + values[middle]);
}

__global__ __launch_bounds__(256) void statistical_temporal_reconstruction_kernel(
    float* reconstructed,
    float* variance,
    float* history_confidence,
    unsigned int* history_length,
    unsigned char* rejection_reason,
    const float* raw_estimate,
    const float* estimate_variance,
    const GpuVec3* normal,
    const float* albedo,
    const float* depth,
    const float* motion,
    const float* motion_time_confidence,
    const unsigned char* validity,
    const float* history_reconstructed,
    const float* history_variance,
    const GpuVec3* history_normal,
    const float* history_albedo,
    const float* history_depth,
    const float* history_pixel_confidence,
    const unsigned int* previous_history_length,
    const unsigned char* history_validity,
    int width,
    int height,
    int component_count,
    unsigned int maximum_history_length,
    GpuStatisticalReconstructionConfig config) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height || component_count <= 0 ||
        component_count > 32) return;
    const int pixel = y * width + x;
    const int base = pixel * component_count;
    history_confidence[pixel] = 0.0f;
    history_length[pixel] = 1;
    rejection_reason[pixel] = 2;
    for (int component = 0; component < component_count; ++component) {
        reconstructed[base + component] = validity[pixel] != 0
            ? raw_estimate[base + component] : 0.0f;
        variance[base + component] = validity[pixel] != 0
            ? estimate_variance[base + component] : 1.0e30f;
    }
    if (validity[pixel] == 0) {
        rejection_reason[pixel] = 1;
        return;
    }
    const float motion_x = motion[pixel * 2];
    const float motion_y = motion[pixel * 2 + 1];
    if (!isfinite(motion_x) || !isfinite(motion_y) ||
        !(motion_time_confidence[pixel] > 0.0f)) {
        rejection_reason[pixel] = 4;
        return;
    }
    const int previous_x = __float2int_rn(static_cast<float>(x) - motion_x);
    const int previous_y = __float2int_rn(static_cast<float>(y) - motion_y);
    if (previous_x < 0 || previous_y < 0 || previous_x >= width ||
        previous_y >= height) {
        rejection_reason[pixel] = 5;
        return;
    }
    const int previous = previous_y * width + previous_x;
    if (history_validity[previous] == 0) {
        rejection_reason[pixel] = 6;
        return;
    }
    const float depth_scale = fmaxf(
        1.0f, fmaxf(fabsf(depth[pixel]), fabsf(history_depth[previous])));
    if (fabsf(depth[pixel] - history_depth[previous]) >
        config.maximum_relative_depth_difference * depth_scale) {
        rejection_reason[pixel] = 7;
        return;
    }
    if (statistical_normal_dot(normal[pixel], history_normal[previous]) <
        config.minimum_normal_dot) {
        rejection_reason[pixel] = 8;
        return;
    }
    float albedo_distance = 0.0f;
    const int history_base = previous * component_count;
    for (int component = 0; component < component_count; ++component) {
        const float difference = albedo[base + component] -
            history_albedo[history_base + component];
        albedo_distance += difference * difference;
    }
    albedo_distance = sqrtf(
        albedo_distance / static_cast<float>(component_count));
    if (albedo_distance > config.maximum_albedo_distance) {
        rejection_reason[pixel] = 9;
        return;
    }
    float current_variance = 0.0f;
    float previous_variance = 0.0f;
    for (int component = 0; component < component_count; ++component) {
        current_variance += estimate_variance[base + component];
        previous_variance += history_variance[history_base + component];
    }
    current_variance /= static_cast<float>(component_count);
    previous_variance /= static_cast<float>(component_count);
    const float current_precision = 1.0f / fmaxf(current_variance, 1.0e-20f);
    const float previous_precision = history_pixel_confidence[previous] *
        motion_time_confidence[pixel] /
        fmaxf(previous_variance, 1.0e-20f);
    const float weight = fminf(config.maximum_history_weight,
        fmaxf(0.0f, previous_precision /
            (current_precision + previous_precision)));
    for (int component = 0; component < component_count; ++component) {
        reconstructed[base + component] =
            raw_estimate[base + component] * (1.0f - weight) +
            history_reconstructed[history_base + component] * weight;
        variance[base + component] =
            estimate_variance[base + component] *
                (1.0f - weight) * (1.0f - weight) +
            history_variance[history_base + component] * weight * weight;
    }
    history_confidence[pixel] = weight;
    history_length[pixel] = min(
        maximum_history_length, previous_history_length[previous] + 1);
    rejection_reason[pixel] = 0;
}

__global__ __launch_bounds__(256) void statistical_atrous_reconstruction_kernel(
    float* reconstructed,
    float* variance,
    float* spatial_support,
    unsigned char* tail_class,
    const float* input_reconstructed,
    const float* input_variance,
    const float* raw_estimate,
    const float* tail_frequency,
    const float* maximum_absolute_contribution,
    const GpuVec3* normal,
    const float* albedo,
    const float* depth,
    const unsigned char* validity,
    int width,
    int height,
    int component_count,
    int step_size,
    int stokes_domain,
    GpuStatisticalReconstructionConfig config) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height || component_count <= 0 ||
        component_count > 32) return;
    const int pixel = y * width + x;
    const int base = pixel * component_count;
    float neighborhood[9];
    int neighborhood_count = 0;
    for (int offset_y = -1; offset_y <= 1; ++offset_y) {
        for (int offset_x = -1; offset_x <= 1; ++offset_x) {
            const int neighbor_x = x + offset_x;
            const int neighbor_y = y + offset_y;
            if (neighbor_x < 0 || neighbor_y < 0 || neighbor_x >= width ||
                neighbor_y >= height) continue;
            const int neighbor = neighbor_y * width + neighbor_x;
            if (validity[neighbor] == 0 ||
                statistical_normal_dot(normal[pixel], normal[neighbor]) <
                    0.85f) continue;
            neighborhood[neighborhood_count++] = statistical_signal(
                raw_estimate + neighbor * component_count,
                component_count, stokes_domain);
        }
    }
    unsigned char classification = 0;
    if (validity[pixel] == 0) {
        classification = 3;
    } else {
        const float center = statistical_signal(
            raw_estimate + base, component_count, stokes_domain);
        const float center_median = statistical_median(
            neighborhood, neighborhood_count);
        for (int index = 0; index < neighborhood_count; ++index) {
            neighborhood[index] = fabsf(
                neighborhood[index] - center_median);
        }
        const float mad = statistical_median(
            neighborhood, neighborhood_count);
        const bool heavy =
            tail_frequency[pixel] >= config.heavy_tail_frequency &&
            maximum_absolute_contribution[pixel] >=
                config.heavy_tail_scale * fmaxf(fabsf(center), 1.0e-12f);
        const bool high_energy = center > center_median +
            config.high_energy_sigma * fmaxf(mad, 1.0e-12f);
        classification = heavy ? 1 : (high_energy ? 2 : 0);
    }
    tail_class[pixel] = classification;
    const float kernel[5] = {1.0f, 4.0f, 6.0f, 4.0f, 1.0f};
    float sum[32] = {};
    float variance_sum[32] = {};
    float sum_weight = 0.0f;
    float sum_squared_weight = 0.0f;
    int support_count = 0;
    for (int kernel_y = -2; kernel_y <= 2; ++kernel_y) {
        for (int kernel_x = -2; kernel_x <= 2; ++kernel_x) {
            const int neighbor_x = x + kernel_x * step_size;
            const int neighbor_y = y + kernel_y * step_size;
            if (neighbor_x < 0 || neighbor_y < 0 || neighbor_x >= width ||
                neighbor_y >= height) continue;
            const int neighbor = neighbor_y * width + neighbor_x;
            if (validity[neighbor] == 0) continue;
            const float dot = statistical_normal_dot(
                normal[pixel], normal[neighbor]);
            if (dot < config.minimum_normal_dot) continue;
            const float depth_scale = fmaxf(
                1.0f, fmaxf(fabsf(depth[pixel]), fabsf(depth[neighbor])));
            const float depth_difference =
                fabsf(depth[pixel] - depth[neighbor]) / depth_scale;
            if (depth_difference >
                config.maximum_relative_depth_difference) continue;
            float albedo_distance = 0.0f;
            float signal_distance = 0.0f;
            float signal_variance = 0.0f;
            const int neighbor_base = neighbor * component_count;
            for (int component = 0; component < component_count; ++component) {
                float difference = albedo[base + component] -
                    albedo[neighbor_base + component];
                albedo_distance += difference * difference;
                difference = input_reconstructed[base + component] -
                    input_reconstructed[neighbor_base + component];
                signal_distance += difference * difference;
                signal_variance += input_variance[base + component] +
                    input_variance[neighbor_base + component];
            }
            albedo_distance = sqrtf(
                albedo_distance / static_cast<float>(component_count));
            if (albedo_distance > config.maximum_albedo_distance) continue;
            signal_distance /= static_cast<float>(component_count);
            signal_variance /= static_cast<float>(component_count);
            const float weight_signal = __expf(-signal_distance /
                (config.signal_sigma * config.signal_sigma *
                 fmaxf(signal_variance, 1.0e-20f)));
            const float weight_normal = __expf(
                -(1.0f - dot) / config.normal_sigma);
            const float weight_depth = __expf(
                -depth_difference / config.depth_sigma);
            const float weight_albedo = __expf(
                -(albedo_distance * albedo_distance) /
                (config.albedo_sigma * config.albedo_sigma));
            const float weight_tail =
                tail_frequency[neighbor] >= config.heavy_tail_frequency &&
                maximum_absolute_contribution[neighbor] >=
                    config.heavy_tail_scale * fmaxf(fabsf(statistical_signal(
                        raw_estimate + neighbor_base, component_count,
                        stokes_domain)), 1.0e-12f)
                ? 1.0f / (1.0f + config.heavy_tail_scale *
                    tail_frequency[neighbor]) : 1.0f;
            const float weight =
                kernel[kernel_x + 2] * kernel[kernel_y + 2] *
                weight_signal * weight_normal * weight_depth *
                weight_albedo * weight_tail;
            if (!(weight > 0.0f) || !isfinite(weight)) continue;
            ++support_count;
            sum_weight += weight;
            sum_squared_weight += weight * weight;
            for (int component = 0; component < component_count; ++component) {
                sum[component] +=
                    input_reconstructed[neighbor_base + component] * weight;
                variance_sum[component] +=
                    input_variance[neighbor_base + component] * weight * weight;
            }
        }
    }
    if (support_count < config.minimum_spatial_support ||
        !(sum_weight > 0.0f)) {
        spatial_support[pixel] = 0.0f;
        for (int component = 0; component < component_count; ++component) {
            reconstructed[base + component] =
                input_reconstructed[base + component];
            variance[base + component] = input_variance[base + component];
        }
        return;
    }
    spatial_support[pixel] = sum_weight * sum_weight /
        fmaxf(sum_squared_weight, 1.0e-20f);
    for (int component = 0; component < component_count; ++component) {
        reconstructed[base + component] = sum[component] / sum_weight;
        variance[base + component] = variance_sum[component] /
            (sum_weight * sum_weight);
    }
}

} // namespace ure::gpu
