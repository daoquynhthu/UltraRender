#include <ure/product/product_measurement.hpp>

#include <bit>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace product = ure::product;
namespace reconstruction = ure::reconstruction;

static int failures = 0;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", \
                         __FILE__, __LINE__, #condition); \
            ++failures; \
        } \
    } while (false)

template <typename T>
static T read_value(const reconstruction::MeasurementPlane& plane,
                    std::size_t index = 0) {
    using Bits = std::conditional_t<
        sizeof(T) == 4, std::uint32_t, std::uint64_t>;
    Bits bits{};
    for (std::size_t byte = 0; byte < sizeof(T); ++byte) {
        bits |= static_cast<Bits>(
            plane.payload[index * sizeof(T) + byte]) << (byte * 8);
    }
    if constexpr (std::is_floating_point_v<T>)
        return std::bit_cast<T>(bits);
    else
        return static_cast<T>(bits);
}

static product::ProductIdentitySet identities() {
    product::ProductIdentitySet value;
    value.build[0] = 1;
    value.snapshot[0] = 2;
    value.objective[0] = 3;
    value.plan[0] = 4;
    return value;
}

static ure::RenderMeasurementStatistics statistics() {
    ure::RenderMeasurementStatistics value;
    value.width = 2;
    value.height = 1;
    value.estimate = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
    value.normal = {0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    value.albedo = {0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f};
    value.depth = {1.0f, 2.0f};
    value.uv = {0.0f, 0.0f, 1.0f, 1.0f};
    value.motion = {0.0f, 0.0f, 0.25f, -0.25f};
    value.valid = true;
    ure::RenderMeasurementEndpointStatistics endpoint;
    endpoint.sample_count = 4;
    endpoint.estimator.mode = ure::IntegratorMode::Wavefront;
    endpoint.estimator.policy =
        ure::IntegratorEstimatorPolicy::Standard;
    endpoint.estimator.sample_space_version = 0;
    value.auxiliary_estimator = endpoint.estimator;
    endpoint.aggregation_weight = 1.0;
    endpoint.first_moment_sums = {4.0, 8.0, 12.0, 16.0, 20.0, 24.0};
    endpoint.second_moment_sums = {
        6.0, 22.0, 46.0, 84.0, 130.0, 186.0};
    endpoint.lag_one_product_sums = {
        2.0, 11.0, 26.0, 50.0, 80.0, 116.0};
    endpoint.first_contributions = {
        0.0, 1.0, 2.0, 3.0, 4.0, 5.0};
    endpoint.last_contributions = {
        2.0, 3.0, 4.0, 5.0, 6.0, 7.0};
    endpoint.maximum_absolute_contributions = {
        2.0f, 4.0f, 6.0f, 8.0f, 10.0f, 12.0f};
    endpoint.tail_event_counts = {0, 0, 0, 0, 0, 0};
    value.endpoints.push_back(std::move(endpoint));
    return value;
}

static void test_product_measurement_is_typed_and_valid() {
    const auto product_identities = identities();
    const auto result = product::make_product_measurement_set(
        statistics(), product_identities, 4, product_identities.build);
    CHECK(reconstruction::validate_measurement_bundle(
        result.estimate).ok());
    CHECK(result.endpoints.size() == 1);
    if (result.endpoints.empty()) return;
    const auto& endpoint = result.endpoints.front();
    CHECK(reconstruction::validate_measurement_bundle(endpoint).ok());
    CHECK(result.estimate.schema.planes[0].observable.value_domain ==
          ure::transport::ValueDomain::LinearRgb);
    CHECK(read_value<float>(result.estimate.planes[0]) == 1.0f);
    CHECK(result.estimate.schema.planes.size() == 8);
    CHECK(read_value<float>(result.estimate.planes[2], 1) == 1.0f);
    CHECK(read_value<float>(result.estimate.planes[4], 1) == 2.0f);
    CHECK(endpoint.schema.planes.size() == 12);
    CHECK(read_value<std::uint64_t>(endpoint.planes[0]) == 4);
    CHECK(read_value<double>(endpoint.planes[1]) == 4.0);
    CHECK(std::abs(read_value<double>(endpoint.planes[6]) -
                   (2.0 / 3.0)) < 1e-12);
    const auto effective = read_value<double>(endpoint.planes[7]);
    CHECK(effective >= 1.0 && effective <= 4.0);
    CHECK(read_value<float>(endpoint.planes[8]) == 2.0f);
    CHECK(read_value<std::uint64_t>(endpoint.planes[9]) == 0);
    CHECK(read_value<double>(endpoint.planes[10]) == 1.0);
    CHECK(read_value<std::uint32_t>(endpoint.planes[11]) ==
          static_cast<std::uint32_t>(ure::IntegratorMode::Wavefront));
    CHECK(endpoint.provenance.identities.observation_snapshot ==
          identities().snapshot);
    CHECK(endpoint.provenance.identities.technique_graph ==
          identities().plan);
}

static void test_product_measurement_rejects_incomplete_statistics() {
    auto value = statistics();
    value.endpoints.front().second_moment_sums.pop_back();
    bool rejected = false;
    try {
        static_cast<void>(product::make_product_measurement_set(
            value, identities(), 4, identities().build));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK(rejected);
}

static void test_product_measurement_canonical_merge() {
    auto first = statistics();
    auto second = statistics();
    const auto split = [](auto& values, double scale) {
        for (auto& value : values)
            value = static_cast<std::remove_reference_t<decltype(value)>>(
                static_cast<double>(value) * scale);
    };
    first.endpoints[0].sample_count = 2;
    second.endpoints[0].sample_count = 2;
    second.endpoints[0].sample_range_start = 2;
    split(first.endpoints[0].first_moment_sums, 0.25);
    split(second.endpoints[0].first_moment_sums, 0.75);
    split(first.endpoints[0].second_moment_sums, 0.25);
    split(second.endpoints[0].second_moment_sums, 0.75);
    split(first.endpoints[0].lag_one_product_sums, 0.25);
    split(second.endpoints[0].lag_one_product_sums, 0.75);
    for (std::size_t value = 0; value < first.estimate.size(); ++value) {
        first.estimate[value] = static_cast<float>(
            first.endpoints[0].first_moment_sums[value] / 2.0);
        second.estimate[value] = static_cast<float>(
            second.endpoints[0].first_moment_sums[value] / 2.0);
    }
    const auto product_identities = identities();
    std::array<product::ProductMeasurementSet, 2> shards{
        product::make_product_measurement_set(
            first, product_identities, 2, product_identities.build),
        product::make_product_measurement_set(
            second, product_identities, 2, product_identities.build)};
    const auto merged = product::merge_product_measurement_sets(shards);
    CHECK(merged.endpoints.size() == 1);
    CHECK(read_value<std::uint64_t>(merged.endpoints[0].planes[0]) == 4);
    CHECK(read_value<double>(merged.endpoints[0].planes[1]) == 4.0);
    CHECK(read_value<float>(merged.estimate.planes[0]) == 1.0f);
    CHECK(merged.estimate.provenance.sample_ranges.size() == 2);
    CHECK(merged.estimate.provenance.sample_ranges[0].start == 0 &&
          merged.estimate.provenance.sample_ranges[0].count == 2 &&
          merged.estimate.provenance.sample_ranges[1].start == 2 &&
          merged.estimate.provenance.sample_ranges[1].count == 2);
}

int main() {
    test_product_measurement_is_typed_and_valid();
    test_product_measurement_rejects_incomplete_statistics();
    test_product_measurement_canonical_merge();
    if (failures != 0) {
        std::fprintf(stderr, "%d product measurement checks failed\n",
                     failures);
        return 1;
    }
    std::printf("Product measurement checks passed\n");
    return 0;
}
