#include "ure/runtime/multi_backend.hpp"
#include "ure/transport/semantics.hpp"

#include <cstdio>
#include <type_traits>

namespace sem = ure::semantic;
namespace tr = ure::transport;

static int failures = 0;

static_assert(std::is_trivially_copyable_v<tr::ObservableDescriptor>);
static_assert(std::is_trivially_copyable_v<tr::MeasureDescriptor>);
static_assert(std::is_trivially_copyable_v<tr::EstimatorDescriptor>);
static_assert(std::is_trivially_copyable_v<tr::SemanticContext>);

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", \
                         __FILE__, __LINE__, #condition); \
            ++failures; \
        } \
    } while (false)

static sem::IdentityDigest identity(std::uint8_t value) {
    sem::IdentityDigest result = {};
    result[0] = value;
    return result;
}

static tr::SemanticContext context() {
    tr::SemanticContext result;
    result.provenance.world_definition = identity(1);
    result.provenance.world_state = identity(2);
    result.provenance.time_sample = identity(3);
    result.provenance.observation_snapshot = identity(4);
    result.provenance.technique_graph = identity(5);
    result.provenance.measurement_schema = identity(6);
    result.observation_time.basis.ticks_per_second = 1000;
    result.observation_time.basis.synchronization_epoch = 7;
    result.observation_time.basis.clock_identity = identity(8);
    result.observation_time.start_tick = 100;
    result.observation_time.end_tick = 125;
    return result;
}

static tr::EstimatorDescriptor estimator(std::uint8_t technique) {
    tr::EstimatorDescriptor result;
    result.technique_identity = identity(technique);
    result.observable.kind = tr::ObservableKind::SpectralRadiance;
    result.observable.value_domain = tr::ValueDomain::Spectrum;
    result.observable.coherence = tr::CoherenceClass::Incoherent;
    result.observable.component_count = 8;
    result.measure.integral_identity = identity(32);
    result.measure.terms[0] = {tr::MeasureDomain::Path, 1};
    result.measure.terms[1] = {tr::MeasureDomain::Wavelength, 1};
    result.measure.term_count = 2;
    result.support.event_mask =
        tr::path_event_mask(tr::PathEvent::Camera) |
        tr::path_event_mask(tr::PathEvent::Diffuse);
    result.support.max_depth = 8;
    result.support.overlap_known = true;
    result.density = tr::DensityKind::ExplicitPdf;
    result.normalization =
        tr::NormalizationKind::MultipleImportanceSampling;
    result.correlation = tr::CorrelationModel::Independent;
    result.bias = tr::BiasClass::Unbiased;
    return result;
}

static void test_identity_and_time_types() {
    static_assert(std::is_same_v<
                  ure::runtime::IdentityDigest,
                  ure::semantic::IdentityDigest>);
    CHECK(sem::identity_empty({}));
    CHECK(!sem::identity_empty(identity(1)));
    CHECK(sem::valid_time_interval(context().observation_time));
    auto invalid = context().observation_time;
    invalid.end_tick = invalid.start_tick - 1;
    CHECK(!sem::valid_time_interval(invalid));
}

static void test_observable_validation() {
    const auto valid = estimator(10).observable;
    CHECK(tr::validate_observable(valid).ok());

    auto wrong_domain = valid;
    wrong_domain.value_domain = tr::ValueDomain::LinearRgb;
    CHECK(tr::validate_observable(wrong_domain).has(
        tr::DescriptorIssue::ObservableDomain));

    auto jones = valid;
    jones.kind = tr::ObservableKind::JonesField;
    jones.value_domain = tr::ValueDomain::ComplexJones;
    jones.coherence = tr::CoherenceClass::Coherent;
    jones.component_count = 4;
    CHECK(tr::validate_observable(jones).has(
        tr::DescriptorIssue::PhaseReference));
    jones.phase_reference_identity = identity(11);
    CHECK(tr::validate_observable(jones).ok());

    auto sensor = valid;
    sensor.kind = tr::ObservableKind::SensorResponse;
    sensor.value_domain = tr::ValueDomain::Scalar;
    sensor.component_count = 1;
    CHECK(tr::validate_observable(sensor).has(
        tr::DescriptorIssue::Identity));
    sensor.sensor_response_identity = identity(12);
    CHECK(tr::validate_observable(sensor).ok());
}

