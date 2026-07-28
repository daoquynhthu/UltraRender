#include "ure/runtime/acceleration.hpp"

#include <cmath>
#include <limits>
#include <unordered_set>

namespace ure::runtime {

AccelerationSelection select_acceleration(
    const AccelerationCapabilities& capabilities,
    const AccelerationRequest& request) {
    const bool compute = acceleration_has_features(
        capabilities.features,
        acceleration_feature_bit(AccelerationFeature::ComputeBvh));
    const bool ray_query = acceleration_has_features(
        capabilities.features,
        acceleration_feature_bit(AccelerationFeature::RayQuery));
    if (request.mode == AccelerationMode::ComputeBvh) {
        if (!compute) {
            throw Error(
                ErrorCode::Unsupported,
                "compute BVH acceleration is unavailable");
        }
        return {AccelerationMode::ComputeBvh, false};
    }
    if (request.mode == AccelerationMode::RayQuery) {
        if (ray_query) {
            return {AccelerationMode::RayQuery, false};
        }
        if (request.unavailable == AccelerationFallback::ComputeBvh &&
            compute) {
            return {AccelerationMode::ComputeBvh, true};
        }
        throw Error(
            ErrorCode::Unsupported,
            "ray query acceleration is unavailable");
    }
    if (ray_query) {
        return {AccelerationMode::RayQuery, false};
    }
    if (request.unavailable == AccelerationFallback::ComputeBvh &&
        compute) {
        return {AccelerationMode::ComputeBvh, true};
    }
    throw Error(
        ErrorCode::Unsupported,
        "no compatible acceleration provider is available");
}

void validate(const TriangleGeometryDesc& desc) {
    if (!desc.vertices || !desc.indices) {
        throw Error(
            ErrorCode::InvalidHandle,
            "acceleration geometry buffer handle is invalid");
    }
    if (desc.vertex_stride < sizeof(float) * 3 ||
        desc.vertex_stride % sizeof(float) != 0 ||
        desc.vertex_count < 3) {
        throw Error(
            ErrorCode::InvalidArgument,
            "acceleration vertex layout is invalid");
    }
    if (desc.vertex_offset % alignof(float) != 0 ||
        desc.index_offset % alignof(std::uint32_t) != 0) {
        throw Error(
            ErrorCode::InvalidArgument,
            "acceleration geometry offset is misaligned");
    }
    if (desc.index_count == 0 || desc.index_count % 3 != 0) {
        throw Error(
            ErrorCode::InvalidArgument,
            "acceleration index count is invalid");
    }
}

void validate(const AccelerationSceneDesc& desc) {
    if (desc.geometries.empty()) {
        throw Error(
            ErrorCode::InvalidArgument,
            "acceleration scene has no geometries");
    }
    if (desc.geometries.size() >
        std::numeric_limits<std::uint32_t>::max()) {
        throw Error(
            ErrorCode::Overflow,
            "acceleration geometry count exceeds contract limit");
    }
    if (desc.build.quality >
            AccelerationBuildQuality::HighQuality ||
        desc.build.update_policy >
            AccelerationUpdatePolicy::Rebuild) {
        throw Error(
            ErrorCode::InvalidArgument,
            "acceleration build policy is invalid");
    }
    std::unordered_set<std::uint32_t> geometry_indices;
    for (const auto& geometry : desc.geometries) {
        validate(geometry);
        if (!geometry_indices.insert(
                geometry.geometry_index).second) {
            throw Error(
                ErrorCode::InvalidArgument,
                "acceleration geometry index is duplicated");
        }
    }
    if (desc.instances.empty()) {
        throw Error(
            ErrorCode::InvalidArgument,
            "acceleration scene has no instances");
    }
    std::unordered_set<std::uint32_t> instance_indices;
    for (const auto& instance : desc.instances) {
        if (instance.geometry_index >= desc.geometries.size()) {
            throw Error(
                ErrorCode::InvalidArgument,
                "acceleration instance geometry index is invalid");
        }
        if (instance.visibility_mask == 0) {
            throw Error(
                ErrorCode::InvalidArgument,
                "acceleration instance visibility mask is zero");
        }
        if (!instance_indices.insert(instance.instance_index).second) {
            throw Error(
                ErrorCode::InvalidArgument,
                "acceleration instance index is duplicated");
        }
        for (const auto value : instance.object_to_world) {
            if (!std::isfinite(value)) {
                throw Error(
                    ErrorCode::InvalidArgument,
                    "acceleration instance transform is non-finite");
            }
        }
        const auto& transform = instance.object_to_world;
        const double determinant =
            static_cast<double>(transform[0]) *
                (static_cast<double>(transform[5]) * transform[10] -
                 static_cast<double>(transform[6]) * transform[9]) -
            static_cast<double>(transform[1]) *
                (static_cast<double>(transform[4]) * transform[10] -
                 static_cast<double>(transform[6]) * transform[8]) +
            static_cast<double>(transform[2]) *
                (static_cast<double>(transform[4]) * transform[9] -
                 static_cast<double>(transform[5]) * transform[8]);
        if (determinant == 0.0) {
            throw Error(
                ErrorCode::InvalidArgument,
                "acceleration instance transform is singular");
        }
    }
}

void validate(
    const AccelerationSceneDesc& scene,
    const AccelerationUpdateDesc& update) {
    if (update.instances.size() != scene.instances.size()) {
        throw Error(
            ErrorCode::InvalidArgument,
            "acceleration update changes instance count");
    }
    AccelerationSceneDesc updated = scene;
    updated.instances = update.instances;
    validate(updated);
    for (std::size_t index = 0;
         index < update.instances.size();
         ++index) {
        const auto& before = scene.instances[index];
        const auto& after = update.instances[index];
        if (before.instance_index != after.instance_index ||
            before.material_index != after.material_index ||
            before.geometry_index != after.geometry_index) {
            throw Error(
                ErrorCode::InvalidArgument,
                "acceleration update changes instance topology");
        }
    }
}

}
