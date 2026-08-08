#include "ure/reconstruction/sample_reconstruction.hpp"

#include "ure/runtime/multi_backend.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>

namespace rec = ure::reconstruction;

static int failures = 0;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", \
                         __FILE__, __LINE__, #condition); \
            ++failures; \
        } \
    } while (false)

static ure::semantic::IdentityDigest id(std::string_view value) {
    return ure::runtime::identity_digest(value);
}

static ure::transport::ObservableDescriptor observable(
    ure::transport::ValueDomain domain,
    std::uint32_t components) {
    ure::transport::ObservableDescriptor value;
    value.kind = domain == ure::transport::ValueDomain::Stokes
        ? ure::transport::ObservableKind::StokesRadiance
        : domain == ure::transport::ValueDomain::ComplexJones
            ? ure::transport::ObservableKind::JonesField
            : ure::transport::ObservableKind::SpectralRadiance;
    value.value_domain = domain;
    value.coherence = domain == ure::transport::ValueDomain::ComplexJones
        ? ure::transport::CoherenceClass::Coherent
        : ure::transport::CoherenceClass::Incoherent;
    value.component_count = components;
    value.unit.dimension.length = -1;
    value.unit.dimension.mass = 1;
    value.unit.dimension.time = -3;
    if (domain == ure::transport::ValueDomain::ComplexJones) {
        value.phase_reference_identity = id("phase-reference");
    }
    return value;
}

static rec::SampleReconstructionBatch batch(
    std::uint32_t width,
    std::uint32_t height,
    ure::transport::ValueDomain domain,
    std::uint32_t components) {
    rec::SampleReconstructionBatch value;
    value.observable = observable(domain, components);
    value.identities.world_definition = id("world-definition");
    value.identities.world_state = id("world-state");
    value.identities.time_sample = id("time-sample");
    value.identities.observation_snapshot = id("snapshot");
    value.identities.technique_graph = id("technique-graph");
    value.identities.measurement_schema = id("measurement-schema");
    value.measurement_schema_identity = value.identities.measurement_schema;
    value.width = width;
    value.height = height;
    value.component_count = components;
    if (domain == ure::transport::ValueDomain::Spectrum) {
        value.component_wavelength_nm.resize(components);
        for (std::uint32_t component = 0; component < components;
             ++component) {
            value.component_wavelength_nm[component] =
                450.0 + 100.0 * component;
        }
    }
    return value;
}

static rec::SampleReconstructionRecord record(
    std::string_view name,
    double x,
    double y,
    std::span<const double> values,
    const ure::semantic::IdentityDigest& phase = {}) {
    rec::SampleReconstructionRecord value;
    value.sample_identity = id(name);
    value.technique_identity = id("wavefront-technique");
    value.path_event_identity = id("diffuse-indirect-event");
    value.material_identity = id("material-diffuse");
    value.medium_identity = id("medium-vacuum");
    value.spectral_resource_identity = id("spectral-resource");
    value.phase_reference_identity = phase;
    value.raster_x = x;
    value.raster_y = y;
    value.detector_wavelength_nm = 550.0;
    value.transport_wavelength_nm = 550.0;
    value.joint_pdf = 1.0;
    value.estimator_weight = 1.0;
    value.kernel_radius = 2.0;
    value.depth = 1.0;
    value.feature_albedo.assign(values.size(), 0.5);
    value.value.assign(values.begin(), values.end());
    return value;
}

static rec::SampleReconstructionConfig spectrum_config() {
    rec::SampleReconstructionConfig value;
    value.projection = rec::SampleProjectionPolicy::
        NonnegativeObservationConsistentSpectrum;
    rec::finalize_sample_reconstruction_config(value);
    return value;
}

static rec::SampleReconstructionBatch spectrum_fixture() {
    auto value = batch(
        5, 1, ure::transport::ValueDomain::Spectrum, 3);
    value.sensor_response = {0.2, 0.3, 0.5};
    value.sensor_observation.assign(5, 2.3);
    for (int pixel = 0; pixel < 5; ++pixel) {
        const double noise = (pixel & 1) == 0 ? 0.75 : -0.75;
        const std::array sample_value{
            1.0 + noise, 2.0 + noise, 3.0 + noise};
        for (int sample = 0; sample < 2; ++sample) {
            const auto name = "spectrum-" + std::to_string(pixel) + "-" +
                std::to_string(sample);
            value.records.push_back(record(
                name, pixel + 0.35 + 0.3 * sample, 0.5, sample_value));
        }
    }
    rec::finalize_sample_reconstruction_batch(value);
    return value;
}