static void test_measure_support_and_estimator_validation() {
    auto value = estimator(12);
    CHECK(tr::validate_estimator(value).ok());

    auto unsorted = value.measure;
    unsorted.terms[0] = {tr::MeasureDomain::Wavelength, 1};
    unsorted.terms[1] = {tr::MeasureDomain::Path, 1};
    CHECK(tr::validate_measure(unsorted).has(
        tr::DescriptorIssue::Measure));

    auto partition = value.support;
    partition.partition_count = 2;
    CHECK(tr::validate_support(partition).has(
        tr::DescriptorIssue::Identity));

    value.correlation = tr::CorrelationModel::MarkovChain;
    CHECK(tr::validate_estimator(value).has(
        tr::DescriptorIssue::Correlation));
    value.density = tr::DensityKind::MarkovTransition;
    value.normalization = tr::NormalizationKind::ChainBootstrap;
    CHECK(tr::validate_estimator(value).ok());
}

static void test_uncertainty_validation() {
    tr::UncertaintyDescriptor uncertainty;
    uncertainty.channel_count = 4;
    uncertainty.first_moment = true;
    uncertainty.second_moment = true;
    uncertainty.cross_moments = true;
    uncertainty.effective_sample_size = 32.0;
    CHECK(tr::validate_uncertainty(uncertainty).ok());

    uncertainty.first_moment = false;
    CHECK(tr::validate_uncertainty(uncertainty).has(
        tr::DescriptorIssue::Uncertainty));
    uncertainty.first_moment = true;
    uncertainty.ood_status = tr::OodStatus::Unknown;
    CHECK(tr::validate_uncertainty(uncertainty).has(
        tr::DescriptorIssue::Uncertainty));
}

static void test_exact_and_unit_compatibility() {
    const auto left = estimator(20);
    auto right = estimator(21);
    const auto current = context();
    auto result = tr::classify_compatibility(
        left, current, right, current);
    CHECK(result.kind == tr::CompatibilityKind::Compatible);
    CHECK(result.rule ==
          tr::CombinationRule::MultipleImportanceSampling);
    CHECK(result.may_apply_mis);

    right.observable.unit.scale_to_si = 0.001;
    result = tr::classify_compatibility(
        left, current, right, current);
    CHECK(result.kind ==
          tr::CompatibilityKind::RequiresTransform);
    CHECK(result.reason == tr::CompatibilityReason::UnitTransform);
    CHECK(result.rule ==
          tr::CombinationRule::ValueTransformThenCombine);

    right = estimator(21);
    right.observable.unit.dimension.time = 1;
    result = tr::classify_compatibility(
        left, current, right, current);
    CHECK(result.kind == tr::CompatibilityKind::Undefined);
    CHECK(result.reason == tr::CompatibilityReason::UnitMismatch);
}

static void test_measure_and_support_compatibility() {
    const auto left = estimator(30);
    auto right = estimator(31);
    const auto current = context();
    right.measure.coordinate_identity = identity(40);
    right.measure.conversion_identity = identity(41);
    auto left_convertible = left;
    left_convertible.measure.conversion_identity = identity(41);
    auto result = tr::classify_compatibility(
        left_convertible, current, right, current);
    CHECK(result.kind ==
          tr::CompatibilityKind::RequiresTransform);
    CHECK(result.reason ==
          tr::CompatibilityReason::MeasureTransform);

    auto partition_left = left;
    auto partition_right = estimator(31);
    partition_left.support.partition_identity = identity(42);
    partition_right.support.partition_identity = identity(42);
    partition_left.support.partition_count = 2;
    partition_right.support.partition_count = 2;
    partition_left.support.partition_index = 0;
    partition_right.support.partition_index = 1;
    result = tr::classify_compatibility(
        partition_left, current, partition_right, current);
    CHECK(result.kind == tr::CompatibilityKind::Compatible);
    CHECK(result.rule == tr::CombinationRule::DisjointSupportSum);
    CHECK(!result.may_apply_mis);

    auto unknown = estimator(31);
    unknown.support.overlap_known = false;
    result = tr::classify_compatibility(
        left, current, unknown, current);
    CHECK(result.kind == tr::CompatibilityKind::Undefined);
    CHECK(result.reason == tr::CompatibilityReason::SupportUnknown);
}

