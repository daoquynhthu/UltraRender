#include "ure/research/experiment.hpp"

#include "ure/runtime/multi_backend.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace ure::research {
namespace {

const ExperimentVariant& find_variant(
    const ExperimentDefinition& definition,
    const semantic::IdentityDigest& identity) {
    const auto found = std::ranges::find(
        definition.variants, identity, &ExperimentVariant::variant_identity);
    if (found == definition.variants.end()) {
        throw std::invalid_argument("Unknown experiment variant");
    }
    return *found;
}

semantic::IdentityDigest derive_seed_namespace(
    const semantic::IdentityDigest& base,
    const semantic::IdentityDigest& variant) {
    std::array<std::uint8_t, 64> bytes{};
    std::ranges::copy(base, bytes.begin());
    std::ranges::copy(variant, bytes.begin() + 32);
    return runtime::identity_digest(std::as_bytes(std::span{bytes}));
}

double mean(const std::vector<ExperimentObservation>& values) {
    return std::transform_reduce(
               values.begin(), values.end(), 0.0, std::plus{},
               [](const ExperimentObservation& value) {
                   return value.value;
               }) /
           static_cast<double>(values.size());
}

double sample_variance(const std::vector<ExperimentObservation>& values,
                       double average) {
    const auto squared = std::transform_reduce(
        values.begin(), values.end(), 0.0, std::plus{},
        [average](const ExperimentObservation& value) {
            const auto delta = value.value - average;
            return delta * delta;
        });
    return squared / static_cast<double>(values.size() - 1);
}

double normal_quantile(double probability) {
    constexpr std::array a{
        -3.969683028665376e+01, 2.209460984245205e+02,
        -2.759285104469687e+02, 1.383577518672690e+02,
        -3.066479806614716e+01, 2.506628277459239e+00};
    constexpr std::array b{
        -5.447609879822406e+01, 1.615858368580409e+02,
        -1.556989798598866e+02, 6.680131188771972e+01,
        -1.328068155288572e+01};
    constexpr std::array c{
        -7.784894002430293e-03, -3.223964580411365e-01,
        -2.400758277161838e+00, -2.549732539343734e+00,
        4.374664141464968e+00, 2.938163982698783e+00};
    constexpr std::array d{
        7.784695709041462e-03, 3.224671290700398e-01,
        2.445134137142996e+00, 3.754408661907416e+00};
    constexpr double low = 0.02425;
    constexpr double high = 1.0 - low;
    if (probability < low) {
        const auto q = std::sqrt(-2.0 * std::log(probability));
        return (((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q +
                  c[4]) *
                     q +
                 c[5]) /
               ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1.0);
    }
    if (probability <= high) {
        const auto q = probability - 0.5;
        const auto r = q * q;
        return (((((a[0] * r + a[1]) * r + a[2]) * r + a[3]) * r +
                  a[4]) *
                     r +
                 a[5]) *
               q /
               (((((b[0] * r + b[1]) * r + b[2]) * r + b[3]) * r +
                 b[4]) *
                    r +
                1.0);
    }
    const auto q = std::sqrt(-2.0 * std::log(1.0 - probability));
    return -(((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q +
              c[4]) *
                 q +
             c[5]) /
           ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1.0);
}

void validate_observation(const ExperimentObservation& observation,
                          std::uint64_t expected_samples) {
    if (!std::isfinite(observation.value) ||
        !std::isfinite(observation.within_run_variance) ||
        observation.within_run_variance < 0.0 ||
        observation.sample_count != expected_samples ||
        semantic::identity_empty(observation.artifact_identity)) {
        throw std::invalid_argument("Invalid experiment observation");
    }
}

void validate_distinct_artifacts(
    const std::vector<ExperimentObservation>& baseline,
    const std::vector<ExperimentObservation>& candidate) {
    std::vector<semantic::IdentityDigest> identities;
    identities.reserve(baseline.size() + candidate.size());
    for (const auto& observation : baseline) {
        identities.push_back(observation.artifact_identity);
    }
    for (const auto& observation : candidate) {
        identities.push_back(observation.artifact_identity);
    }
    std::ranges::sort(identities);
    if (std::ranges::adjacent_find(identities) != identities.end()) {
        throw std::invalid_argument(
            "Experiment replicates reuse an artifact identity");
    }
}

}

