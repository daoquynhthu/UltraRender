#include "ure/transport/pilot.hpp"

#include "ure/runtime/multi_backend.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <limits>
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

template <typename Issue>
void add(std::vector<Issue>& issues, Issue issue) {
    if (std::ranges::find(issues, issue) == issues.end()) {
        issues.push_back(issue);
    }
}

bool ranges_valid(std::span<const PilotSampleRange> ranges) {
    if (ranges.empty()) return false;
    std::uint64_t end = 0;
    bool first = true;
    for (const auto& range : ranges) {
        if (range.count == 0 ||
            range.start > std::numeric_limits<std::uint64_t>::max() -
                              range.count ||
            (!first && range.start < end)) {
            return false;
        }
        end = range.start + range.count;
        first = false;
    }
    return true;
}

bool ranges_overlap(std::span<const PilotSampleRange> left,
                    std::span<const PilotSampleRange> right) {
    std::size_t a = 0;
    std::size_t b = 0;
    while (a < left.size() && b < right.size()) {
        const auto left_end = left[a].start + left[a].count;
        const auto right_end = right[b].start + right[b].count;
        if (left[a].start < right_end && right[b].start < left_end) {
            return true;
        }
        if (left_end <= right_end) {
            ++a;
        } else {
            ++b;
        }
    }
    return false;
}

bool range_contains(std::span<const PilotSampleRange> ranges,
                    std::uint64_t sample_identity) {
    return std::ranges::any_of(
        ranges,
        [sample_identity](const PilotSampleRange& range) {
            return sample_identity >= range.start &&
                sample_identity - range.start < range.count;
        });
}

void encode_ranges(Encoder& encoder,
                   std::span<const PilotSampleRange> ranges) {
    encoder.u32(static_cast<std::uint32_t>(ranges.size()));
    for (const auto& range : ranges) {
        encoder.u64(range.start);
        encoder.u64(range.count);
    }
}

void encode_doubles(Encoder& encoder,
                    std::span<const double> values) {
    encoder.u32(static_cast<std::uint32_t>(values.size()));
    for (const auto value : values) encoder.f64(value);
}

void encode_counts(Encoder& encoder,
                   std::span<const std::uint64_t> values) {
    encoder.u32(static_cast<std::uint32_t>(values.size()));
    for (const auto value : values) encoder.u64(value);
}

bool finite_values(std::span<const double> values) {
    return std::ranges::all_of(
        values, [](double value) { return std::isfinite(value); });
}

semantic::IdentityDigest estimate_identity(
    const TechniquePilotEstimate& estimate) {
    Encoder encoder;
    encoder.u32(estimate.node_ordinal);
    encoder.digest(estimate.support_partition_identity);
    encoder.digest(estimate.pilot_provenance_identity);
    encoder.u64(estimate.sample_count);
    encoder.u64(estimate.nanoseconds_per_sample);
    encoder.u64(estimate.peak_scratch_bytes);
    encoder.u64(estimate.persistent_bytes);
    encode_doubles(encoder, estimate.means);
    encode_doubles(encoder, estimate.sample_variances);
    encode_doubles(encoder, estimate.absolute_tail_thresholds);
    encode_doubles(encoder, estimate.tail_exceedance_rates);
    encode_doubles(encoder, estimate.mean_absolute_tail_excesses);
    encode_doubles(encoder, estimate.maximum_absolute_contributions);
    encoder.f64(estimate.effective_sample_size);
    return runtime::identity_digest(encoder.bytes());
}

semantic::IdentityDigest covariance_identity(
    const TechniquePilotCovariance& covariance) {
    Encoder encoder;
    encoder.u32(covariance.left_node_ordinal);
    encoder.u32(covariance.right_node_ordinal);
    encoder.digest(covariance.support_partition_identity);
    encoder.digest(covariance.pilot_provenance_identity);
    encoder.digest(covariance.pairing_identity);
    encoder.digest(covariance.left_observation_identity);
    encoder.digest(covariance.right_observation_identity);
    encoder.u64(covariance.paired_sample_count);
    encode_doubles(encoder, covariance.sample_covariances);
    return runtime::identity_digest(encoder.bytes());
}

}

