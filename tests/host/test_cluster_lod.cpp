#include <ure/runtime/cluster_lod.hpp>

#include <cmath>
#include <cstdio>
#include <limits>

#include "../shared/cluster_lod_fixture.hpp"

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

static void test_path_class_selection() {
    const ure::test::ClusterLodFixture fixture;
    const auto resource = fixture.build();
    const auto residency =
        ure::test::make_complete_cluster_lod_residency(
            resource);

    const auto camera = rt::select_cluster_lod(
        resource,
        residency,
        0,
        ure::test::cluster_lod_query(
            rt::ClusterPathClass::Camera));
    const auto diffuse = rt::select_cluster_lod(
        resource,
        residency,
        0,
        ure::test::cluster_lod_query(
            rt::ClusterPathClass::Diffuse));
    const auto rough_glossy = rt::select_cluster_lod(
        resource,
        residency,
        0,
        ure::test::cluster_lod_query(
            rt::ClusterPathClass::Glossy,
            1.0f));
    const auto sharp_glossy = rt::select_cluster_lod(
        resource,
        residency,
        0,
        ure::test::cluster_lod_query(
            rt::ClusterPathClass::Glossy,
            0.0f));
    const auto specular = rt::select_cluster_lod(
        resource,
        residency,
        0,
        ure::test::cluster_lod_query(
            rt::ClusterPathClass::Specular));
    const auto shadow = rt::select_cluster_lod(
        resource,
        residency,
        0,
        ure::test::cluster_lod_query(
            rt::ClusterPathClass::Shadow));
    const auto caustic = rt::select_cluster_lod(
        resource,
        residency,
        0,
        ure::test::cluster_lod_query(
            rt::ClusterPathClass::Caustic));

    CHECK(camera.cluster_index == 1);
    CHECK(diffuse.cluster_index == 1);
    CHECK(rough_glossy.cluster_index == 1);
    CHECK(sharp_glossy.cluster_index == 0);
    CHECK(specular.cluster_index == 0);
    CHECK(shadow.cluster_index == 0);
    CHECK(caustic.cluster_index == 0);
    CHECK(diffuse.evaluation.footprint > 4.0f);
    CHECK(
        diffuse.evaluation.combined_position_error ==
        1.1f);
}

static void test_residency_and_invalid_query() {
    const ure::test::ClusterLodFixture fixture;
    const auto resource = fixture.build();
    auto residency =
        ure::test::make_complete_cluster_lod_residency(
            resource);
    rt::set_cluster_page_resident(residency, 0, false);
    CHECK(throws_code(
        [&] {
            static_cast<void>(rt::select_cluster_lod(
                resource,
                residency,
                0,
                ure::test::cluster_lod_query(
                    rt::ClusterPathClass::Shadow)));
        },
        rt::ErrorCode::Unsupported));
    const auto diffuse = rt::select_cluster_lod(
        resource,
        residency,
        0,
        ure::test::cluster_lod_query(
            rt::ClusterPathClass::Diffuse));
    CHECK(diffuse.cluster_index == 1);

    auto invalid = ure::test::cluster_lod_query(
        rt::ClusterPathClass::Camera);
    invalid.differential.direction_spread =
        std::numeric_limits<float>::quiet_NaN();
    CHECK(throws_code(
        [&] {
            rt::validate_cluster_lod_query(invalid);
        },
        rt::ErrorCode::InvalidArgument));
    rt::ClusterLodPolicy invalid_policy;
    invalid_policy.camera_normal_radians =
        std::numeric_limits<float>::infinity();
    CHECK(throws_code(
        [&] {
            rt::validate_cluster_lod_query(
                ure::test::cluster_lod_query(
                    rt::ClusterPathClass::Camera),
                invalid_policy);
        },
        rt::ErrorCode::InvalidArgument));
}

static void test_hierarchy_boundaries_and_error_bounds() {
    const ure::test::ClusterLodFixture fixture;
    const auto valid = fixture.build();
    const auto packed =
        rt::pack_clustered_geometry(valid);
    const auto* header =
        reinterpret_cast<const rt::ClusterGpuHeader*>(
            packed.bytes.data());
    const auto* records =
        reinterpret_cast<const rt::ClusterGpuRecord*>(
            packed.bytes.data() +
            header->cluster_records_offset);
    CHECK(records[1].bounds_minimum.x <= -1.1f);
    CHECK(records[1].bounds_maximum.x >= 2.6f);

    auto wrong_boundary = valid;
    auto second =
        wrong_boundary.boundaries.front();
    second.material.local_id = 99;
    wrong_boundary.boundaries.push_back(second);
    wrong_boundary.clusters[1].boundary_index = 1;
    CHECK(throws_code(
        [&] {
            static_cast<void>(
                rt::validate_clustered_geometry(
                    wrong_boundary));
        },
        rt::ErrorCode::InvalidArgument));

    auto insufficient_error = valid;
    insufficient_error.clusters[1].
        lod_error.position = 0.01f;
    CHECK(throws_code(
        [&] {
            static_cast<void>(
                rt::validate_clustered_geometry(
                    insufficient_error));
        },
        rt::ErrorCode::InvalidArgument));

    auto inexact_finest = valid;
    inexact_finest.clusters[0].
        lod_error.position = 0.01f;
    CHECK(throws_code(
        [&] {
            static_cast<void>(
                rt::validate_clustered_geometry(
                    inexact_finest));
        },
        rt::ErrorCode::InvalidArgument));

    auto missing_provenance = valid;
    missing_provenance.boundaries[0].displacement = {};
    CHECK(throws_code(
        [&] {
            static_cast<void>(
                rt::validate_clustered_geometry(
                    missing_provenance));
        },
        rt::ErrorCode::InvalidArgument));
}

int main() {
    test_path_class_selection();
    test_residency_and_invalid_query();
    test_hierarchy_boundaries_and_error_bounds();
    std::fprintf(
        stderr,
        "[Cluster LoD Test] failures: %d\n",
        failures);
    return failures == 0 ? 0 : 1;
}
