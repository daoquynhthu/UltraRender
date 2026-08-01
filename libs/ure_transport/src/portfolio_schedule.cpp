#include "ure/transport/portfolio.hpp"

#include "ure/runtime/multi_backend.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
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
    void i64(std::int64_t value) {
        u64(std::bit_cast<std::uint64_t>(value));
    }
    void f64(double value) {
        u64(std::bit_cast<std::uint64_t>(value));
    }
    void digest(const semantic::IdentityDigest& value) {
        for (const auto byte : value) u8(byte);
    }
    std::span<const std::byte> bytes() const { return bytes_; }

private:
    std::vector<std::byte> bytes_;
};

template <typename Issue>
void add(std::vector<Issue>& issues, Issue issue) {
    if (std::ranges::find(issues, issue) == issues.end()) {
        issues.push_back(issue);
    }
}

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

std::uint64_t round_up(std::uint64_t value, std::uint64_t quantum) {
    const auto remainder = value % quantum;
    if (remainder == 0) return value;
    std::uint64_t result = 0;
    if (add_overflow(value, quantum - remainder, result)) {
        throw std::overflow_error("Portfolio sample rounding overflow");
    }
    return result;
}

bool valid_domain(const PortfolioWorkDomain& domain) {
    const auto& tile = domain.tile;
    std::uint64_t tile_x_end = 0;
    std::uint64_t tile_y_end = 0;
    std::uint64_t wavelength_end = 0;
    return domain.version == kPortfolioContractVersion &&
        !semantic::identity_empty(domain.domain_identity) &&
        domain.domain_identity ==
            compute_portfolio_work_domain_identity(domain) &&
        !semantic::identity_empty(domain.spectral_domain_identity) &&
        !semantic::identity_empty(domain.device_identity) &&
        !semantic::identity_empty(domain.sample_namespace_identity) &&
        tile.image_width != 0 && tile.image_height != 0 &&
        tile.width != 0 && tile.height != 0 &&
        !add_overflow(tile.x, tile.width, tile_x_end) &&
        !add_overflow(tile.y, tile.height, tile_y_end) &&
        tile_x_end <= tile.image_width &&
        tile_y_end <= tile.image_height &&
        domain.wavelength_count != 0 &&
        !add_overflow(
            domain.wavelength_begin, domain.wavelength_count,
            wavelength_end) &&
        semantic::valid_time_interval(domain.time_interval);
}

bool valid_budget(const PortfolioBudget& budget) {
    return budget.total_nanoseconds != 0 &&
        budget.resident_bytes != 0 &&
        budget.scratch_bytes != 0 &&
        budget.maximum_samples != 0 &&
        budget.maximum_allocations != 0;
}

bool ranges_cover(std::span<const PilotSampleRange> ranges,
                  std::uint64_t begin, std::uint64_t count) {
    std::uint64_t end = 0;
    if (count == 0 || add_overflow(begin, count, end)) return false;
    auto cursor = begin;
    for (const auto& range : ranges) {
        std::uint64_t range_end = 0;
        if (add_overflow(range.start, range.count, range_end)) return false;
        if (range_end <= cursor) continue;
        if (range.start > cursor) return false;
        cursor = std::min(end, range_end);
        if (cursor == end) return true;
    }
    return false;
}

bool valid_policy(const PortfolioPolicy& policy) {
    return policy.version == kPortfolioContractVersion &&
        !semantic::identity_empty(policy.policy_identity) &&
        policy.policy_identity == compute_portfolio_policy_identity(policy) &&
        std::isfinite(policy.exploration_budget_fraction) &&
        policy.exploration_budget_fraction > 0.0 &&
        policy.exploration_budget_fraction <= 1.0 &&
        std::isfinite(policy.tail_risk_weight) &&
        policy.tail_risk_weight >= 0.0 &&
        std::isfinite(policy.minimum_gain_per_nanosecond) &&
        policy.minimum_gain_per_nanosecond >= 0.0 &&
        policy.maximum_greedy_iterations != 0;
}

