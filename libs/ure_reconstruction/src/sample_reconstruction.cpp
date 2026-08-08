#include "ure/reconstruction/sample_reconstruction.hpp"

#include "ure/runtime/multi_backend.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace ure::reconstruction {
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
    void doubles(std::span<const double> values) {
        u64(static_cast<std::uint64_t>(values.size()));
        for (const auto value : values) f64(value);
    }
    void digests(std::span<const semantic::IdentityDigest> values) {
        u64(static_cast<std::uint64_t>(values.size()));
        for (const auto& value : values) digest(value);
    }
    void observable(const transport::ObservableDescriptor& value) {
        u32(value.version);
        u8(static_cast<std::uint8_t>(value.kind));
        u8(static_cast<std::uint8_t>(value.value_domain));
        u8(static_cast<std::uint8_t>(value.coherence));
        u32(value.component_count);
        u8(value.time_resolved ? 1 : 0);
        u8(static_cast<std::uint8_t>(value.unit.dimension.length));
        u8(static_cast<std::uint8_t>(value.unit.dimension.mass));
        u8(static_cast<std::uint8_t>(value.unit.dimension.time));
        u8(static_cast<std::uint8_t>(
            value.unit.dimension.electric_current));
        u8(static_cast<std::uint8_t>(value.unit.dimension.temperature));
        u8(static_cast<std::uint8_t>(value.unit.dimension.amount));
        u8(static_cast<std::uint8_t>(
            value.unit.dimension.luminous_intensity));
        f64(value.unit.scale_to_si);
        f64(value.unit.offset_to_si);
        u8(value.unit.affine ? 1 : 0);
        digest(value.phase_reference_identity);
        digest(value.sensor_response_identity);
    }
    void provenance(const semantic::ProvenanceIdentitySet& value) {
        digest(value.world_definition);
        digest(value.world_state);
        digest(value.time_sample);
        digest(value.observation_snapshot);
        digest(value.technique_graph);
        digest(value.measurement_schema);
        digest(value.parameter_set);
        digest(value.solver_semantics);
        digest(value.evidence);
    }
    std::span<const std::byte> bytes() const { return bytes_; }

private:
    std::vector<std::byte> bytes_;
};

void add(SampleReconstructionValidation& result,
         SampleReconstructionIssue issue) {
    if (std::ranges::find(result.issues, issue) == result.issues.end()) {
        result.issues.push_back(issue);
    }
}

bool finite(std::span<const double> values) {
    return std::ranges::all_of(
        values, [](double value) { return std::isfinite(value); });
}

bool required_provenance(
    const semantic::ProvenanceIdentitySet& identities) {
    return !semantic::identity_empty(identities.world_definition) &&
        !semantic::identity_empty(identities.world_state) &&
        !semantic::identity_empty(identities.time_sample) &&
        !semantic::identity_empty(identities.observation_snapshot) &&
        !semantic::identity_empty(identities.technique_graph) &&
        !semantic::identity_empty(identities.measurement_schema);
}

std::size_t pixel_count(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0 ||
        width > std::numeric_limits<std::size_t>::max() / height) {
        return 0;
    }
    return static_cast<std::size_t>(width) * height;
}

bool supported_observable(
    const transport::ObservableDescriptor& observable) {
    if (!transport::validate_observable(observable).ok()) return false;
    if (observable.value_domain == transport::ValueDomain::Spectrum) {
        return observable.coherence == transport::CoherenceClass::Incoherent;
    }
    if (observable.value_domain == transport::ValueDomain::Stokes) {
        return observable.coherence == transport::CoherenceClass::Incoherent &&
            observable.component_count == 4;
    }
    if (observable.value_domain == transport::ValueDomain::ComplexJones) {
        return observable.coherence == transport::CoherenceClass::Coherent &&
            observable.component_count == 4 &&
            !semantic::identity_empty(observable.phase_reference_identity);
    }
    return false;
}

bool physical_value(std::span<const double> value,
                    transport::ValueDomain domain) {
    if (!finite(value)) return false;
    if (domain == transport::ValueDomain::Spectrum) {
        return std::ranges::all_of(
            value, [](double component) { return component >= 0.0; });
    }
    if (domain == transport::ValueDomain::Stokes) {
        if (value.size() != 4 || value[0] < 0.0) return false;
        const double polarized = std::sqrt(
            value[1] * value[1] + value[2] * value[2] +
            value[3] * value[3]);
        return polarized <= value[0] +
            1e-10 * std::max(1.0, value[0]);
    }
    return true;
}

void encode_record(Encoder& encoder,
                   const SampleReconstructionRecord& record) {
    encoder.digest(record.sample_identity);
    encoder.digest(record.technique_identity);
    encoder.digest(record.path_event_identity);
    encoder.digest(record.material_identity);
    encoder.digest(record.medium_identity);
    encoder.digest(record.spectral_resource_identity);
    encoder.digest(record.phase_reference_identity);
    encoder.f64(record.raster_x);
    encoder.f64(record.raster_y);
    encoder.f64(record.time_seconds);
    encoder.f64(record.detector_wavelength_nm);
    encoder.f64(record.transport_wavelength_nm);
    encoder.f64(record.joint_pdf);
    encoder.f64(record.estimator_weight);
    encoder.f64(record.kernel_radius);
    encoder.f64(record.depth);
    encoder.doubles(record.normal);
    encoder.doubles(record.feature_albedo);
    encoder.doubles(record.value);
    encoder.u8(record.valid ? 1 : 0);
}

void encode_applicability(
    Encoder& encoder,
    const SampleReconstructionApplicability& applicability) {
    encoder.f64(applicability.minimum_wavelength_nm);
    encoder.f64(applicability.maximum_wavelength_nm);
    encoder.f64(applicability.maximum_polarization_degree);
    encoder.u64(applicability.minimum_sample_count);
    encoder.u64(applicability.maximum_sample_count);
    encoder.u32(applicability.component_count);
    encoder.u8(applicability.spectrum ? 1 : 0);
    encoder.u8(applicability.stokes ? 1 : 0);
    encoder.u8(applicability.complex_jones ? 1 : 0);
    encoder.digests(applicability.world_definition_identities);
    encoder.digests(applicability.measurement_schema_identities);
    encoder.digests(applicability.technique_identities);
    encoder.digests(applicability.material_identities);
}

