#include "ure/transport/portfolio.hpp"

#include "ure/runtime/multi_backend.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

namespace ure::transport {
namespace {

class Encoder {
public:
    void u8(std::uint8_t value) {
        bytes_.push_back(static_cast<std::byte>(value));
    }
    void u32(std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) {
            u8(static_cast<std::uint8_t>(value >> shift));
        }
    }
    void u64(std::uint64_t value) {
        for (int shift = 0; shift < 64; shift += 8) {
            u8(static_cast<std::uint8_t>(value >> shift));
        }
    }
    void digest(const semantic::IdentityDigest& value) {
        for (const auto byte : value) u8(byte);
    }
    std::span<const std::byte> bytes() const { return bytes_; }

private:
    std::vector<std::byte> bytes_;
};

bool add_overflow(std::uint64_t left, std::uint64_t right,
                  std::uint64_t& result) {
    if (left > std::numeric_limits<std::uint64_t>::max() - right) {
        return true;
    }
    result = left + right;
    return false;
}

bool multiply_overflow(std::uint64_t left, std::uint64_t right,
                       std::uint64_t& result) {
    if (left != 0 &&
        right > std::numeric_limits<std::uint64_t>::max() / left) {
        return true;
    }
    result = left * right;
    return false;
}

bool identity_set_valid(
    std::span<const semantic::IdentityDigest> identities) {
    if (identities.empty() ||
        std::ranges::any_of(identities, semantic::identity_empty)) {
        return false;
    }
    std::set<semantic::IdentityDigest> unique(
        identities.begin(), identities.end());
    return unique.size() == identities.size();
}

bool contains(std::span<const semantic::IdentityDigest> identities,
              const semantic::IdentityDigest& identity) {
    return std::ranges::find(identities, identity) != identities.end();
}

bool valid_worker(const PortfolioWorkerDescriptor& worker) {
    return !semantic::identity_empty(worker.worker_identity) &&
        !semantic::identity_empty(worker.executable_identity) &&
        identity_set_valid(worker.device_identities) &&
        identity_set_valid(worker.execution_semantics_identities);
}

semantic::IdentityDigest shard_identity(
    const PortfolioScheduleShard& shard) {
    Encoder encoder;
    encoder.u32(shard.version);
    encoder.digest(shard.schedule_identity);
    encoder.digest(shard.technique_graph_identity);
    encoder.digest(shard.composition_plan_identity);
    encoder.digest(shard.pilot_provenance_identity);
    encoder.digest(shard.world_state_identity);
    encoder.digest(shard.observation_snapshot_identity);
    encoder.digest(shard.worker.worker_identity);
    encoder.digest(shard.worker.executable_identity);
    auto devices = shard.worker.device_identities;
    auto semantics = shard.worker.execution_semantics_identities;
    std::ranges::sort(devices);
    std::ranges::sort(semantics);
    encoder.u32(static_cast<std::uint32_t>(devices.size()));
    for (const auto& identity : devices) encoder.digest(identity);
    encoder.u32(static_cast<std::uint32_t>(semantics.size()));
    for (const auto& identity : semantics) encoder.digest(identity);
    encoder.u32(static_cast<std::uint32_t>(shard.slices.size()));
    for (const auto& slice : shard.slices) {
        encoder.digest(slice.candidate_identity);
        encoder.digest(slice.sample_namespace_identity);
        encoder.digest(slice.chain_namespace_identity);
        encoder.u64(slice.sample_begin);
        encoder.u64(slice.sample_count);
        encoder.u64(slice.chain_begin);
        encoder.u64(slice.chain_count);
    }
    return runtime::identity_digest(encoder.bytes());
}

semantic::IdentityDigest coverage_identity(
    const PortfolioCoverageReport& report) {
    Encoder encoder;
    encoder.u32(report.version);
    encoder.digest(report.schedule_identity);
    encoder.digest(report.shard_set_identity);
    encoder.u8(report.complete ? 1 : 0);
    encoder.u32(static_cast<std::uint32_t>(report.issues.size()));
    for (const auto issue : report.issues) {
        encoder.u8(static_cast<std::uint8_t>(issue));
    }
    return runtime::identity_digest(encoder.bytes());
}

semantic::IdentityDigest shard_set_identity(
    std::span<const PortfolioScheduleShard> shards) {
    std::vector<semantic::IdentityDigest> identities;
    for (const auto& shard : shards) identities.push_back(shard.shard_identity);
    std::ranges::sort(identities);
    Encoder encoder;
    encoder.u32(static_cast<std::uint32_t>(identities.size()));
    for (const auto& identity : identities) encoder.digest(identity);
    return runtime::identity_digest(encoder.bytes());
}

template <typename Issue>
void add(std::vector<Issue>& issues, Issue issue) {
    if (std::ranges::find(issues, issue) == issues.end()) {
        issues.push_back(issue);
    }
}

struct Interval {
    std::uint64_t begin = 0;
    std::uint64_t end = 0;
};

bool exact_coverage(std::vector<Interval> intervals,
                    std::uint64_t begin, std::uint64_t count,
                    bool& overlap) {
    std::uint64_t expected_end = 0;
    if (add_overflow(begin, count, expected_end)) return false;
    std::ranges::sort(intervals, {}, &Interval::begin);
    auto cursor = begin;
    for (const auto& interval : intervals) {
        if (interval.begin < cursor) {
            overlap = true;
            return false;
        }
        if (interval.begin != cursor || interval.end < interval.begin) {
            return false;
        }
        cursor = interval.end;
    }
    return cursor == expected_end;
}

}