bool valid_candidate(const PortfolioCandidate& candidate) {
    std::uint64_t sample_end = 0;
    std::uint64_t chain_end = 0;
    return candidate.version == kPortfolioContractVersion &&
        !semantic::identity_empty(candidate.candidate_identity) &&
        candidate.candidate_identity ==
            compute_portfolio_candidate_identity(candidate) &&
        !semantic::identity_empty(candidate.domain_identity) &&
        !semantic::identity_empty(candidate.estimate_identity) &&
        !semantic::identity_empty(
            candidate.execution_semantics_identity) &&
        candidate.layer >= EstimateLayer::Unbiased &&
        candidate.layer <= EstimateLayer::Research &&
        std::isfinite(candidate.aggregation_coefficient) &&
        candidate.aggregation_coefficient != 0.0 &&
        std::isfinite(candidate.projected_variance) &&
        candidate.projected_variance > 0.0 &&
        std::isfinite(candidate.projected_tail_second_moment) &&
        candidate.projected_tail_second_moment >= 0.0 &&
        std::isfinite(candidate.effective_sample_fraction) &&
        candidate.effective_sample_fraction > 0.0 &&
        candidate.effective_sample_fraction <= 1.0 &&
        candidate.nanoseconds_per_sample != 0 &&
        candidate.sample_quantum != 0 &&
        candidate.minimum_exploration_samples != 0 &&
        candidate.maximum_samples >=
            candidate.minimum_exploration_samples &&
        candidate.starvation_epoch_limit != 0 &&
        candidate.starvation_recovery_samples != 0 &&
        (candidate.chains_per_quantum == 0 ||
         (!semantic::identity_empty(
              candidate.execution_semantics_identity) &&
          candidate.sample_quantum % candidate.chains_per_quantum == 0)) &&
        !add_overflow(
            candidate.next_sample_index, candidate.maximum_samples,
            sample_end) &&
        !multiply_overflow(
            candidate.maximum_samples / candidate.sample_quantum,
            candidate.chains_per_quantum, chain_end) &&
        !add_overflow(candidate.next_chain_index, chain_end, chain_end);
}

double adjusted_variance(const PortfolioCandidate& candidate,
                         const PortfolioPolicy& policy) {
    return (candidate.projected_variance +
            policy.tail_risk_weight *
                candidate.projected_tail_second_moment) /
        candidate.effective_sample_fraction;
}

double predicted_variance(
    std::span<const PortfolioCandidate> candidates,
    std::span<const PortfolioCovarianceEdge> edges,
    const PortfolioPolicy& policy,
    std::span<const std::uint64_t> sample_counts,
    const std::map<semantic::IdentityDigest, std::size_t>& indices) {
    double result = 0.0;
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        const auto samples = static_cast<double>(sample_counts[index]);
        if (samples <= 0.0) return std::numeric_limits<double>::infinity();
        const auto coefficient =
            candidates[index].aggregation_coefficient;
        result += coefficient * coefficient *
            adjusted_variance(candidates[index], policy) / samples;
    }
    for (const auto& edge : edges) {
        const auto left = indices.at(edge.left_candidate_identity);
        const auto right = indices.at(edge.right_candidate_identity);
        const auto left_samples =
            static_cast<double>(sample_counts[left]);
        const auto right_samples =
            static_cast<double>(sample_counts[right]);
        const auto paired = std::min(left_samples, right_samples);
        result += 2.0 * candidates[left].aggregation_coefficient *
            candidates[right].aggregation_coefficient *
            edge.projected_covariance * paired /
            (left_samples * right_samples);
    }
    if (!std::isfinite(result) || result < -1e-12) {
        throw std::runtime_error("Invalid portfolio variance objective");
    }
    return std::max(0.0, result);
}

bool covariance_psd(
    std::span<const PortfolioCandidate> candidates,
    std::span<const PortfolioCovarianceEdge> edges,
    const std::map<semantic::IdentityDigest, std::size_t>& indices) {
    const auto count = candidates.size();
    std::vector<double> matrix(count * count, 0.0);
    for (std::size_t index = 0; index < count; ++index) {
        matrix[index * count + index] =
            candidates[index].projected_variance;
    }
    for (const auto& edge : edges) {
        const auto left = indices.at(edge.left_candidate_identity);
        const auto right = indices.at(edge.right_candidate_identity);
        matrix[left * count + right] = edge.projected_covariance;
        matrix[right * count + left] = edge.projected_covariance;
    }
    std::vector<double> lower(count * count, 0.0);
    for (std::size_t row = 0; row < count; ++row) {
        for (std::size_t column = 0; column <= row; ++column) {
            double value = matrix[row * count + column];
            for (std::size_t inner = 0; inner < column; ++inner) {
                value -= lower[row * count + inner] *
                    lower[column * count + inner];
            }
            const auto tolerance = 1e-10 * std::max(
                1.0, std::abs(matrix[row * count + row]));
            if (row == column) {
                if (value < -tolerance) return false;
                lower[row * count + column] =
                    std::sqrt(std::max(0.0, value));
            } else if (lower[column * count + column] > tolerance) {
                lower[row * count + column] =
                    value / lower[column * count + column];
            } else if (std::abs(value) > tolerance) {
                return false;
            }
        }
    }
    return true;
}