bool PilotProvenanceValidation::has(PilotProvenanceIssue issue) const {
    return std::ranges::find(issues, issue) != issues.end();
}

PilotProvenanceValidation validate_pilot_sampling_provenance(
    const PilotSamplingProvenance& provenance,
    double relative_tolerance) {
    PilotProvenanceValidation result;
    if (provenance.version != kPilotContractVersion) {
        add(result.issues, PilotProvenanceIssue::Version);
    }
    if (provenance.reuse_policy <
            PilotReusePolicy::IndependentHoldout ||
        provenance.reuse_policy >
            PilotReusePolicy::SelectionProbabilityCorrected) {
        add(result.issues, PilotProvenanceIssue::Policy);
    }
    if (semantic::identity_empty(provenance.pilot_identity) ||
        semantic::identity_empty(
            provenance.technique_graph_identity) ||
        semantic::identity_empty(provenance.world_state_identity) ||
        semantic::identity_empty(
            provenance.observation_snapshot_identity) ||
        semantic::identity_empty(
            provenance.pilot_namespace_identity) ||
        semantic::identity_empty(
            provenance.production_namespace_identity)) {
        add(result.issues, PilotProvenanceIssue::Identity);
    }
    if (!ranges_valid(provenance.pilot_ranges) ||
        !ranges_valid(provenance.production_ranges)) {
        add(result.issues, PilotProvenanceIssue::Range);
    }
    if (!std::isfinite(relative_tolerance) ||
        relative_tolerance < 0.0) {
        add(result.issues, PilotProvenanceIssue::Probability);
        return result;
    }
    switch (provenance.reuse_policy) {
    case PilotReusePolicy::IndependentHoldout:
        if (provenance.pilot_namespace_identity ==
                provenance.production_namespace_identity &&
            ranges_valid(provenance.pilot_ranges) &&
            ranges_valid(provenance.production_ranges) &&
            ranges_overlap(provenance.pilot_ranges,
                           provenance.production_ranges)) {
            add(result.issues, PilotProvenanceIssue::Overlap);
        }
        if (provenance.fold_count != 0 ||
            !semantic::identity_empty(
                provenance.fold_assignment_identity)) {
            add(result.issues, PilotProvenanceIssue::Fold);
        }
        if (provenance.selection_probability != 1.0 ||
            provenance.inverse_selection_weight != 1.0 ||
            !semantic::identity_empty(
                provenance.selection_probability_identity) ||
            !semantic::identity_empty(provenance.correction_identity)) {
            add(result.issues, PilotProvenanceIssue::Correction);
        }
        break;
    case PilotReusePolicy::CrossFitted:
        if (provenance.fold_count < 2 ||
            provenance.selection_fold >= provenance.fold_count ||
            provenance.evaluation_fold >= provenance.fold_count ||
            provenance.selection_fold == provenance.evaluation_fold ||
            semantic::identity_empty(
                provenance.fold_assignment_identity)) {
            add(result.issues, PilotProvenanceIssue::Fold);
        }
        if (provenance.selection_probability != 1.0 ||
            provenance.inverse_selection_weight != 1.0 ||
            !semantic::identity_empty(
                provenance.selection_probability_identity) ||
            !semantic::identity_empty(provenance.correction_identity)) {
            add(result.issues, PilotProvenanceIssue::Correction);
        }
        break;
    case PilotReusePolicy::SelectionProbabilityCorrected:
        if (provenance.fold_count != 0 ||
            !semantic::identity_empty(
                provenance.fold_assignment_identity)) {
            add(result.issues, PilotProvenanceIssue::Fold);
        }
        if (semantic::identity_empty(
                provenance.selection_probability_identity) ||
            semantic::identity_empty(provenance.correction_identity)) {
            add(result.issues, PilotProvenanceIssue::Correction);
        }
        if (!std::isfinite(provenance.selection_probability) ||
            provenance.selection_probability <= 0.0 ||
            provenance.selection_probability > 1.0 ||
            !std::isfinite(provenance.inverse_selection_weight) ||
            provenance.inverse_selection_weight < 1.0 ||
            std::abs(provenance.inverse_selection_weight -
                     1.0 / provenance.selection_probability) >
                relative_tolerance * std::max(
                    1.0, provenance.inverse_selection_weight)) {
            add(result.issues, PilotProvenanceIssue::Probability);
        }
        break;
    default:
        break;
    }
    return result;
}