PortfolioScheduleShard make_portfolio_schedule_shard(
    const PortfolioSchedule& schedule,
    const PortfolioWorkerDescriptor& worker,
    std::span<const PortfolioShardSlice> slices) {
    if (!validate_portfolio_schedule(schedule).ok() ||
        !valid_worker(worker) || slices.empty()) {
        throw std::invalid_argument("Invalid portfolio shard inputs");
    }
    PortfolioScheduleShard result;
    result.schedule_identity = schedule.schedule_identity;
    result.technique_graph_identity = schedule.technique_graph_identity;
    result.composition_plan_identity = schedule.composition_plan_identity;
    result.pilot_provenance_identity =
        schedule.pilot_provenance_identity;
    result.world_state_identity = schedule.world_state_identity;
    result.observation_snapshot_identity =
        schedule.observation_snapshot_identity;
    result.worker = worker;
    result.slices.assign(slices.begin(), slices.end());
    std::ranges::sort(
        result.slices,
        [](const PortfolioShardSlice& left,
           const PortfolioShardSlice& right) {
            if (left.candidate_identity != right.candidate_identity) {
                return left.candidate_identity < right.candidate_identity;
            }
            return left.sample_begin < right.sample_begin;
        });
    result.shard_identity = shard_identity(result);
    if (!validate_portfolio_schedule_shard(schedule, result)) {
        throw std::invalid_argument("Invalid portfolio shard");
    }
    return result;
}