double normal_dot(const std::array<double, 3>& left,
                  const std::array<double, 3>& right) {
    const double left_length = std::sqrt(
        left[0] * left[0] + left[1] * left[1] + left[2] * left[2]);
    const double right_length = std::sqrt(
        right[0] * right[0] + right[1] * right[1] +
        right[2] * right[2]);
    if (!(left_length > 0.0) || !(right_length > 0.0)) return -1.0;
    return std::clamp(
        (left[0] * right[0] + left[1] * right[1] +
         left[2] * right[2]) / (left_length * right_length),
        -1.0, 1.0);
}

double squared_distance(std::span<const double> left,
                        std::span<const double> right) {
    double result = 0.0;
    for (std::size_t index = 0; index < left.size(); ++index) {
        const double difference = left[index] - right[index];
        result += difference * difference;
    }
    return result / static_cast<double>(left.size());
}

bool contains_identity(
    std::span<const semantic::IdentityDigest> values,
    const semantic::IdentityDigest& target) {
    return values.empty() ||
        std::ranges::find(values, target) != values.end();
}

bool valid_identity_set(
    std::span<const semantic::IdentityDigest> values) {
    semantic::IdentityDigest previous = {};
    bool first = true;
    for (const auto& value : values) {
        if (semantic::identity_empty(value) ||
            (!first && !(previous < value))) {
            return false;
        }
        previous = value;
        first = false;
    }
    return true;
}

double polarization_degree(const SampleReconstructionRecord& record) {
    if (record.value.size() != 4 || !(record.value[0] > 0.0)) return 0.0;
    return std::sqrt(
        record.value[1] * record.value[1] +
        record.value[2] * record.value[2] +
        record.value[3] * record.value[3]) / record.value[0];
}

const SampleReconstructionWeight* find_weight(
    const SampleReconstructionExternalWeights* weights,
    const semantic::IdentityDigest& sample_identity) {
    if (!weights) return nullptr;
    const auto found = std::ranges::lower_bound(
        weights->weights, sample_identity, {},
        &SampleReconstructionWeight::sample_identity);
    if (found == weights->weights.end() ||
        found->sample_identity != sample_identity) {
        return nullptr;
    }
    return &*found;
}

void project_stokes(std::span<double> value) {
    const double radius = std::sqrt(
        value[1] * value[1] + value[2] * value[2] +
        value[3] * value[3]);
    if (radius <= value[0]) return;
    if (radius <= -value[0]) {
        std::ranges::fill(value, 0.0);
        return;
    }
    const double intensity = 0.5 * (radius + value[0]);
    const double scale = intensity / radius;
    value[0] = intensity;
    value[1] *= scale;
    value[2] *= scale;
    value[3] *= scale;
}

bool project_spectrum(std::span<double> value,
                      std::span<const double> response,
                      double observation,
                      double tolerance) {
    if (response.empty()) {
        for (auto& component : value) component = std::max(0.0, component);
        return true;
    }
    double low = -1.0;
    double high = 1.0;
    const auto residual = [&](double lambda) {
        double result = -observation;
        for (std::size_t index = 0; index < value.size(); ++index) {
            result += response[index] *
                std::max(0.0, value[index] + lambda * response[index]);
        }
        return result;
    };
    for (int iteration = 0; iteration < 128 && residual(low) > 0.0;
         ++iteration) {
        low *= 2.0;
    }
    for (int iteration = 0; iteration < 128 && residual(high) < 0.0;
         ++iteration) {
        high *= 2.0;
    }
    for (int iteration = 0; iteration < 128; ++iteration) {
        const double middle = 0.5 * (low + high);
        if (residual(middle) < 0.0) low = middle;
        else high = middle;
    }
    const double lambda = 0.5 * (low + high);
    double projected_observation = 0.0;
    for (std::size_t index = 0; index < value.size(); ++index) {
        value[index] = std::max(
            0.0, value[index] + lambda * response[index]);
        projected_observation += response[index] * value[index];
    }
    return std::abs(projected_observation - observation) <=
        tolerance * std::max(1.0, std::abs(observation));
}

}

bool SampleReconstructionValidation::has(
    SampleReconstructionIssue issue) const {
    return std::ranges::find(issues, issue) != issues.end();
}

semantic::IdentityDigest compute_sample_reconstruction_batch_identity(
    const SampleReconstructionBatch& batch) {
    Encoder encoder;
    encoder.u32(batch.version);
    encoder.observable(batch.observable);
    encoder.provenance(batch.identities);
    encoder.digest(batch.measurement_schema_identity);
    encoder.u32(batch.width);
    encoder.u32(batch.height);
    encoder.u32(batch.component_count);
    encoder.doubles(batch.component_wavelength_nm);
    encoder.doubles(batch.sensor_response);
    encoder.doubles(batch.sensor_observation);
    std::vector<const SampleReconstructionRecord*> records;
    records.reserve(batch.records.size());
    for (const auto& record : batch.records) records.push_back(&record);
    std::ranges::sort(records, {},
        [](const SampleReconstructionRecord* record) {
            return record->sample_identity;
        });
    encoder.u64(static_cast<std::uint64_t>(records.size()));
    for (const auto* record : records) encode_record(encoder, *record);
    return runtime::identity_digest(encoder.bytes());
}

void finalize_sample_reconstruction_batch(SampleReconstructionBatch& batch) {
    batch.batch_identity = compute_sample_reconstruction_batch_identity(batch);
}

semantic::IdentityDigest compute_sample_reconstruction_candidate_identity(
    const SampleReconstructionCandidate& candidate) {
    Encoder encoder;
    encoder.u32(candidate.version);
    encoder.digest(candidate.capsule_identity);
    encoder.digest(candidate.source_identity);
    encoder.digest(candidate.hypothesis_identity);
    encoder.digest(candidate.algorithm_identity);
    encoder.digest(candidate.provider_identity);
    encoder.digest(candidate.artifact_identity);
    encoder.digest(candidate.failure_domain_identity);
    encoder.u8(static_cast<std::uint8_t>(candidate.maturity));
    encoder.u8(static_cast<std::uint8_t>(candidate.method));
    encode_applicability(encoder, candidate.applicability);
    encoder.u8(candidate.permutation_invariant ? 1 : 0);
    encoder.u8(candidate.consumes_technique_metadata ? 1 : 0);
    encoder.u8(candidate.consumes_path_metadata ? 1 : 0);
    encoder.u8(candidate.consumes_spectral_metadata ? 1 : 0);
    return runtime::identity_digest(encoder.bytes());
}

