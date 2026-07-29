#pragma once

#include <cstdint>

#include "ure/runtime/clustered_geometry.hpp"

namespace ure::runtime {

enum class ClusterPathClass : std::uint32_t {
    Camera,
    Diffuse,
    Glossy,
    Specular,
    Shadow,
    Caustic
};

enum ClusterLodRejection : std::uint32_t {
    ClusterLodAccept = 0,
    ClusterLodRejectPosition = 1u << 0,
    ClusterLodRejectNormal = 1u << 1,
    ClusterLodRejectOpacity = 1u << 2,
    ClusterLodRejectSpectral = 1u << 3,
    ClusterLodRejectInvalid = 1u << 31
};

struct ClusterRayDifferential {
    float origin_radius = 0.0f;
    float direction_spread = 0.0f;
    float distance = 0.0f;
};

struct ClusterLodQuery {
    ClusterRayDifferential differential;
    ClusterPathClass path_class = ClusterPathClass::Camera;
    float material_roughness = 0.5f;
    float wavelength_span_nm = 0.0f;
};

struct ClusterLodPolicy {
    float minimum_footprint = 1.0e-6f;
    float camera_position_fraction = 0.25f;
    float diffuse_position_fraction = 0.5f;
    float glossy_position_fraction_min = 0.01f;
    float glossy_position_fraction_max = 0.2f;
    float camera_normal_radians = 0.2f;
    float diffuse_normal_radians = 0.5f;
    float glossy_normal_radians_min = 0.001f;
    float glossy_normal_radians_max = 0.15f;
    float camera_opacity_error = 0.01f;
    float diffuse_opacity_error = 0.025f;
    float glossy_opacity_error = 0.005f;
    float camera_spectral_relative = 0.01f;
    float diffuse_spectral_relative = 0.025f;
    float glossy_spectral_relative = 0.005f;
};

struct ClusterLodEvaluation {
    float footprint = 0.0f;
    float combined_position_error = 0.0f;
    float position_limit = 0.0f;
    float normal_limit = 0.0f;
    float opacity_limit = 0.0f;
    float spectral_limit = 0.0f;
    std::uint32_t rejection_mask = ClusterLodRejectInvalid;
};

struct ClusterLodDecision {
    std::uint32_t cluster_index = kInvalidClusterIndex;
    std::uint32_t lod_level = 0;
    ClusterLodEvaluation evaluation;
};

#if defined(__CUDACC__)
#define URE_CLUSTER_LOD_HD __host__ __device__
#else
#define URE_CLUSTER_LOD_HD
#endif

URE_CLUSTER_LOD_HD inline bool cluster_lod_finite(
    float value) {
    return value == value &&
        value >= -3.402823466e+38F &&
        value <= 3.402823466e+38F;
}

URE_CLUSTER_LOD_HD inline bool cluster_lod_resource_present(
    const ClusterGpuResourceId& id) {
    return id.namespace_id != 0 || id.local_id != 0;
}

URE_CLUSTER_LOD_HD inline bool cluster_lod_page_resident(
    const std::uint64_t* resident_pages,
    std::uint32_t resident_word_count,
    std::uint32_t page_index) {
    return resident_pages &&
        page_index / 64 < resident_word_count &&
        (resident_pages[page_index / 64] &
         (std::uint64_t{1} << (page_index % 64))) != 0;
}

URE_CLUSTER_LOD_HD inline bool cluster_lod_policy_valid(
    const ClusterLodPolicy& policy) {
    return
        cluster_lod_finite(policy.minimum_footprint) &&
        policy.minimum_footprint > 0.0f &&
        cluster_lod_finite(
            policy.camera_position_fraction) &&
        policy.camera_position_fraction > 0.0f &&
        cluster_lod_finite(
            policy.diffuse_position_fraction) &&
        policy.diffuse_position_fraction > 0.0f &&
        cluster_lod_finite(
            policy.glossy_position_fraction_min) &&
        policy.glossy_position_fraction_min > 0.0f &&
        cluster_lod_finite(
            policy.glossy_position_fraction_max) &&
        policy.glossy_position_fraction_max >=
            policy.glossy_position_fraction_min &&
        cluster_lod_finite(
            policy.camera_normal_radians) &&
        policy.camera_normal_radians > 0.0f &&
        cluster_lod_finite(
            policy.diffuse_normal_radians) &&
        policy.diffuse_normal_radians > 0.0f &&
        cluster_lod_finite(
            policy.glossy_normal_radians_min) &&
        policy.glossy_normal_radians_min > 0.0f &&
        cluster_lod_finite(
            policy.glossy_normal_radians_max) &&
        policy.glossy_normal_radians_max >=
            policy.glossy_normal_radians_min &&
        cluster_lod_finite(
            policy.camera_opacity_error) &&
        policy.camera_opacity_error >= 0.0f &&
        cluster_lod_finite(
            policy.diffuse_opacity_error) &&
        policy.diffuse_opacity_error >= 0.0f &&
        cluster_lod_finite(
            policy.glossy_opacity_error) &&
        policy.glossy_opacity_error >= 0.0f &&
        cluster_lod_finite(
            policy.camera_spectral_relative) &&
        policy.camera_spectral_relative >= 0.0f &&
        cluster_lod_finite(
            policy.diffuse_spectral_relative) &&
        policy.diffuse_spectral_relative >= 0.0f &&
        cluster_lod_finite(
            policy.glossy_spectral_relative) &&
        policy.glossy_spectral_relative >= 0.0f;
}

URE_CLUSTER_LOD_HD inline bool cluster_lod_error_valid(
    const ClusterLodError& error) {
    return cluster_lod_finite(error.position) &&
        cluster_lod_finite(error.displacement) &&
        cluster_lod_finite(error.normal_radians) &&
        cluster_lod_finite(error.opacity) &&
        cluster_lod_finite(error.spectral_relative) &&
        error.position >= 0.0f &&
        error.displacement >= 0.0f &&
        error.normal_radians >= 0.0f &&
        error.normal_radians <=
            3.14159265358979323846f &&
        error.opacity >= 0.0f &&
        error.opacity <= 1.0f &&
        error.spectral_relative >= 0.0f;
}

URE_CLUSTER_LOD_HD inline ClusterLodEvaluation
evaluate_cluster_lod(
    const ClusterLodError& error,
    const ClusterGpuBoundaryRecord& boundary,
    const ClusterLodQuery& query,
    const ClusterLodPolicy& policy = {}) {
    ClusterLodEvaluation result;
    const bool query_valid =
        static_cast<std::uint32_t>(query.path_class) <=
            static_cast<std::uint32_t>(
                ClusterPathClass::Caustic) &&
        cluster_lod_finite(
            query.differential.origin_radius) &&
        cluster_lod_finite(
            query.differential.direction_spread) &&
        cluster_lod_finite(query.differential.distance) &&
        cluster_lod_finite(query.material_roughness) &&
        cluster_lod_finite(query.wavelength_span_nm) &&
        query.differential.origin_radius >= 0.0f &&
        query.differential.direction_spread >= 0.0f &&
        query.differential.distance >= 0.0f &&
        query.material_roughness >= 0.0f &&
        query.material_roughness <= 1.0f &&
        query.wavelength_span_nm >= 0.0f &&
        cluster_lod_policy_valid(policy) &&
        cluster_lod_error_valid(error);
    if (!query_valid) {
        return result;
    }
    const float projected =
        query.differential.origin_radius +
        query.differential.direction_spread *
            query.differential.distance;
    result.footprint = projected > policy.minimum_footprint
        ? projected
        : policy.minimum_footprint;
    const float roughness = query.material_roughness;
    switch (query.path_class) {
    case ClusterPathClass::Camera:
        result.position_limit =
            result.footprint *
            policy.camera_position_fraction;
        result.normal_limit =
            policy.camera_normal_radians;
        result.opacity_limit =
            policy.camera_opacity_error;
        result.spectral_limit =
            policy.camera_spectral_relative;
        break;
    case ClusterPathClass::Diffuse:
        result.position_limit =
            result.footprint *
            policy.diffuse_position_fraction;
        result.normal_limit =
            policy.diffuse_normal_radians;
        result.opacity_limit =
            policy.diffuse_opacity_error;
        result.spectral_limit =
            policy.diffuse_spectral_relative;
        break;
    case ClusterPathClass::Glossy:
        result.position_limit =
            result.footprint *
            (policy.glossy_position_fraction_min +
             (policy.glossy_position_fraction_max -
              policy.glossy_position_fraction_min) *
                 roughness);
        result.normal_limit =
            policy.glossy_normal_radians_min +
            (policy.glossy_normal_radians_max -
             policy.glossy_normal_radians_min) *
                roughness;
        result.opacity_limit =
            policy.glossy_opacity_error * roughness;
        result.spectral_limit =
            policy.glossy_spectral_relative *
            roughness;
        break;
    case ClusterPathClass::Specular:
    case ClusterPathClass::Shadow:
    case ClusterPathClass::Caustic:
        break;
    }
    if (cluster_lod_resource_present(
            boundary.normal_field)) {
        result.normal_limit *= 0.5f;
    }
    if (cluster_lod_resource_present(
            boundary.spectral)) {
        result.spectral_limit /=
            1.0f + query.wavelength_span_nm / 100.0f;
    }
    result.combined_position_error =
        error.position + error.displacement;
    result.rejection_mask = ClusterLodAccept;
    if (result.combined_position_error >
        result.position_limit) {
        result.rejection_mask |=
            ClusterLodRejectPosition;
    }
    if (error.normal_radians >
        result.normal_limit) {
        result.rejection_mask |=
            ClusterLodRejectNormal;
    }
    if (error.opacity > result.opacity_limit) {
        result.rejection_mask |=
            ClusterLodRejectOpacity;
    }
    if (error.spectral_relative >
        result.spectral_limit) {
        result.rejection_mask |=
            ClusterLodRejectSpectral;
    }
    return result;
}

URE_CLUSTER_LOD_HD inline std::uint32_t
select_cluster_lod_gpu(
    const ClusterGpuRecord* clusters,
    std::uint32_t cluster_count,
    const ClusterGpuBoundaryRecord* boundaries,
    std::uint32_t boundary_count,
    const std::uint64_t* resident_pages,
    std::uint32_t resident_word_count,
    std::uint32_t finest_cluster,
    const ClusterLodQuery& query,
    const ClusterLodPolicy& policy = {},
    ClusterLodEvaluation* selected_evaluation = nullptr) {
    if (!clusters || !boundaries ||
        finest_cluster >= cluster_count ||
        clusters[finest_cluster].lod_level != 0) {
        return kInvalidClusterIndex;
    }
    std::uint32_t selected = kInvalidClusterIndex;
    ClusterLodEvaluation selected_result;
    std::uint32_t candidate = finest_cluster;
    for (std::uint32_t step = 0;
         step < cluster_count;
         ++step) {
        const auto& cluster = clusters[candidate];
        if (cluster.boundary_index >= boundary_count) {
            return kInvalidClusterIndex;
        }
        const ClusterLodError error{
            cluster.lod_error_primary.x,
            cluster.lod_error_primary.y,
            cluster.lod_error_primary.z,
            cluster.lod_error_primary.w,
            cluster.lod_error_secondary.x};
        const auto evaluation = evaluate_cluster_lod(
            error,
            boundaries[cluster.boundary_index],
            query,
            policy);
        if ((evaluation.rejection_mask &
             ClusterLodRejectInvalid) != 0) {
            return kInvalidClusterIndex;
        }
        if (evaluation.rejection_mask !=
            ClusterLodAccept) {
            break;
        }
        if (cluster_lod_page_resident(
                resident_pages,
                resident_word_count,
                cluster.page_index)) {
            selected = candidate;
            selected_result = evaluation;
        }
        if (cluster.parent_cluster ==
            kInvalidClusterIndex) {
            break;
        }
        if (cluster.parent_cluster >= cluster_count) {
            return kInvalidClusterIndex;
        }
        const auto& parent =
            clusters[cluster.parent_cluster];
        if (parent.lod_group != cluster.lod_group ||
            parent.boundary_index !=
                cluster.boundary_index ||
            parent.lod_level <= cluster.lod_level ||
            parent.lod_error_primary.x <
                cluster.lod_error_primary.x ||
            parent.lod_error_primary.y <
                cluster.lod_error_primary.y ||
            parent.lod_error_primary.z <
                cluster.lod_error_primary.z ||
            parent.lod_error_primary.w <
                cluster.lod_error_primary.w ||
            parent.lod_error_secondary.x <
                cluster.lod_error_secondary.x) {
            return kInvalidClusterIndex;
        }
        candidate = cluster.parent_cluster;
    }
    if (selected_evaluation) {
        *selected_evaluation = selected_result;
        if (selected == kInvalidClusterIndex) {
            selected_evaluation->rejection_mask =
                ClusterLodRejectInvalid;
        }
    }
    return selected;
}

#undef URE_CLUSTER_LOD_HD

void validate_cluster_lod_query(
    const ClusterLodQuery& query,
    const ClusterLodPolicy& policy = {});
ClusterLodDecision select_cluster_lod(
    const ClusteredGeometryResource& resource,
    const ClusterResidencyState& residency,
    std::uint32_t finest_cluster,
    const ClusterLodQuery& query,
    const ClusterLodPolicy& policy = {});

}