semantic::IdentityDigest pilot_sampling_provenance_identity(
    const PilotSamplingProvenance& provenance) {
    if (!validate_pilot_sampling_provenance(provenance).ok()) {
        throw std::invalid_argument("Invalid pilot sampling provenance");
    }
    Encoder encoder;
    encoder.u32(provenance.version);
    encoder.digest(provenance.pilot_identity);
    encoder.digest(provenance.technique_graph_identity);
    encoder.digest(provenance.world_state_identity);
    encoder.digest(provenance.observation_snapshot_identity);
    encoder.digest(provenance.pilot_namespace_identity);
    encoder.digest(provenance.production_namespace_identity);
    encoder.digest(provenance.fold_assignment_identity);
    encoder.digest(provenance.selection_probability_identity);
    encoder.digest(provenance.correction_identity);
    encoder.u8(static_cast<std::uint8_t>(provenance.reuse_policy));
    encode_ranges(encoder, provenance.pilot_ranges);
    encode_ranges(encoder, provenance.production_ranges);
    encoder.u32(provenance.fold_count);
    encoder.u32(provenance.selection_fold);
    encoder.u32(provenance.evaluation_fold);
    encoder.f64(provenance.selection_probability);
    encoder.f64(provenance.inverse_selection_weight);
    return runtime::identity_digest(encoder.bytes());
}

bool PilotObservationValidation::has(PilotObservationIssue issue) const {
    return std::ranges::find(issues, issue) != issues.end();
}

semantic::IdentityDigest compute_technique_pilot_observation_identity(
    const TechniquePilotObservation& observation) {
    Encoder encoder;
    encoder.u32(observation.version);
    encoder.u32(observation.node_ordinal);
    encoder.digest(observation.support_partition_identity);
    encoder.digest(observation.pilot_provenance_identity);
    encoder.u64(observation.sample_count);
    encoder.u64(observation.elapsed_nanoseconds);
    encoder.u64(observation.peak_scratch_bytes);
    encoder.u64(observation.persistent_bytes);
    encode_doubles(encoder, observation.first_moment_sums);
    encode_doubles(encoder, observation.second_moment_sums);
    encode_doubles(encoder, observation.absolute_tail_thresholds);
    encode_counts(encoder, observation.tail_exceedance_counts);
    encode_doubles(encoder, observation.absolute_tail_excess_sums);
    encode_doubles(encoder, observation.maximum_absolute_contributions);
    encoder.f64(observation.importance_weight_sum);
    encoder.f64(observation.squared_importance_weight_sum);
    encoder.u64(observation.non_finite_sample_count);
    return runtime::identity_digest(encoder.bytes());
}

semantic::IdentityDigest compute_technique_pilot_cross_observation_identity(
    const TechniquePilotCrossObservation& observation) {
    Encoder encoder;
    encoder.u32(observation.version);
    encoder.u32(observation.left_node_ordinal);
    encoder.u32(observation.right_node_ordinal);
    encoder.digest(observation.support_partition_identity);
    encoder.digest(observation.pairing_identity);
    encoder.digest(observation.left_observation_identity);
    encoder.digest(observation.right_observation_identity);
    encoder.digest(observation.pilot_provenance_identity);
    encoder.u64(observation.paired_sample_count);
    encode_doubles(encoder, observation.cross_moment_sums);
    return runtime::identity_digest(encoder.bytes());
}