void finalize_sample_reconstruction_candidate(
    SampleReconstructionCandidate& candidate) {
    std::ranges::sort(candidate.applicability.technique_identities);
    std::ranges::sort(candidate.applicability.material_identities);
    std::ranges::sort(
        candidate.applicability.world_definition_identities);
    std::ranges::sort(
        candidate.applicability.measurement_schema_identities);
    candidate.candidate_identity =
        compute_sample_reconstruction_candidate_identity(candidate);
}

semantic::IdentityDigest compute_sample_reconstruction_weights_identity(
    const SampleReconstructionExternalWeights& weights) {
    Encoder encoder;
    encoder.u32(weights.version);
    encoder.digest(weights.batch_identity);
    encoder.digest(weights.candidate_identity);
    encoder.digest(weights.provider_identity);
    encoder.digest(weights.artifact_identity);
    encoder.u64(static_cast<std::uint64_t>(weights.weights.size()));
    for (const auto& weight : weights.weights) {
        encoder.digest(weight.sample_identity);
        encoder.f64(weight.multiplier);
        encoder.f64(weight.kernel_radius);
        encoder.f64(weight.confidence);
    }
    return runtime::identity_digest(encoder.bytes());
}

void finalize_sample_reconstruction_weights(
    SampleReconstructionExternalWeights& weights) {
    std::ranges::sort(weights.weights, {},
        &SampleReconstructionWeight::sample_identity);
    weights.weights_identity =
        compute_sample_reconstruction_weights_identity(weights);
}

semantic::IdentityDigest compute_sample_reconstruction_config_identity(
    const SampleReconstructionConfig& config) {
    Encoder encoder;
    encoder.u32(config.version);
    encoder.u8(static_cast<std::uint8_t>(config.method));
    encoder.u8(static_cast<std::uint8_t>(config.projection));
    encoder.f64(config.normal_sigma);
    encoder.f64(config.depth_sigma);
    encoder.f64(config.albedo_sigma);
    encoder.f64(config.time_sigma_seconds);
    encoder.f64(config.wavelength_sigma_nm);
    encoder.f64(config.maximum_kernel_radius);
    encoder.f64(config.observation_tolerance);
    encoder.u32(config.minimum_support);
    encoder.u8(config.explicit_research_opt_in ? 1 : 0);
    encoder.u8(config.allow_ood ? 1 : 0);
    return runtime::identity_digest(encoder.bytes());
}

void finalize_sample_reconstruction_config(
    SampleReconstructionConfig& config) {
    config.config_identity =
        compute_sample_reconstruction_config_identity(config);
}

SampleReconstructionValidation validate_sample_reconstruction_batch(
    const SampleReconstructionBatch& batch) {
    SampleReconstructionValidation result;
    if (batch.version != kSampleReconstructionVersion) {
        add(result, SampleReconstructionIssue::Version);
    }
    const auto pixels = pixel_count(batch.width, batch.height);
    if (pixels == 0 || batch.component_count == 0 || batch.records.empty() ||
        batch.observable.component_count != batch.component_count ||
        (batch.observable.value_domain == transport::ValueDomain::Spectrum &&
         batch.component_wavelength_nm.size() != batch.component_count) ||
        (!batch.component_wavelength_nm.empty() &&
         batch.component_wavelength_nm.size() != batch.component_count) ||
        (!batch.sensor_response.empty() &&
         batch.sensor_response.size() != batch.component_count) ||
        (!batch.sensor_observation.empty() &&
         batch.sensor_observation.size() != pixels) ||
        (batch.sensor_response.empty() != batch.sensor_observation.empty())) {
        add(result, SampleReconstructionIssue::Shape);
    }
    if (!supported_observable(batch.observable)) {
        add(result, SampleReconstructionIssue::Observable);
    }
    if (!required_provenance(batch.identities) ||
        batch.measurement_schema_identity !=
            batch.identities.measurement_schema) {
        add(result, SampleReconstructionIssue::Provenance);
    }
    if (!finite(batch.component_wavelength_nm) ||
        !finite(batch.sensor_response) ||
        !finite(batch.sensor_observation) ||
        std::ranges::any_of(
            batch.sensor_response,
            [](double value) { return value < 0.0; }) ||
        std::ranges::any_of(
            batch.sensor_observation,
            [](double value) { return value < 0.0; }) ||
        (!batch.sensor_response.empty() &&
         !std::ranges::any_of(
             batch.sensor_response,
             [](double value) { return value > 0.0; }))) {
        add(result, SampleReconstructionIssue::NonFinite);
    }
    if (batch.observable.value_domain == transport::ValueDomain::Spectrum) {
        double previous_wavelength = 0.0;
        for (const double wavelength : batch.component_wavelength_nm) {
            if (!(wavelength > previous_wavelength)) {
                add(result, SampleReconstructionIssue::Shape);
            }
            previous_wavelength = wavelength;
        }
    }
    std::vector<semantic::IdentityDigest> sample_identities;
    sample_identities.reserve(batch.records.size());
    for (const auto& record : batch.records) {
        sample_identities.push_back(record.sample_identity);
        const std::array scalars{
            record.raster_x, record.raster_y, record.time_seconds,
            record.detector_wavelength_nm, record.transport_wavelength_nm,
            record.joint_pdf, record.estimator_weight,
            record.kernel_radius, record.depth,
            record.normal[0], record.normal[1], record.normal[2]};
        if (semantic::identity_empty(record.sample_identity) ||
            semantic::identity_empty(record.technique_identity) ||
            semantic::identity_empty(record.path_event_identity) ||
            semantic::identity_empty(record.material_identity) ||
            semantic::identity_empty(record.spectral_resource_identity) ||
            !finite(scalars) || !finite(record.feature_albedo) ||
            !finite(record.value) || record.joint_pdf <= 0.0 ||
            record.estimator_weight < 0.0 ||
            record.kernel_radius <= 0.0 ||
            record.detector_wavelength_nm <= 0.0 ||
            record.transport_wavelength_nm <= 0.0 ||
            record.feature_albedo.size() != batch.component_count ||
            record.value.size() != batch.component_count ||
            (batch.observable.value_domain ==
                 transport::ValueDomain::ComplexJones &&
             record.phase_reference_identity !=
                 batch.observable.phase_reference_identity)) {
            add(result, SampleReconstructionIssue::Sample);
        }
    }
    std::ranges::sort(sample_identities);
    if (std::ranges::adjacent_find(sample_identities) !=
        sample_identities.end()) {
        add(result, SampleReconstructionIssue::Sample);
    }
    if (semantic::identity_empty(batch.batch_identity) ||
        batch.batch_identity !=
            compute_sample_reconstruction_batch_identity(batch)) {
        add(result, SampleReconstructionIssue::Identity);
    }
    return result;
}

