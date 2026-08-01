#include "ure/reconstruction/portfolio_measurement.hpp"

#include <algorithm>
#include <stdexcept>

namespace ure::reconstruction {
namespace {

bool compatible_identity(const semantic::IdentityDigest& supplied,
                         const semantic::IdentityDigest& required) {
    return semantic::identity_empty(supplied) || supplied == required;
}

}

MeasurementProvenance make_portfolio_measurement_provenance(
    const transport::PortfolioSchedule& schedule,
    const transport::PortfolioScheduleShard& shard,
    std::size_t slice_ordinal,
    semantic::ProvenanceIdentitySet identities) {
    if (!transport::validate_portfolio_schedule(schedule).ok() ||
        !transport::validate_portfolio_schedule_shard(schedule, shard) ||
        slice_ordinal >= shard.slices.size() ||
        semantic::identity_empty(identities.world_definition) ||
        semantic::identity_empty(identities.time_sample) ||
        !compatible_identity(
            identities.world_state, schedule.world_state_identity) ||
        !compatible_identity(
            identities.observation_snapshot,
            schedule.observation_snapshot_identity) ||
        !compatible_identity(
            identities.technique_graph,
            schedule.technique_graph_identity)) {
        throw std::invalid_argument(
            "Invalid portfolio measurement provenance inputs");
    }
    const auto& slice = shard.slices[slice_ordinal];
    const auto allocation = std::ranges::find_if(
        schedule.allocations,
        [&slice](const transport::PortfolioAllocation& value) {
            return value.candidate_identity == slice.candidate_identity;
        });
    if (allocation == schedule.allocations.end()) {
        throw std::invalid_argument("Portfolio allocation is missing");
    }
    const auto domain = std::ranges::find_if(
        schedule.domains,
        [&allocation](const transport::PortfolioWorkDomain& value) {
            return value.domain_identity == allocation->domain_identity;
        });
    if (domain == schedule.domains.end()) {
        throw std::invalid_argument("Portfolio work domain is missing");
    }
    identities.world_state = schedule.world_state_identity;
    identities.observation_snapshot =
        schedule.observation_snapshot_identity;
    identities.technique_graph = schedule.technique_graph_identity;
    MeasurementProvenance result;
    result.identities = identities;
    result.exposure = domain->time_interval;
    result.portfolio_schedule_identity = schedule.schedule_identity;
    result.sample_namespace_identity = slice.sample_namespace_identity;
    result.producer_identity = shard.shard_identity;
    result.sample_ranges.push_back({slice.sample_begin, slice.sample_count});
    return result;
}

}
