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

std::uint32_t reason(PortfolioDriftReason value) {
    return static_cast<std::uint32_t>(value);
}

bool valid_policy(const PortfolioDriftPolicy& policy) {
    return policy.version == kPortfolioContractVersion &&
        !semantic::identity_empty(policy.policy_identity) &&
        policy.policy_identity ==
            compute_portfolio_drift_policy_identity(policy) &&
        std::isfinite(policy.mean_z_threshold) &&
        policy.mean_z_threshold > 0.0 &&
        std::isfinite(policy.maximum_variance_ratio) &&
        policy.maximum_variance_ratio >= 1.0 &&
        std::isfinite(policy.maximum_cost_ratio) &&
        policy.maximum_cost_ratio >= 1.0 &&
        policy.minimum_samples >= 2 &&
        policy.consecutive_breaches != 0 &&
        std::isfinite(policy.global_repilot_fraction) &&
        policy.global_repilot_fraction > 0.0 &&
        policy.global_repilot_fraction <= 1.0;
}

bool valid_observation(const PortfolioDriftObservation& observation) {
    return !semantic::identity_empty(observation.candidate_identity) &&
        !semantic::identity_empty(observation.world_state_identity) &&
        !semantic::identity_empty(
            observation.observation_snapshot_identity) &&
        !semantic::identity_empty(observation.schedule_identity) &&
        observation.epoch != 0 && observation.sample_count >= 2 &&
        std::isfinite(observation.mean) &&
        std::isfinite(observation.sample_variance) &&
        observation.sample_variance >= 0.0 &&
        std::isfinite(observation.nanoseconds_per_sample) &&
        observation.nanoseconds_per_sample > 0.0;
}

semantic::IdentityDigest observation_identity(
    const PortfolioDriftObservation& observation) {
    Encoder encoder;
    encoder.digest(observation.candidate_identity);
    encoder.digest(observation.world_state_identity);
    encoder.digest(observation.observation_snapshot_identity);
    encoder.digest(observation.schedule_identity);
    encoder.u64(observation.epoch);
    encoder.u64(observation.sample_count);
    encoder.f64(observation.mean);
    encoder.f64(observation.sample_variance);
    encoder.f64(observation.nanoseconds_per_sample);
    return runtime::identity_digest(encoder.bytes());
}

semantic::IdentityDigest state_identity(
    const PortfolioDriftState& state) {
    Encoder encoder;
    encoder.digest(state.candidate_identity);
    encoder.digest(state.baseline_observation_identity);
    encoder.u64(state.last_epoch);
    encoder.u32(state.consecutive_breach_count);
    return runtime::identity_digest(encoder.bytes());
}

semantic::IdentityDigest report_identity(
    const PortfolioDriftReport& report) {
    Encoder encoder;
    encoder.u32(report.version);
    encoder.digest(report.policy_identity);
    encoder.digest(report.schedule_identity);
    encoder.u8(static_cast<std::uint8_t>(report.action));
    encoder.u32(static_cast<std::uint32_t>(report.states.size()));
    for (const auto& state : report.states) {
        encoder.digest(state.state_identity);
    }
    encoder.u32(static_cast<std::uint32_t>(report.decisions.size()));
    for (const auto& decision : report.decisions) {
        encoder.digest(decision.candidate_identity);
        encoder.u8(static_cast<std::uint8_t>(decision.action));
        encoder.u32(decision.reason_mask);
        encoder.f64(decision.mean_z_score);
        encoder.f64(decision.variance_ratio);
        encoder.f64(decision.cost_ratio);
    }
    return runtime::identity_digest(encoder.bytes());
}

double symmetric_ratio(double left, double right) {
    if (left == 0.0 && right == 0.0) return 1.0;
    if (left == 0.0 || right == 0.0) {
        return std::numeric_limits<double>::max();
    }
    return std::max(left / right, right / left);
}

}

semantic::IdentityDigest compute_portfolio_drift_policy_identity(
    const PortfolioDriftPolicy& policy) {
    Encoder encoder;
    encoder.u32(policy.version);
    encoder.f64(policy.mean_z_threshold);
    encoder.f64(policy.maximum_variance_ratio);
    encoder.f64(policy.maximum_cost_ratio);
    encoder.u64(policy.minimum_samples);
    encoder.u32(policy.consecutive_breaches);
    encoder.f64(policy.global_repilot_fraction);
    return runtime::identity_digest(encoder.bytes());
}

void finalize_portfolio_drift_policy(PortfolioDriftPolicy& policy) {
    policy.policy_identity =
        compute_portfolio_drift_policy_identity(policy);
    if (!valid_policy(policy)) {
        throw std::invalid_argument("Invalid portfolio drift policy");
    }
}