SampleReconstructionValidation validate_sample_reconstruction_candidate(
    const SampleReconstructionCandidate& candidate) {
    SampleReconstructionValidation result;
    if (candidate.version != kSampleReconstructionVersion) {
        add(result, SampleReconstructionIssue::Version);
    }
    const auto& applicability = candidate.applicability;
    const bool external = candidate.method !=
        SampleReconstructionMethod::AnalyticKernelSplat;
    if (semantic::identity_empty(candidate.capsule_identity) ||
        semantic::identity_empty(candidate.source_identity) ||
        semantic::identity_empty(candidate.hypothesis_identity) ||
        semantic::identity_empty(candidate.algorithm_identity) ||
        semantic::identity_empty(candidate.failure_domain_identity) ||
        (external &&
         (semantic::identity_empty(candidate.provider_identity) ||
          semantic::identity_empty(candidate.artifact_identity))) ||
        candidate.maturity != research::Maturity::Research ||
        candidate.method <
            SampleReconstructionMethod::AnalyticKernelSplat ||
        candidate.method > SampleReconstructionMethod::ExternalHybrid ||
        !candidate.permutation_invariant ||
        !candidate.consumes_technique_metadata ||
        !candidate.consumes_path_metadata ||
        !candidate.consumes_spectral_metadata ||
        !std::isfinite(applicability.minimum_wavelength_nm) ||
        !std::isfinite(applicability.maximum_wavelength_nm) ||
        !std::isfinite(applicability.maximum_polarization_degree) ||
        applicability.minimum_wavelength_nm < 0.0 ||
        applicability.maximum_wavelength_nm <
            applicability.minimum_wavelength_nm ||
        applicability.maximum_polarization_degree < 0.0 ||
        applicability.maximum_polarization_degree > 1.0 ||
        applicability.minimum_sample_count == 0 ||
        !valid_identity_set(
            applicability.world_definition_identities) ||
        !valid_identity_set(
            applicability.measurement_schema_identities) ||
        !valid_identity_set(applicability.technique_identities) ||
        !valid_identity_set(applicability.material_identities) ||
        (applicability.maximum_sample_count != 0 &&
         applicability.maximum_sample_count <
             applicability.minimum_sample_count)) {
        add(result, SampleReconstructionIssue::Candidate);
    }
    if (semantic::identity_empty(candidate.candidate_identity) ||
        candidate.candidate_identity !=
            compute_sample_reconstruction_candidate_identity(candidate)) {
        add(result, SampleReconstructionIssue::Identity);
    }
    return result;
}

SampleReconstructionValidation validate_sample_reconstruction_weights(
    const SampleReconstructionExternalWeights& weights) {
    SampleReconstructionValidation result;
    if (weights.version != kSampleReconstructionVersion) {
        add(result, SampleReconstructionIssue::Version);
    }
    if (semantic::identity_empty(weights.batch_identity) ||
        semantic::identity_empty(weights.candidate_identity) ||
        semantic::identity_empty(weights.provider_identity) ||
        semantic::identity_empty(weights.artifact_identity) ||
        weights.weights.empty()) {
        add(result, SampleReconstructionIssue::ExternalWeights);
    }
    semantic::IdentityDigest previous = {};
    bool first = true;
    for (const auto& weight : weights.weights) {
        if (semantic::identity_empty(weight.sample_identity) ||
            !std::isfinite(weight.multiplier) ||
            !std::isfinite(weight.kernel_radius) ||
            !std::isfinite(weight.confidence) ||
            weight.multiplier < 0.0 || weight.kernel_radius <= 0.0 ||
            weight.confidence < 0.0 || weight.confidence > 1.0 ||
            (!first && !(previous < weight.sample_identity))) {
            add(result, SampleReconstructionIssue::ExternalWeights);
        }
        previous = weight.sample_identity;
        first = false;
    }
    if (semantic::identity_empty(weights.weights_identity) ||
        weights.weights_identity !=
            compute_sample_reconstruction_weights_identity(weights)) {
        add(result, SampleReconstructionIssue::Identity);
    }
    return result;
}

SampleReconstructionValidation validate_sample_reconstruction_config(
    const SampleReconstructionConfig& config) {
    SampleReconstructionValidation result;
    if (config.version != kSampleReconstructionVersion) {
        add(result, SampleReconstructionIssue::Version);
    }
    const std::array values{
        config.normal_sigma, config.depth_sigma, config.albedo_sigma,
        config.time_sigma_seconds, config.wavelength_sigma_nm,
        config.maximum_kernel_radius, config.observation_tolerance};
    if (!finite(values) || config.normal_sigma <= 0.0 ||
        config.depth_sigma <= 0.0 || config.albedo_sigma <= 0.0 ||
        config.time_sigma_seconds <= 0.0 ||
        config.wavelength_sigma_nm <= 0.0 ||
        config.maximum_kernel_radius <= 0.0 ||
        config.observation_tolerance <= 0.0 ||
        config.minimum_support == 0 ||
        config.method < SampleReconstructionMethod::AnalyticKernelSplat ||
        config.method > SampleReconstructionMethod::ExternalHybrid ||
        config.projection < SampleProjectionPolicy::None ||
        config.projection > SampleProjectionPolicy::GaugePreservingComplex) {
        add(result, SampleReconstructionIssue::Configuration);
    }
    if (semantic::identity_empty(config.config_identity) ||
        config.config_identity !=
            compute_sample_reconstruction_config_identity(config)) {
        add(result, SampleReconstructionIssue::Identity);
    }
    return result;
}