PilotObservationValidation validate_technique_pilot_observation(
    const TechniquePilotObservation& observation) {
    PilotObservationValidation result;
    if (observation.version != kPilotContractVersion) {
        add(result.issues, PilotObservationIssue::Version);
    }
    if (semantic::identity_empty(
            observation.support_partition_identity) ||
        semantic::identity_empty(observation.observation_identity) ||
        semantic::identity_empty(
            observation.pilot_provenance_identity) ||
        observation.observation_identity !=
            compute_technique_pilot_observation_identity(observation)) {
        add(result.issues, PilotObservationIssue::Identity);
    }
    const auto channels = observation.first_moment_sums.size();
    if (channels == 0 || observation.second_moment_sums.size() != channels ||
        observation.absolute_tail_thresholds.size() != channels ||
        observation.tail_exceedance_counts.size() != channels ||
        observation.absolute_tail_excess_sums.size() != channels ||
        observation.maximum_absolute_contributions.size() != channels) {
        add(result.issues, PilotObservationIssue::Shape);
        return result;
    }
    if (observation.sample_count < 2) {
        add(result.issues, PilotObservationIssue::SampleCount);
    }
    if (observation.elapsed_nanoseconds == 0) {
        add(result.issues, PilotObservationIssue::Cost);
    }
    if (!finite_values(observation.first_moment_sums) ||
        !finite_values(observation.second_moment_sums) ||
        !finite_values(observation.absolute_tail_thresholds) ||
        !finite_values(observation.absolute_tail_excess_sums) ||
        !finite_values(observation.maximum_absolute_contributions) ||
        observation.non_finite_sample_count != 0) {
        add(result.issues, PilotObservationIssue::NonFinite);
    }
    for (std::size_t index = 0; index < channels; ++index) {
        const auto minimum_second =
            observation.first_moment_sums[index] *
            observation.first_moment_sums[index] /
            static_cast<double>(std::max<std::uint64_t>(
                1, observation.sample_count));
        if (observation.second_moment_sums[index] <
            minimum_second - 1e-12 * std::max(1.0, minimum_second)) {
            add(result.issues, PilotObservationIssue::NonFinite);
        }
        if (observation.absolute_tail_thresholds[index] < 0.0 ||
            observation.tail_exceedance_counts[index] >
                observation.sample_count ||
            observation.absolute_tail_excess_sums[index] < 0.0 ||
            observation.maximum_absolute_contributions[index] < 0.0 ||
            (observation.tail_exceedance_counts[index] != 0 &&
             observation.maximum_absolute_contributions[index] <
                 observation.absolute_tail_thresholds[index])) {
            add(result.issues, PilotObservationIssue::Tail);
        }
    }
    if (!std::isfinite(observation.importance_weight_sum) ||
        !std::isfinite(observation.squared_importance_weight_sum) ||
        observation.importance_weight_sum <= 0.0 ||
        observation.squared_importance_weight_sum <= 0.0 ||
        observation.importance_weight_sum *
                observation.importance_weight_sum /
                observation.squared_importance_weight_sum >
            static_cast<double>(observation.sample_count) *
                (1.0 + 1e-12)) {
        add(result.issues, PilotObservationIssue::Weight);
    }
    return result;
}

PilotObservationValidation validate_technique_pilot_cross_observation(
    const TechniquePilotCrossObservation& observation) {
    PilotObservationValidation result;
    if (observation.version != kPilotContractVersion) {
        add(result.issues, PilotObservationIssue::Version);
    }
    if (observation.left_node_ordinal >=
            observation.right_node_ordinal ||
        observation.cross_moment_sums.empty()) {
        add(result.issues, PilotObservationIssue::Pairing);
    }
    if (semantic::identity_empty(
            observation.support_partition_identity) ||
        semantic::identity_empty(observation.pairing_identity) ||
        semantic::identity_empty(
            observation.left_observation_identity) ||
        semantic::identity_empty(
            observation.right_observation_identity) ||
        semantic::identity_empty(observation.observation_identity) ||
        semantic::identity_empty(
            observation.pilot_provenance_identity) ||
        observation.observation_identity !=
            compute_technique_pilot_cross_observation_identity(
                observation)) {
        add(result.issues, PilotObservationIssue::Identity);
    }
    if (observation.paired_sample_count < 2) {
        add(result.issues, PilotObservationIssue::SampleCount);
    }
    if (!finite_values(observation.cross_moment_sums)) {
        add(result.issues, PilotObservationIssue::NonFinite);
    }
    return result;
}

