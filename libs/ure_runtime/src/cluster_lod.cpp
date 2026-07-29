#include "ure/runtime/cluster_lod.hpp"

#include <cmath>

namespace ure::runtime {
namespace {

bool positive_finite(float value) {
    return std::isfinite(value) && value > 0.0f;
}

ClusterGpuBoundaryRecord gpu_boundary(
    const ClusterBoundaryKey& source) {
    return {
        {source.material.namespace_id,
         source.material.local_id},
        {source.spectral.namespace_id,
         source.spectral.local_id},
        {source.displacement.namespace_id,
         source.displacement.local_id},
        {source.opacity.namespace_id,
         source.opacity.local_id},
        {source.normal_field.namespace_id,
         source.normal_field.local_id},
        source.material_slot,
        0,
        0,
        0};
}

}

void validate_cluster_lod_query(
    const ClusterLodQuery& query,
    const ClusterLodPolicy& policy) {
    const auto valid_nonnegative = [](float value) {
        return std::isfinite(value) && value >= 0.0f;
    };
    if (static_cast<std::uint32_t>(query.path_class) >
            static_cast<std::uint32_t>(
                ClusterPathClass::Caustic) ||
        !valid_nonnegative(
            query.differential.origin_radius) ||
        !valid_nonnegative(
            query.differential.direction_spread) ||
        !valid_nonnegative(query.differential.distance) ||
        !valid_nonnegative(query.material_roughness) ||
        query.material_roughness > 1.0f ||
        !valid_nonnegative(query.wavelength_span_nm) ||
        !positive_finite(policy.minimum_footprint) ||
        !positive_finite(
            policy.camera_position_fraction) ||
        !positive_finite(
            policy.diffuse_position_fraction) ||
        !positive_finite(
            policy.glossy_position_fraction_min) ||
        !positive_finite(
            policy.glossy_position_fraction_max) ||
        policy.glossy_position_fraction_min >
            policy.glossy_position_fraction_max ||
        !positive_finite(
            policy.camera_normal_radians) ||
        !positive_finite(
            policy.diffuse_normal_radians) ||
        !positive_finite(
            policy.glossy_normal_radians_min) ||
        !positive_finite(
            policy.glossy_normal_radians_max) ||
        policy.glossy_normal_radians_min >
            policy.glossy_normal_radians_max ||
        !valid_nonnegative(
            policy.camera_opacity_error) ||
        !valid_nonnegative(
            policy.diffuse_opacity_error) ||
        !valid_nonnegative(
            policy.glossy_opacity_error) ||
        !valid_nonnegative(
            policy.camera_spectral_relative) ||
        !valid_nonnegative(
            policy.diffuse_spectral_relative) ||
        !valid_nonnegative(
            policy.glossy_spectral_relative)) {
        throw Error(
            ErrorCode::InvalidArgument,
            "cluster LoD query or policy is invalid");
    }
}

ClusterLodDecision select_cluster_lod(
    const ClusteredGeometryResource& resource,
    const ClusterResidencyState& residency,
    std::uint32_t finest_cluster,
    const ClusterLodQuery& query,
    const ClusterLodPolicy& policy) {
    validate_cluster_lod_query(query, policy);
    static_cast<void>(
        validate_clustered_geometry(resource));
    static_cast<void>(
        validate_cluster_residency(resource, residency));
    if (finest_cluster >= resource.clusters.size() ||
        resource.clusters[finest_cluster].lod_level != 0) {
        throw Error(
            ErrorCode::InvalidArgument,
            "cluster LoD selection requires a finest level cluster");
    }
    ClusterLodDecision decision;
    std::uint32_t candidate = finest_cluster;
    for (std::size_t step = 0;
         step < resource.clusters.size();
         ++step) {
        const auto& cluster =
            resource.clusters[candidate];
        const auto evaluation = evaluate_cluster_lod(
            cluster.lod_error,
            gpu_boundary(
                resource.boundaries[
                    cluster.boundary_index]),
            query,
            policy);
        if (evaluation.rejection_mask !=
            ClusterLodAccept) {
            break;
        }
        const auto page_index = cluster.page_index;
        const bool resident =
            (residency.resident_pages[
                page_index / 64] &
             (std::uint64_t{1} <<
              (page_index % 64))) != 0;
        if (resident) {
            decision.cluster_index = candidate;
            decision.lod_level = cluster.lod_level;
            decision.evaluation = evaluation;
        }
        if (cluster.parent_cluster ==
            kInvalidClusterIndex) {
            break;
        }
        candidate = cluster.parent_cluster;
    }
    if (decision.cluster_index ==
        kInvalidClusterIndex) {
        throw Error(
            ErrorCode::Unsupported,
            "no physically valid resident cluster LoD is available");
    }
    return decision;
}

}
