#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <float.h>
#include <math.h>
#include "ure/gpu_structs.hpp"
#include "ure/gpu_spectrum_utils.cuh"
#include "ure/path_tracer_sampling.cuh"

using namespace ure::gpu;

namespace ure::gpu {

// SoA queue throughput load/store helpers (duplicated for this TU)
__device__ inline SpectralPacket load_throughput(const RayQueue& q, int idx) {
    SpectralPacket t;
    for (int c = 0; c < q.num_spectral_channels; ++c) {
        t.set_sample(c, q.throughput_vals[c * q.capacity + idx]);
        t.set_wavelength(c, q.throughput_wavelengths[c * q.capacity + idx]);
    }
    return t;
}
__device__ inline void store_throughput(RayQueue& q, int idx, const SpectralPacket& t) {
    for (int c = 0; c < q.num_spectral_channels; ++c) {
        q.throughput_vals[c * q.capacity + idx] = t.sample(c);
        q.throughput_wavelengths[c * q.capacity + idx] = t.wavelength(c);
    }
}

__device__ inline void store_stokes(RayQueue& q, int idx, int channel, const StokesVector& s) {
    int offset = channel * q.capacity + idx;
    q.stokes_i[offset] = s.I;
    q.stokes_q[offset] = s.Q;
    q.stokes_u[offset] = s.U;
    q.stokes_v[offset] = s.V;
}

__device__ inline void store_stokes_packet(RayQueue& q, int idx, const StokesVector& s) {
    for (int c = 0; c < q.num_spectral_channels; ++c) {
        store_stokes(q, idx, c, s);
    }
}

__global__ __launch_bounds__(512) void generate_rays_kernel(
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
    
    float r1 = sample_dimension(sample_index, pixel_index, kSampleDimCameraX);
    float r2 = sample_dimension(sample_index, pixel_index, kSampleDimCameraY);
    
    float dx = (r1 < 0.5f) ? sqrtf(2.0f * r1) - 1.0f : 1.0f - sqrtf(2.0f * (1.0f - r1));
    float dy = (r2 < 0.5f) ? sqrtf(2.0f * r2) - 1.0f : 1.0f - sqrtf(2.0f * (1.0f - r2));

    float u = (float(i) + 0.5f + dx) / float(width);
    float v = (float(height - 1 - j) + 0.5f + dy) / float(height);
    
    GpuRay r;
    r.origin = camera.origin;
    r.direction = (camera.lower_left_corner + u * camera.horizontal + v * camera.vertical - camera.origin).normalize();
    
    queue.origins[ray_index] = r.origin;
    queue.directions[ray_index] = r.direction;

    int spectral_mode = queue.initial_spectral_mode;
    int active_channel = -1;
    float r_lambda = 0.0f;
    float sampled_lambda = kSpectralLambdaMin;
    float wavelength_pdf = 1.0f / (kSpectralLambdaMax - kSpectralLambdaMin);
    if (spectral_mode_is_sampled(spectral_mode)) {
        r_lambda = sample_dimension(sample_index, pixel_index, kSampleDimWavelength);
        if (queue.wavelength_sampling_strategy == SpectralWavelengthSamplingSceneSpectralPower &&
            queue.wavelength_proposal_cdf &&
            queue.wavelength_proposal_pdf &&
            queue.wavelength_proposal_count > 0) {
            sampled_lambda = sample_scene_cie_mixture_wavelength(
                r_lambda,
                queue.wavelength_proposal_cdf,
                queue.wavelength_proposal_pdf,
                queue.wavelength_proposal_count,
                queue.wavelength_proposal_lambda_min,
                queue.wavelength_proposal_lambda_max,
                &wavelength_pdf);
        } else if (queue.wavelength_sampling_strategy == SpectralWavelengthSamplingCieYImportance ||
                   queue.wavelength_sampling_strategy == SpectralWavelengthSamplingSceneSpectralPower) {
            sampled_lambda = sample_cie_y_importance_wavelength(r_lambda, &wavelength_pdf);
        } else {
            float domain = kSpectralLambdaMax - kSpectralLambdaMin;
            sampled_lambda = kSpectralLambdaMin + r_lambda * domain;
            wavelength_pdf = 1.0f / domain;
        }
        float normalized_lambda = (sampled_lambda - kSpectralLambdaMin) / (kSpectralLambdaMax - kSpectralLambdaMin);
        active_channel = min(int(normalized_lambda * queue.num_spectral_channels), queue.num_spectral_channels - 1);
        if (queue.num_spectral_channels == 1) active_channel = 0;
    }

    float domain = kSpectralLambdaMax - kSpectralLambdaMin;
    float bin_width = domain / float(queue.num_spectral_channels);
    for (int c = 0; c < queue.num_spectral_channels; ++c) {
        queue.throughput_vals[c * queue.capacity + ray_index] =
            spectral_mode_is_sampled(spectral_mode) ? (c == active_channel ? 1.0f : 0.0f) : 1.0f;
        queue.throughput_wavelengths[c * queue.capacity + ray_index] =
            spectral_mode == SpectralRayModeSampled && c == active_channel
                ? sampled_lambda
                : kSpectralLambdaMin + (float(c) + 0.5f) * bin_width;
    }
    if (spectral_mode_is_sampled(spectral_mode)) {
        for (int c = 0; c < queue.num_spectral_channels; ++c) {
            store_stokes(queue, ray_index, c,
                c == active_channel ? StokesVector(1.0f, 0.0f, 0.0f, 0.0f) : StokesVector(0.0f, 0.0f, 0.0f, 0.0f));
        }
    } else {
        store_stokes_packet(queue, ray_index, StokesVector(1.0f, 0.0f, 0.0f, 0.0f));
    }
    queue.medium_indices[ray_index] = -1;
    queue.seeds[ray_index] = seed;
    queue.sample_indices[ray_index] = static_cast<std::uint32_t>(sample_index);
    queue.pixel_indices[ray_index] = pixel_index;
    queue.depths[ray_index] = 0;
    queue.flags[ray_index] = 1;
    queue.last_pdf[ray_index] = 0.0f;
    queue.spectral_modes[ray_index] = spectral_mode;
    queue.active_channels[ray_index] = active_channel;
    queue.wavelength_pdfs[ray_index] = spectral_mode == SpectralRayModeSampled
        ? wavelength_pdf
        : 1.0f / fmaxf(1.0f, float(queue.num_spectral_channels));
}

} // namespace ure::gpu
