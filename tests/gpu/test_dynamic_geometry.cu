#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>

#include "ure/log.hpp"
#include "ure/session.hpp"
#include "test_framework.cuh"

static std::shared_ptr<ure::Mesh> make_dynamic_mesh() {
    auto mesh = std::make_shared<ure::Mesh>();
    mesh->vertices = {
        {{-1.0f, -1.0f, 0.0f},
         {0.0f, 0.0f, 1.0f},
         {0.0f, 0.0f}},
        {{1.0f, -1.0f, 0.0f},
         {0.0f, 0.0f, 1.0f},
         {1.0f, 0.0f}},
        {{0.0f, 1.0f, 0.0f},
         {0.0f, 0.0f, 1.0f},
         {0.5f, 1.0f}}};
    mesh->indices = {0, 1, 2};
    return mesh;
}

static ure::scene_ir::SceneIR make_dynamic_scene() {
    ure::scene_ir::SceneIR scene;
    auto material =
        std::make_shared<ure::scene_ir::MaterialNode>();
    material->name = "dynamic";
    material->base_color = {0.8f, 0.8f, 0.8f};
    auto resource =
        std::make_shared<ure::scene_ir::MeshResource>();
    resource->name = "dynamic";
    resource->mesh = make_dynamic_mesh();
    scene.materials.push_back(material);
    scene.meshes.push_back(resource);
    ure::scene_ir::InstanceNode instance;
    instance.name = "dynamic";
    instance.mesh = resource;
    instance.material = material;
    scene.instances.push_back(instance);
    scene.width = 8;
    scene.height = 8;
    scene.camera.position = {0.0f, 0.0f, 3.0f};
    scene.camera.look_at = {0.0f, 0.0f, 0.0f};
    scene.camera.fov = 35.0f;
    return scene;
}

template <typename Fn>
static double elapsed_ms(Fn&& fn) {
    const auto start = std::chrono::steady_clock::now();
    fn();
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
}

static bool throws_update(auto&& fn) {
    try {
        fn();
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

static float center_depth(ure::RenderSession& session) {
    if (session.render_pass() < 1) {
        return NAN;
    }
    const auto& depth = session.get_aov(
        ure::AovType::Depth);
    if (depth.size() != 64) {
        return NAN;
    }
    return depth[4 * 8 + 4];
}

static int test_dynamic_geometry_lifecycle() {
    REQUIRE_GPU();
    ure::log::set_min_level(ure::log::Level::Warn);
    ure::RenderConfig config;
    config.num_wavelengths = 8;
    config.max_trace_depth = 2;
    config.acceleration.collect_stats = true;
    auto scene = make_dynamic_scene();
    auto session = ure::RenderSession::create(config);
    session.load_scene(scene);
    const float initial_depth = center_depth(session);
    CHECK(std::isfinite(initial_depth));
    CHECK(initial_depth > 0.0f);

    const double rigid_ms = elapsed_ms([&] {
        session.mutate_scene(
            ure::SceneDiff::update_instance_transform(
                0,
                {0.0f, 0.0f, 0.25f}));
    });
    const float rigid_depth = center_depth(session);
    CHECK(rigid_depth < initial_depth);
    auto stats = session.get_dynamic_geometry_stats();
    CHECK(stats.rigid_update_count == 1);
    CHECK(stats.tlas_refit_count == 1);

    auto deformed = make_dynamic_mesh();
    for (auto& vertex : deformed->vertices) {
        vertex.position.z += 0.25f;
    }
    const double deforming_ms = elapsed_ms([&] {
        session.mutate_scene(
            ure::SceneDiff::update_scene_ir_mesh(
                0, deformed));
    });
    const float deformed_depth = center_depth(session);
    CHECK(deformed_depth < rigid_depth);
    stats = session.get_dynamic_geometry_stats();
    CHECK(stats.deforming_update_count == 1);
    CHECK(stats.blas_rebuild_count == 1);
    CHECK(stats.tlas_rebuild_count == 1);

    auto topology = std::make_shared<ure::Mesh>(*deformed);
    topology->vertices.push_back({
        {1.5f, -1.0f, 0.25f},
        {0.0f, 0.0f, 1.0f},
        {1.0f, 0.0f}});
    topology->indices = {0, 1, 2, 1, 3, 2};
    const double topology_ms = elapsed_ms([&] {
        session.mutate_scene(
            ure::SceneDiff::update_scene_ir_mesh(
                0, topology));
    });
    static_cast<void>(center_depth(session));
    stats = session.get_dynamic_geometry_stats();
    CHECK(stats.topology_change_count == 1);
    CHECK(stats.blas_rebuild_count == 2);
    CHECK(stats.tlas_rebuild_count == 2);
    const auto acceleration =
        session.get_acceleration_stats();
    CHECK(acceleration.invalid_acceleration_count == 0);
    CHECK(acceleration.stack_overflow_count == 0);

    ure::RenderConfig refit_config = config;
    refit_config.acceleration.update_policy =
        ure::AccelerationUpdatePolicy::Refit;
    auto refit_session =
        ure::RenderSession::create(refit_config);
    refit_session.load_scene(make_dynamic_scene());
    CHECK(throws_update([&] {
        refit_session.mutate_scene(
            ure::SceneDiff::update_scene_ir_mesh(
                0, deformed));
    }));

    ure::RenderConfig rebuild_config = config;
    rebuild_config.acceleration.update_policy =
        ure::AccelerationUpdatePolicy::Rebuild;
    auto rebuild_session =
        ure::RenderSession::create(rebuild_config);
    rebuild_session.load_scene(make_dynamic_scene());
    rebuild_session.mutate_scene(
        ure::SceneDiff::update_instance_transform(
            0,
            {0.0f, 0.0f, 0.1f}));
    const auto rebuild_stats =
        rebuild_session.get_dynamic_geometry_stats();
    CHECK(rebuild_stats.rigid_update_count == 1);
    CHECK(rebuild_stats.tlas_rebuild_count == 1);
    CHECK(rebuild_stats.tlas_refit_count == 0);

    std::printf(
        "schema=ure.phase_v.dynamic_geometry.v1 "
        "rigid_ms=%.6f deforming_ms=%.6f "
        "topology_ms=%.6f rigid_refit=%llu "
        "blas_rebuild=%llu topology_rebuild=%llu "
        "unsupported_refit=1\n",
        rigid_ms,
        deforming_ms,
        topology_ms,
        static_cast<unsigned long long>(
            stats.rigid_update_count),
        static_cast<unsigned long long>(
            stats.blas_rebuild_count),
        static_cast<unsigned long long>(
            stats.topology_change_count));
    return 0;
}

int main() {
    RUN_TEST(test_dynamic_geometry_lifecycle);
    std::printf(
        "[GPU Dynamic Geometry] %d passed, %d failed\n",
        g_tests_passed,
        g_tests_failed);
    return g_test_result;
}
