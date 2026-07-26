#pragma once

#include <cstddef>
#include <map>
#include <span>
#include <vector>

#include <cuda_runtime.h>

#include "ure/resource_types.hpp"

namespace ure::gpu {

struct CudaTextureBinding {
    cudaTextureObject_t texture_object = 0;
    const float* spectral_values = nullptr;
};

class CudaResourceRegistry final {
public:
    CudaResourceRegistry() = default;
    ~CudaResourceRegistry();
    CudaResourceRegistry(const CudaResourceRegistry&) = delete;
    CudaResourceRegistry& operator=(const CudaResourceRegistry&) = delete;

    void retain_allocation(void* allocation);
    void retain_material_resource(void* allocation);
    void release_material_resources();

    CudaTextureBinding create_rgba32_image(
        resource::ResourceId id,
        int width,
        int height,
        std::span<const float4> values);
    CudaTextureBinding create_spectral_table(
        resource::ResourceId id,
        std::span<const float> values);
    CudaTextureBinding texture_binding(resource::ResourceId id) const;

private:
    struct NativeTexture {
        cudaArray_t array = nullptr;
        cudaTextureObject_t texture_object = 0;
        float* spectral_values = nullptr;
    };

    std::vector<void*> allocations_;
    std::vector<void*> material_resources_;
    std::map<resource::ResourceId, NativeTexture> textures_;
};

}