std::uint32_t assess_sample_reconstruction_ood(
    const SampleReconstructionBatch& batch,
    const SampleReconstructionCandidate& candidate) {
    using Reason = SampleReconstructionOodReason;
    std::uint32_t mask = 0;
    const auto set = [&mask](Reason reason) {
        mask |= static_cast<std::uint32_t>(reason);
    };
    const auto& applicability = candidate.applicability;
    const auto domain = batch.observable.value_domain;
    if (!contains_identity(
            applicability.world_definition_identities,
            batch.identities.world_definition)) {
        set(Reason::World);
    }
    if (!contains_identity(
            applicability.measurement_schema_identities,
            batch.measurement_schema_identity)) {
        set(Reason::MeasurementSchema);
    }
    if ((domain == transport::ValueDomain::Spectrum &&
         !applicability.spectrum) ||
        (domain == transport::ValueDomain::Stokes &&
         !applicability.stokes) ||
        (domain == transport::ValueDomain::ComplexJones &&
         !applicability.complex_jones)) {
        set(Reason::Observable);
    }
    if (applicability.component_count != 0 &&
        applicability.component_count != batch.component_count) {
        set(Reason::ComponentLayout);
    }
    if (batch.records.size() < applicability.minimum_sample_count ||
        (applicability.maximum_sample_count != 0 &&
         batch.records.size() > applicability.maximum_sample_count)) {
        set(Reason::SampleCount);
    }
    for (const auto& record : batch.records) {
        if ((applicability.minimum_wavelength_nm > 0.0 &&
             record.detector_wavelength_nm <
                 applicability.minimum_wavelength_nm) ||
            (applicability.maximum_wavelength_nm > 0.0 &&
             record.detector_wavelength_nm >
                 applicability.maximum_wavelength_nm)) {
            set(Reason::Wavelength);
        }
        if (!contains_identity(
                applicability.technique_identities,
                record.technique_identity)) {
            set(Reason::Technique);
        }
        if (!contains_identity(
                applicability.material_identities,
                record.material_identity)) {
            set(Reason::Material);
        }
        if (domain == transport::ValueDomain::Stokes &&
            polarization_degree(record) >
                applicability.maximum_polarization_degree) {
            set(Reason::Polarization);
        }
    }
    return mask;
}

