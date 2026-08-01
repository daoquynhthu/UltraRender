#include "ure/reconstruction/checkpoint.hpp"

#include "ure/runtime/multi_backend.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <vector>

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

static ure::transport::ObservableDescriptor stokes_observable() {
    ure::transport::ObservableDescriptor value;
    value.kind = ure::transport::ObservableKind::StokesRadiance;
    value.value_domain = ure::transport::ValueDomain::Stokes;
    value.coherence = ure::transport::CoherenceClass::Incoherent;
    value.component_count = 4;
    value.unit.dimension.length = -1;
    value.unit.dimension.mass = 1;
    value.unit.dimension.time = -3;
    return value;
}

static rec::MeasurementPlaneDescriptor plane(
    rec::MeasurementPlaneKind kind,
    rec::MeasurementScalarType scalar,
    rec::MeasurementMergeRule merge,
    rec::MeasurementRetention retention,
    std::string_view identity,
    std::uint64_t elements,
    std::uint32_t components = 1) {
    rec::MeasurementPlaneDescriptor value;
    value.kind = kind;
    value.scalar_type = scalar;
    value.merge_rule = merge;
    value.retention = retention;
    value.semantic_identity = id(identity);
    value.element_count = elements;
    value.component_count = components;
    value.required = retention == rec::MeasurementRetention::Required;
    return value;
}

static rec::MeasurementSchema schema() {
    rec::MeasurementSchema value;
    value.width = 2;
    value.height = 2;
    auto observable = plane(
        rec::MeasurementPlaneKind::Observable,
        rec::MeasurementScalarType::Float32,
        rec::MeasurementMergeRule::Sum,
        rec::MeasurementRetention::Required,
        "measurement.observable", 4, 4);
    observable.observable = stokes_observable();
    observable.unit = observable.observable.unit;
    value.planes.push_back(observable);
    value.planes.push_back(plane(
        rec::MeasurementPlaneKind::SampleCount,
        rec::MeasurementScalarType::UInt64,
        rec::MeasurementMergeRule::Sum,
        rec::MeasurementRetention::Required,
        "measurement.sample-count", 4));
    value.planes.push_back(plane(
        rec::MeasurementPlaneKind::ValidityMask,
        rec::MeasurementScalarType::UInt8,
        rec::MeasurementMergeRule::RequireEqual,
        rec::MeasurementRetention::Required,
        "measurement.geometry-validity", 4));
    value.planes.push_back(plane(
        rec::MeasurementPlaneKind::FirstMoment,
        rec::MeasurementScalarType::Float64,
        rec::MeasurementMergeRule::Sum,
        rec::MeasurementRetention::Statistics,
        "measurement.first-moment", 4, 4));
    value.planes.push_back(plane(
        rec::MeasurementPlaneKind::SecondMoment,
        rec::MeasurementScalarType::Float64,
        rec::MeasurementMergeRule::Sum,
        rec::MeasurementRetention::Statistics,
        "measurement.second-moment", 4, 4));
    auto variance = plane(
        rec::MeasurementPlaneKind::Variance,
        rec::MeasurementScalarType::Float64,
        rec::MeasurementMergeRule::Derived,
        rec::MeasurementRetention::Statistics,
        "measurement.variance", 4, 4);
    variance.derivation.kind =
        rec::MeasurementDerivationKind::SampleVariance;
    variance.derivation.count_plane = 1;
    variance.derivation.first_plane = 3;
    variance.derivation.second_plane = 4;
    value.planes.push_back(variance);
    value.planes.push_back(plane(
        rec::MeasurementPlaneKind::TechniqueIdentity,
        rec::MeasurementScalarType::UInt64,
        rec::MeasurementMergeRule::RequireEqual,
        rec::MeasurementRetention::Attribution,
        "measurement.technique", 4));
    auto normal = plane(
        rec::MeasurementPlaneKind::Normal,
        rec::MeasurementScalarType::Float32,
        rec::MeasurementMergeRule::RequireEqual,
        rec::MeasurementRetention::Geometry,
        "measurement.normal", 4, 3);
    normal.validity_plane = 2;
    value.planes.push_back(normal);
    value.planes.push_back(plane(
        rec::MeasurementPlaneKind::SampleRecord,
        rec::MeasurementScalarType::Float32,
        rec::MeasurementMergeRule::Append,
        rec::MeasurementRetention::SampleRecords,
        "measurement.sample-record", 32, 4));
    rec::finalize_measurement_schema(value);
    return value;
}