PortfolioDriftReport evaluate_portfolio_drift(
    const PortfolioSchedule& schedule,
    const PortfolioDriftPolicy& policy,
    std::span<const PortfolioDriftObservation> baselines,
    std::span<const PortfolioDriftObservation> current,
    std::span<const PortfolioDriftState> previous_states) {
    if (!validate_portfolio_schedule(schedule).ok() ||
        !valid_policy(policy) || baselines.empty() ||
        baselines.size() != schedule.allocations.size() ||
        current.size() != schedule.allocations.size() ||
        (!previous_states.empty() &&
         previous_states.size() != schedule.allocations.size())) {
        throw std::invalid_argument("Invalid portfolio drift inputs");
    }
    std::set<semantic::IdentityDigest> scheduled;
    for (const auto& allocation : schedule.allocations) {
        scheduled.insert(allocation.candidate_identity);
    }
    std::map<semantic::IdentityDigest, PortfolioDriftObservation>
        baseline_map;
    std::map<semantic::IdentityDigest, PortfolioDriftObservation>
        current_map;
    for (const auto& observation : baselines) {
        if (!valid_observation(observation) ||
            !scheduled.contains(observation.candidate_identity) ||
            !baseline_map.emplace(
                observation.candidate_identity, observation).second) {
            throw std::invalid_argument("Invalid drift baselines");
        }
    }
    for (const auto& observation : current) {
        if (!valid_observation(observation) ||
            observation.schedule_identity != schedule.schedule_identity ||
            observation.epoch < schedule.epoch ||
            !scheduled.contains(observation.candidate_identity) ||
            !current_map.emplace(
                observation.candidate_identity, observation).second) {
            throw std::invalid_argument("Invalid current drift evidence");
        }
    }
    std::map<semantic::IdentityDigest, PortfolioDriftState> state_map;
    for (const auto& state : previous_states) {
        const auto baseline = baseline_map.find(state.candidate_identity);
        if (baseline == baseline_map.end() ||
            semantic::identity_empty(state.state_identity) ||
            state.baseline_observation_identity !=
                observation_identity(baseline->second) ||
            state.state_identity != state_identity(state) ||
            !state_map.emplace(
                state.candidate_identity, state).second) {
            throw std::invalid_argument("Invalid drift state");
        }
    }

    PortfolioDriftReport result;
    result.policy_identity = policy.policy_identity;
    result.schedule_identity = schedule.schedule_identity;
    std::size_t candidate_repilots = 0;
    bool global_identity_change = false;
    for (const auto& allocation : schedule.allocations) {
        const auto& baseline =
            baseline_map.at(allocation.candidate_identity);
        const auto& observation =
            current_map.at(allocation.candidate_identity);
        PortfolioDriftDecision decision;
        decision.candidate_identity = allocation.candidate_identity;
        if (baseline.world_state_identity !=
                observation.world_state_identity ||
            observation.world_state_identity !=
                schedule.world_state_identity) {
            decision.reason_mask |= reason(
                PortfolioDriftReason::WorldState);
            global_identity_change = true;
        }
        if (baseline.observation_snapshot_identity !=
                observation.observation_snapshot_identity ||
            observation.observation_snapshot_identity !=
                schedule.observation_snapshot_identity) {
            decision.reason_mask |= reason(
                PortfolioDriftReason::ObservationSnapshot);
            global_identity_change = true;
        }
        const bool sufficient =
            baseline.sample_count >= policy.minimum_samples &&
            observation.sample_count >= policy.minimum_samples;
        bool breached = false;
        if (!sufficient) {
            decision.reason_mask |= reason(
                PortfolioDriftReason::InsufficientEvidence);
        } else {
            const auto standard_error = std::sqrt(
                baseline.sample_variance /
                    static_cast<double>(baseline.sample_count) +
                observation.sample_variance /
                    static_cast<double>(observation.sample_count));
            decision.mean_z_score = standard_error == 0.0
                ? (baseline.mean == observation.mean
                    ? 0.0
                    : std::numeric_limits<double>::max())
                : std::abs(observation.mean - baseline.mean) /
                    standard_error;
            decision.variance_ratio = symmetric_ratio(
                baseline.sample_variance,
                observation.sample_variance);
            decision.cost_ratio = symmetric_ratio(
                baseline.nanoseconds_per_sample,
                observation.nanoseconds_per_sample);
            if (decision.mean_z_score > policy.mean_z_threshold) {
                decision.reason_mask |= reason(PortfolioDriftReason::Mean);
                breached = true;
            }
            if (decision.variance_ratio >
                policy.maximum_variance_ratio) {
                decision.reason_mask |= reason(
                    PortfolioDriftReason::Variance);
                breached = true;
            }
            if (decision.cost_ratio > policy.maximum_cost_ratio) {
                decision.reason_mask |= reason(PortfolioDriftReason::Cost);
                breached = true;
            }
        }
        PortfolioDriftState state;
        const auto previous = state_map.find(allocation.candidate_identity);
        if (previous != state_map.end()) {
            if (observation.epoch <= previous->second.last_epoch) {
                throw std::invalid_argument(
                    "Non-monotonic portfolio drift epoch");
            }
            state = previous->second;
        }
        state.candidate_identity = allocation.candidate_identity;
        state.baseline_observation_identity =
            observation_identity(baseline);
        state.last_epoch = observation.epoch;
        state.consecutive_breach_count = breached
            ? std::min(
                  std::numeric_limits<std::uint32_t>::max(),
                  state.consecutive_breach_count +
                      (state.consecutive_breach_count !=
                       std::numeric_limits<std::uint32_t>::max()))
            : 0;
        if (state.consecutive_breach_count >=
            policy.consecutive_breaches) {
            decision.action = PortfolioDriftAction::RepilotCandidate;
            ++candidate_repilots;
        }
        state.state_identity = state_identity(state);
        result.states.push_back(state);
        result.decisions.push_back(decision);
    }
    std::ranges::sort(
        result.states, {}, &PortfolioDriftState::candidate_identity);
    std::ranges::sort(
        result.decisions, {}, &PortfolioDriftDecision::candidate_identity);
    if (global_identity_change ||
        static_cast<double>(candidate_repilots) /
                static_cast<double>(result.decisions.size()) >=
            policy.global_repilot_fraction) {
        result.action = PortfolioDriftAction::RepilotAll;
        for (auto& decision : result.decisions) {
            decision.action = PortfolioDriftAction::RepilotAll;
        }
    } else if (candidate_repilots != 0) {
        result.action = PortfolioDriftAction::RepilotCandidate;
    }
    result.report_identity = report_identity(result);
    if (!validate_portfolio_drift_report(result)) {
        throw std::runtime_error("Generated invalid drift report");
    }
    return result;
}