SampleReconstructionOutput reconstruct_samples(
    const SampleReconstructionBatch& batch,
    const SampleReconstructionConfig& config,
    const SampleReconstructionCandidate* candidate,
    const SampleReconstructionExternalWeights* external_weights) {
    if (!validate_sample_reconstruction_batch(batch).ok() ||
        !validate_sample_reconstruction_config(config).ok()) {
        throw std::invalid_argument("Invalid sample reconstruction input");
    }
    const auto domain = batch.observable.value_domain;
    if ((domain == transport::ValueDomain::Spectrum &&
         config.projection != SampleProjectionPolicy::
             NonnegativeObservationConsistentSpectrum) ||
        (domain == transport::ValueDomain::Stokes &&
         config.projection != SampleProjectionPolicy::PhysicalStokesCone) ||
        (domain == transport::ValueDomain::ComplexJones &&
         config.projection !=
             SampleProjectionPolicy::GaugePreservingComplex)) {
        throw std::invalid_argument(
            "Sample reconstruction projection does not match observable");
    }
    const bool external = config.method !=
        SampleReconstructionMethod::AnalyticKernelSplat;
    if ((candidate &&
         (!validate_sample_reconstruction_candidate(*candidate).ok() ||
          candidate->method != config.method)) ||
        (!candidate && external) ||
        (external &&
         (!config.explicit_research_opt_in || !external_weights ||
          !validate_sample_reconstruction_weights(*external_weights).ok() ||
          external_weights->batch_identity != batch.batch_identity ||
          external_weights->candidate_identity !=
              candidate->candidate_identity ||
          external_weights->provider_identity !=
              candidate->provider_identity ||
          external_weights->artifact_identity !=
              candidate->artifact_identity))) {
        throw std::invalid_argument(
            "Invalid sample reconstruction research candidate");
    }
    const std::uint32_t ood_mask = candidate
        ? assess_sample_reconstruction_ood(batch, *candidate) : 0;
    if (ood_mask != 0 && !config.allow_ood) {
        throw std::invalid_argument(
            "Sample reconstruction candidate is out of domain");
    }
    const auto pixels = pixel_count(batch.width, batch.height);
    const auto components = static_cast<std::size_t>(batch.component_count);
    const auto values = pixels * components;
    SampleReconstructionOutput output;
    output.batch_identity = batch.batch_identity;
    output.config_identity = config.config_identity;
    output.candidate_identity = candidate
        ? candidate->candidate_identity : semantic::IdentityDigest{};
    output.weights_identity = external_weights
        ? external_weights->weights_identity : semantic::IdentityDigest{};
    output.maturity = research::Maturity::Research;
    output.observable = batch.observable;
    output.width = batch.width;
    output.height = batch.height;
    output.component_count = batch.component_count;
    output.ood_mask = ood_mask;
    output.raw_estimate.assign(values, 0.0);
    output.raw_uncertainty.assign(values, 0.0);
    output.reconstructed.assign(values, 0.0);
    output.uncertainty.assign(values, 0.0);
    output.effective_support.assign(pixels, 0.0);
    output.confidence.assign(pixels, 0.0);
    output.projection_delta.assign(pixels, 0.0);
    output.rejection_reason.assign(
        pixels, SampleReconstructionRejection::InvalidSample);

    std::vector<const SampleReconstructionRecord*> records;
    records.reserve(batch.records.size());
    for (const auto& record : batch.records) records.push_back(&record);
    std::ranges::sort(records, {},
        [](const SampleReconstructionRecord* record) {
            return record->sample_identity;
        });
    std::vector<double> raw_sum(values, 0.0);
    std::vector<double> raw_square(values, 0.0);
    std::vector<std::uint64_t> raw_count(pixels, 0);
    std::vector<std::array<double, 3>> target_normal(pixels);
    std::vector<double> target_depth(pixels, 0.0);
    std::vector<double> target_time(pixels, 0.0);
    std::vector<double> target_wavelength(pixels, 0.0);
    std::vector<double> target_albedo(values, 0.0);
    for (const auto* record : records) {
        if (!record->valid) continue;
        const auto x = static_cast<std::int64_t>(std::floor(record->raster_x));
        const auto y = static_cast<std::int64_t>(std::floor(record->raster_y));
        if (x < 0 || y < 0 || x >= batch.width || y >= batch.height) continue;
        const auto pixel = static_cast<std::size_t>(y) * batch.width +
            static_cast<std::size_t>(x);
        ++raw_count[pixel];
        target_depth[pixel] += record->depth;
        target_time[pixel] += record->time_seconds;
        target_wavelength[pixel] += record->detector_wavelength_nm;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            target_normal[pixel][axis] += record->normal[axis];
        }
        for (std::size_t component = 0; component < components; ++component) {
            const auto index = pixel * components + component;
            const double contribution = record->value[component] *
                record->estimator_weight / record->joint_pdf;
            raw_sum[index] += contribution;
            raw_square[index] += contribution * contribution;
            target_albedo[index] += record->feature_albedo[component];
        }
    }
    for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
        if (raw_count[pixel] == 0) continue;
        const double inverse = 1.0 / static_cast<double>(raw_count[pixel]);
        target_depth[pixel] *= inverse;
        target_time[pixel] *= inverse;
        target_wavelength[pixel] *= inverse;
        double normal_length = 0.0;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            target_normal[pixel][axis] *= inverse;
            normal_length += target_normal[pixel][axis] *
                target_normal[pixel][axis];
        }
        normal_length = std::sqrt(normal_length);
        if (normal_length > 0.0) {
            for (auto& axis : target_normal[pixel]) axis /= normal_length;
        }
        for (std::size_t component = 0; component < components; ++component) {
            const auto index = pixel * components + component;
            target_albedo[index] *= inverse;
            output.raw_estimate[index] = raw_sum[index] * inverse;
            const double sample_variance = raw_count[pixel] > 1
                ? std::max(0.0,
                    (raw_square[index] - raw_sum[index] * raw_sum[index] *
                        inverse) /
                    static_cast<double>(raw_count[pixel] - 1))
                : 0.0;
            output.raw_uncertainty[index] = std::sqrt(
                sample_variance * inverse);
        }
    }

    std::vector<double> sum(values, 0.0);
    std::vector<double> square(values, 0.0);
    std::vector<double> sum_weight(pixels, 0.0);
    std::vector<double> sum_squared_weight(pixels, 0.0);
    std::vector<double> confidence_weight(pixels, 0.0);
    std::vector<std::uint32_t> support_count(pixels, 0);
    for (const auto* record : records) {
        if (!record->valid) continue;
        const auto* external_weight = find_weight(
            external_weights, record->sample_identity);
        if (external && !external_weight) {
            throw std::invalid_argument(
                "External sample reconstruction weight is missing");
        }
        const double multiplier = external_weight
            ? external_weight->multiplier : 1.0;
        const double sample_confidence = external_weight
            ? external_weight->confidence : 1.0;
        const double radius = std::min(
            config.maximum_kernel_radius,
            external_weight ? external_weight->kernel_radius
                            : record->kernel_radius);
        const auto minimum_x = std::max<std::int64_t>(0,
            static_cast<std::int64_t>(std::floor(record->raster_x - radius)));
        const auto maximum_x = std::min<std::int64_t>(batch.width - 1,
            static_cast<std::int64_t>(std::floor(record->raster_x + radius)));
        const auto minimum_y = std::max<std::int64_t>(0,
            static_cast<std::int64_t>(std::floor(record->raster_y - radius)));
        const auto maximum_y = std::min<std::int64_t>(batch.height - 1,
            static_cast<std::int64_t>(std::floor(record->raster_y + radius)));
        for (auto y = minimum_y; y <= maximum_y; ++y) {
            for (auto x = minimum_x; x <= maximum_x; ++x) {
                const auto pixel = static_cast<std::size_t>(y) * batch.width +
                    static_cast<std::size_t>(x);
                if (raw_count[pixel] == 0) continue;
                const double dx = static_cast<double>(x) + 0.5 -
                    record->raster_x;
                const double dy = static_cast<double>(y) + 0.5 -
                    record->raster_y;
                const double distance_squared = dx * dx + dy * dy;
                if (distance_squared > radius * radius) continue;
                const double dot = normal_dot(
                    target_normal[pixel], record->normal);
                const double depth_scale = std::max(
                    {1.0, std::abs(target_depth[pixel]),
                     std::abs(record->depth)});
                const double depth_difference = std::abs(
                    target_depth[pixel] - record->depth) / depth_scale;
                const double albedo_distance = std::sqrt(squared_distance(
                    std::span(
                        target_albedo.data() + pixel * components, components),
                    record->feature_albedo));
                const double time_difference = std::abs(
                    target_time[pixel] - record->time_seconds);
                const double wavelength_difference = std::abs(
                    target_wavelength[pixel] -
                    record->detector_wavelength_nm);
                const double weight = multiplier * std::exp(
                    -0.5 * distance_squared / (radius * radius) -
                    (1.0 - dot) / config.normal_sigma -
                    depth_difference / config.depth_sigma -
                    albedo_distance * albedo_distance /
                        (config.albedo_sigma * config.albedo_sigma) -
                    time_difference / config.time_sigma_seconds -
                    wavelength_difference / config.wavelength_sigma_nm);
                if (!(weight > 0.0) || !std::isfinite(weight)) continue;
                ++support_count[pixel];
                sum_weight[pixel] += weight;
                sum_squared_weight[pixel] += weight * weight;
                confidence_weight[pixel] += weight * sample_confidence;
                for (std::size_t component = 0; component < components;
                     ++component) {
                    const auto index = pixel * components + component;
                    const double contribution = record->value[component] *
                        record->estimator_weight / record->joint_pdf;
                    sum[index] += contribution * weight;
                    square[index] += contribution * contribution * weight;
                }
            }
        }
    }
    for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
        const auto base = pixel * components;
        if (support_count[pixel] < config.minimum_support ||
            !(sum_weight[pixel] > 0.0)) {
            for (std::size_t component = 0; component < components;
                 ++component) {
                output.reconstructed[base + component] =
                    output.raw_estimate[base + component];
                output.uncertainty[base + component] =
                    output.raw_uncertainty[base + component];
            }
            output.rejection_reason[pixel] = raw_count[pixel] == 0
                ? SampleReconstructionRejection::InvalidSample
                : SampleReconstructionRejection::InsufficientSupport;
            if (raw_count[pixel] != 0) {
                const std::vector<double> unprojected(
                    output.reconstructed.begin() +
                        static_cast<std::ptrdiff_t>(base),
                    output.reconstructed.begin() +
                        static_cast<std::ptrdiff_t>(base + components));
                auto reconstructed = std::span(
                    output.reconstructed.data() + base, components);
                bool observation_consistent = true;
                if (domain == transport::ValueDomain::Spectrum) {
                    observation_consistent = project_spectrum(
                        reconstructed, batch.sensor_response,
                        batch.sensor_observation.empty()
                            ? 0.0 : batch.sensor_observation[pixel],
                        config.observation_tolerance);
                } else if (domain == transport::ValueDomain::Stokes) {
                    project_stokes(reconstructed);
                }
                output.projection_delta[pixel] = std::sqrt(
                    squared_distance(reconstructed, unprojected));
                for (std::size_t component = 0; component < components;
                     ++component) {
                    const double projection =
                        reconstructed[component] - unprojected[component];
                    output.uncertainty[base + component] = std::hypot(
                        output.uncertainty[base + component], projection);
                }
                if (!observation_consistent) {
                    output.rejection_reason[pixel] =
                        SampleReconstructionRejection::
                            ObservationInconsistent;
                }
            }
            continue;
        }
        const double inverse_weight = 1.0 / sum_weight[pixel];
        const double effective_support = sum_weight[pixel] *
            sum_weight[pixel] /
            std::max(sum_squared_weight[pixel], 1e-30);
        output.effective_support[pixel] = effective_support;
        output.confidence[pixel] = std::clamp(
            confidence_weight[pixel] * inverse_weight *
                (ood_mask == 0 ? 1.0 : 0.25) *
                std::min(1.0, effective_support /
                    static_cast<double>(config.minimum_support)),
            0.0, 1.0);
        for (std::size_t component = 0; component < components; ++component) {
            const auto index = base + component;
            const double mean = sum[index] * inverse_weight;
            const double variance = std::max(
                0.0, square[index] * inverse_weight - mean * mean);
            output.reconstructed[index] = mean;
            output.uncertainty[index] = std::sqrt(
                variance / std::max(effective_support, 1.0));
        }
        output.rejection_reason[pixel] = ood_mask == 0
            ? SampleReconstructionRejection::None
            : SampleReconstructionRejection::OutOfDomain;
        const std::vector<double> unprojected(
            output.reconstructed.begin() +
                static_cast<std::ptrdiff_t>(base),
            output.reconstructed.begin() +
                static_cast<std::ptrdiff_t>(base + components));
        auto reconstructed = std::span(
            output.reconstructed.data() + base, components);
        bool observation_consistent = true;
        if (domain == transport::ValueDomain::Spectrum) {
            observation_consistent = project_spectrum(
                reconstructed, batch.sensor_response,
                batch.sensor_observation.empty()
                    ? 0.0 : batch.sensor_observation[pixel],
                config.observation_tolerance);
        } else if (domain == transport::ValueDomain::Stokes) {
            project_stokes(reconstructed);
        }
        output.projection_delta[pixel] = std::sqrt(squared_distance(
            reconstructed, unprojected));
        for (std::size_t component = 0; component < components; ++component) {
            const double projection =
                reconstructed[component] - unprojected[component];
            output.uncertainty[base + component] = std::hypot(
                output.uncertainty[base + component], projection);
        }
        if (!observation_consistent) {
            output.rejection_reason[pixel] =
                SampleReconstructionRejection::ObservationInconsistent;
        }
    }
    output.output_identity =
        compute_sample_reconstruction_output_identity(output);
    if (!validate_sample_reconstruction_output(output)) {
        throw std::runtime_error(
            "Sample reconstruction produced invalid output");
    }
    return output;
}

