#pragma once

#include <cuda_runtime.h>

#include "ure/gpu_structs.hpp"
#include "ure/resource_types.hpp"

namespace ure::gpu {

struct GpuTexture {
    int width;
    int height;
    int channels;
    resource::ResourceId resource_id;
    cudaTextureObject_t texture_object;
    SpectralTextureResourceKind spectral_kind =
        SpectralTextureResourceKind::None;
    const float* spectral_source_values = nullptr;
    int spectral_sample_count = 0;
    float spectral_lambda_min = kSpectralLambdaMin;
    float spectral_lambda_max = kSpectralLambdaMax;
};

}