static void test_correlation_and_preview_compatibility() {
    auto left = estimator(50);
    auto right = estimator(51);
    const auto current = context();
    left.correlation = tr::CorrelationModel::ReservoirReuse;
    left.density = tr::DensityKind::NormalizedReservoirWeight;
    left.normalization =
        tr::NormalizationKind::ReservoirNormalization;
    auto result = tr::classify_compatibility(
        left, current, right, current);
    CHECK(result.kind == tr::CompatibilityKind::Compatible);
    CHECK(result.rule == tr::CombinationRule::GeneralizedResampling);

    left = estimator(50);
    right.correlation = tr::CorrelationModel::MarkovChain;
    right.density = tr::DensityKind::MarkovTransition;
    right.normalization = tr::NormalizationKind::ChainBootstrap;
    result = tr::classify_compatibility(
        left, current, right, current);
    CHECK(result.kind ==
          tr::CompatibilityKind::IndependentAggregate);
    CHECK(result.rule ==
          tr::CombinationRule::IndependentReplicateAggregate);

    right = estimator(51);
    right.bias = tr::BiasClass::BiasedPreview;
    result = tr::classify_compatibility(
        left, current, right, current);
    CHECK(result.kind == tr::CompatibilityKind::PreviewOnly);
    CHECK(result.rule == tr::CombinationRule::SeparatePreview);
}

static void test_context_compatibility() {
    const auto left = estimator(60);
    const auto right = estimator(61);
    const auto left_context = context();
    auto right_context = context();

    right_context.provenance.world_state = identity(70);
    auto result = tr::classify_compatibility(
        left, left_context, right, right_context);
    CHECK(result.kind == tr::CompatibilityKind::Undefined);
    CHECK(result.reason ==
          tr::CompatibilityReason::ProvenanceMismatch);

    right_context = context();
    right_context.observation_time.basis.ticks_per_second = 2000;
    result = tr::classify_compatibility(
        left, left_context, right, right_context);
    CHECK(result.kind ==
          tr::CompatibilityKind::RequiresTransform);
    CHECK(result.rule ==
          tr::CombinationRule::TemporalTransformThenCombine);

    right_context = context();
    right_context.observation_time.basis.clock_identity = identity(71);
    result = tr::classify_compatibility(
        left, left_context, right, right_context);
    CHECK(result.kind == tr::CompatibilityKind::Undefined);
    CHECK(result.reason ==
          tr::CompatibilityReason::TimeClockMismatch);

    right_context = context();
    right_context.observation_time.end_tick += 1;
    result = tr::classify_compatibility(
        left, left_context, right, right_context);
    CHECK(result.kind == tr::CompatibilityKind::Undefined);
    CHECK(result.reason ==
          tr::CompatibilityReason::TimeIntervalMismatch);
}

int main() {
    test_identity_and_time_types();
    test_observable_validation();
    test_measure_support_and_estimator_validation();
    test_uncertainty_validation();
    test_exact_and_unit_compatibility();
    test_measure_and_support_compatibility();
    test_correlation_and_preview_compatibility();
    test_context_compatibility();
    if (failures != 0) {
        std::fprintf(stderr, "%d high-order semantic checks failed\n",
                     failures);
        return 1;
    }
    std::printf("High-order semantic contract checks passed\n");
    return 0;
}
