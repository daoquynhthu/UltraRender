#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>

#include "ure/runtime/runtime.hpp"

namespace ure::runtime {

enum class AccelerationFeature : std::uint32_t {
    ComputeBvh = 1u << 0,
    RayQuery = 1u << 1,
    RayTracingPipeline = 1u << 2
};

using AccelerationFeatureSet = std::uint32_t;

constexpr AccelerationFeatureSet acceleration_feature_bit(
    AccelerationFeature feature) {
    return static_cast<AccelerationFeatureSet>(feature);
}

constexpr bool acceleration_has_features(
    AccelerationFeatureSet available,
    AccelerationFeatureSet required) {
    return (available & required) == required;
}

enum class AccelerationMode : std::uint8_t {
    Automatic,
    ComputeBvh,
    RayQuery
};

enum class AccelerationFallback : std::uint8_t {
    Reject,
    ComputeBvh
};

struct AccelerationCapabilities {
    AccelerationFeatureSet features = 0;
    std::uint32_t max_triangle_geometries = 0;
    std::uint32_t max_instances = 0;
};

struct AccelerationRequest {
    AccelerationMode mode = AccelerationMode::Automatic;
    AccelerationFallback unavailable =
        AccelerationFallback::ComputeBvh;
};

struct AccelerationSelection {
    AccelerationMode mode = AccelerationMode::ComputeBvh;
    bool fallback_used = false;
};

enum class IndexFormat : std::uint8_t {
    Uint32
};

struct TriangleGeometryDesc {
    BufferHandle vertices;
    std::uint64_t vertex_offset = 0;
    std::uint32_t vertex_stride = 0;
    std::uint32_t vertex_count = 0;
    BufferHandle indices;
    std::uint64_t index_offset = 0;
    std::uint32_t index_count = 0;
    IndexFormat index_format = IndexFormat::Uint32;
    std::uint32_t geometry_index = 0;
};

struct AccelerationInstanceDesc {
    std::array<float, 12> object_to_world = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f};
    std::uint32_t instance_index = 0;
    std::uint32_t material_index = 0;
    std::uint8_t visibility_mask = 0xff;
};

struct AccelerationSceneDesc {
    TriangleGeometryDesc geometry;
    std::span<const AccelerationInstanceDesc> instances;
    std::string label;
};

struct alignas(16) AccelerationRay {
    std::array<float, 4> origin_tmin = {};
    std::array<float, 4> direction_tmax = {};
    std::array<std::uint32_t, 4> mask_flags = {
        0xff, 0, 0, 0};
};

struct alignas(16) AccelerationHit {
    std::array<float, 4> position_t = {};
    std::array<float, 4> shading_normal = {};
    std::array<float, 4> geometric_normal = {};
    std::array<float, 4> uv_barycentrics = {};
    std::array<std::uint32_t, 4> ids = {
        0xffffffffu,
        0xffffffffu,
        0xffffffffu,
        0xffffffffu};
};

AccelerationSelection select_acceleration(
    const AccelerationCapabilities& capabilities,
    const AccelerationRequest& request);
void validate(const TriangleGeometryDesc& desc);
void validate(const AccelerationSceneDesc& desc);

class AccelerationProvider {
public:
    virtual ~AccelerationProvider() = default;

    virtual AccelerationCapabilities
    acceleration_capabilities() const noexcept = 0;
    virtual AccelerationSceneHandle create_acceleration_scene(
        const AccelerationSceneDesc& desc) = 0;
    virtual void destroy(AccelerationSceneHandle scene) = 0;
};

static_assert(sizeof(AccelerationRay) == 48);
static_assert(sizeof(AccelerationHit) == 80);

}