static rec::MeasurementProvenance provenance(
    std::uint64_t start,
    std::uint64_t count,
    std::string_view producer) {
    rec::MeasurementProvenance value;
    value.identities.world_definition = id("world-definition");
    value.identities.world_state = id("world-state");
    value.identities.time_sample = id("time-sample");
    value.identities.observation_snapshot = id("snapshot");
    value.identities.technique_graph = id("technique-graph");
    value.identities.parameter_set = id("parameters");
    value.exposure.basis.ticks_per_second = 1000000;
    value.exposure.basis.clock_identity = id("clock");
    value.exposure.start_tick = 10;
    value.exposure.end_tick = 20;
    value.portfolio_schedule_identity = id("portfolio-schedule");
    value.sample_namespace_identity = id("sample-namespace");
    value.producer_identity = id(producer);
    value.sample_ranges.push_back({start, count});
    return value;
}

template <typename T>
static void fill(rec::MeasurementPlane& plane_value, T value) {
    for (std::size_t offset = 0; offset < plane_value.payload.size();
         offset += sizeof(T)) {
        std::memcpy(plane_value.payload.data() + offset,
                    &value, sizeof(T));
    }
}

template <typename T>
static T first(const rec::MeasurementPlane& plane_value) {
    T value{};
    std::memcpy(&value, plane_value.payload.data(), sizeof(T));
    return value;
}

static void test_schema_and_budget_selection() {
    const auto full = schema();
    CHECK(rec::validate_measurement_schema(full).ok());
    CHECK(full.schema_identity ==
          rec::compute_measurement_schema_identity(full));
    const auto required_bytes =
        rec::measurement_plane_bytes(full.planes[0]) +
        rec::measurement_plane_bytes(full.planes[1]) +
        rec::measurement_plane_bytes(full.planes[2]);
    const auto selected = rec::select_measurement_schema(
        full, required_bytes,
        rec::MeasurementRetention::SampleRecords);
    CHECK(selected.schema.planes.size() == 3);
    CHECK(selected.report.selected_bytes == required_bytes);
    CHECK(selected.report.losses.size() == 6);
    CHECK(selected.report.selected_schema_identity ==
          selected.schema.schema_identity);

    const auto statistics = rec::select_measurement_schema(
        full, required_bytes +
                  rec::measurement_plane_bytes(full.planes[3]) +
                  rec::measurement_plane_bytes(full.planes[4]) +
                  rec::measurement_plane_bytes(full.planes[5]),
        rec::MeasurementRetention::Statistics);
    CHECK(statistics.schema.planes.size() == 6);
    CHECK(statistics.report.losses.size() == 3);
    CHECK(statistics.report.losses.back().reason ==
          rec::MeasurementLossReason::RetentionLimit);

    auto sparse = full;
    sparse.schema_identity = {};
    sparse.planes.insert(sparse.planes.begin() + 3, plane(
        rec::MeasurementPlaneKind::TechniqueIdentity,
        rec::MeasurementScalarType::UInt64,
        rec::MeasurementMergeRule::RequireEqual,
        rec::MeasurementRetention::Attribution,
        "measurement.early-technique", 4));
    sparse.planes[6].derivation.count_plane = 1;
    sparse.planes[6].derivation.first_plane = 4;
    sparse.planes[6].derivation.second_plane = 5;
    rec::finalize_measurement_schema(sparse);
    const auto sparse_statistics = rec::select_measurement_schema(
        sparse, required_bytes +
                    rec::measurement_plane_bytes(sparse.planes[4]) +
                    rec::measurement_plane_bytes(sparse.planes[5]) +
                    rec::measurement_plane_bytes(sparse.planes[6]),
        rec::MeasurementRetention::Statistics);
    CHECK(rec::validate_measurement_schema(
        sparse_statistics.schema).ok());
    CHECK(sparse_statistics.schema.planes[5].derivation.first_plane == 3);
    CHECK(sparse_statistics.schema.planes[5].derivation.second_plane == 4);

    bool rejected = false;
    try {
        static_cast<void>(rec::select_measurement_schema(
            full, required_bytes - 1,
            rec::MeasurementRetention::Required));
    } catch (const std::length_error&) {
        rejected = true;
    }
    CHECK(rejected);
}

