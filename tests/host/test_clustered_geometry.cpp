#include <ure/runtime/clustered_geometry.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>

#include "../shared/clustered_geometry_fixture.hpp"

namespace rt = ure::runtime;

static int failures = 0;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::fprintf( \
                stderr, \
                "FAIL %s:%d: %s\n", \
                __FILE__, \
                __LINE__, \
                #condition); \
            ++failures; \
        } \
    } while (false)

template <typename Fn>
static bool throws_code(Fn&& fn, rt::ErrorCode code) {
    try {
        fn();
    } catch (const rt::Error& error) {
        return error.code() == code;
    }
    return false;
}

static rt::ClusteredGeometryResource build_fixture() {
    return ure::test::build_clustered_geometry_fixture();
}

static void test_boundary_split_and_bounds() {
    const auto resource = build_fixture();
    const auto summary =
        rt::validate_clustered_geometry(resource);
    CHECK(summary.cluster_count == 3);
    CHECK(summary.page_count == 2);
    CHECK(summary.boundary_count == 2);
    CHECK(summary.primitive_count == 4);
    CHECK(resource.clusters[0].primitive_count == 2);
    CHECK(resource.clusters[1].primitive_count == 1);
    CHECK(resource.clusters[2].primitive_count == 1);
    CHECK(
        resource.clusters[0].boundary_index !=
        resource.clusters[1].boundary_index);
    CHECK(
        resource.clusters[0].boundary_index ==
        resource.clusters[2].boundary_index);
    CHECK(
        resource.boundaries[
            resource.clusters[0].boundary_index].
            material.local_id == 11);
    CHECK(
        resource.boundaries[
            resource.clusters[1].boundary_index].
            material.local_id == 17);
    CHECK(
        resource.boundaries[
            resource.clusters[1].boundary_index].
            spectral.local_id == 17);
    CHECK(resource.clusters[0].bounds.minimum[0] == 0.0f);
    CHECK(resource.clusters[0].bounds.maximum[0] == 1.0f);
    CHECK(resource.clusters[0].bounds.maximum[1] == 1.0f);
    CHECK(resource.clusters[0].lod_error.position == 0.0f);
}

static void test_streaming_residency_and_upload_plan() {
    const auto resource = build_fixture();
    auto residency = rt::make_cluster_residency(resource);
    const auto initial =
        rt::validate_cluster_residency(resource, residency);
    CHECK(initial.resident_page_count == 1);
    CHECK(initial.resident_cluster_count == 2);
    const std::array required = {0u, 1u};
    rt::require_clusters_resident(
        resource, residency, required);
    const std::array missing = {2u};
    CHECK(throws_code(
        [&] {
            rt::require_clusters_resident(
                resource, residency, missing);
        },
        rt::ErrorCode::InvalidArgument));

    const auto packed =
        rt::pack_clustered_geometry(resource);
    CHECK(packed.layout.cluster_count == 3);
    CHECK(packed.layout.pages.size() == 2);
    const auto partial_plan = rt::make_cluster_upload_plan(
        resource,
        packed,
        residency,
        packed.bytes.size());
    const auto partial_summary = rt::validate(partial_plan);
    CHECK(
        partial_summary.initial_upload_bytes ==
        initial.resident_bytes);
    CHECK(
        partial_summary.initial_upload_bytes <
        partial_summary.logical_bytes);

    rt::set_cluster_page_resident(residency, 1, true);
    rt::require_clusters_resident(
        resource, residency, missing);
    const auto complete =
        rt::validate_cluster_residency(resource, residency);
    CHECK(complete.resident_page_count == 2);
    CHECK(complete.resident_cluster_count == 3);
    const auto complete_plan = rt::make_cluster_upload_plan(
        resource,
        packed,
        residency,
        packed.bytes.size());
    CHECK(
        rt::validate(complete_plan).initial_upload_bytes ==
        packed.bytes.size());
}

static void test_invalid_resources_fail_loud() {
    const auto valid = build_fixture();

    auto bad_index = valid;
    bad_index.local_indices[
        bad_index.clusters[0].local_index_offset] =
        static_cast<std::uint16_t>(
            bad_index.clusters[0].vertex_count);
    CHECK(throws_code(
        [&] {
            static_cast<void>(
                rt::validate_clustered_geometry(bad_index));
        },
        rt::ErrorCode::InvalidArgument));

    auto bad_bounds = valid;
    bad_bounds.clusters[0].bounds.maximum[0] = 0.25f;
    CHECK(throws_code(
        [&] {
            static_cast<void>(
                rt::validate_clustered_geometry(bad_bounds));
        },
        rt::ErrorCode::InvalidArgument));

    auto bad_error = valid;
    bad_error.clusters[0].lod_error.position =
        std::numeric_limits<float>::quiet_NaN();
    CHECK(throws_code(
        [&] {
            static_cast<void>(
                rt::validate_clustered_geometry(bad_error));
        },
        rt::ErrorCode::InvalidArgument));

    auto bad_page = valid;
    bad_page.pages[1].first_cluster = 1;
    CHECK(throws_code(
        [&] {
            static_cast<void>(
                rt::validate_clustered_geometry(bad_page));
        },
        rt::ErrorCode::InvalidArgument));

    auto bad_parent = valid;
    bad_parent.clusters[0].parent_cluster = 1;
    CHECK(throws_code(
        [&] {
            static_cast<void>(
                rt::validate_clustered_geometry(bad_parent));
        },
        rt::ErrorCode::InvalidArgument));

    auto bad_boundary = valid;
    bad_boundary.clusters[0].boundary_index =
        static_cast<std::uint32_t>(
            bad_boundary.boundaries.size());
    CHECK(throws_code(
        [&] {
            static_cast<void>(
                rt::validate_clustered_geometry(
                    bad_boundary));
        },
        rt::ErrorCode::InvalidArgument));
}

static void test_invalid_source_and_budget_fail_loud() {
    ure::test::ClusteredGeometryFixture fixture;
    fixture.triangles[0].vertex_indices[2] = 99;
    CHECK(throws_code(
        [&] {
            static_cast<void>(
                rt::build_clustered_geometry(
                    fixture.input()));
        },
        rt::ErrorCode::InvalidArgument));

    const auto resource = build_fixture();
    const auto packed =
        rt::pack_clustered_geometry(resource);
    const auto residency =
        rt::make_cluster_residency(resource);
    CHECK(throws_code(
        [&] {
            static_cast<void>(
                rt::make_cluster_upload_plan(
                    resource,
                    packed,
                    residency,
                    1));
        },
        rt::ErrorCode::OutOfMemory));
}

int main() {
    test_boundary_split_and_bounds();
    test_streaming_residency_and_upload_plan();
    test_invalid_resources_fail_loud();
    test_invalid_source_and_budget_fail_loud();
    std::fprintf(
        stderr,
        "[Clustered Geometry Test] failures: %d\n",
        failures);
    return failures == 0 ? 0 : 1;
}
