#include <ure/product/product_measurement.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace ure::product {
namespace {

namespace rec = reconstruction;

Identity identity(std::string_view domain) {
    return content_identity(std::span(
        reinterpret_cast<const std::uint8_t*>(domain.data()), domain.size()));
}

template <typename T>
std::vector<std::uint8_t> encode(std::span<const T> values) {
    using Bits = std::conditional_t<
        sizeof(T) == 4, std::uint32_t, std::uint64_t>;
    std::vector<std::uint8_t> output(values.size() * sizeof(T));
    for (std::size_t value_index = 0; value_index < values.size();
         ++value_index) {
        const Bits bits = [&]() {
            if constexpr (std::is_floating_point_v<T>)
                return std::bit_cast<Bits>(values[value_index]);
            else
                return static_cast<Bits>(values[value_index]);
        }();
        for (std::size_t byte = 0; byte < sizeof(T); ++byte) {
            output[value_index * sizeof(T) + byte] =
                static_cast<std::uint8_t>(bits >> (byte * 8));
        }
    }
    return output;
}

template <typename T>
std::vector<T> decode(const rec::MeasurementPlane& plane) {
    if (plane.payload.size() % sizeof(T) != 0)
        throw std::invalid_argument("Measurement payload size is invalid");
    using Bits = std::conditional_t<
        sizeof(T) == 4, std::uint32_t, std::uint64_t>;
    std::vector<T> values(plane.payload.size() / sizeof(T));
    for (std::size_t index = 0; index < values.size(); ++index) {
        Bits bits{};
        for (std::size_t byte = 0; byte < sizeof(T); ++byte)
            bits |= static_cast<Bits>(
                plane.payload[index * sizeof(T) + byte]) << (byte * 8);
        if constexpr (std::is_floating_point_v<T>)
            values[index] = std::bit_cast<T>(bits);
        else
            values[index] = static_cast<T>(bits);
    }
    return values;
}

rec::MeasurementPlaneDescriptor plane(
    rec::MeasurementPlaneKind kind,
    rec::MeasurementScalarType scalar,
    rec::MeasurementMergeRule merge,
    std::string identity_name,
    std::uint64_t elements,
    std::uint32_t components = 1) {
    rec::MeasurementPlaneDescriptor result;
    result.kind = kind;
    result.scalar_type = scalar;
    result.merge_rule = merge;
    result.retention = rec::MeasurementRetention::Required;
    result.semantic_identity = identity(identity_name);
    result.element_count = elements;
    result.component_count = components;
    return result;
}

transport::ObservableDescriptor rgb_observable() {
    transport::ObservableDescriptor result;
    result.kind = transport::ObservableKind::SensorResponse;
    result.value_domain = transport::ValueDomain::LinearRgb;
    result.coherence = transport::CoherenceClass::Incoherent;
    result.component_count = 3;
    result.unit.dimension.mass = 1;
    result.unit.dimension.time = -3;
    result.sensor_response_identity = identity(
        "UltraRender.SensorResponse.LinearRgb.v1");
    return result;
}

std::string estimator_key(
    const IntegratorEstimatorMetadata& estimator) {
    return std::to_string(static_cast<std::uint32_t>(estimator.mode)) + "." +
        std::to_string(static_cast<std::uint32_t>(estimator.policy)) + "." +
        std::to_string(estimator.biased ? 1 : 0) + "." +
        std::to_string(estimator.temporal_reuse ? 1 : 0) + "." +
        std::to_string(estimator.spatial_reuse ? 1 : 0) + "." +
        std::to_string(estimator.sample_space_version) + "." +
        std::to_string(estimator.scene_epoch);
}

rec::MeasurementProvenance provenance(
    const ProductIdentitySet& identities,
    const Identity& sample_namespace,
    const Identity& producer,
    std::uint64_t sample_start,
    std::uint64_t sample_count) {
    rec::MeasurementProvenance result;
    result.identities.world_definition = identities.snapshot;
    result.identities.world_state = identities.snapshot;
    result.identities.time_sample = identity("UltraRender.Time.Static.v1");
    result.identities.observation_snapshot = identities.snapshot;
    result.identities.technique_graph = identities.plan;
    result.identities.parameter_set = identities.objective;
    result.identities.solver_semantics = identities.build;
    result.exposure.basis.ticks_per_second = 1;
    result.exposure.basis.clock_identity = identity(
        "UltraRender.Clock.Static.v1");
    result.portfolio_schedule_identity = identities.plan;
    result.sample_namespace_identity = sample_namespace;
    result.producer_identity = producer;
    result.sample_ranges.push_back({sample_start, sample_count});
    return result;
}

void assign_payload(rec::MeasurementBundle& bundle,
                    std::size_t plane_index,
                    std::vector<std::uint8_t> payload) {
    bundle.planes.at(plane_index).payload = std::move(payload);
}

rec::MeasurementBundle make_estimate_bundle(
    const RenderMeasurementStatistics& statistics,
    const ProductIdentitySet& identities,
    std::uint64_t accepted_samples,
    const Identity& producer_identity) {
    const auto pixels = static_cast<std::uint64_t>(statistics.width) *
                        static_cast<std::uint64_t>(statistics.height);
    rec::MeasurementSchema schema;
    schema.width = statistics.width;
    schema.height = statistics.height;
    auto estimate = plane(
        rec::MeasurementPlaneKind::Observable,
        rec::MeasurementScalarType::Float32,
        rec::MeasurementMergeRule::RequireEqual,
        "UltraRender.ProductMeasurement.Estimate.LinearRgb.v1", pixels, 3);
    estimate.observable = rgb_observable();
    estimate.unit = estimate.observable.unit;
    schema.planes.push_back(estimate);
    schema.planes.push_back(plane(
        rec::MeasurementPlaneKind::ValidityMask,
        rec::MeasurementScalarType::UInt8,
        rec::MeasurementMergeRule::RequireEqual,
        "UltraRender.ProductMeasurement.Validity.v1", pixels));
    const auto add_auxiliary = [&](rec::MeasurementPlaneKind kind,
                                   std::string name,
                                   std::uint32_t components) {
        auto descriptor = plane(
            kind, rec::MeasurementScalarType::Float32,
            rec::MeasurementMergeRule::RequireEqual,
            std::move(name), pixels, components);
        descriptor.validity_plane = 1;
        schema.planes.push_back(std::move(descriptor));
    };
    const auto auxiliary_key = estimator_key(
        statistics.auxiliary_estimator);
    add_auxiliary(
        rec::MeasurementPlaneKind::Normal,
        "UltraRender.ProductMeasurement.Normal." + auxiliary_key, 3);
    add_auxiliary(
        rec::MeasurementPlaneKind::Albedo,
        "UltraRender.ProductMeasurement.Albedo." + auxiliary_key, 3);
    add_auxiliary(
        rec::MeasurementPlaneKind::Depth,
        "UltraRender.ProductMeasurement.Depth." + auxiliary_key, 1);
    schema.planes.back().unit.dimension.length = 1;
    add_auxiliary(
        rec::MeasurementPlaneKind::Uv,
        "UltraRender.ProductMeasurement.Uv." + auxiliary_key, 2);
    add_auxiliary(
        rec::MeasurementPlaneKind::Motion,
        "UltraRender.ProductMeasurement.Motion." + auxiliary_key, 2);
    schema.planes.push_back(plane(
        rec::MeasurementPlaneKind::TechniqueIdentity,
        rec::MeasurementScalarType::UInt32,
        rec::MeasurementMergeRule::RequireEqual,
        "UltraRender.ProductMeasurement.AuxiliaryTechnique." +
            auxiliary_key,
        1, 7));
    rec::finalize_measurement_schema(schema);
    const auto first_sample = std::ranges::min_element(
        statistics.endpoints, {},
        &RenderMeasurementEndpointStatistics::sample_range_start);
    if (first_sample == statistics.endpoints.end())
        throw std::invalid_argument(
            "Product measurement estimate has no sample domain");
    auto bundle = rec::make_measurement_bundle(
        schema,
        provenance(
            identities,
            identity("UltraRender.ProductMeasurement.FinalNamespace.v1"),
            producer_identity,
            first_sample->sample_range_start,
            accepted_samples));
    assign_payload(bundle, 0, encode<float>(statistics.estimate));
    assign_payload(bundle, 1, std::vector<std::uint8_t>(pixels, 1));
    assign_payload(bundle, 2, encode<float>(statistics.normal));
    assign_payload(bundle, 3, encode<float>(statistics.albedo));
    assign_payload(bundle, 4, encode<float>(statistics.depth));
    assign_payload(bundle, 5, encode<float>(statistics.uv));
    assign_payload(bundle, 6, encode<float>(statistics.motion));
    const std::array<std::uint32_t, 7> estimator_values{
        static_cast<std::uint32_t>(statistics.auxiliary_estimator.mode),
        static_cast<std::uint32_t>(statistics.auxiliary_estimator.policy),
        statistics.auxiliary_estimator.biased ? 1u : 0u,
        statistics.auxiliary_estimator.temporal_reuse ? 1u : 0u,
        statistics.auxiliary_estimator.spatial_reuse ? 1u : 0u,
        statistics.auxiliary_estimator.sample_space_version,
        statistics.auxiliary_estimator.scene_epoch};
    assign_payload(bundle, 7, encode<std::uint32_t>(estimator_values));
    return bundle;
}

rec::MeasurementBundle make_endpoint_bundle(
    const RenderMeasurementStatistics& statistics,
    const RenderMeasurementEndpointStatistics& endpoint,
    const ProductIdentitySet& identities,
    std::size_t endpoint_index,
    const Identity& producer_identity) {
    const auto pixels = static_cast<std::uint64_t>(statistics.width) *
                        static_cast<std::uint64_t>(statistics.height);
    const std::string prefix = "UltraRender.ProductMeasurement.Endpoint." +
        std::to_string(endpoint_index) + "." +
        estimator_key(endpoint.estimator);
    rec::MeasurementSchema schema;
    schema.width = statistics.width;
    schema.height = statistics.height;
    schema.planes.push_back(plane(
        rec::MeasurementPlaneKind::SampleCount,
        rec::MeasurementScalarType::UInt64,
        rec::MeasurementMergeRule::Sum,
        prefix + ".SampleCount", pixels));
    schema.planes.push_back(plane(
        rec::MeasurementPlaneKind::FirstMoment,
        rec::MeasurementScalarType::Float64,
        rec::MeasurementMergeRule::Sum,
        prefix + ".FirstMoment", pixels, 3));
    schema.planes.push_back(plane(
        rec::MeasurementPlaneKind::SecondMoment,
        rec::MeasurementScalarType::Float64,
        rec::MeasurementMergeRule::Sum,
        prefix + ".SecondMoment", pixels, 3));
    schema.planes.push_back(plane(
        rec::MeasurementPlaneKind::CrossMoment,
        rec::MeasurementScalarType::Float64,
        rec::MeasurementMergeRule::Sum,
        prefix + ".LagOneProduct", pixels, 3));
    schema.planes.push_back(plane(
        rec::MeasurementPlaneKind::FirstContribution,
        rec::MeasurementScalarType::Float64,
        rec::MeasurementMergeRule::KeepFirst,
        prefix + ".FirstContribution", pixels, 3));
    schema.planes.push_back(plane(
        rec::MeasurementPlaneKind::LastContribution,
        rec::MeasurementScalarType::Float64,
        rec::MeasurementMergeRule::KeepLast,
        prefix + ".LastContribution", pixels, 3));
    auto variance = plane(
        rec::MeasurementPlaneKind::Variance,
        rec::MeasurementScalarType::Float64,
        rec::MeasurementMergeRule::Derived,
        prefix + ".SampleVariance", pixels, 3);
    variance.derivation = {
        rec::MeasurementDerivationKind::SampleVariance, 0, 1, 2,
        rec::kNoValidityPlane, rec::kNoValidityPlane,
        rec::kNoValidityPlane};
    schema.planes.push_back(variance);
    auto effective = plane(
        rec::MeasurementPlaneKind::EffectiveSampleCount,
        rec::MeasurementScalarType::Float64,
        rec::MeasurementMergeRule::Derived,
        prefix + ".LagOneEffectiveSampleCount", pixels, 3);
    effective.derivation = {
        rec::MeasurementDerivationKind::LagOneEffectiveSampleCount,
        0, 1, 2, 3, 4, 5};
    schema.planes.push_back(effective);
    schema.planes.push_back(plane(
        rec::MeasurementPlaneKind::MaximumAbsoluteContribution,
        rec::MeasurementScalarType::Float32,
        rec::MeasurementMergeRule::Maximum,
        prefix + ".MaximumAbsoluteContribution", pixels, 3));
    schema.planes.push_back(plane(
        rec::MeasurementPlaneKind::TailEventCount,
        rec::MeasurementScalarType::UInt64,
        rec::MeasurementMergeRule::Sum,
        prefix + ".TailEventCount.AbsoluteRadianceThreshold64", pixels, 3));
    schema.planes.push_back(plane(
        rec::MeasurementPlaneKind::EstimatorWeight,
        rec::MeasurementScalarType::Float64,
        rec::MeasurementMergeRule::RequireEqual,
        prefix + ".AggregationWeight", 1));
    schema.planes.push_back(plane(
        rec::MeasurementPlaneKind::TechniqueIdentity,
        rec::MeasurementScalarType::UInt32,
        rec::MeasurementMergeRule::RequireEqual,
        prefix + ".Technique", 1));
    rec::finalize_measurement_schema(schema);

    const auto sample_namespace = identity(prefix + ".SampleNamespace");
    auto bundle = rec::make_measurement_bundle(
        schema, provenance(identities, sample_namespace, producer_identity,
                           endpoint.sample_range_start,
                           endpoint.sample_count));
    std::vector<std::uint64_t> counts(
        static_cast<std::size_t>(pixels), endpoint.sample_count);
    const double aggregation_weight = endpoint.aggregation_weight;
    const std::uint32_t technique =
        static_cast<std::uint32_t>(endpoint.estimator.mode);
    assign_payload(bundle, 0, encode<std::uint64_t>(counts));
    assign_payload(bundle, 1, encode<double>(endpoint.first_moment_sums));
    assign_payload(bundle, 2, encode<double>(endpoint.second_moment_sums));
    assign_payload(bundle, 3, encode<double>(endpoint.lag_one_product_sums));
    assign_payload(bundle, 4, encode<double>(endpoint.first_contributions));
    assign_payload(bundle, 5, encode<double>(endpoint.last_contributions));
    assign_payload(bundle, 8,
                   encode<float>(endpoint.maximum_absolute_contributions));
    assign_payload(bundle, 9,
                   encode<std::uint64_t>(endpoint.tail_event_counts));
    assign_payload(bundle, 10,
                   encode<double>(std::span(&aggregation_weight, 1)));
    assign_payload(bundle, 11,
                   encode<std::uint32_t>(std::span(&technique, 1)));
    rec::refresh_derived_measurement_planes(bundle);
    return bundle;
}

}