void finalize_technique_pilot_observation(
    TechniquePilotObservation& observation) {
    observation.observation_identity =
        compute_technique_pilot_observation_identity(observation);
    if (!validate_technique_pilot_observation(observation).ok()) {
        throw std::invalid_argument("Invalid technique pilot observation");
    }
}

void finalize_technique_pilot_cross_observation(
    TechniquePilotCrossObservation& observation) {
    observation.observation_identity =
        compute_technique_pilot_cross_observation_identity(observation);
    if (!validate_technique_pilot_cross_observation(observation).ok()) {
        throw std::invalid_argument("Invalid pilot cross observation");
    }
}

TechniquePilotObservation accumulate_technique_pilot_samples(
    std::uint32_t node_ordinal,
    const semantic::IdentityDigest& support_partition_identity,
    const PilotSamplingProvenance& pilot_provenance,
    std::span<const TechniquePilotSample> samples,
    std::span<const double> absolute_tail_thresholds,
    std::uint64_t elapsed_nanoseconds,
    std::uint64_t peak_scratch_bytes,
    std::uint64_t persistent_bytes) {
    if (!validate_pilot_sampling_provenance(pilot_provenance).ok() ||
        samples.size() < 2 || absolute_tail_thresholds.empty() ||
        !finite_values(absolute_tail_thresholds) ||
        std::ranges::any_of(
            absolute_tail_thresholds,
            [](double value) { return value < 0.0; }) ||
        semantic::identity_empty(support_partition_identity) ||
        elapsed_nanoseconds == 0) {
        throw std::invalid_argument("Invalid pilot sample accumulation");
    }
    TechniquePilotObservation result;
    result.node_ordinal = node_ordinal;
    result.support_partition_identity = support_partition_identity;
    result.pilot_provenance_identity =
        pilot_sampling_provenance_identity(pilot_provenance);
    result.sample_count = static_cast<std::uint64_t>(samples.size());
    result.elapsed_nanoseconds = elapsed_nanoseconds;
    result.peak_scratch_bytes = peak_scratch_bytes;
    result.persistent_bytes = persistent_bytes;
    result.first_moment_sums.assign(
        absolute_tail_thresholds.size(), 0.0);
    result.second_moment_sums.assign(
        absolute_tail_thresholds.size(), 0.0);
    result.absolute_tail_thresholds.assign(
        absolute_tail_thresholds.begin(),
        absolute_tail_thresholds.end());
    result.tail_exceedance_counts.assign(
        absolute_tail_thresholds.size(), 0);
    result.absolute_tail_excess_sums.assign(
        absolute_tail_thresholds.size(), 0.0);
    result.maximum_absolute_contributions.assign(
        absolute_tail_thresholds.size(), 0.0);
    std::uint64_t previous_identity = 0;
    bool first = true;
    for (const auto& sample : samples) {
        if ((!first && sample.global_sample_identity <=
                         previous_identity) ||
            !range_contains(
                pilot_provenance.pilot_ranges,
                sample.global_sample_identity) ||
            sample.contributions.size() !=
                absolute_tail_thresholds.size() ||
            !finite_values(sample.contributions) ||
            !std::isfinite(sample.importance_weight) ||
            sample.importance_weight <= 0.0) {
            throw std::invalid_argument("Invalid pilot sample record");
        }
        previous_identity = sample.global_sample_identity;
        first = false;
        result.importance_weight_sum += sample.importance_weight;
        result.squared_importance_weight_sum +=
            sample.importance_weight * sample.importance_weight;
        for (std::size_t channel = 0;
             channel < sample.contributions.size(); ++channel) {
            const auto contribution = sample.contributions[channel];
            const auto absolute = std::abs(contribution);
            result.first_moment_sums[channel] += contribution;
            result.second_moment_sums[channel] +=
                contribution * contribution;
            result.maximum_absolute_contributions[channel] = std::max(
                result.maximum_absolute_contributions[channel], absolute);
            if (absolute > absolute_tail_thresholds[channel]) {
                ++result.tail_exceedance_counts[channel];
                result.absolute_tail_excess_sums[channel] +=
                    absolute - absolute_tail_thresholds[channel];
            }
        }
    }
    if (!finite_values(result.first_moment_sums) ||
        !finite_values(result.second_moment_sums) ||
        !finite_values(result.absolute_tail_excess_sums) ||
        !std::isfinite(result.importance_weight_sum) ||
        !std::isfinite(result.squared_importance_weight_sum)) {
        throw std::overflow_error("Pilot sample accumulation overflow");
    }
    finalize_technique_pilot_observation(result);
    return result;
}

