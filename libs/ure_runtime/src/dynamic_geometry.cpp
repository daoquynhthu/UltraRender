#include "ure/runtime/dynamic_geometry.hpp"

#include <cmath>

namespace ure::runtime {
namespace {

void validate_snapshot(
    const GeometrySnapshot& snapshot) {
    if (!snapshot.id ||
        snapshot.vertex_count == 0 ||
        snapshot.index_count == 0 ||
        snapshot.index_count % 3 != 0 ||
        snapshot.topology_hash == 0 ||
        snapshot.boundary_hash == 0 ||
        snapshot.attribute_hash == 0 ||
        !std::isfinite(snapshot.maximum_displacement) ||
        snapshot.maximum_displacement < 0.0f ||
        !std::isfinite(
            snapshot.cluster_position_budget) ||
        snapshot.cluster_position_budget < 0.0f) {
        throw Error(
            ErrorCode::InvalidArgument,
            "dynamic geometry snapshot is invalid");
    }
}

void count_actions(
    GeometryUpdatePlan& plan,
    std::uint32_t actions) {
    if ((actions & GeometryUpdateBlasRefit) != 0) {
        ++plan.blas_refit_count;
    }
    if ((actions & GeometryUpdateBlasRebuild) != 0) {
        ++plan.blas_rebuild_count;
    }
    if ((actions & GeometryUpdateTlasRefit) != 0) {
        ++plan.tlas_refit_count;
    }
    if ((actions & GeometryUpdateTlasRebuild) != 0) {
        ++plan.tlas_rebuild_count;
    }
    if ((actions &
         GeometryUpdateClusterBoundsRefit) != 0) {
        ++plan.cluster_bounds_refit_count;
    }
    if ((actions & GeometryUpdateRecluster) != 0) {
        ++plan.recluster_count;
    }
}

}

GeometryMutationClass classify_geometry_mutation(
    const GeometryMutation& mutation) {
    validate_snapshot(mutation.before);
    validate_snapshot(mutation.after);
    if (mutation.before.id != mutation.after.id) {
        throw Error(
            ErrorCode::InvalidArgument,
            "dynamic geometry mutation changed resource identity");
    }
    const bool topology_changed =
        mutation.before.vertex_count !=
            mutation.after.vertex_count ||
        mutation.before.index_count !=
            mutation.after.index_count ||
        mutation.before.topology_hash !=
            mutation.after.topology_hash ||
        mutation.before.boundary_hash !=
            mutation.after.boundary_hash;
    if (topology_changed) {
        return GeometryMutationClass::TopologyChange;
    }
    if (mutation.before.attribute_hash !=
            mutation.after.attribute_hash ||
        mutation.after.maximum_displacement > 0.0f) {
        return GeometryMutationClass::Deforming;
    }
    if (mutation.transform_changed) {
        return GeometryMutationClass::Rigid;
    }
    throw Error(
        ErrorCode::InvalidArgument,
        "dynamic geometry mutation contains no change");
}

GeometryUpdatePlan plan_dynamic_geometry_updates(
    std::span<const GeometryMutation> mutations,
    AccelerationUpdatePolicy policy,
    bool clustered_geometry_enabled,
    const GeometryUpdateCapabilities& capabilities) {
    if (mutations.empty()) {
        throw Error(
            ErrorCode::InvalidArgument,
            "dynamic geometry update plan is empty");
    }
    if (policy == AccelerationUpdatePolicy::Static) {
        throw Error(
            ErrorCode::Unsupported,
            "static acceleration policy rejects geometry mutation");
    }
    GeometryUpdatePlan plan;
    for (const auto& mutation : mutations) {
        const auto mutation_class =
            classify_geometry_mutation(mutation);
        std::uint32_t actions = GeometryUpdateNone;
        switch (mutation_class) {
        case GeometryMutationClass::Rigid:
            ++plan.rigid_count;
            actions = policy ==
                    AccelerationUpdatePolicy::Rebuild
                ? GeometryUpdateTlasRebuild
                : GeometryUpdateTlasRefit;
            break;
        case GeometryMutationClass::Deforming:
            ++plan.deforming_count;
            if (policy ==
                AccelerationUpdatePolicy::Refit) {
                if (!capabilities.blas_refit) {
                    throw Error(
                        ErrorCode::Unsupported,
                        "requested deforming BLAS refit is unavailable");
                }
                actions =
                    GeometryUpdateBlasRefit |
                    GeometryUpdateTlasRefit;
            } else if (
                policy ==
                    AccelerationUpdatePolicy::Rebuild ||
                !capabilities.blas_refit) {
                actions =
                    GeometryUpdateBlasRebuild |
                    GeometryUpdateTlasRebuild;
            } else {
                actions =
                    GeometryUpdateBlasRefit |
                    GeometryUpdateTlasRefit;
            }
            if (clustered_geometry_enabled) {
                if (capabilities.cluster_bounds_refit &&
                    mutation.after.maximum_displacement <=
                        mutation.after.
                            cluster_position_budget) {
                    actions |=
                        GeometryUpdateClusterBoundsRefit;
                } else if (capabilities.recluster) {
                    actions |= GeometryUpdateRecluster;
                } else {
                    throw Error(
                        ErrorCode::Unsupported,
                        "deforming clustered geometry requires refit or recluster support");
                }
            }
            break;
        case GeometryMutationClass::TopologyChange:
            ++plan.topology_change_count;
            if (policy ==
                AccelerationUpdatePolicy::Refit) {
                throw Error(
                    ErrorCode::Unsupported,
                    "topology-changing geometry cannot use refit policy");
            }
            actions =
                GeometryUpdateBlasRebuild |
                GeometryUpdateTlasRebuild;
            if (clustered_geometry_enabled) {
                if (!capabilities.recluster) {
                    throw Error(
                        ErrorCode::Unsupported,
                        "topology-changing clustered geometry requires recluster support");
                }
                actions |= GeometryUpdateRecluster;
            }
            break;
        }
        count_actions(plan, actions);
        plan.entries.push_back({
            mutation.after.id,
            mutation_class,
            actions});
    }
    return plan;
}

void accumulate_dynamic_geometry_stats(
    DynamicGeometryStats& stats,
    const GeometryUpdatePlan& plan,
    std::uint64_t update_nanoseconds) {
    stats.rigid_update_count += plan.rigid_count;
    stats.deforming_update_count +=
        plan.deforming_count;
    stats.topology_change_count +=
        plan.topology_change_count;
    stats.blas_refit_count += plan.blas_refit_count;
    stats.blas_rebuild_count +=
        plan.blas_rebuild_count;
    stats.tlas_refit_count += plan.tlas_refit_count;
    stats.tlas_rebuild_count +=
        plan.tlas_rebuild_count;
    stats.cluster_bounds_refit_count +=
        plan.cluster_bounds_refit_count;
    stats.recluster_count += plan.recluster_count;
    stats.last_update_nanoseconds = update_nanoseconds;
    stats.total_update_nanoseconds +=
        update_nanoseconds;
}

}