ProductMeasurementSet make_product_measurement_set(
    const RenderMeasurementStatistics& statistics,
    const ProductIdentitySet& identities,
    std::uint64_t accepted_samples,
    const Identity& producer_identity) {
    const std::uint64_t pixels =
        static_cast<std::uint64_t>(statistics.width) * statistics.height;
    const std::uint64_t scalars = pixels * 3;
    if (!statistics.valid || statistics.width == 0 ||
        statistics.height == 0 || accepted_samples == 0 ||
        statistics.estimate.size() != scalars ||
        statistics.normal.size() != scalars ||
        statistics.albedo.size() != scalars ||
        statistics.depth.size() != pixels ||
        statistics.uv.size() != pixels * 2 ||
        statistics.motion.size() != pixels * 2 ||
        !validate_integrator_estimator_metadata(
            statistics.auxiliary_estimator) ||
        statistics.endpoints.empty()) {
        throw std::invalid_argument(
            "Product measurement statistics are incomplete");
    }
    ProductMeasurementSet result;
    result.auxiliary_outputs_wavefront_only =
        statistics.auxiliary_outputs_wavefront_only;
    result.estimate = make_estimate_bundle(
        statistics, identities, accepted_samples, producer_identity);
    result.endpoints.reserve(statistics.endpoints.size());
    std::uint64_t endpoint_sample_count = 0;
    double aggregation_weight_sum = 0.0;
    std::vector<double> reconstructed_estimate(
        static_cast<std::size_t>(scalars), 0.0);
    for (std::size_t index = 0; index < statistics.endpoints.size(); ++index) {
        const auto& endpoint = statistics.endpoints[index];
        if (endpoint.sample_count == 0 ||
            !validate_integrator_estimator_metadata(endpoint.estimator) ||
            endpoint.estimator.biased ||
            endpoint.sample_range_start >
                std::numeric_limits<std::uint64_t>::max() -
                    endpoint.sample_count ||
            endpoint.first_moment_sums.size() != scalars ||
            endpoint.second_moment_sums.size() != scalars ||
            endpoint.lag_one_product_sums.size() != scalars ||
            endpoint.first_contributions.size() != scalars ||
            endpoint.last_contributions.size() != scalars ||
            endpoint.maximum_absolute_contributions.size() != scalars ||
            endpoint.tail_event_counts.size() != scalars ||
            !std::isfinite(endpoint.aggregation_weight) ||
            endpoint.aggregation_weight < 0.0) {
            throw std::invalid_argument(
                "Product endpoint measurement statistics are incomplete");
        }
        if (endpoint_sample_count >
            std::numeric_limits<std::uint64_t>::max() -
                endpoint.sample_count) {
            throw std::length_error(
                "Product endpoint sample count overflow");
        }
        endpoint_sample_count += endpoint.sample_count;
        aggregation_weight_sum += endpoint.aggregation_weight;
        for (std::size_t value = 0;
             value < reconstructed_estimate.size(); ++value) {
            reconstructed_estimate[value] += endpoint.aggregation_weight *
                endpoint.first_moment_sums[value] /
                static_cast<double>(endpoint.sample_count);
        }
        result.endpoints.push_back(make_endpoint_bundle(
            statistics, endpoint, identities, index, producer_identity));
    }
    if (endpoint_sample_count != accepted_samples ||
        std::abs(aggregation_weight_sum - 1.0) > 1e-12) {
        throw std::invalid_argument(
            "Product endpoint measurement coverage is inconsistent");
    }
    for (std::size_t value = 0;
         value < reconstructed_estimate.size(); ++value) {
        const double expected = reconstructed_estimate[value];
        const double actual = statistics.estimate[value];
        const double tolerance = 1e-5 * std::max(1.0, std::abs(expected));
        if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
            throw std::invalid_argument(
                "Product aggregate estimate is inconsistent");
        }
    }
    if (!rec::validate_measurement_bundle(result.estimate).ok() ||
        std::ranges::any_of(
            result.endpoints,
            [](const rec::MeasurementBundle& bundle) {
                return !rec::validate_measurement_bundle(bundle).ok();
            })) {
        throw std::logic_error("Product measurement set is invalid");
    }
    return result;
}

