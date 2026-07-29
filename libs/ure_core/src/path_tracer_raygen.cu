#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <float.h>
#include <math.h>
#include "ure/detail/cuda_structs.cuh"
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

static __device__ void initialize_camera_ray(
    RayQueue queue, int ray_index, int pixel_index, int width, int height,
    GpuCamera camera, int sample_index, int* sample_counts,
    float r1, float r2, float r_lambda) {
    int i = pixel_index % width;
    int j = pixel_index / width;
    if (sample_counts) {
        sample_counts[pixel_index] += 1;
    }
    
    unsigned int seed = wang_hash(1984 + pixel_index + sample_index * width * height);
    
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
    float sampled_lambda = kSpectralLambdaMin;
    float wavelength_pdf = 1.0f / (kSpectralLambdaMax - kSpectralLambdaMin);
    if (spectral_mode_is_sampled(spectral_mode)) {
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
        if (queue.film_wavelengths) {
            queue.film_wavelengths[
                c * queue.capacity + ray_index] =
                queue.throughput_wavelengths[
                    c * queue.capacity + ray_index];
        }
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
    queue.path_indices[ray_index] = static_cast<std::uint32_t>(pixel_index);
    queue.pixel_indices[ray_index] = pixel_index;
    queue.depths[ray_index] = 0;
    queue.flags[ray_index] = 1;
    queue.last_pdf[ray_index] = 0.0f;
    queue.spectral_modes[ray_index] = spectral_mode;
    queue.active_channels[ray_index] = active_channel;
    queue.wavelength_pdfs[ray_index] = spectral_mode == SpectralRayModeSampled
        ? wavelength_pdf
        : 1.0f / fmaxf(1.0f, float(queue.num_spectral_channels));
    if (queue.fluorescence_delay_seconds) {
        queue.fluorescence_delay_seconds[ray_index] = 0.0f;
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
    initialize_camera_ray(
        queue, pixel_index, pixel_index, width, height, camera, sample_index,
        sample_counts,
        sample_dimension(sample_index, pixel_index, kSampleDimCameraX),
        sample_dimension(sample_index, pixel_index, kSampleDimCameraY),
        sample_dimension(sample_index, pixel_index, kSampleDimWavelength));
}

__global__ __launch_bounds__(256) void generate_primary_sample_rays_kernel(
    RayQueue queue,
    int chain_count,
    int width,
    int height,
    GpuCamera camera,
    int mutation_index,
    int* film_pixels
) {
    int chain_index = blockIdx.x * blockDim.x + threadIdx.x;
    if (chain_index >= chain_count) return;
    const float film_x = sample_dimension(
        queue, mutation_index, chain_index, kSampleDimFilmX);
    const float film_y = sample_dimension(
        queue, mutation_index, chain_index, kSampleDimFilmY);
    const int i = min(int(film_x * width), width - 1);
    const int j = min(int(film_y * height), height - 1);
    const int film_pixel = j * width + i;
    if (film_pixels) film_pixels[chain_index] = film_pixel;
    initialize_camera_ray(
        queue, chain_index, film_pixel, width, height, camera,
        mutation_index, nullptr,
        sample_dimension(
            queue, mutation_index, chain_index, kSampleDimCameraX),
        sample_dimension(
            queue, mutation_index, chain_index, kSampleDimCameraY),
        sample_dimension(
            queue, mutation_index, chain_index, kSampleDimWavelength));
    queue.path_indices[chain_index] = static_cast<std::uint32_t>(chain_index);
    queue.pixel_indices[chain_index] = chain_index;
}

} // namespace ure::gpu