semantic::IdentityDigest compute_sample_reconstruction_output_identity(
    const SampleReconstructionOutput& output) {
    Encoder encoder;
    encoder.u32(output.version);
    encoder.digest(output.batch_identity);
    encoder.digest(output.config_identity);
    encoder.digest(output.candidate_identity);
    encoder.digest(output.weights_identity);
    encoder.u8(static_cast<std::uint8_t>(output.maturity));
    encoder.observable(output.observable);
    encoder.u32(output.width);
    encoder.u32(output.height);
    encoder.u32(output.component_count);
    encoder.u32(output.ood_mask);
    encoder.doubles(output.raw_estimate);
    encoder.doubles(output.raw_uncertainty);
    encoder.doubles(output.reconstructed);
    encoder.doubles(output.uncertainty);
    encoder.doubles(output.effective_support);
    encoder.doubles(output.confidence);
    encoder.doubles(output.projection_delta);
    encoder.u64(static_cast<std::uint64_t>(output.rejection_reason.size()));
    for (const auto reason : output.rejection_reason) {
        encoder.u8(static_cast<std::uint8_t>(reason));
    }
    return runtime::identity_digest(encoder.bytes());
}

bool validate_sample_reconstruction_output(
    const SampleReconstructionOutput& output) {
    const auto pixels = pixel_count(output.width, output.height);
    const auto components = static_cast<std::size_t>(output.component_count);
    const auto values = pixels * components;
    if (output.version != kSampleReconstructionVersion || pixels == 0 ||
        components == 0 || !supported_observable(output.observable) ||
        output.observable.component_count != output.component_count ||
        semantic::identity_empty(output.batch_identity) ||
        semantic::identity_empty(output.config_identity) ||
        output.maturity != research::Maturity::Research ||
        semantic::identity_empty(output.output_identity) ||
        output.output_identity !=
            compute_sample_reconstruction_output_identity(output) ||
        output.raw_estimate.size() != values ||
        output.raw_uncertainty.size() != values ||
        output.reconstructed.size() != values ||
        output.uncertainty.size() != values ||
        output.effective_support.size() != pixels ||
        output.confidence.size() != pixels ||
        output.projection_delta.size() != pixels ||
        output.rejection_reason.size() != pixels ||
        !finite(output.raw_estimate) || !finite(output.raw_uncertainty) ||
        !finite(output.reconstructed) || !finite(output.uncertainty) ||
        !finite(output.effective_support) || !finite(output.confidence) ||
        !finite(output.projection_delta)) {
        return false;
    }
    for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
        if (output.effective_support[pixel] < 0.0 ||
            output.confidence[pixel] < 0.0 ||
            output.confidence[pixel] > 1.0 ||
            output.projection_delta[pixel] < 0.0 ||
            output.rejection_reason[pixel] <
                SampleReconstructionRejection::None ||
            output.rejection_reason[pixel] >
                SampleReconstructionRejection::ObservationInconsistent ||
            !physical_value(
                std::span(
                    output.reconstructed.data() + pixel * components,
                    components),
                output.observable.value_domain)) {
            return false;
        }
    }
    return std::ranges::all_of(
        output.raw_uncertainty,
        [](double value) { return value >= 0.0; }) &&
        std::ranges::all_of(
            output.uncertainty,
            [](double value) { return value >= 0.0; });
}