ProductMeasurementSet merge_product_measurement_sets(
    std::span<const ProductMeasurementSet> sets) {
    if (sets.empty())
        throw std::invalid_argument("No product measurement sets to merge");
    const auto endpoint_count = sets.front().endpoints.size();
    if (endpoint_count == 0 ||
        std::ranges::any_of(sets, [&](const auto& set) {
            return set.endpoints.size() != endpoint_count ||
                   set.auxiliary_outputs_wavefront_only !=
                       sets.front().auxiliary_outputs_wavefront_only;
        }))
        throw std::invalid_argument(
            "Product measurement sets have incompatible topology");
    std::vector<rec::MeasurementBundle> normalized_estimates;
    normalized_estimates.reserve(sets.size());
    for (const auto& set : sets) {
        if (set.estimate.schema.planes.empty() ||
            set.estimate.schema.planes.front().kind !=
                rec::MeasurementPlaneKind::Observable)
            throw std::invalid_argument(
                "Product measurement estimate is missing Beauty");
        normalized_estimates.push_back(set.estimate);
        std::ranges::fill(normalized_estimates.back().planes.front().payload,
                          std::uint8_t{});
    }
    ProductMeasurementSet result;
    result.auxiliary_outputs_wavefront_only =
        sets.front().auxiliary_outputs_wavefront_only;
    result.estimate = rec::merge_measurement_bundles(normalized_estimates);
    result.endpoints.reserve(endpoint_count);
    for (std::size_t endpoint = 0; endpoint < endpoint_count; ++endpoint) {
        std::vector<rec::MeasurementBundle> shards;
        shards.reserve(sets.size());
        for (const auto& set : sets)
            shards.push_back(set.endpoints[endpoint]);
        result.endpoints.push_back(rec::merge_measurement_bundles(shards));
    }
    const auto scalar_count =
        static_cast<std::size_t>(result.estimate.schema.width) *
        result.estimate.schema.height * 3;
    std::vector<double> estimate(scalar_count, 0.0);
    double weight_sum = 0.0;
    for (const auto& endpoint : result.endpoints) {
        if (endpoint.schema.planes.size() != 12 ||
            endpoint.schema.planes[0].kind !=
                rec::MeasurementPlaneKind::SampleCount ||
            endpoint.schema.planes[1].kind !=
                rec::MeasurementPlaneKind::FirstMoment ||
            endpoint.schema.planes[10].kind !=
                rec::MeasurementPlaneKind::EstimatorWeight)
            throw std::invalid_argument(
                "Product endpoint schema is incompatible");
        const auto counts = decode<std::uint64_t>(endpoint.planes[0]);
        const auto sums = decode<double>(endpoint.planes[1]);
        const auto weights = decode<double>(endpoint.planes[10]);
        if (counts.size() * 3 != scalar_count ||
            sums.size() != scalar_count || weights.size() != 1 ||
            !std::isfinite(weights.front()) || weights.front() < 0.0)
            throw std::invalid_argument(
                "Product endpoint payload is incompatible");
        weight_sum += weights.front();
        for (std::size_t value = 0; value < scalar_count; ++value) {
            const auto count = counts[value / 3];
            if (count == 0)
                throw std::invalid_argument(
                    "Product endpoint has zero sample support");
            estimate[value] += weights.front() * sums[value] /
                               static_cast<double>(count);
        }
    }
    if (std::abs(weight_sum - 1.0) > 1e-12)
        throw std::invalid_argument(
            "Product endpoint weights do not form one estimator");
    std::vector<float> encoded_estimate(scalar_count);
    for (std::size_t value = 0; value < scalar_count; ++value) {
        if (!std::isfinite(estimate[value]))
            throw std::invalid_argument(
                "Merged product estimate is non-finite");
        encoded_estimate[value] = static_cast<float>(estimate[value]);
    }
    assign_payload(result.estimate, 0, encode<float>(encoded_estimate));
    return result;
}

}