bool validate_portfolio_schedule_shard(
    const PortfolioSchedule& schedule,
    const PortfolioScheduleShard& shard) {
    if (!validate_portfolio_schedule(schedule).ok() ||
        shard.version != kPortfolioContractVersion ||
        semantic::identity_empty(shard.shard_identity) ||
        shard.schedule_identity != schedule.schedule_identity ||
        shard.technique_graph_identity !=
            schedule.technique_graph_identity ||
        shard.composition_plan_identity !=
            schedule.composition_plan_identity ||
        shard.pilot_provenance_identity !=
            schedule.pilot_provenance_identity ||
        shard.world_state_identity != schedule.world_state_identity ||
        shard.observation_snapshot_identity !=
            schedule.observation_snapshot_identity ||
        !valid_worker(shard.worker) || shard.slices.empty() ||
        shard.shard_identity != shard_identity(shard)) {
        return false;
    }
    std::map<semantic::IdentityDigest, PortfolioAllocation> allocations;
    for (const auto& allocation : schedule.allocations) {
        allocations.emplace(allocation.candidate_identity, allocation);
    }
    std::map<semantic::IdentityDigest, PortfolioWorkDomain> domains;
    for (const auto& domain : schedule.domains) {
        domains.emplace(domain.domain_identity, domain);
    }
    std::map<semantic::IdentityDigest, std::vector<Interval>> samples;
    std::map<semantic::IdentityDigest, std::vector<Interval>> chains;
    for (const auto& slice : shard.slices) {
        const auto allocation = allocations.find(slice.candidate_identity);
        if (allocation == allocations.end() || slice.sample_count == 0 ||
            slice.sample_namespace_identity !=
                allocation->second.sample_namespace_identity ||
            slice.chain_namespace_identity !=
                allocation->second.chain_namespace_identity ||
            !contains(
                shard.worker.execution_semantics_identities,
                allocation->second.execution_semantics_identity)) {
            return false;
        }
        const auto domain = domains.find(allocation->second.domain_identity);
        if (domain == domains.end() ||
            !contains(
                shard.worker.device_identities,
                domain->second.device_identity)) {
            return false;
        }
        std::uint64_t sample_end = 0;
        std::uint64_t allocation_sample_end = 0;
        std::uint64_t chain_end = 0;
        std::uint64_t allocation_chain_end = 0;
        if (add_overflow(
                slice.sample_begin, slice.sample_count, sample_end) ||
            add_overflow(
                allocation->second.sample_begin,
                allocation->second.sample_count,
                allocation_sample_end) ||
            slice.sample_begin < allocation->second.sample_begin ||
            sample_end > allocation_sample_end ||
            add_overflow(
                slice.chain_begin, slice.chain_count, chain_end) ||
            add_overflow(
                allocation->second.chain_begin,
                allocation->second.chain_count,
                allocation_chain_end) ||
            slice.chain_begin < allocation->second.chain_begin ||
            chain_end > allocation_chain_end ||
            (allocation->second.chain_count == 0 &&
             slice.chain_count != 0) ||
            (allocation->second.chain_count != 0 &&
             slice.chain_count == 0)) {
            return false;
        }
        if (allocation->second.chain_count != 0) {
            if (allocation->second.sample_count %
                    allocation->second.chain_count != 0) {
                return false;
            }
            const auto samples_per_chain =
                allocation->second.sample_count /
                allocation->second.chain_count;
            std::uint64_t expected_sample_count = 0;
            std::uint64_t chain_offset_samples = 0;
            std::uint64_t expected_sample_begin = 0;
            if (slice.chain_begin < allocation->second.chain_begin ||
                multiply_overflow(
                    slice.chain_count, samples_per_chain,
                    expected_sample_count) ||
                multiply_overflow(
                    slice.chain_begin - allocation->second.chain_begin,
                    samples_per_chain, chain_offset_samples) ||
                add_overflow(
                    allocation->second.sample_begin,
                    chain_offset_samples, expected_sample_begin) ||
                slice.sample_count != expected_sample_count ||
                slice.sample_begin != expected_sample_begin) {
                return false;
            }
        }
        samples[slice.candidate_identity].push_back(
            {slice.sample_begin, sample_end});
        if (slice.chain_count != 0) {
            chains[slice.candidate_identity].push_back(
                {slice.chain_begin, chain_end});
        }
    }
    for (const auto& [candidate, intervals] : samples) {
        auto ordered = intervals;
        std::ranges::sort(ordered, {}, &Interval::begin);
        for (std::size_t index = 1; index < ordered.size(); ++index) {
            if (ordered[index].begin < ordered[index - 1].end) return false;
        }
        if (chains.contains(candidate)) {
            auto ordered_chains = chains.at(candidate);
            std::ranges::sort(ordered_chains, {}, &Interval::begin);
            for (std::size_t index = 1;
                 index < ordered_chains.size(); ++index) {
                if (ordered_chains[index].begin <
                    ordered_chains[index - 1].end) {
                    return false;
                }
            }
        }
    }
    return true;
}