static void test_bundle_merge_is_canonical() {
    const auto descriptor = schema();
    auto left = rec::make_measurement_bundle(
        descriptor, provenance(0, 4, "producer-a"));
    auto right = rec::make_measurement_bundle(
        descriptor, provenance(4, 4, "producer-b"));
    fill(left.planes[0], 1.0f);
    fill(right.planes[0], 2.0f);
    fill(left.planes[1], std::uint64_t{2});
    fill(right.planes[1], std::uint64_t{2});
    fill(left.planes[2], std::uint8_t{1});
    fill(right.planes[2], std::uint8_t{1});
    fill(left.planes[3], 3.0);
    fill(right.planes[3], 7.0);
    fill(left.planes[4], 5.0);
    fill(right.planes[4], 25.0);
    fill(left.planes[6], std::uint64_t{7});
    fill(right.planes[6], std::uint64_t{7});
    fill(left.planes[7], 0.5f);
    fill(right.planes[7], 0.5f);
    left.planes[8].payload.resize(4 * sizeof(float), 0);
    right.planes[8].payload.resize(4 * sizeof(float), 0);
    fill(left.planes[8], 9.0f);
    fill(right.planes[8], 10.0f);
    CHECK(rec::validate_measurement_bundle(left).ok());
    CHECK(rec::validate_measurement_bundle(right).ok());

    const std::vector forward{left, right};
    const std::vector reverse{right, left};
    const auto merged = rec::merge_measurement_bundles(forward);
    const auto reversed = rec::merge_measurement_bundles(reverse);
    CHECK(merged.provenance.sample_ranges.size() == 2);
    CHECK(merged.provenance.sample_ranges[0].start == 0);
    CHECK(merged.provenance.sample_ranges[1].start == 4);
    CHECK(std::fabs(first<float>(merged.planes[0]) - 3.0f) < 1e-6f);
    CHECK(first<std::uint64_t>(merged.planes[1]) == 4);
    CHECK(std::fabs(first<double>(merged.planes[3]) - 10.0) < 1e-12);
    CHECK(std::fabs(first<double>(merged.planes[5]) -
                    (5.0 / 3.0)) < 1e-12);
    CHECK(first<std::uint64_t>(merged.planes[6]) == 7);
    CHECK(merged.planes[8].payload.size() == 8 * sizeof(float));
    CHECK(merged.provenance.producer_identity ==
          reversed.provenance.producer_identity);
    CHECK(merged.planes[0].payload == reversed.planes[0].payload);
    CHECK(merged.planes[8].payload == reversed.planes[8].payload);
}

