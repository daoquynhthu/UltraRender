#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "ure/render_config.hpp"
#include "ure/resource_types.hpp"
#include "ure/runtime/runtime.hpp"

namespace ure::runtime {

enum class GeometryMutationClass : std::uint32_t {
    Rigid,
    Deforming,
    TopologyChange
};

enum GeometryUpdateAction : std::uint32_t {
    GeometryUpdateNone = 0,
    GeometryUpdateTlasRefit = 1u << 0,
    GeometryUpdateTlasRebuild = 1u << 1,
    GeometryUpdateBlasRefit = 1u << 2,
    GeometryUpdateBlasRebuild = 1u << 3,
    GeometryUpdateClusterBoundsRefit = 1u << 4,
    GeometryUpdateRecluster = 1u << 5
};

struct GeometrySnapshot {
    resource::ResourceId id;
    std::uint64_t vertex_count = 0;
    std::uint64_t index_count = 0;
    std::uint64_t topology_hash = 0;
    std::uint64_t boundary_hash = 0;
    std::uint64_t attribute_hash = 0;
    float maximum_displacement = 0.0f;
    float cluster_position_budget = 0.0f;
};

struct GeometryMutation {
    GeometrySnapshot before;
    GeometrySnapshot after;
    bool transform_changed = false;
};

struct GeometryUpdateCapabilities {
    bool blas_refit = false;
    bool cluster_bounds_refit = false;
    bool recluster = false;
};

struct GeometryUpdatePlanEntry {
    resource::ResourceId resource;
    GeometryMutationClass mutation_class =
        GeometryMutationClass::Rigid;
    std::uint32_t actions = GeometryUpdateNone;
};

struct GeometryUpdatePlan {
    std::vector<GeometryUpdatePlanEntry> entries;
    std::uint32_t rigid_count = 0;
    std::uint32_t deforming_count = 0;
    std::uint32_t topology_change_count = 0;
    std::uint32_t blas_refit_count = 0;
    std::uint32_t blas_rebuild_count = 0;
    std::uint32_t tlas_refit_count = 0;
    std::uint32_t tlas_rebuild_count = 0;
    std::uint32_t cluster_bounds_refit_count = 0;
    std::uint32_t recluster_count = 0;
};

struct DynamicGeometryStats {
    std::uint64_t rigid_update_count = 0;
    std::uint64_t deforming_update_count = 0;
    std::uint64_t topology_change_count = 0;
    std::uint64_t blas_refit_count = 0;
    std::uint64_t blas_rebuild_count = 0;
    std::uint64_t tlas_refit_count = 0;
    std::uint64_t tlas_rebuild_count = 0;
    std::uint64_t cluster_bounds_refit_count = 0;
    std::uint64_t recluster_count = 0;
    std::uint64_t last_update_nanoseconds = 0;
    std::uint64_t total_update_nanoseconds = 0;
};

GeometryMutationClass classify_geometry_mutation(
    const GeometryMutation& mutation);
GeometryUpdatePlan plan_dynamic_geometry_updates(
    std::span<const GeometryMutation> mutations,
    AccelerationUpdatePolicy policy,
    bool clustered_geometry_enabled,
    const GeometryUpdateCapabilities& capabilities);
void accumulate_dynamic_geometry_stats(
    DynamicGeometryStats& stats,
    const GeometryUpdatePlan& plan,
    std::uint64_t update_nanoseconds);

}