static rec::SampleReconstructionCandidate candidate(
    rec::SampleReconstructionMethod method) {
    rec::SampleReconstructionCandidate value;
    value.capsule_identity = id("hr2-capsule");
    value.source_identity = id("hr2-source");
    value.hypothesis_identity = id("sample-metadata-hypothesis");
    value.algorithm_identity = id("external-permutation-invariant-weights");
    value.provider_identity = id("research-provider");
    value.artifact_identity = id("research-artifact");
    value.failure_domain_identity = id("research-failure-domain");
    value.method = method;
    value.applicability.minimum_wavelength_nm = 360.0;
    value.applicability.maximum_wavelength_nm = 830.0;
    value.applicability.minimum_sample_count = 1;
    value.applicability.maximum_sample_count = 1000;
    value.applicability.component_count = 3;
    value.applicability.spectrum = true;
    value.applicability.world_definition_identities = {
        id("world-definition")};
    value.applicability.measurement_schema_identities = {
        id("measurement-schema")};
    value.applicability.technique_identities = {id("wavefront-technique")};
    value.applicability.material_identities = {id("material-diffuse")};
    rec::finalize_sample_reconstruction_candidate(value);
    return value;
}

static rec::SampleReconstructionExternalWeights weights(
    const rec::SampleReconstructionBatch& input,
    const rec::SampleReconstructionCandidate& model) {
    rec::SampleReconstructionExternalWeights value;
    value.batch_identity = input.batch_identity;
    value.candidate_identity = model.candidate_identity;
    value.provider_identity = model.provider_identity;
    value.artifact_identity = model.artifact_identity;
    for (const auto& sample : input.records) {
        value.weights.push_back({sample.sample_identity, 1.0, 2.0, 0.8});
    }
    rec::finalize_sample_reconstruction_weights(value);
    return value;
}

static void test_sample_splat_and_permutation() {
    const auto input = spectrum_fixture();
    const auto settings = spectrum_config();
    const auto output = rec::reconstruct_samples(input, settings);
    std::vector<double> reference;
    for (int pixel = 0; pixel < 5; ++pixel) {
        reference.insert(reference.end(), {1.0, 2.0, 3.0});
    }
    auto permuted = input;
    std::ranges::reverse(permuted.records);
    rec::finalize_sample_reconstruction_batch(permuted);
    CHECK(permuted.batch_identity == input.batch_identity);
    const auto permuted_output = rec::reconstruct_samples(permuted, settings);
    const auto evaluation = rec::evaluate_sample_reconstruction(
        output, reference, &permuted_output,
        input.sensor_response, input.sensor_observation);
    CHECK(rec::validate_sample_reconstruction_output(output));
    CHECK(output.maturity == ure::research::Maturity::Research);
    CHECK(rec::validate_sample_reconstruction_evaluation(evaluation));
    CHECK(evaluation.reconstructed_mse < evaluation.raw_mse);
    CHECK(evaluation.maximum_permutation_error == 0.0);
    CHECK(evaluation.maximum_observation_residual < 1e-8);
    CHECK(evaluation.coverage_two_sigma >= 0.8);
    CHECK(evaluation.calibration_error_one_sigma >= 0.0);
    CHECK(evaluation.calibration_error_two_sigma >= 0.0);
    CHECK(evaluation.physical_violation_count == 0);
    CHECK(std::ranges::all_of(output.reconstructed,
        [](double value) { return value >= 0.0; }));
    CHECK(output.raw_estimate != output.reconstructed);
}