static void test_merge_rejects_invalid_semantics() {
    const auto descriptor = schema();
    auto left = rec::make_measurement_bundle(
        descriptor, provenance(0, 4, "producer-a"));
    auto right = rec::make_measurement_bundle(
        descriptor, provenance(2, 4, "producer-b"));
    bool rejected = false;
    try {
        const std::vector values{left, right};
        static_cast<void>(rec::merge_measurement_bundles(values));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK(rejected);

    right = rec::make_measurement_bundle(
        descriptor, provenance(4, 4, "producer-b"));
    fill(right.planes[6], std::uint64_t{1});
    rejected = false;
    try {
        const std::vector values{left, right};
        static_cast<void>(rec::merge_measurement_bundles(values));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK(rejected);

    right = rec::make_measurement_bundle(
        descriptor, provenance(4, 4, "producer-b"));
    right.provenance.identities.observation_snapshot = id("other-snapshot");
    rejected = false;
    try {
        const std::vector values{left, right};
        static_cast<void>(rec::merge_measurement_bundles(values));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK(rejected);

    right = rec::make_measurement_bundle(
        descriptor, provenance(4, 4, "producer-b"));
    right.provenance.portfolio_schedule_identity = id("other-schedule");
    rejected = false;
    try {
        const std::vector values{left, right};
        static_cast<void>(rec::merge_measurement_bundles(values));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK(rejected);
}

static void test_typed_complex_plane() {
    rec::MeasurementSchema value;
    value.width = 1;
    value.height = 1;
    auto field = plane(
        rec::MeasurementPlaneKind::JonesField,
        rec::MeasurementScalarType::ComplexFloat64,
        rec::MeasurementMergeRule::Sum,
        rec::MeasurementRetention::Required,
        "measurement.jones", 1, 4);
    field.observable.kind = ure::transport::ObservableKind::JonesField;
    field.observable.value_domain =
        ure::transport::ValueDomain::ComplexJones;
    field.observable.coherence = ure::transport::CoherenceClass::Coherent;
    field.observable.component_count = 4;
    field.observable.phase_reference_identity = id("phase-reference");
    value.planes.push_back(field);
    rec::finalize_measurement_schema(value);
    CHECK(rec::validate_measurement_schema(value).ok());
    value.planes[0].observable.kind =
        ure::transport::ObservableKind::SpectralRadiance;
    value.schema_identity =
        rec::compute_measurement_schema_identity(value);
    CHECK(rec::validate_measurement_schema(value).has(
        rec::MeasurementIssue::Observable));
}

static rec::MeasurementSchema statistical_schema() {
    rec::MeasurementSchema value;
    value.width = 1;
    value.height = 1;
    value.planes.push_back(plane(
        rec::MeasurementPlaneKind::SampleCount,
        rec::MeasurementScalarType::UInt64,
        rec::MeasurementMergeRule::Sum,
        rec::MeasurementRetention::Required,
        "statistics.count", 1));
    const auto sum = [&value](rec::MeasurementPlaneKind kind,
                              std::string_view identity) {
        value.planes.push_back(plane(
            kind, rec::MeasurementScalarType::Float64,
            rec::MeasurementMergeRule::Sum,
            rec::MeasurementRetention::Statistics,
            identity, 1));
    };
    sum(rec::MeasurementPlaneKind::FirstMoment, "statistics.sum-a");
    sum(rec::MeasurementPlaneKind::SecondMoment, "statistics.sum-a2");
    auto variance = plane(
        rec::MeasurementPlaneKind::Variance,
        rec::MeasurementScalarType::Float64,
        rec::MeasurementMergeRule::Derived,
        rec::MeasurementRetention::Statistics,
        "statistics.variance", 1);
    variance.derivation = {
        rec::MeasurementDerivationKind::SampleVariance, 0, 1, 2,
        rec::kNoValidityPlane};
    value.planes.push_back(variance);
    sum(rec::MeasurementPlaneKind::EstimatorWeight, "statistics.weight");
    sum(rec::MeasurementPlaneKind::SecondMoment, "statistics.weight2");
    auto ess = plane(
        rec::MeasurementPlaneKind::EffectiveSampleCount,
        rec::MeasurementScalarType::Float64,
        rec::MeasurementMergeRule::Derived,
        rec::MeasurementRetention::Statistics,
        "statistics.ess", 1);
    ess.derivation = {
        rec::MeasurementDerivationKind::EffectiveSampleCount,
        rec::kNoValidityPlane, 4, 5, rec::kNoValidityPlane};
    value.planes.push_back(ess);
    sum(rec::MeasurementPlaneKind::FirstMoment, "statistics.sum-b");
    sum(rec::MeasurementPlaneKind::CrossMoment, "statistics.cross");
    auto covariance = plane(
        rec::MeasurementPlaneKind::Covariance,
        rec::MeasurementScalarType::Float64,
        rec::MeasurementMergeRule::Derived,
        rec::MeasurementRetention::Statistics,
        "statistics.covariance", 1);
    covariance.derivation = {
        rec::MeasurementDerivationKind::SampleCovariance, 0, 1, 7, 8};
    value.planes.push_back(covariance);
    rec::finalize_measurement_schema(value);
    return value;
}

static void test_derived_statistics_merge() {
    const auto descriptor = statistical_schema();
    auto left = rec::make_measurement_bundle(
        descriptor, provenance(0, 2, "statistics-a"));
    auto right = rec::make_measurement_bundle(
        descriptor, provenance(2, 2, "statistics-b"));
    fill(left.planes[0], std::uint64_t{2});
    fill(right.planes[0], std::uint64_t{2});
    fill(left.planes[1], 3.0);
    fill(right.planes[1], 7.0);
    fill(left.planes[2], 5.0);
    fill(right.planes[2], 25.0);
    fill(left.planes[4], 2.0);
    fill(right.planes[4], 2.0);
    fill(left.planes[5], 2.0);
    fill(right.planes[5], 2.0);
    fill(left.planes[7], 6.0);
    fill(right.planes[7], 14.0);
    fill(left.planes[8], 10.0);
    fill(right.planes[8], 50.0);
    const std::vector shards{left, right};
    const auto merged = rec::merge_measurement_bundles(shards);
    CHECK(std::fabs(first<double>(merged.planes[3]) -
                    5.0 / 3.0) < 1e-12);
    CHECK(std::fabs(first<double>(merged.planes[6]) - 4.0) < 1e-12);
    CHECK(std::fabs(first<double>(merged.planes[9]) -
                    10.0 / 3.0) < 1e-12);
}

static void test_checkpoint_and_partial_read() {
    const auto descriptor = schema();
    auto bundle = rec::make_measurement_bundle(
        descriptor, provenance(12, 8, "checkpoint-producer"));
    fill(bundle.planes[0], 2.5f);
    fill(bundle.planes[1], std::uint64_t{8});
    fill(bundle.planes[2], std::uint8_t{1});
    fill(bundle.planes[3], 4.0);
    fill(bundle.planes[4], 16.0);
    rec::refresh_derived_measurement_planes(bundle);
    fill(bundle.planes[6], std::uint64_t{3});
    fill(bundle.planes[7], 0.25f);
    bundle.planes[8].payload.resize(4 * sizeof(float));
    fill(bundle.planes[8], 7.0f);
    const auto bytes = rec::write_measurement_checkpoint(bundle);
    const auto prefix_size =
        ure::research::measurement_artifact_index_size(bytes);
    const auto index = rec::inspect_measurement_checkpoint(
        std::span<const std::uint8_t>(bytes).first(
            static_cast<std::size_t>(prefix_size)),
        bytes.size());
    CHECK(index.plane_chunks.size() == descriptor.planes.size());
    const auto& chunk =
        index.artifact.chunks[index.plane_chunks[2]];
    const auto partial = rec::read_measurement_checkpoint_plane(
        std::span<const std::uint8_t>(bytes).subspan(
            static_cast<std::size_t>(chunk.offset),
            static_cast<std::size_t>(chunk.stored_size)),
        index, 2, descriptor);
    CHECK(partial.payload == bundle.planes[2].payload);

    const auto restored = rec::read_measurement_checkpoint(bytes);
    CHECK(restored.schema.schema_identity == descriptor.schema_identity);
    CHECK(restored.provenance.sample_ranges ==
          bundle.provenance.sample_ranges);
    CHECK(restored.provenance.portfolio_schedule_identity ==
          bundle.provenance.portfolio_schedule_identity);
    CHECK(restored.planes[0].payload == bundle.planes[0].payload);
    CHECK(restored.planes[8].payload == bundle.planes[8].payload);

    auto legacy_artifact =
        ure::research::read_measurement_artifact(bytes);
    auto metadata = std::ranges::find_if(
        legacy_artifact.chunks,
        [](const ure::research::ArtifactChunk& value) {
            return value.kind ==
                ure::research::ArtifactChunkKind::Metadata;
        });
    CHECK(metadata != legacy_artifact.chunks.end());
    if (metadata != legacy_artifact.chunks.end()) {
        const auto schedule =
            bundle.provenance.portfolio_schedule_identity;
        const auto position = std::search(
            metadata->payload.begin(), metadata->payload.end(),
            schedule.begin(), schedule.end());
        CHECK(position != metadata->payload.end());
        if (position != metadata->payload.end()) {
            metadata->payload.erase(
                position, position + schedule.size());
            metadata->payload[4] = 1;
            metadata->payload[5] = 0;
            metadata->payload[6] = 0;
            metadata->payload[7] = 0;
            metadata->semantic_identity =
                id("ure.measurement-bundle.metadata.v1");
            legacy_artifact.schema_version = 1;
            legacy_artifact.source_identity =
                ure::runtime::identity_digest(std::as_bytes(
                    std::span<const std::uint8_t>(metadata->payload)));
            const auto legacy_bytes =
                ure::research::write_measurement_artifact(
                    legacy_artifact);
            const auto legacy_restored =
                rec::read_measurement_checkpoint(legacy_bytes);
            CHECK(ure::semantic::identity_empty(
                legacy_restored.provenance
                    .portfolio_schedule_identity));
        }
    }

    auto corrupt = bytes;
    corrupt[static_cast<std::size_t>(chunk.offset)] ^= 1;
    bool rejected = false;
    try {
        static_cast<void>(rec::read_measurement_checkpoint(corrupt));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK(rejected);
}

int main() {
    test_schema_and_budget_selection();
    test_bundle_merge_is_canonical();
    test_merge_rejects_invalid_semantics();
    test_typed_complex_plane();
    test_derived_statistics_merge();
    test_checkpoint_and_partial_read();
    if (failures != 0) {
        std::fprintf(stderr, "%d measurement bundle checks failed\n",
                     failures);
        return 1;
    }
    std::printf("Measurement bundle checks passed\n");
    return 0;
}