TechniquePilotCrossObservation accumulate_technique_pilot_cross_samples(
    const TechniquePilotObservation& left_observation,
    const TechniquePilotObservation& right_observation,
    const PilotSamplingProvenance& pilot_provenance,
    const semantic::IdentityDigest& pairing_identity,
    std::span<const TechniquePilotSample> left,
    std::span<const TechniquePilotSample> right) {
    if (!validate_pilot_sampling_provenance(pilot_provenance).ok() ||
        !validate_technique_pilot_observation(left_observation).ok() ||
        !validate_technique_pilot_observation(right_observation).ok() ||
        left_observation.node_ordinal >= right_observation.node_ordinal ||
        left_observation.support_partition_identity !=
            right_observation.support_partition_identity ||
        left_observation.pilot_provenance_identity !=
            right_observation.pilot_provenance_identity ||
        left_observation.pilot_provenance_identity !=
            pilot_sampling_provenance_identity(pilot_provenance) ||
        left.size() < 2 ||
        left.size() != right.size() ||
        left.front().contributions.empty() ||
        left_observation.sample_count != left.size() ||
        right_observation.sample_count != right.size() ||
        left_observation.first_moment_sums.size() !=
            left.front().contributions.size() ||
        right_observation.first_moment_sums.size() !=
            right.front().contributions.size() ||
        semantic::identity_empty(pairing_identity)) {
        throw std::invalid_argument("Invalid pilot cross accumulation");
    }
    TechniquePilotCrossObservation result;
    result.left_node_ordinal = left_observation.node_ordinal;
    result.right_node_ordinal = right_observation.node_ordinal;
    result.support_partition_identity =
        left_observation.support_partition_identity;
    result.pairing_identity = pairing_identity;
    result.left_observation_identity =
        left_observation.observation_identity;
    result.right_observation_identity =
        right_observation.observation_identity;
    result.pilot_provenance_identity =
        left_observation.pilot_provenance_identity;
    result.paired_sample_count =
        static_cast<std::uint64_t>(left.size());
    result.cross_moment_sums.assign(
        left.front().contributions.size(), 0.0);
    std::vector<double> left_first_moment_sums(
        result.cross_moment_sums.size(), 0.0);
    std::vector<double> right_first_moment_sums(
        result.cross_moment_sums.size(), 0.0);
    std::uint64_t previous_identity = 0;
    bool first = true;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (left[index].global_sample_identity !=
                right[index].global_sample_identity ||
            (!first && left[index].global_sample_identity <=
                          previous_identity) ||
            !range_contains(
                pilot_provenance.pilot_ranges,
                left[index].global_sample_identity) ||
            left[index].contributions.size() !=
                result.cross_moment_sums.size() ||
            right[index].contributions.size() !=
                result.cross_moment_sums.size() ||
            !finite_values(left[index].contributions) ||
            !finite_values(right[index].contributions)) {
            throw std::invalid_argument("Unpaired pilot sample record");
        }
        previous_identity = left[index].global_sample_identity;
        first = false;
        for (std::size_t channel = 0;
             channel < result.cross_moment_sums.size(); ++channel) {
            left_first_moment_sums[channel] +=
                left[index].contributions[channel];
            right_first_moment_sums[channel] +=
                right[index].contributions[channel];
            result.cross_moment_sums[channel] +=
                left[index].contributions[channel] *
                right[index].contributions[channel];
        }
    }
    if (!finite_values(result.cross_moment_sums)) {
        throw std::overflow_error("Pilot cross accumulation overflow");
    }
    if (left_first_moment_sums !=
            left_observation.first_moment_sums ||
        right_first_moment_sums !=
            right_observation.first_moment_sums) {
        throw std::invalid_argument(
            "Pilot cross samples do not match observations");
    }
    finalize_technique_pilot_cross_observation(result);
    return result;
}