PortfolioCoverageReport validate_portfolio_shard_coverage(
    const PortfolioSchedule& schedule,
    std::span<const PortfolioScheduleShard> shards) {
    PortfolioCoverageReport result;
    result.schedule_identity = schedule.schedule_identity;
    result.shard_set_identity = shard_set_identity(shards);
    if (!validate_portfolio_schedule(schedule).ok()) {
        add(result.issues, PortfolioCoverageIssue::Schedule);
        result.report_identity = coverage_identity(result);
        return result;
    }
    std::set<semantic::IdentityDigest> shard_identities;
    std::map<semantic::IdentityDigest, std::vector<Interval>> samples;
    std::map<semantic::IdentityDigest, std::vector<Interval>> chains;
    for (const auto& shard : shards) {
        if (!validate_portfolio_schedule_shard(schedule, shard)) {
            add(result.issues, PortfolioCoverageIssue::Shard);
            continue;
        }
        if (!shard_identities.insert(shard.shard_identity).second) {
            add(result.issues, PortfolioCoverageIssue::DuplicateShard);
            continue;
        }
        for (const auto& slice : shard.slices) {
            std::uint64_t sample_end = 0;
            std::uint64_t chain_end = 0;
            if (add_overflow(
                    slice.sample_begin, slice.sample_count, sample_end) ||
                add_overflow(
                    slice.chain_begin, slice.chain_count, chain_end)) {
                add(result.issues, PortfolioCoverageIssue::OutsideAllocation);
                continue;
            }
            samples[slice.candidate_identity].push_back(
                {slice.sample_begin, sample_end});
            if (slice.chain_count != 0) {
                chains[slice.candidate_identity].push_back(
                    {slice.chain_begin, chain_end});
            }
        }
    }
    for (const auto& allocation : schedule.allocations) {
        bool sample_overlap = false;
        if (!exact_coverage(
                samples[allocation.candidate_identity],
                allocation.sample_begin, allocation.sample_count,
                sample_overlap)) {
            add(result.issues, sample_overlap
                ? PortfolioCoverageIssue::Overlap
                : PortfolioCoverageIssue::MissingCoverage);
        }
        if (allocation.chain_count != 0) {
            bool chain_overlap = false;
            if (!exact_coverage(
                    chains[allocation.candidate_identity],
                    allocation.chain_begin, allocation.chain_count,
                    chain_overlap)) {
                add(result.issues, chain_overlap
                    ? PortfolioCoverageIssue::Overlap
                    : PortfolioCoverageIssue::MissingCoverage);
            }
        } else if (!chains[allocation.candidate_identity].empty()) {
            add(result.issues, PortfolioCoverageIssue::OutsideAllocation);
        }
    }
    result.complete = result.issues.empty() && !shards.empty();
    result.report_identity = coverage_identity(result);
    return result;
}

bool validate_portfolio_coverage_report(
    const PortfolioCoverageReport& report) {
    if (report.version != kPortfolioContractVersion ||
        semantic::identity_empty(report.report_identity) ||
        semantic::identity_empty(report.schedule_identity) ||
        semantic::identity_empty(report.shard_set_identity) ||
        report.complete != report.issues.empty()) {
        return false;
    }
    std::set<PortfolioCoverageIssue> unique;
    for (const auto issue : report.issues) {
        if (issue < PortfolioCoverageIssue::Schedule ||
            issue > PortfolioCoverageIssue::OutsideAllocation ||
            !unique.insert(issue).second) {
            return false;
        }
    }
    return report.report_identity == coverage_identity(report);
}

}
