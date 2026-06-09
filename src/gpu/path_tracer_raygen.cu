#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <float.h>
#include <math.h>
#include "gpu/gpu_structs.hpp"
#include "gpu/path_tracer_sampling.cuh"

using namespace ure::gpu;

namespace ure::gpu {

__global__ void generate_rays_kernel(
    RayQueue queue,
    int width,
    int height,
    GpuCamera camera,
    int sample_index,
    int* sample_counts
) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    int j = blockIdx.y * blockDim.y + threadIdx.y;
    
    if (i >= width || j >= height) return;
    
    int pixel_index = j * width + i;
    int ray_index = pixel_index; 
    
    if (sample_counts) {
        sample_counts[pixel_index] += 1;
    }
    
    unsigned int seed = wang_hash(1984 + pixel_index + sample_index * width * height);
    
    float4 ray_wavelengths;
    {
        float domain = GpuSpectrum::kLambdaMax - GpuSpectrum::kLambdaMin;
        float bin_width = domain / 4.0f;
        float offset = rand_float(seed);
        
        ray_wavelengths.x = GpuSpectrum::kLambdaMin + (0.0f + offset) * bin_width;
        ray_wavelengths.y = GpuSpectrum::kLambdaMin + (1.0f + offset) * bin_width;
        ray_wavelengths.z = GpuSpectrum::kLambdaMin + (2.0f + offset) * bin_width;
        ray_wavelengths.w = GpuSpectrum::kLambdaMin + (3.0f + offset) * bin_width;
    }

    float r1 = sample_dimension(sample_index, pixel_index, 0);
    float r2 = sample_dimension(sample_index, pixel_index, 1);
    
    float dx = (r1 < 0.5f) ? sqrtf(2.0f * r1) - 1.0f : 1.0f - sqrtf(2.0f * (1.0f - r1));
    float dy = (r2 < 0.5f) ? sqrtf(2.0f * r2) - 1.0f : 1.0f - sqrtf(2.0f * (1.0f - r2));

    float u = (float(i) + 0.5f + dx) / float(width);
    float v = (float(height - 1 - j) + 0.5f + dy) / float(height);
    
    GpuRay r;
    r.origin = camera.origin;
    r.direction = (camera.lower_left_corner + u * camera.horizontal + v * camera.vertical - camera.origin).normalize();
    
    queue.origins[ray_index] = r.origin;
    queue.directions[ray_index] = r.direction;

    GpuSpectrum initial_throughput = GpuSpectrum(1.0f); 
    initial_throughput.wavelengths = ray_wavelengths;
    
    queue.throughputs[ray_index] = initial_throughput;
    queue.stokes[ray_index] = StokesVector(1.0f, 0.0f, 0.0f, 0.0f);
    queue.medium_indices[ray_index] = -1;
    queue.seeds[ray_index] = seed;
    queue.pixel_indices[ray_index] = pixel_index;
    queue.depths[ray_index] = 0;
    queue.flags[ray_index] = 1;
}

} // namespace ure::gpu
