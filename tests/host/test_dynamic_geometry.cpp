#include <ure/runtime/dynamic_geometry.hpp>

#include <cstdio>

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

static rt::GeometrySnapshot snapshot(
    std::uint64_t attribute_hash = 31,
    std::uint64_t topology_hash = 41,
    std::uint64_t boundary_hash = 51,
    std::uint64_t vertex_count = 4,
    std::uint64_t index_count = 6) {
    return {
        {0x47454f4d45545259ull, 7},
        vertex_count,
        index_count,
        topology_hash,
        boundary_hash,
        attribute_hash,
        0.0f,
        0.25f};
}

static void test_classification() {
    const auto before = snapshot();
    auto rigid_after = before;
    CHECK(rt::classify_geometry_mutation(
        {before, rigid_after, true}) ==
        rt::GeometryMutationClass::Rigid);

    auto deforming_after = before;
    deforming_after.attribute_hash = 32;
    deforming_after.maximum_displacement = 0.1f;
    CHECK(rt::classify_geometry_mutation(
        {before, deforming_after, false}) ==
        rt::GeometryMutationClass::Deforming);

    auto topology_after = before;
    topology_after.index_count = 9;
    topology_after.topology_hash = 42;
    CHECK(rt::classify_geometry_mutation(
        {before, topology_after, false}) ==
        rt::GeometryMutationClass::TopologyChange);
    CHECK(throws_code(
        [&] {
            static_cast<void>(
                rt::classify_geometry_mutation(
                    {before, before, false}));
        },
        rt::ErrorCode::InvalidArgument));
}

static void test_refit_rebuild_and_recluster_plans() {
    const auto before = snapshot();
    auto deforming_after = before;
    deforming_after.attribute_hash = 32;
    deforming_after.maximum_displacement = 0.1f;
    const rt::GeometryMutation deforming{
        before, deforming_after, false};
    const auto refit = rt::plan_dynamic_geometry_updates(
        std::span<const rt::GeometryMutation>{
            &deforming, 1},
        ure::AccelerationUpdatePolicy::Refit,
        true,
        {true, true, true});
    CHECK(refit.deforming_count == 1);
    CHECK(refit.blas_refit_count == 1);
    CHECK(refit.tlas_refit_count == 1);
    CHECK(refit.cluster_bounds_refit_count == 1);
    CHECK(refit.recluster_count == 0);

    deforming_after.maximum_displacement = 0.5f;
    const rt::GeometryMutation large_deformation{
        before, deforming_after, false};
    const auto recluster =
        rt::plan_dynamic_geometry_updates(
            std::span<const rt::GeometryMutation>{
                &large_deformation, 1},
            ure::AccelerationUpdatePolicy::Automatic,
            true,
            {true, true, true});
    CHECK(recluster.blas_refit_count == 1);
    CHECK(recluster.recluster_count == 1);

    auto topology_after = before;
    topology_after.vertex_count = 5;
    topology_after.topology_hash = 99;
    const rt::GeometryMutation topology{
        before, topology_after, false};
    const auto rebuild =
        rt::plan_dynamic_geometry_updates(
            std::span<const rt::GeometryMutation>{
                &topology, 1},
            ure::AccelerationUpdatePolicy::Automatic,
            true,
            {false, false, true});
    CHECK(rebuild.topology_change_count == 1);
    CHECK(rebuild.blas_rebuild_count == 1);
    CHECK(rebuild.tlas_rebuild_count == 1);
    CHECK(rebuild.recluster_count == 1);
}

static void test_unsupported_paths_fail_loud() {
    const auto before = snapshot();
    auto deforming_after = before;
    deforming_after.attribute_hash = 32;
    deforming_after.maximum_displacement = 0.1f;
    const rt::GeometryMutation deforming{
        before, deforming_after, false};
    CHECK(throws_code(
        [&] {
            static_cast<void>(
                rt::plan_dynamic_geometry_updates(
                    std::span<const rt::GeometryMutation>{
                        &deforming, 1},
                    ure::AccelerationUpdatePolicy::Refit,
                    false,
                    {}));
        },
        rt::ErrorCode::Unsupported));
    CHECK(throws_code(
        [&] {
            static_cast<void>(
                rt::plan_dynamic_geometry_updates(
                    std::span<const rt::GeometryMutation>{
                        &deforming, 1},
                    ure::AccelerationUpdatePolicy::Static,
                    false,
                    {}));
        },
        rt::ErrorCode::Unsupported));
    CHECK(throws_code(
        [&] {
            static_cast<void>(
                rt::plan_dynamic_geometry_updates(
                    std::span<const rt::GeometryMutation>{
                        &deforming, 1},
                    ure::AccelerationUpdatePolicy::Automatic,
                    true,
                    {}));
        },
        rt::ErrorCode::Unsupported));
}

static void test_stats_accumulation() {
    const auto before = snapshot();
    auto after = before;
    after.attribute_hash = 32;
    after.maximum_displacement = 0.1f;
    const rt::GeometryMutation mutation{
        before, after, false};
    const auto plan = rt::plan_dynamic_geometry_updates(
        std::span<const rt::GeometryMutation>{
            &mutation, 1},
        ure::AccelerationUpdatePolicy::Automatic,
        false,
        {});
    rt::DynamicGeometryStats stats;
    rt::accumulate_dynamic_geometry_stats(
        stats, plan, 125);
    rt::accumulate_dynamic_geometry_stats(
        stats, plan, 75);
    CHECK(stats.deforming_update_count == 2);
    CHECK(stats.blas_rebuild_count == 2);
    CHECK(stats.tlas_rebuild_count == 2);
    CHECK(stats.last_update_nanoseconds == 75);
    CHECK(stats.total_update_nanoseconds == 200);
}

int main() {
    test_classification();
    test_refit_rebuild_and_recluster_plans();
    test_unsupported_paths_fail_loud();
    test_stats_accumulation();
    std::fprintf(
        stderr,
        "[Dynamic Geometry Test] failures: %d\n",
        failures);
    return failures == 0 ? 0 : 1;
}