TechniquePilotEstimate summarize_technique_pilot(
    const TechniquePilotObservation& observation) {
    if (!validate_technique_pilot_observation(observation).ok()) {
        throw std::invalid_argument("Invalid pilot observation");
    }
    TechniquePilotEstimate result;
    result.node_ordinal = observation.node_ordinal;
    result.support_partition_identity =
        observation.support_partition_identity;
    result.pilot_provenance_identity =
        observation.pilot_provenance_identity;
    result.sample_count = observation.sample_count;
    result.nanoseconds_per_sample =
        observation.elapsed_nanoseconds / observation.sample_count +
        (observation.elapsed_nanoseconds % observation.sample_count != 0);
    result.peak_scratch_bytes = observation.peak_scratch_bytes;
    result.persistent_bytes = observation.persistent_bytes;
    const auto count = static_cast<double>(observation.sample_count);
    for (std::size_t index = 0;
         index < observation.first_moment_sums.size(); ++index) {
        const auto mean = observation.first_moment_sums[index] / count;
        auto variance =
            (observation.second_moment_sums[index] -
             observation.first_moment_sums[index] * mean) /
            static_cast<double>(observation.sample_count - 1);
        if (variance < 0.0 && variance > -1e-12) variance = 0.0;
        result.means.push_back(mean);
        result.sample_variances.push_back(variance);
        result.absolute_tail_thresholds.push_back(
            observation.absolute_tail_thresholds[index]);
        result.tail_exceedance_rates.push_back(
            static_cast<double>(
                observation.tail_exceedance_counts[index]) /
            count);
        result.mean_absolute_tail_excesses.push_back(
            observation.tail_exceedance_counts[index] == 0
            ? 0.0
            : observation.absolute_tail_excess_sums[index] /
                  static_cast<double>(
                      observation.tail_exceedance_counts[index]));
        result.maximum_absolute_contributions.push_back(
            observation.maximum_absolute_contributions[index]);
    }
    result.effective_sample_size =
        observation.importance_weight_sum *
        observation.importance_weight_sum /
        observation.squared_importance_weight_sum;
    result.estimate_identity = estimate_identity(result);
    return result;
}

bool validate_technique_pilot_estimate(
    const TechniquePilotEstimate& estimate) {
    const auto channels = estimate.means.size();
    return estimate.sample_count >= 2 &&
        estimate.nanoseconds_per_sample != 0 &&
        !semantic::identity_empty(
            estimate.support_partition_identity) &&
        !semantic::identity_empty(
            estimate.pilot_provenance_identity) &&
        !semantic::identity_empty(estimate.estimate_identity) &&
        estimate.estimate_identity == estimate_identity(estimate) &&
        channels != 0 &&
        estimate.sample_variances.size() == channels &&
        estimate.absolute_tail_thresholds.size() == channels &&
        estimate.tail_exceedance_rates.size() == channels &&
        estimate.mean_absolute_tail_excesses.size() == channels &&
        estimate.maximum_absolute_contributions.size() == channels &&
        finite_values(estimate.means) &&
        finite_values(estimate.sample_variances) &&
        finite_values(estimate.absolute_tail_thresholds) &&
        finite_values(estimate.tail_exceedance_rates) &&
        finite_values(estimate.mean_absolute_tail_excesses) &&
        finite_values(estimate.maximum_absolute_contributions) &&
        std::ranges::all_of(
            estimate.sample_variances,
            [](double value) { return value >= 0.0; }) &&
        std::ranges::all_of(
            estimate.absolute_tail_thresholds,
            [](double value) { return value >= 0.0; }) &&
        std::ranges::all_of(
            estimate.tail_exceedance_rates,
            [](double value) {
                return value >= 0.0 && value <= 1.0;
            }) &&
        std::ranges::all_of(
            estimate.mean_absolute_tail_excesses,
            [](double value) { return value >= 0.0; }) &&
        std::ranges::all_of(
            estimate.maximum_absolute_contributions,
            [](double value) { return value >= 0.0; }) &&
        std::isfinite(estimate.effective_sample_size) &&
        estimate.effective_sample_size > 0.0 &&
        estimate.effective_sample_size <=
            static_cast<double>(estimate.sample_count) * (1.0 + 1e-12);
}