static void test_external_research_methods_and_ood() {
    const auto input = spectrum_fixture();
    for (const auto method : {
             rec::SampleReconstructionMethod::ExternalKernelPrediction,
             rec::SampleReconstructionMethod::ExternalSampleTransformer,
             rec::SampleReconstructionMethod::ExternalHybrid}) {
        const auto model = candidate(method);
        const auto predicted = weights(input, model);
        auto settings = spectrum_config();
        settings.method = method;
        settings.explicit_research_opt_in = true;
        rec::finalize_sample_reconstruction_config(settings);
        const auto output = rec::reconstruct_samples(
            input, settings, &model, &predicted);
        CHECK(!ure::semantic::identity_empty(output.candidate_identity));
        CHECK(output.weights_identity == predicted.weights_identity);
        CHECK(output.ood_mask == 0);
        CHECK(output.confidence[2] > 0.0);

        settings.explicit_research_opt_in = false;
        rec::finalize_sample_reconstruction_config(settings);
        bool rejected = false;
        try {
            (void)rec::reconstruct_samples(
                input, settings, &model, &predicted);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        CHECK(rejected);
    }

    auto model = candidate(
        rec::SampleReconstructionMethod::ExternalSampleTransformer);
    model.applicability.maximum_wavelength_nm = 500.0;
    rec::finalize_sample_reconstruction_candidate(model);
    const auto predicted = weights(input, model);
    auto settings = spectrum_config();
    settings.method = model.method;
    settings.explicit_research_opt_in = true;
    rec::finalize_sample_reconstruction_config(settings);
    bool rejected = false;
    try {
        (void)rec::reconstruct_samples(input, settings, &model, &predicted);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK(rejected);
    settings.allow_ood = true;
    rec::finalize_sample_reconstruction_config(settings);
    const auto ood = rec::reconstruct_samples(
        input, settings, &model, &predicted);
    CHECK((ood.ood_mask & static_cast<std::uint32_t>(
        rec::SampleReconstructionOodReason::Wavelength)) != 0);
    CHECK(ood.rejection_reason[2] ==
          rec::SampleReconstructionRejection::OutOfDomain);
    CHECK(ood.confidence[2] <= 0.25);

    auto cross_domain = input;
    cross_domain.identities.world_definition = id("unseen-world");
    cross_domain.identities.measurement_schema = id("unseen-schema");
    cross_domain.measurement_schema_identity =
        cross_domain.identities.measurement_schema;
    cross_domain.records[0].material_identity = id("unseen-material");
    cross_domain.records[0].technique_identity = id("unseen-technique");
    rec::finalize_sample_reconstruction_batch(cross_domain);
    settings.allow_ood = true;
    rec::finalize_sample_reconstruction_config(settings);
    bool stale_weights_rejected = false;
    try {
        (void)rec::reconstruct_samples(
            cross_domain, settings, &model, &predicted);
    } catch (const std::invalid_argument&) {
        stale_weights_rejected = true;
    }
    CHECK(stale_weights_rejected);
    const auto cross_domain_weights = weights(cross_domain, model);
    const auto cross_domain_output = rec::reconstruct_samples(
        cross_domain, settings, &model, &cross_domain_weights);
    CHECK((cross_domain_output.ood_mask & static_cast<std::uint32_t>(
        rec::SampleReconstructionOodReason::World)) != 0);
    CHECK((cross_domain_output.ood_mask & static_cast<std::uint32_t>(
        rec::SampleReconstructionOodReason::MeasurementSchema)) != 0);
    CHECK((cross_domain_output.ood_mask & static_cast<std::uint32_t>(
        rec::SampleReconstructionOodReason::Material)) != 0);
    CHECK((cross_domain_output.ood_mask & static_cast<std::uint32_t>(
        rec::SampleReconstructionOodReason::Technique)) != 0);
}

static void test_stokes_physical_projection() {
    auto input = batch(1, 1, ure::transport::ValueDomain::Stokes, 4);
    const std::array overpolarized{1.0, 2.0, 0.5, 0.25};
    for (int sample = 0; sample < 3; ++sample) {
        input.records.push_back(record(
            "stokes-" + std::to_string(sample),
            0.5, 0.5, overpolarized));
    }
    rec::finalize_sample_reconstruction_batch(input);
    rec::SampleReconstructionConfig settings;
    settings.projection = rec::SampleProjectionPolicy::PhysicalStokesCone;
    rec::finalize_sample_reconstruction_config(settings);
    const auto output = rec::reconstruct_samples(input, settings);
    const double intensity = output.reconstructed[0];
    const double polarized = std::sqrt(
        output.reconstructed[1] * output.reconstructed[1] +
        output.reconstructed[2] * output.reconstructed[2] +
        output.reconstructed[3] * output.reconstructed[3]);
    CHECK(polarized <= intensity + 1e-10);
    CHECK(output.projection_delta[0] > 0.0);
    CHECK(output.raw_estimate[1] > output.raw_estimate[0]);
}

static void test_adversarial_spectrum_projection() {
    auto input = batch(
        1, 1, ure::transport::ValueDomain::Spectrum, 3);
    input.component_wavelength_nm = {450.0, 550.0, 650.0};
    input.sensor_response = {0.2, 0.3, 0.5};
    input.sensor_observation = {2.3};
    const std::array invalid_spectrum{-1.0, 2.0, 4.0};
    for (int sample = 0; sample < 3; ++sample) {
        input.records.push_back(record(
            "negative-spectrum-" + std::to_string(sample),
            0.5, 0.5, invalid_spectrum));
    }
    rec::finalize_sample_reconstruction_batch(input);
    const auto output = rec::reconstruct_samples(input, spectrum_config());
    CHECK(std::ranges::all_of(output.reconstructed,
        [](double value) { return value >= 0.0; }));
    const double observation =
        output.reconstructed[0] * 0.2 +
        output.reconstructed[1] * 0.3 +
        output.reconstructed[2] * 0.5;
    CHECK(std::abs(observation - 2.3) < 1e-8);
    CHECK(output.projection_delta[0] > 0.0);
}

static std::array<double, 4> rotate_complex(
    std::span<const double> value,
    double angle) {
    const double cosine = std::cos(angle);
    const double sine = std::sin(angle);
    return {
        value[0] * cosine - value[1] * sine,
        value[0] * sine + value[1] * cosine,
        value[2] * cosine - value[3] * sine,
        value[2] * sine + value[3] * cosine};
}

static void test_complex_gauge_covariance() {
    auto input = batch(
        1, 1, ure::transport::ValueDomain::ComplexJones, 4);
    const std::array field{1.0, 0.25, -0.5, 0.75};
    for (int sample = 0; sample < 3; ++sample) {
        input.records.push_back(record(
            "complex-" + std::to_string(sample), 0.5, 0.5, field,
            input.observable.phase_reference_identity));
    }
    rec::finalize_sample_reconstruction_batch(input);
    rec::SampleReconstructionConfig settings;
    settings.projection =
        rec::SampleProjectionPolicy::GaugePreservingComplex;
    rec::finalize_sample_reconstruction_config(settings);
    const auto output = rec::reconstruct_samples(input, settings);

    auto rotated = input;
    constexpr double angle = 0.7;
    for (auto& sample : rotated.records) {
        const auto value = rotate_complex(sample.value, angle);
        sample.value.assign(value.begin(), value.end());
    }
    rec::finalize_sample_reconstruction_batch(rotated);
    const auto rotated_output = rec::reconstruct_samples(rotated, settings);
    const auto expected = rotate_complex(output.reconstructed, angle);
    for (std::size_t component = 0; component < expected.size();
         ++component) {
        CHECK(std::abs(rotated_output.reconstructed[component] -
                       expected[component]) < 1e-10);
    }
    const double energy = output.reconstructed[0] * output.reconstructed[0] +
        output.reconstructed[1] * output.reconstructed[1] +
        output.reconstructed[2] * output.reconstructed[2] +
        output.reconstructed[3] * output.reconstructed[3];
    const double rotated_energy =
        std::inner_product(
            rotated_output.reconstructed.begin(),
            rotated_output.reconstructed.end(),
            rotated_output.reconstructed.begin(), 0.0);
    CHECK(std::abs(energy - rotated_energy) < 1e-10);

    auto mismatched = input;
    mismatched.records[0].phase_reference_identity = id("wrong-phase");
    rec::finalize_sample_reconstruction_batch(mismatched);
    CHECK(rec::validate_sample_reconstruction_batch(mismatched).has(
        rec::SampleReconstructionIssue::Sample));
}

static void test_tamper_and_invalid_boundaries() {
    auto input = spectrum_fixture();
    input.records[0].value[0] += 1.0;
    CHECK(rec::validate_sample_reconstruction_batch(input).has(
        rec::SampleReconstructionIssue::Identity));
    auto model = candidate(
        rec::SampleReconstructionMethod::ExternalKernelPrediction);
    model.maturity = ure::research::Maturity::Production;
    rec::finalize_sample_reconstruction_candidate(model);
    CHECK(rec::validate_sample_reconstruction_candidate(model).has(
        rec::SampleReconstructionIssue::Candidate));
    auto settings = spectrum_config();
    settings.projection = rec::SampleProjectionPolicy::PhysicalStokesCone;
    rec::finalize_sample_reconstruction_config(settings);
    bool rejected = false;
    try {
        (void)rec::reconstruct_samples(spectrum_fixture(), settings);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK(rejected);
}

int main() {
    test_sample_splat_and_permutation();
    test_external_research_methods_and_ood();
    test_stokes_physical_projection();
    test_adversarial_spectrum_projection();
    test_complex_gauge_covariance();
    test_tamper_and_invalid_boundaries();
    if (failures != 0) {
        std::fprintf(stderr, "%d sample reconstruction checks failed\n",
                     failures);
        return 1;
    }
    std::puts("Sample reconstruction tests passed");
    return 0;
}