semantic::IdentityDigest candidate_set_identity(
    std::span<const PortfolioCandidate> candidates) {
    std::vector<semantic::IdentityDigest> identities;
    for (const auto& candidate : candidates) {
        identities.push_back(candidate.candidate_identity);
    }
    std::ranges::sort(identities);
    Encoder encoder;
    encoder.u32(static_cast<std::uint32_t>(identities.size()));
    for (const auto& identity : identities) encoder.digest(identity);
    return runtime::identity_digest(encoder.bytes());
}

semantic::IdentityDigest allocation_candidate_set_identity(
    std::span<const PortfolioAllocation> allocations) {
    std::vector<semantic::IdentityDigest> identities;
    for (const auto& allocation : allocations) {
        identities.push_back(allocation.candidate_identity);
    }
    std::ranges::sort(identities);
    Encoder encoder;
    encoder.u32(static_cast<std::uint32_t>(identities.size()));
    for (const auto& identity : identities) encoder.digest(identity);
    return runtime::identity_digest(encoder.bytes());
}

semantic::IdentityDigest covariance_set_identity(
    std::span<const PortfolioCovarianceEdge> edges) {
    std::vector<PortfolioCovarianceEdge> ordered(edges.begin(), edges.end());
    std::ranges::sort(
        ordered,
        [](const PortfolioCovarianceEdge& left,
           const PortfolioCovarianceEdge& right) {
            if (left.left_candidate_identity !=
                right.left_candidate_identity) {
                return left.left_candidate_identity <
                    right.left_candidate_identity;
            }
            return left.right_candidate_identity <
                right.right_candidate_identity;
        });
    Encoder encoder;
    encoder.u32(static_cast<std::uint32_t>(ordered.size()));
    for (const auto& edge : ordered) {
        encoder.u32(edge.version);
        encoder.digest(edge.left_candidate_identity);
        encoder.digest(edge.right_candidate_identity);
        encoder.digest(edge.covariance_identity);
        encoder.digest(edge.pairing_identity);
        encoder.f64(edge.projected_covariance);
    }
    return runtime::identity_digest(encoder.bytes());
}

semantic::IdentityDigest compute_schedule_identity(
    const PortfolioSchedule& schedule) {
    Encoder encoder;
    encoder.u32(schedule.version);
    encoder.digest(schedule.technique_graph_identity);
    encoder.digest(schedule.composition_plan_identity);
    encoder.digest(schedule.qualification_report_identity);
    encoder.digest(schedule.pilot_provenance_identity);
    encoder.digest(schedule.world_state_identity);
    encoder.digest(schedule.observation_snapshot_identity);
    encoder.digest(schedule.policy_identity);
    encoder.digest(schedule.candidate_set_identity);
    encoder.digest(schedule.covariance_set_identity);
    encoder.u64(schedule.epoch);
    encoder.u64(schedule.budget.total_nanoseconds);
    encoder.u64(schedule.budget.resident_bytes);
    encoder.u64(schedule.budget.scratch_bytes);
    encoder.u64(schedule.budget.maximum_samples);
    encoder.u32(schedule.budget.maximum_allocations);
    encoder.u64(schedule.spent_nanoseconds);
    encoder.u64(schedule.reserved_resident_bytes);
    encoder.u64(schedule.reserved_scratch_bytes);
    encoder.f64(schedule.variance_at_exploration_floor);
    encoder.f64(schedule.predicted_variance);
    encoder.u32(static_cast<std::uint32_t>(schedule.domains.size()));
    for (const auto& domain : schedule.domains) {
        encoder.digest(domain.domain_identity);
    }
    encoder.u32(static_cast<std::uint32_t>(schedule.allocations.size()));
    for (const auto& allocation : schedule.allocations) {
        encoder.digest(allocation.candidate_identity);
        encoder.digest(allocation.domain_identity);
        encoder.digest(allocation.sample_namespace_identity);
        encoder.digest(allocation.chain_namespace_identity);
        encoder.digest(allocation.execution_semantics_identity);
        encoder.u32(allocation.node_ordinal);
        encoder.u64(allocation.sample_begin);
        encoder.u64(allocation.sample_count);
        encoder.u64(allocation.chain_begin);
        encoder.u64(allocation.chain_count);
        encoder.u64(allocation.estimated_nanoseconds);
        encoder.u8(allocation.starvation_recovery ? 1 : 0);
        encoder.u8(allocation.experimental ? 1 : 0);
    }
    return runtime::identity_digest(encoder.bytes());
}

}

