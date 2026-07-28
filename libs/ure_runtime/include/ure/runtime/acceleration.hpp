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
    RayTracingPipeline = 1u << 2,
    Compaction = 1u << 3,
    Refit = 1u << 4
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
    std::uint64_t scratch_alignment = 1;
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
    std::uint32_t geometry_index = 0;
    std::uint8_t visibility_mask = 0xff;
};

enum class AccelerationBuildQuality : std::uint8_t {
    FastBuild,
    Balanced,
    HighQuality
};

enum class AccelerationUpdatePolicy : std::uint8_t {
    Static,
    Refit,
    Rebuild
};

struct AccelerationBuildConfig {
    AccelerationBuildQuality quality =
        AccelerationBuildQuality::Balanced;
    AccelerationUpdatePolicy update_policy =
        AccelerationUpdatePolicy::Static;
    bool compact = true;
    std::uint64_t scratch_budget_bytes = 0;
};

struct AccelerationSceneDesc {
    std::span<const TriangleGeometryDesc> geometries;
    std::span<const AccelerationInstanceDesc> instances;
    AccelerationBuildConfig build;
    std::string label;
};

struct AccelerationUpdateDesc {
    std::span<const AccelerationInstanceDesc> instances;
};

struct AccelerationBuildStats {
    std::uint32_t geometry_count = 0;
    std::uint32_t instance_count = 0;
    std::uint64_t build_nanoseconds = 0;
    std::uint64_t update_nanoseconds = 0;
    std::uint64_t rebuild_count = 0;
    std::uint64_t refit_count = 0;
    std::uint64_t scratch_peak_bytes = 0;
    std::uint64_t uncompacted_bytes = 0;
    std::uint64_t compacted_bytes = 0;
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
    std::array<float, 4> tangent_handedness = {};
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
void validate(
    const AccelerationSceneDesc& scene,
    const AccelerationUpdateDesc& update);

class AccelerationProvider {
public:
    virtual ~AccelerationProvider() = default;

    virtual AccelerationCapabilities
    acceleration_capabilities() const noexcept = 0;
    virtual AccelerationSceneHandle create_acceleration_scene(
        const AccelerationSceneDesc& desc) = 0;
    virtual void update_acceleration_scene(
        AccelerationSceneHandle scene,
        const AccelerationUpdateDesc& desc) = 0;
    virtual AccelerationBuildStats acceleration_build_stats(
        AccelerationSceneHandle scene) const = 0;
    virtual void destroy(AccelerationSceneHandle scene) = 0;
};

static_assert(sizeof(AccelerationRay) == 48);
static_assert(sizeof(AccelerationHit) == 96);

}