void ExperimentRegistry::add(ExperimentDefinition definition) {
    if (semantic::identity_empty(definition.capsule_identity) ||
        semantic::identity_empty(definition.source_identity) ||
        semantic::identity_empty(definition.seed_namespace_identity) ||
        !transport::validate_context(definition.semantics).ok() ||
        definition.variants.size() < 2 ||
        std::ranges::any_of(
            definitions_,
            [&definition](const ExperimentDefinition& existing) {
                return existing.capsule_identity == definition.capsule_identity;
            })) {
        throw std::invalid_argument("Invalid experiment definition");
    }
    for (std::size_t index = 0; index < definition.variants.size(); ++index) {
        const auto& variant = definition.variants[index];
        if (semantic::identity_empty(variant.variant_identity) ||
            semantic::identity_empty(variant.parameter_identity) ||
            std::ranges::find(definition.variants.begin(),
                              definition.variants.begin() + index,
                              variant.variant_identity,
                              &ExperimentVariant::variant_identity) !=
                definition.variants.begin() + index) {
            throw std::invalid_argument("Invalid experiment variant");
        }
    }
    definitions_.push_back(std::move(definition));
}

const ExperimentDefinition& ExperimentRegistry::find(
    const semantic::IdentityDigest& capsule_identity) const {
    const auto found = std::ranges::find(
        definitions_, capsule_identity,
        &ExperimentDefinition::capsule_identity);
    if (found == definitions_.end()) {
        throw std::invalid_argument("Unknown experiment capsule");
    }
    return *found;
}

ComparisonResult run_comparison(
    const ExperimentRegistry& registry,
    const ComparisonRequest& request,
    const std::vector<FeatureCapability>& capabilities,
    const ExperimentExecutor& executor) {
    if (!executor || request.replicate_count < 2 ||
        request.samples_per_replicate == 0 ||
        request.counters_per_sample == 0 || request.workers.empty() ||
        !std::isfinite(request.confidence_level) ||
        request.confidence_level <= 0.5 ||
        request.confidence_level >= 1.0 ||
        request.baseline_variant_identity ==
            request.candidate_variant_identity) {
        throw std::invalid_argument("Invalid experiment comparison request");
    }
    const auto& definition = registry.find(request.capsule_identity);
    const auto& baseline = find_variant(
        definition, request.baseline_variant_identity);
    const auto& candidate = find_variant(
        definition, request.candidate_variant_identity);
    if (!negotiate_capabilities(baseline.requirements, capabilities).executable ||
        !negotiate_capabilities(candidate.requirements, capabilities).executable) {
        throw std::runtime_error("Experiment capabilities are not executable");
    }

    ComparisonResult result;
    result.baseline.reserve(request.replicate_count);
    result.candidate.reserve(request.replicate_count);
    const auto execute_variant = [&](const ExperimentVariant& variant,
                                     std::uint32_t replicate) {
        ResearchExecutionManifest manifest;
        manifest.capsule_identity = definition.capsule_identity;
        manifest.source_identity = definition.source_identity;
        manifest.parameter_identity = variant.parameter_identity;
        manifest.seed_namespace_identity = derive_seed_namespace(
            definition.seed_namespace_identity, variant.variant_identity);
        manifest.semantics = definition.semantics;
        manifest.global_seed = request.global_seed;
        manifest.replicate_index = replicate;
        manifest.mode = request.mode;
        ExperimentInvocation invocation;
        invocation.variant_identity = variant.variant_identity;
        invocation.manifest = manifest;
        invocation.shards = allocate_execution_shards(
            manifest,
            request.workers,
            request.samples_per_replicate,
            request.counters_per_sample);
        auto observation = executor(invocation);
        validate_observation(observation,
                             request.samples_per_replicate);
        return observation;
    };

    for (std::uint32_t replicate = 0;
         replicate < request.replicate_count;
         ++replicate) {
        result.baseline.push_back(execute_variant(baseline, replicate));
        result.candidate.push_back(execute_variant(candidate, replicate));
    }
    validate_distinct_artifacts(result.baseline, result.candidate);
    result.baseline_mean = mean(result.baseline);
    result.candidate_mean = mean(result.candidate);
    result.difference_mean =
        result.candidate_mean - result.baseline_mean;
    const auto count = static_cast<double>(request.replicate_count);
    result.standard_error = std::sqrt(
        sample_variance(result.baseline, result.baseline_mean) / count +
        sample_variance(result.candidate, result.candidate_mean) / count);
    const auto quantile = normal_quantile(
        0.5 + request.confidence_level * 0.5);
    result.difference_interval = {
        request.confidence_level,
        result.difference_mean - quantile * result.standard_error,
        result.difference_mean + quantile * result.standard_error};
    if (!validate_comparison_result(result)) {
        throw std::runtime_error("Generated invalid comparison result");
    }
    return result;
}