semantic::IdentityDigest compute_portfolio_work_domain_identity(
    const PortfolioWorkDomain& domain) {
    Encoder encoder;
    encoder.u32(domain.version);
    encoder.digest(domain.spectral_domain_identity);
    encoder.digest(domain.device_identity);
    encoder.digest(domain.sample_namespace_identity);
    encoder.digest(domain.chain_namespace_identity);
    encoder.u32(domain.tile.image_width);
    encoder.u32(domain.tile.image_height);
    encoder.u32(domain.tile.x);
    encoder.u32(domain.tile.y);
    encoder.u32(domain.tile.width);
    encoder.u32(domain.tile.height);
    encoder.u64(domain.wavelength_begin);
    encoder.u64(domain.wavelength_count);
    encoder.u64(domain.time_interval.basis.ticks_per_second);
    encoder.i64(domain.time_interval.basis.synchronization_epoch);
    encoder.digest(domain.time_interval.basis.clock_identity);
    encoder.i64(domain.time_interval.start_tick);
    encoder.i64(domain.time_interval.end_tick);
    return runtime::identity_digest(encoder.bytes());
}

void finalize_portfolio_work_domain(PortfolioWorkDomain& domain) {
    domain.domain_identity = compute_portfolio_work_domain_identity(domain);
    if (!valid_domain(domain)) {
        throw std::invalid_argument("Invalid portfolio work domain");
    }
}

semantic::IdentityDigest compute_portfolio_policy_identity(
    const PortfolioPolicy& policy) {
    Encoder encoder;
    encoder.u32(policy.version);
    encoder.f64(policy.exploration_budget_fraction);
    encoder.f64(policy.tail_risk_weight);
    encoder.f64(policy.minimum_gain_per_nanosecond);
    encoder.u32(policy.maximum_greedy_iterations);
    encoder.u8(policy.allow_experimental ? 1 : 0);
    return runtime::identity_digest(encoder.bytes());
}

void finalize_portfolio_policy(PortfolioPolicy& policy) {
    policy.policy_identity = compute_portfolio_policy_identity(policy);
    if (!valid_policy(policy)) {
        throw std::invalid_argument("Invalid portfolio policy");
    }
}

semantic::IdentityDigest compute_portfolio_candidate_identity(
    const PortfolioCandidate& candidate) {
    Encoder encoder;
    encoder.u32(candidate.version);
    encoder.digest(candidate.domain_identity);
    encoder.digest(candidate.estimate_identity);
    encoder.digest(candidate.execution_semantics_identity);
    encoder.u32(candidate.node_ordinal);
    encoder.u8(static_cast<std::uint8_t>(candidate.layer));
    encoder.f64(candidate.aggregation_coefficient);
    encoder.f64(candidate.projected_variance);
    encoder.f64(candidate.projected_tail_second_moment);
    encoder.f64(candidate.effective_sample_fraction);
    encoder.u64(candidate.nanoseconds_per_sample);
    encoder.u64(candidate.persistent_bytes);
    encoder.u64(candidate.scratch_bytes);
    encoder.u64(candidate.sample_quantum);
    encoder.u64(candidate.minimum_exploration_samples);
    encoder.u64(candidate.maximum_samples);
    encoder.u64(candidate.next_sample_index);
    encoder.u32(candidate.chains_per_quantum);
    encoder.u64(candidate.next_chain_index);
    encoder.u64(candidate.last_served_epoch);
    encoder.u32(candidate.starvation_epoch_limit);
    encoder.u64(candidate.starvation_recovery_samples);
    return runtime::identity_digest(encoder.bytes());
}