TechniquePilotCovariance summarize_technique_pilot_covariance(
    const TechniquePilotObservation& left,
    const TechniquePilotObservation& right,
    const TechniquePilotCrossObservation& cross) {
    if (!validate_technique_pilot_observation(left).ok() ||
        !validate_technique_pilot_observation(right).ok() ||
        !validate_technique_pilot_cross_observation(cross).ok() ||
        left.node_ordinal != cross.left_node_ordinal ||
        right.node_ordinal != cross.right_node_ordinal ||
        left.support_partition_identity !=
            cross.support_partition_identity ||
        right.support_partition_identity !=
            cross.support_partition_identity ||
        left.pilot_provenance_identity !=
            cross.pilot_provenance_identity ||
        right.pilot_provenance_identity !=
            cross.pilot_provenance_identity ||
        left.observation_identity !=
            cross.left_observation_identity ||
        right.observation_identity !=
            cross.right_observation_identity ||
        left.sample_count != cross.paired_sample_count ||
        right.sample_count != cross.paired_sample_count ||
        left.first_moment_sums.size() !=
            cross.cross_moment_sums.size() ||
        right.first_moment_sums.size() !=
            cross.cross_moment_sums.size()) {
        throw std::invalid_argument("Invalid pilot covariance inputs");
    }
    TechniquePilotCovariance result;
    result.left_node_ordinal = left.node_ordinal;
    result.right_node_ordinal = right.node_ordinal;
    result.support_partition_identity =
        cross.support_partition_identity;
    result.pilot_provenance_identity =
        cross.pilot_provenance_identity;
    result.pairing_identity = cross.pairing_identity;
    result.left_observation_identity =
        cross.left_observation_identity;
    result.right_observation_identity =
        cross.right_observation_identity;
    result.paired_sample_count = cross.paired_sample_count;
    const auto count = static_cast<double>(cross.paired_sample_count);
    for (std::size_t index = 0;
         index < cross.cross_moment_sums.size(); ++index) {
        result.sample_covariances.push_back(
            (cross.cross_moment_sums[index] -
             left.first_moment_sums[index] *
                 right.first_moment_sums[index] / count) /
            static_cast<double>(cross.paired_sample_count - 1));
    }
    result.covariance_identity = covariance_identity(result);
    return result;
}

bool validate_technique_pilot_covariance(
    const TechniquePilotCovariance& covariance) {
    return covariance.left_node_ordinal !=
            covariance.right_node_ordinal &&
        !semantic::identity_empty(
            covariance.support_partition_identity) &&
        !semantic::identity_empty(
            covariance.pilot_provenance_identity) &&
        !semantic::identity_empty(covariance.pairing_identity) &&
        !semantic::identity_empty(
            covariance.left_observation_identity) &&
        !semantic::identity_empty(
            covariance.right_observation_identity) &&
        !semantic::identity_empty(covariance.covariance_identity) &&
        covariance.paired_sample_count >= 2 &&
        !covariance.sample_covariances.empty() &&
        finite_values(covariance.sample_covariances) &&
        covariance.covariance_identity == covariance_identity(covariance);
}

}