bool validate_portfolio_drift_report(
    const PortfolioDriftReport& report) {
    if (report.version != kPortfolioContractVersion ||
        semantic::identity_empty(report.report_identity) ||
        semantic::identity_empty(report.policy_identity) ||
        semantic::identity_empty(report.schedule_identity) ||
        report.action < PortfolioDriftAction::Stable ||
        report.action > PortfolioDriftAction::RepilotAll ||
        report.states.empty() ||
        report.states.size() != report.decisions.size()) {
        return false;
    }
    std::set<semantic::IdentityDigest> state_candidates;
    std::set<semantic::IdentityDigest> decision_candidates;
    PortfolioDriftAction aggregate = PortfolioDriftAction::Stable;
    for (const auto& state : report.states) {
        if (semantic::identity_empty(state.candidate_identity) ||
            semantic::identity_empty(
                state.baseline_observation_identity) ||
            semantic::identity_empty(state.state_identity) ||
            state.last_epoch == 0 ||
            state.state_identity != state_identity(state) ||
            !state_candidates.insert(state.candidate_identity).second) {
            return false;
        }
    }
    for (const auto& decision : report.decisions) {
        constexpr auto kKnownReasons =
            static_cast<std::uint32_t>(PortfolioDriftReason::WorldState) |
            static_cast<std::uint32_t>(
                PortfolioDriftReason::ObservationSnapshot) |
            static_cast<std::uint32_t>(PortfolioDriftReason::Mean) |
            static_cast<std::uint32_t>(PortfolioDriftReason::Variance) |
            static_cast<std::uint32_t>(PortfolioDriftReason::Cost) |
            static_cast<std::uint32_t>(
                PortfolioDriftReason::InsufficientEvidence);
        if (semantic::identity_empty(decision.candidate_identity) ||
            decision.action < PortfolioDriftAction::Stable ||
            decision.action > PortfolioDriftAction::RepilotAll ||
            !std::isfinite(decision.mean_z_score) ||
            !std::isfinite(decision.variance_ratio) ||
            !std::isfinite(decision.cost_ratio) ||
            decision.mean_z_score < 0.0 ||
            decision.variance_ratio < 1.0 ||
            decision.cost_ratio < 1.0 ||
            (decision.reason_mask & ~kKnownReasons) != 0 ||
            !decision_candidates.insert(
                decision.candidate_identity).second) {
            return false;
        }
        aggregate = std::max(aggregate, decision.action);
    }
    return state_candidates == decision_candidates &&
        report.action == aggregate &&
        report.report_identity == report_identity(report);
}

}
