#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <math.h>
#include "ure/detail/cuda_structs.cuh"
#include "ure/gpu_spectrum_utils.cuh"

using namespace ure::gpu;

namespace ure::gpu {

__global__ __launch_bounds__(256) void resolve_framebuffer_kernel(
    GpuVec3* accum_buffer,
    int* sample_counts,
    GpuVec3* output,
    int width,
    int height
) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    int j = blockIdx.y * blockDim.y + threadIdx.y;

    if (i >= width || j >= height) return;
    int pixel_index = j * width + i;
    
    int N = sample_counts[pixel_index];
    if (N > 0) {
        GpuVec3 final_val = accum_buffer[pixel_index] * (1.0f / N);
        if (!isfinite(final_val.x) || !isfinite(final_val.y) || !isfinite(final_val.z)) {
            final_val = GpuVec3(0, 0, 0);
        }
        final_val.x = fmaxf(0.0f, final_val.x);
        final_val.y = fmaxf(0.0f, final_val.y);
        final_val.z = fmaxf(0.0f, final_val.z);
        output[pixel_index] = final_val;
    } else {
        output[pixel_index] = GpuVec3(0, 0, 0);
    }
}

__global__ __launch_bounds__(256)
void resolve_diffraction_framebuffer_kernel(
    const GpuVec3* spectral_accumulation,
    const float* psf_weights,
    const float* psf_prefix,
    const int* sample_counts,
    GpuVec3* output,
    int width,
    int height,
    int radius_pixels,
    int wavelength_count) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;
    const int pixel_index = y * width + x;
    const int kernel_width = radius_pixels * 2 + 1;
    const int kernel_area = kernel_width * kernel_width;
    const int prefix_width = kernel_width + 1;
    const int prefix_area = prefix_width * prefix_width;
    const int pixel_count = width * height;
    GpuVec3 xyz;
    for (int wavelength_index = 0;
         wavelength_index < wavelength_count;
         ++wavelength_index) {
        GpuVec3 filtered;
        const int film_offset =
            wavelength_index * pixel_count;
        const int kernel_offset =
            wavelength_index * kernel_area;
        const int prefix_offset =
            wavelength_index * prefix_area;
        for (int ky = 0; ky < kernel_width; ++ky) {
            const int source_y =
                y - ky + radius_pixels;
            if (source_y < 0 ||
                source_y >= height) {
                continue;
            }
            for (int kx = 0; kx < kernel_width; ++kx) {
                const int source_x =
                    x - kx + radius_pixels;
                if (source_x < 0 ||
                    source_x >= width) {
                    continue;
                }
                const int source_index =
                    source_y * width + source_x;
                const int source_sample_count =
                    sample_counts[source_index];
                if (source_sample_count <= 0) {
                    continue;
                }
                const int min_kernel_x = max(
                    0,
                    radius_pixels - source_x);
                const int max_kernel_x = min(
                    kernel_width - 1,
                    radius_pixels +
                        width - 1 - source_x);
                const int min_kernel_y = max(
                    0,
                    radius_pixels - source_y);
                const int max_kernel_y = min(
                    kernel_width - 1,
                    radius_pixels +
                        height - 1 - source_y);
                const int prefix_x0 =
                    min_kernel_x;
                const int prefix_x1 =
                    max_kernel_x + 1;
                const int prefix_y0 =
                    min_kernel_y;
                const int prefix_y1 =
                    max_kernel_y + 1;
                const float valid_weight =
                    psf_prefix[
                        prefix_offset +
                        prefix_y1 * prefix_width +
                        prefix_x1] -
                    psf_prefix[
                        prefix_offset +
                        prefix_y0 * prefix_width +
                        prefix_x1] -
                    psf_prefix[
                        prefix_offset +
                        prefix_y1 * prefix_width +
                        prefix_x0] +
                    psf_prefix[
                        prefix_offset +
                        prefix_y0 * prefix_width +
                        prefix_x0];
                filtered = filtered +
                    spectral_accumulation[
                        film_offset +
                        source_index] *
                    (psf_weights[
                         kernel_offset +
                         ky * kernel_width + kx] /
                     (fmaxf(valid_weight, 1.0e-20f) *
                      float(source_sample_count)));
            }
        }
        xyz = xyz + filtered;
    }
    GpuVec3 final_value = xyz_to_rgb(xyz);
    if (!isfinite(final_value.x) ||
        !isfinite(final_value.y) ||
        !isfinite(final_value.z)) {
        final_value = {};
    }
    final_value.x = fmaxf(0.0f, final_value.x);
    final_value.y = fmaxf(0.0f, final_value.y);
    final_value.z = fmaxf(0.0f, final_value.z);
    output[pixel_index] = final_value;
}