SampleReconstructionEvaluation evaluate_sample_reconstruction(
    const SampleReconstructionOutput& output,
    std::span<const double> reference,
    const SampleReconstructionOutput* permuted_output,
    std::span<const double> sensor_response,
    std::span<const double> sensor_observation) {
    if (!validate_sample_reconstruction_output(output) ||
        reference.size() != output.reconstructed.size() ||
        !finite(reference) ||
        (permuted_output &&
         (!validate_sample_reconstruction_output(*permuted_output) ||
          permuted_output->reconstructed.size() != reference.size())) ||
        (!sensor_response.empty() &&
         sensor_response.size() != output.component_count) ||
        (!sensor_observation.empty() &&
         sensor_observation.size() !=
             pixel_count(output.width, output.height)) ||
        (sensor_response.empty() != sensor_observation.empty())) {
        throw std::invalid_argument(
            "Invalid sample reconstruction evaluation input");
    }
    SampleReconstructionEvaluation evaluation;
    evaluation.output_identity = output.output_identity;
    std::uint64_t one_sigma = 0;
    std::uint64_t two_sigma = 0;
    for (std::size_t index = 0; index < reference.size(); ++index) {
        const double raw_error = output.raw_estimate[index] - reference[index];
        const double reconstructed_error =
            output.reconstructed[index] - reference[index];
        evaluation.raw_mse += raw_error * raw_error;
        evaluation.reconstructed_mse +=
            reconstructed_error * reconstructed_error;
        if (std::abs(reconstructed_error) <= output.uncertainty[index]) {
            ++one_sigma;
        }
        if (std::abs(reconstructed_error) <=
            2.0 * output.uncertainty[index]) {
            ++two_sigma;
        }
        if (permuted_output) {
            evaluation.maximum_permutation_error = std::max(
                evaluation.maximum_permutation_error,
                std::abs(output.reconstructed[index] -
                    permuted_output->reconstructed[index]));
        }
    }
    const double inverse = 1.0 / static_cast<double>(reference.size());
    evaluation.raw_mse *= inverse;
    evaluation.reconstructed_mse *= inverse;
    evaluation.coverage_one_sigma =
        static_cast<double>(one_sigma) * inverse;
    evaluation.coverage_two_sigma =
        static_cast<double>(two_sigma) * inverse;
    evaluation.calibration_error_one_sigma = std::abs(
        evaluation.coverage_one_sigma - 0.6826894921370859);
    evaluation.calibration_error_two_sigma = std::abs(
        evaluation.coverage_two_sigma - 0.9544997361036416);
    const auto pixels = pixel_count(output.width, output.height);
    const auto components = static_cast<std::size_t>(output.component_count);
    for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
        const auto value = std::span(
            output.reconstructed.data() + pixel * components, components);
        if (!physical_value(value, output.observable.value_domain)) {
            ++evaluation.physical_violation_count;
        }
        if (!sensor_response.empty()) {
            double observation = 0.0;
            for (std::size_t component = 0; component < components;
                 ++component) {
                observation += sensor_response[component] * value[component];
            }
            evaluation.maximum_observation_residual = std::max(
                evaluation.maximum_observation_residual,
                std::abs(observation - sensor_observation[pixel]));
        }
    }
    Encoder encoder;
    encoder.digest(evaluation.output_identity);
    encoder.f64(evaluation.raw_mse);
    encoder.f64(evaluation.reconstructed_mse);
    encoder.f64(evaluation.coverage_one_sigma);
    encoder.f64(evaluation.coverage_two_sigma);
    encoder.f64(evaluation.calibration_error_one_sigma);
    encoder.f64(evaluation.calibration_error_two_sigma);
    encoder.f64(evaluation.maximum_observation_residual);
    encoder.f64(evaluation.maximum_permutation_error);
    encoder.u64(evaluation.physical_violation_count);
    evaluation.evaluation_identity = runtime::identity_digest(encoder.bytes());
    return evaluation;
}

bool validate_sample_reconstruction_evaluation(
    const SampleReconstructionEvaluation& evaluation) {
    const std::array values{
        evaluation.raw_mse, evaluation.reconstructed_mse,
        evaluation.coverage_one_sigma, evaluation.coverage_two_sigma,
        evaluation.calibration_error_one_sigma,
        evaluation.calibration_error_two_sigma,
        evaluation.maximum_observation_residual,
        evaluation.maximum_permutation_error};
    Encoder encoder;
    encoder.digest(evaluation.output_identity);
    encoder.f64(evaluation.raw_mse);
    encoder.f64(evaluation.reconstructed_mse);
    encoder.f64(evaluation.coverage_one_sigma);
    encoder.f64(evaluation.coverage_two_sigma);
    encoder.f64(evaluation.calibration_error_one_sigma);
    encoder.f64(evaluation.calibration_error_two_sigma);
    encoder.f64(evaluation.maximum_observation_residual);
    encoder.f64(evaluation.maximum_permutation_error);
    encoder.u64(evaluation.physical_violation_count);
    return !semantic::identity_empty(evaluation.evaluation_identity) &&
        evaluation.evaluation_identity ==
            runtime::identity_digest(encoder.bytes()) &&
        !semantic::identity_empty(evaluation.output_identity) &&
        finite(values) && evaluation.raw_mse >= 0.0 &&
        evaluation.reconstructed_mse >= 0.0 &&
        evaluation.coverage_one_sigma >= 0.0 &&
        evaluation.coverage_one_sigma <= 1.0 &&
        evaluation.coverage_two_sigma >= evaluation.coverage_one_sigma &&
        evaluation.coverage_two_sigma <= 1.0 &&
        evaluation.calibration_error_one_sigma >= 0.0 &&
        evaluation.calibration_error_two_sigma >= 0.0 &&
        evaluation.maximum_observation_residual >= 0.0 &&
        evaluation.maximum_permutation_error >= 0.0;
}

}
