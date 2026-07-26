#include "cuda_resource_registry.cuh"

#include <stdexcept>

#include "cuda_check.cuh"

namespace ure::gpu {

CudaResourceRegistry::~CudaResourceRegistry() {
    for (auto& [id, texture] : textures_) {
        static_cast<void>(id);
        if (texture.texture_object) {
            cudaDestroyTextureObject(texture.texture_object);
        }
        if (texture.array) cudaFreeArray(texture.array);
        if (texture.spectral_values) cudaFree(texture.spectral_values);
    }
    for (void* allocation : material_resources_) cudaFree(allocation);
    for (void* allocation : allocations_) cudaFree(allocation);
}

void CudaResourceRegistry::retain_allocation(void* allocation) {
    if (!allocation) {
        throw std::invalid_argument(
            "cannot retain a null CUDA allocation");
    }
    allocations_.push_back(allocation);
}

void CudaResourceRegistry::retain_material_resource(void* allocation) {
    if (!allocation) {
        throw std::invalid_argument(
            "cannot retain a null CUDA material resource");
    }
    material_resources_.push_back(allocation);
}

void CudaResourceRegistry::release_material_resources() {
    for (void* allocation : material_resources_) {
        UR_CUDA_CHECK(cudaFree(allocation));
    }
    material_resources_.clear();
}

CudaTextureBinding CudaResourceRegistry::create_rgba32_image(
    resource::ResourceId id,
    int width,
    int height,
    std::span<const float4> values) {
    if (!id || width <= 0 || height <= 0 ||
        values.size() !=
            static_cast<std::size_t>(width) *
                static_cast<std::size_t>(height) ||
        textures_.contains(id)) {
        throw std::invalid_argument("invalid CUDA image resource");
    }
    auto [entry, inserted] = textures_.try_emplace(id);
    if (!inserted) {
        throw std::invalid_argument("CUDA image resource id is duplicated");
    }
    NativeTexture& texture = entry->second;
    const cudaChannelFormatDesc channel =
        cudaCreateChannelDesc<float4>();
    UR_CUDA_CHECK(cudaMallocArray(
        &texture.array,
        &channel,
        width,
        height));
    UR_CUDA_CHECK(cudaMemcpy2DToArray(
        texture.array,
        0,
        0,
        values.data(),
        static_cast<std::size_t>(width) * sizeof(float4),
        static_cast<std::size_t>(width) * sizeof(float4),
        height,
        cudaMemcpyHostToDevice));
    cudaResourceDesc resource_desc{};
    resource_desc.resType = cudaResourceTypeArray;
    resource_desc.res.array.array = texture.array;
    cudaTextureDesc texture_desc{};
    texture_desc.addressMode[0] = cudaAddressModeWrap;
    texture_desc.addressMode[1] = cudaAddressModeWrap;
    texture_desc.filterMode = cudaFilterModeLinear;
    texture_desc.readMode = cudaReadModeElementType;
    texture_desc.normalizedCoords = 1;
    UR_CUDA_CHECK(cudaCreateTextureObject(
        &texture.texture_object,
        &resource_desc,
        &texture_desc,
        nullptr));
    return CudaTextureBinding{
        texture.texture_object,
        nullptr
    };
}

CudaTextureBinding CudaResourceRegistry::create_spectral_table(
    resource::ResourceId id,
    std::span<const float> values) {
    if (!id || values.empty() || textures_.contains(id)) {
        throw std::invalid_argument("invalid CUDA spectral resource");
    }
    auto [entry, inserted] = textures_.try_emplace(id);
    if (!inserted) {
        throw std::invalid_argument(
            "CUDA spectral resource id is duplicated");
    }
    NativeTexture& texture = entry->second;
    UR_CUDA_CHECK(cudaMalloc(
        &texture.spectral_values,
        values.size_bytes()));
    UR_CUDA_CHECK(cudaMemcpy(
        texture.spectral_values,
        values.data(),
        values.size_bytes(),
        cudaMemcpyHostToDevice));
    return CudaTextureBinding{
        0,
        texture.spectral_values
    };
}

CudaTextureBinding CudaResourceRegistry::texture_binding(
    resource::ResourceId id) const {
    const auto found = textures_.find(id);
    if (found == textures_.end()) {
        throw std::out_of_range("CUDA resource id is not registered");
    }
    return {
        found->second.texture_object,
        found->second.spectral_values
    };
}

}