static __device__ __forceinline__ float luma(GpuVec3 rgb) {
    return rgb.x * 0.299f + rgb.y * 0.587f + rgb.z * 0.114f;
}

__device__ GpuVec3 sample_bilinear(const GpuVec3* buffer, int width, int height, float x, float y) {
    int x0 = floorf(x);
    int y0 = floorf(y);
    int x1 = min(x0 + 1, width - 1);
    int y1 = min(y0 + 1, height - 1);
    x0 = max(x0, 0);
    y0 = max(y0, 0);
    
    float dx = x - x0;
    float dy = y - y0;
    
    int idx_y0 = y0 * width;
    int idx_y1 = y1 * width;
    
    GpuVec3 c00 = buffer[idx_y0 + x0];
    GpuVec3 c10 = buffer[idx_y0 + x1];
    GpuVec3 c01 = buffer[idx_y1 + x0];
    GpuVec3 c11 = buffer[idx_y1 + x1];
    
    GpuVec3 top = c00 * (1.0f - dx) + c10 * dx;
    GpuVec3 bot = c01 * (1.0f - dx) + c11 * dx;
    return top * (1.0f - dy) + bot * dy;
}

__global__ __launch_bounds__(256) void fxaa_kernel(
    GpuVec3* output,
    const GpuVec3* input,
    int width,
    int height
) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    
    if (x >= width || y >= height) return;
    
    int idx = y * width + x;
    
    const float FXAA_EDGE_THRESHOLD = 1.0f/16.0f;
    const float FXAA_EDGE_THRESHOLD_MIN = 1.0f/24.0f;
    const int FXAA_SEARCH_STEPS = 12;
    const float FXAA_SUBPIX_TRIM = 1.0f/4.0f;
    const float FXAA_SUBPIX_CAP = 3.0f/4.0f;
    const float FXAA_SUBPIX_TRIM_SCALE = 1.0f/(1.0f - FXAA_SUBPIX_TRIM);

    GpuVec3 rgbM = input[idx];
    
    auto load = [&](int dx, int dy) {
        int nx = min(max(x + dx, 0), width - 1);
        int ny = min(max(y + dy, 0), height - 1);
        return input[ny * width + nx];
    };
    
    GpuVec3 rgbN = load(0, -1);
    GpuVec3 rgbW = load(-1, 0);
    GpuVec3 rgbE = load(1, 0);
    GpuVec3 rgbS = load(0, 1);
    
    float lumaM = luma(rgbM);
    float lumaN = luma(rgbN);
    float lumaW = luma(rgbW);
    float lumaE = luma(rgbE);
    float lumaS = luma(rgbS);
    
    float lumaMin = min(lumaM, min(min(lumaN, lumaW), min(lumaS, lumaE)));
    float lumaMax = max(lumaM, max(max(lumaN, lumaW), max(lumaS, lumaE)));
    float lumaRange = lumaMax - lumaMin;
    
    if (lumaRange < max(FXAA_EDGE_THRESHOLD_MIN, lumaMax * FXAA_EDGE_THRESHOLD)) {
        output[idx] = rgbM;
        return;
    }
    
    GpuVec3 rgbNW = load(-1, -1);
    GpuVec3 rgbNE = load(1, -1);
    GpuVec3 rgbSW = load(-1, 1);
    GpuVec3 rgbSE = load(1, 1);
    
    float lumaNW = luma(rgbNW);
    float lumaNE = luma(rgbNE);
    float lumaSW = luma(rgbSW);
    float lumaSE = luma(rgbSE);
    
    float lumaL = (lumaN + lumaW + lumaE + lumaS) * 0.25f;
    float rangeL = fabsf(lumaL - lumaM);
    float blendL = max(0.0f, (rangeL / lumaRange) - FXAA_SUBPIX_TRIM) * FXAA_SUBPIX_TRIM_SCALE;
    blendL = min(FXAA_SUBPIX_CAP, blendL);
    
    float edgeVert = 
        fabsf((0.25f * lumaNW) + (-0.5f * lumaN) + (0.25f * lumaNE)) +
        fabsf((0.50f * lumaW ) + (-1.0f * lumaM) + (0.50f * lumaE )) +
        fabsf((0.25f * lumaSW) + (-0.5f * lumaS) + (0.25f * lumaSE));
        
    float edgeHorz = 
        fabsf((0.25f * lumaNW) + (-0.5f * lumaW) + (0.25f * lumaSW)) +
        fabsf((0.50f * lumaN ) + (-1.0f * lumaM) + (0.50f * lumaS )) +
        fabsf((0.25f * lumaNE) + (-0.5f * lumaE) + (0.25f * lumaSE));
        
    bool isHorz = edgeHorz >= edgeVert;
    
    float luma1 = isHorz ? lumaN : lumaW;
    float luma2 = isHorz ? lumaS : lumaE;
    
    float gradient1 = luma1 - lumaM;
    float gradient2 = luma2 - lumaM;
    
    bool is1Steepest = fabsf(gradient1) >= fabsf(gradient2);
    float gradientScaled = 0.25f * max(fabsf(gradient1), fabsf(gradient2));
    
    float stepX = 0.0f;
    float stepY = 0.0f;
    
    if (isHorz) {
        stepY = is1Steepest ? -1.0f : 1.0f;
        stepX = 0.0f;
    } else {
        stepX = is1Steepest ? -1.0f : 1.0f;
        stepY = 0.0f;
    }
    
    float lumaLocalAverage = 0.0f;
    if (is1Steepest) {
        lumaLocalAverage = 0.5f * (luma1 + lumaM);
    } else {
        lumaLocalAverage = 0.5f * (luma2 + lumaM);
    }
    
    float currX = float(x) + 0.5f;
    float currY = float(y) + 0.5f;
    
    if (isHorz) {
        currY += stepY * 0.5f;
    } else {
        currX += stepX * 0.5f;
    }
    
    float2 offset = isHorz ? make_float2(1.0f, 0.0f) : make_float2(0.0f, 1.0f);
    
    float2 uv1 = make_float2(currX - offset.x, currY - offset.y);
    float2 uv2 = make_float2(currX + offset.x, currY + offset.y);
    
    float lumaEnd1 = 0.0f;
    float lumaEnd2 = 0.0f;
    bool reached1 = false;
    bool reached2 = false;
    bool reachedBoth = false;
    
    for (int i = 0; i < FXAA_SEARCH_STEPS; ++i) {
        if (!reached1) {
            lumaEnd1 = luma(sample_bilinear(input, width, height, uv1.x, uv1.y));
            lumaEnd1 -= lumaLocalAverage;
        }
        if (!reached2) {
            lumaEnd2 = luma(sample_bilinear(input, width, height, uv2.x, uv2.y));
            lumaEnd2 -= lumaLocalAverage;
        }
        
        reached1 = fabsf(lumaEnd1) >= gradientScaled;
        reached2 = fabsf(lumaEnd2) >= gradientScaled;
        reachedBoth = reached1 && reached2;
        
        if (!reached1) {
            uv1.x -= offset.x;
            uv1.y -= offset.y;
        }
        if (!reached2) {
            uv2.x += offset.x;
            uv2.y += offset.y;
        }
        
        if (reachedBoth) break;
    }
    
    float dist1 = isHorz ? (currX - uv1.x) : (currY - uv1.y);
    float dist2 = isHorz ? (uv2.x - currX) : (uv2.y - currY);
    
    bool isDirection1 = dist1 < dist2;
    float distMin = min(dist1, dist2);
    float distTotal = dist1 + dist2;
    
    float edgeBlend = 0.5f - (distMin / distTotal);
    
    bool isLumaEndSteepest = isDirection1 ? (lumaEnd1 < 0.0f) : (lumaEnd2 < 0.0f);
    bool isLumaLocalSteepest = (lumaLocalAverage - lumaM) < 0.0f;
    
    if (isLumaEndSteepest != isLumaLocalSteepest) {
        edgeBlend = 0.0f;
    }
    
    float finalBlend = max(blendL, edgeBlend);
    
    float finalStepX = 0.0f;
    float finalStepY = 0.0f;
    
    if (isHorz) {
        finalStepY = stepY * finalBlend;
    } else {
        finalStepX = stepX * finalBlend;
    }
    
    output[idx] = sample_bilinear(input, width, height, float(x) + 0.5f + finalStepX, float(y) + 0.5f + finalStepY);
}

} // namespace ure::gpu