bool validate_comparison_result(
    const ComparisonResult& result,
    double relative_tolerance) {
    if (result.baseline.size() < 2 ||
        result.baseline.size() != result.candidate.size() ||
        !std::isfinite(relative_tolerance) ||
        relative_tolerance < 0.0 ||
        !std::isfinite(result.baseline_mean) ||
        !std::isfinite(result.candidate_mean) ||
        !std::isfinite(result.difference_mean) ||
        !std::isfinite(result.standard_error) ||
        result.standard_error < 0.0 ||
        !std::isfinite(result.difference_interval.level) ||
        result.difference_interval.level <= 0.5 ||
        result.difference_interval.level >= 1.0 ||
        !std::isfinite(result.difference_interval.lower) ||
        !std::isfinite(result.difference_interval.upper) ||
        result.difference_interval.lower > result.difference_interval.upper) {
        return false;
    }
    const auto sample_count = result.baseline.front().sample_count;
    if (sample_count == 0) return false;
    const auto valid_observations = [sample_count](
        const std::vector<ExperimentObservation>& values) {
        return std::ranges::all_of(
            values,
            [sample_count](const ExperimentObservation& value) {
                return std::isfinite(value.value) &&
                    std::isfinite(value.within_run_variance) &&
                    value.within_run_variance >= 0.0 &&
                    value.sample_count == sample_count &&
                    !semantic::identity_empty(value.artifact_identity);
            });
    };
    if (!valid_observations(result.baseline) ||
        !valid_observations(result.candidate)) {
        return false;
    }
    try {
        validate_distinct_artifacts(result.baseline, result.candidate);
    } catch (const std::invalid_argument&) {
        return false;
    }
    const auto baseline_mean = mean(result.baseline);
    const auto candidate_mean = mean(result.candidate);
    const auto difference = candidate_mean - baseline_mean;
    const auto count = static_cast<double>(result.baseline.size());
    const auto standard_error = std::sqrt(
        sample_variance(result.baseline, baseline_mean) / count +
        sample_variance(result.candidate, candidate_mean) / count);
    const auto quantile = normal_quantile(
        0.5 + result.difference_interval.level * 0.5);
    const auto lower = difference - quantile * standard_error;
    const auto upper = difference + quantile * standard_error;
    const auto close = [relative_tolerance](double left, double right) {
        return std::abs(left - right) <= relative_tolerance *
            std::max({1.0, std::abs(left), std::abs(right)});
    };
    return close(result.baseline_mean, baseline_mean) &&
        close(result.candidate_mean, candidate_mean) &&
        close(result.difference_mean, difference) &&
        close(result.standard_error, standard_error) &&
        close(result.difference_interval.lower, lower) &&
        close(result.difference_interval.upper, upper);
}

}