void finalize_portfolio_candidate(PortfolioCandidate& candidate) {
    candidate.candidate_identity =
        compute_portfolio_candidate_identity(candidate);
    if (!valid_candidate(candidate)) {
        throw std::invalid_argument("Invalid portfolio candidate");
    }
}

bool PortfolioScheduleValidation::has(
    PortfolioScheduleIssue issue) const {
    return std::ranges::find(issues, issue) != issues.end();
}

PortfolioSchedule schedule_portfolio(
    const TechniqueGraph& technique_graph,
    const CompiledCompositionPlan& composition_plan,
    const PilotQualificationReport& qualification_report,
    const PilotSamplingProvenance& sampling_provenance,
    const semantic::IdentityDigest& world_state_identity,
    const semantic::IdentityDigest& observation_snapshot_identity,
    std::uint64_t epoch,
    const PortfolioBudget& budget,
    const PortfolioPolicy& policy,
    std::span<const PortfolioWorkDomain> domains,
    std::span<const PortfolioCandidate> candidates,
    std::span<const PortfolioCovarianceEdge> covariance_edges) {
    if (!validate_technique_graph(technique_graph).ok() ||
        !validate_compiled_composition_plan(composition_plan) ||
        !validate_pilot_qualification_report(qualification_report) ||
        !validate_pilot_sampling_provenance(sampling_provenance).ok() ||
        composition_plan.technique_graph_identity !=
            technique_graph.graph_identity ||
        qualification_report.technique_graph_identity !=
            technique_graph.graph_identity ||
        qualification_report.composition_plan_identity !=
            composition_plan.plan_identity ||
        qualification_report.pilot_provenance_identity !=
            pilot_sampling_provenance_identity(sampling_provenance) ||
        sampling_provenance.technique_graph_identity !=
            technique_graph.graph_identity ||
        sampling_provenance.world_state_identity !=
            world_state_identity ||
        sampling_provenance.observation_snapshot_identity !=
            observation_snapshot_identity ||
        qualification_report.world_state_identity !=
            world_state_identity ||
        qualification_report.observation_snapshot_identity !=
            observation_snapshot_identity ||
        semantic::identity_empty(world_state_identity) ||
        semantic::identity_empty(observation_snapshot_identity) ||
        epoch == 0 || !valid_budget(budget) || !valid_policy(policy) ||
        domains.empty() || candidates.empty() ||
        domains.size() > budget.maximum_allocations ||
        candidates.size() > budget.maximum_allocations) {
        throw std::invalid_argument("Invalid portfolio schedule inputs");
    }
    std::map<semantic::IdentityDigest, PortfolioWorkDomain> domain_map;
    for (const auto& domain : domains) {
        if (!valid_domain(domain) ||
            !domain_map.emplace(domain.domain_identity, domain).second) {
            throw std::invalid_argument("Invalid portfolio domains");
        }
    }
    std::map<std::uint32_t, TechniqueQualificationDecision> decisions;
    for (const auto& decision : qualification_report.decisions) {
        decisions.emplace(decision.node_ordinal, decision);
    }
    std::set<std::uint32_t> binding_nodes;
    for (const auto& binding : composition_plan.bindings) {
        binding_nodes.insert(binding.node_ordinal);
    }
    std::map<semantic::IdentityDigest, std::size_t> indices;
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        const auto& candidate = candidates[index];
        const auto decision = decisions.find(candidate.node_ordinal);
        const bool eligible = decision != decisions.end() &&
            decision->second.status == QualificationStatus::Eligible;
        const bool experimental = decision != decisions.end() &&
            decision->second.status ==
                QualificationStatus::ExperimentalOverride &&
            policy.allow_experimental;
        const bool node_valid =
            candidate.node_ordinal < technique_graph.nodes.size();
        const bool markov_chain = node_valid &&
            technique_graph.nodes[candidate.node_ordinal]
                .descriptor.estimator.correlation ==
            CorrelationModel::MarkovChain;
        if (!valid_candidate(candidate) ||
            !domain_map.contains(candidate.domain_identity) ||
            domain_map.at(candidate.domain_identity)
                    .sample_namespace_identity !=
                sampling_provenance.production_namespace_identity ||
            !ranges_cover(
                sampling_provenance.production_ranges,
                candidate.next_sample_index,
                candidate.maximum_samples) ||
            candidate.last_served_epoch > epoch ||
            !node_valid ||
            (candidate.chains_per_quantum != 0 &&
             semantic::identity_empty(
                 domain_map.at(candidate.domain_identity)
                     .chain_namespace_identity)) ||
            !binding_nodes.contains(candidate.node_ordinal) ||
            markov_chain != (candidate.chains_per_quantum != 0) ||
            (!eligible && !experimental) ||
            decision->second.estimate_identity !=
                candidate.estimate_identity ||
            !indices.emplace(candidate.candidate_identity, index).second) {
            throw std::invalid_argument("Invalid portfolio candidate set");
        }
    }
    std::set<std::pair<semantic::IdentityDigest,
                       semantic::IdentityDigest>> edge_keys;
    for (const auto& edge : covariance_edges) {
        const auto left = indices.find(edge.left_candidate_identity);
        const auto right = indices.find(edge.right_candidate_identity);
        const bool chain_covariance =
            left != indices.end() && right != indices.end() &&
            (technique_graph.nodes[
                 candidates[left->second].node_ordinal]
                     .descriptor.estimator.correlation ==
                 CorrelationModel::MarkovChain ||
             technique_graph.nodes[
                 candidates[right->second].node_ordinal]
                     .descriptor.estimator.correlation ==
                 CorrelationModel::MarkovChain);
        if (edge.version != kPortfolioContractVersion ||
            left == indices.end() || right == indices.end() ||
            chain_covariance ||
            edge.left_candidate_identity >= edge.right_candidate_identity ||
            semantic::identity_empty(edge.covariance_identity) ||
            semantic::identity_empty(edge.pairing_identity) ||
            !std::isfinite(edge.projected_covariance) ||
            candidates[left->second].domain_identity !=
                candidates[right->second].domain_identity ||
            candidates[left->second].next_sample_index !=
                candidates[right->second].next_sample_index ||
            std::abs(edge.projected_covariance) >
                std::sqrt(
                    candidates[left->second].projected_variance *
                    candidates[right->second].projected_variance) *
                    (1.0 + 1e-12) ||
            !edge_keys.emplace(
                edge.left_candidate_identity,
                edge.right_candidate_identity).second) {
            throw std::invalid_argument("Invalid portfolio covariance");
        }
    }
    if (!covariance_psd(candidates, covariance_edges, indices)) {
        throw std::invalid_argument(
            "Portfolio covariance is not positive semidefinite");
    }

    std::vector<std::uint64_t> sample_counts(candidates.size(), 0);
    std::vector<bool> starvation(candidates.size(), false);
    std::uint64_t spent = 0;
    std::uint64_t total_samples = 0;
    std::uint64_t resident = 0;
    std::uint64_t scratch = 0;
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        const auto& candidate = candidates[index];
        starvation[index] =
            epoch > candidate.last_served_epoch &&
            epoch - candidate.last_served_epoch >=
                candidate.starvation_epoch_limit;
        auto floor = candidate.minimum_exploration_samples;
        if (starvation[index]) {
            floor = std::max(
                floor, candidate.starvation_recovery_samples);
        }
        floor = round_up(floor, candidate.sample_quantum);
        if (floor > candidate.maximum_samples) {
            throw std::invalid_argument(
                "Portfolio exploration exceeds candidate limit");
        }
        std::uint64_t cost = 0;
        if (multiply_overflow(
                floor, candidate.nanoseconds_per_sample, cost) ||
            add_overflow(spent, cost, spent) ||
            add_overflow(total_samples, floor, total_samples) ||
            add_overflow(
                resident, candidate.persistent_bytes, resident) ||
            add_overflow(scratch, candidate.scratch_bytes, scratch)) {
            throw std::overflow_error("Portfolio exploration overflow");
        }
        sample_counts[index] = floor;
    }
    if (spent > budget.total_nanoseconds ||
        total_samples > budget.maximum_samples ||
        static_cast<double>(spent) >
            static_cast<double>(budget.total_nanoseconds) *
                policy.exploration_budget_fraction) {
        throw std::invalid_argument("Portfolio exploration budget exceeded");
    }
    if (resident > budget.resident_bytes || scratch > budget.scratch_bytes) {
        throw std::invalid_argument("Portfolio memory budget exceeded");
    }
    const auto floor_variance = predicted_variance(
        candidates, covariance_edges, policy, sample_counts, indices);
    auto current_variance = floor_variance;
    for (std::uint32_t iteration = 0;
         iteration < policy.maximum_greedy_iterations; ++iteration) {
        std::size_t best = candidates.size();
        double best_score = policy.minimum_gain_per_nanosecond;
        double best_variance = current_variance;
        std::uint64_t best_cost = 0;
        for (std::size_t index = 0; index < candidates.size(); ++index) {
            const auto& candidate = candidates[index];
            if (sample_counts[index] >
                candidate.maximum_samples - candidate.sample_quantum) {
                continue;
            }
            std::uint64_t cost = 0;
            std::uint64_t next_spent = 0;
            std::uint64_t next_samples = 0;
            if (multiply_overflow(
                    candidate.sample_quantum,
                    candidate.nanoseconds_per_sample, cost) ||
                add_overflow(spent, cost, next_spent) ||
                add_overflow(
                    total_samples, candidate.sample_quantum,
                    next_samples) ||
                next_spent > budget.total_nanoseconds ||
                next_samples > budget.maximum_samples) {
                continue;
            }
            auto trial = sample_counts;
            trial[index] += candidate.sample_quantum;
            const auto variance = predicted_variance(
                candidates, covariance_edges, policy, trial, indices);
            const auto gain = current_variance - variance;
            const auto score = gain / static_cast<double>(cost);
            if (gain > 0.0 &&
                (score > best_score + 1e-18 ||
                 (std::abs(score - best_score) <= 1e-18 &&
                  (best == candidates.size() ||
                   candidate.candidate_identity <
                       candidates[best].candidate_identity)))) {
                best = index;
                best_score = score;
                best_variance = variance;
                best_cost = cost;
            }
        }
        if (best == candidates.size()) break;
        sample_counts[best] += candidates[best].sample_quantum;
        spent += best_cost;
        total_samples += candidates[best].sample_quantum;
        current_variance = best_variance;
    }

    PortfolioSchedule result;
    result.technique_graph_identity = technique_graph.graph_identity;
    result.composition_plan_identity = composition_plan.plan_identity;
    result.qualification_report_identity =
        qualification_report.report_identity;
    result.pilot_provenance_identity =
        qualification_report.pilot_provenance_identity;
    result.world_state_identity = world_state_identity;
    result.observation_snapshot_identity =
        observation_snapshot_identity;
    result.policy_identity = policy.policy_identity;
    result.candidate_set_identity = candidate_set_identity(candidates);
    result.covariance_set_identity =
        covariance_set_identity(covariance_edges);
    result.epoch = epoch;
    result.budget = budget;
    result.spent_nanoseconds = spent;
    result.reserved_resident_bytes = resident;
    result.reserved_scratch_bytes = scratch;
    result.variance_at_exploration_floor = floor_variance;
    result.predicted_variance = current_variance;
    result.domains.assign(domains.begin(), domains.end());
    std::ranges::sort(
        result.domains, {}, &PortfolioWorkDomain::domain_identity);
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        const auto& candidate = candidates[index];
        const auto& domain = domain_map.at(candidate.domain_identity);
        PortfolioAllocation allocation;
        allocation.candidate_identity = candidate.candidate_identity;
        allocation.domain_identity = candidate.domain_identity;
        allocation.sample_namespace_identity =
            domain.sample_namespace_identity;
        allocation.chain_namespace_identity =
            domain.chain_namespace_identity;
        allocation.execution_semantics_identity =
            candidate.execution_semantics_identity;
        allocation.node_ordinal = candidate.node_ordinal;
        allocation.sample_begin = candidate.next_sample_index;
        allocation.sample_count = sample_counts[index];
        allocation.chain_begin = candidate.next_chain_index;
        allocation.chain_count =
            sample_counts[index] / candidate.sample_quantum *
            candidate.chains_per_quantum;
        allocation.estimated_nanoseconds = sample_counts[index] *
            candidate.nanoseconds_per_sample;
        allocation.starvation_recovery = starvation[index];
        allocation.experimental = decisions.at(candidate.node_ordinal)
            .status == QualificationStatus::ExperimentalOverride;
        result.allocations.push_back(allocation);
    }
    std::ranges::sort(
        result.allocations, {}, &PortfolioAllocation::candidate_identity);
    result.schedule_identity = compute_schedule_identity(result);
    if (!validate_portfolio_schedule(result).ok()) {
        throw std::runtime_error("Generated invalid portfolio schedule");
    }
    return result;
}

PortfolioScheduleValidation validate_portfolio_schedule(
    const PortfolioSchedule& schedule) {
    PortfolioScheduleValidation result;
    if (schedule.version != kPortfolioContractVersion) {
        add(result.issues, PortfolioScheduleIssue::Version);
    }
    if (semantic::identity_empty(schedule.schedule_identity) ||
        semantic::identity_empty(schedule.technique_graph_identity) ||
        semantic::identity_empty(schedule.composition_plan_identity) ||
        semantic::identity_empty(
            schedule.qualification_report_identity) ||
        semantic::identity_empty(schedule.pilot_provenance_identity) ||
        semantic::identity_empty(schedule.world_state_identity) ||
        semantic::identity_empty(
            schedule.observation_snapshot_identity) ||
        semantic::identity_empty(schedule.policy_identity) ||
        semantic::identity_empty(schedule.candidate_set_identity) ||
        semantic::identity_empty(schedule.covariance_set_identity)) {
        add(result.issues, PortfolioScheduleIssue::Identity);
    }
    if (!valid_budget(schedule.budget) || schedule.epoch == 0 ||
        schedule.spent_nanoseconds > schedule.budget.total_nanoseconds ||
        schedule.reserved_resident_bytes > schedule.budget.resident_bytes ||
        schedule.reserved_scratch_bytes > schedule.budget.scratch_bytes) {
        add(result.issues, PortfolioScheduleIssue::Budget);
    }
    if (schedule.domains.empty() ||
        schedule.domains.size() > schedule.budget.maximum_allocations) {
        add(result.issues, PortfolioScheduleIssue::Domain);
    }
    std::map<semantic::IdentityDigest, PortfolioWorkDomain> domains;
    for (const auto& domain : schedule.domains) {
        if (!valid_domain(domain) ||
            !domains.emplace(domain.domain_identity, domain).second) {
            add(result.issues, PortfolioScheduleIssue::Domain);
        }
    }
    std::set<semantic::IdentityDigest> candidates;
    std::uint64_t spent = 0;
    std::uint64_t samples = 0;
    for (const auto& allocation : schedule.allocations) {
        std::uint64_t sample_end = 0;
        std::uint64_t chain_end = 0;
        if (semantic::identity_empty(allocation.candidate_identity) ||
            semantic::identity_empty(allocation.domain_identity) ||
            semantic::identity_empty(
                allocation.sample_namespace_identity) ||
            semantic::identity_empty(
                allocation.execution_semantics_identity) ||
            !domains.contains(allocation.domain_identity) ||
            allocation.sample_namespace_identity !=
                domains.at(allocation.domain_identity)
                    .sample_namespace_identity ||
            allocation.chain_namespace_identity !=
                domains.at(allocation.domain_identity)
                    .chain_namespace_identity ||
            (allocation.chain_count != 0 &&
             semantic::identity_empty(
                 allocation.chain_namespace_identity)) ||
            allocation.sample_count == 0 ||
            allocation.estimated_nanoseconds == 0 ||
            !candidates.insert(allocation.candidate_identity).second ||
            add_overflow(
                allocation.sample_begin, allocation.sample_count,
                sample_end) ||
            add_overflow(
                allocation.chain_begin, allocation.chain_count,
                chain_end) ||
            add_overflow(spent, allocation.estimated_nanoseconds, spent) ||
            add_overflow(samples, allocation.sample_count, samples)) {
            add(result.issues, PortfolioScheduleIssue::Allocation);
        }
    }
    if (schedule.allocations.empty() ||
        schedule.allocations.size() > schedule.budget.maximum_allocations ||
        spent != schedule.spent_nanoseconds ||
        samples > schedule.budget.maximum_samples ||
        !std::isfinite(schedule.variance_at_exploration_floor) ||
        !std::isfinite(schedule.predicted_variance) ||
        schedule.predicted_variance < 0.0 ||
        schedule.predicted_variance >
            schedule.variance_at_exploration_floor * (1.0 + 1e-12)) {
        add(result.issues, PortfolioScheduleIssue::Allocation);
    }
    if (!schedule.allocations.empty() &&
        schedule.candidate_set_identity !=
            allocation_candidate_set_identity(schedule.allocations)) {
        add(result.issues, PortfolioScheduleIssue::IdentityMismatch);
    }
    if (result.issues.empty() &&
        schedule.schedule_identity != compute_schedule_identity(schedule)) {
        add(result.issues, PortfolioScheduleIssue::IdentityMismatch);
    }
    return result;
}

}
