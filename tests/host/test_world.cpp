#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <ure/world.hpp>

// Minimal test framework for host-side tests
static int g_passed = 0, g_failed = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        g_failed++; \
        return 1; \
    } \
    g_passed++; \
} while(0)

#define CHECK_FLOAT_EQ(a, b, eps) do { \
    float _a = (a), _b = (b), _e = (eps); \
    if (fabsf(_a - _b) > _e) { \
        fprintf(stderr, "  FAIL: %s:%d: %s ≈ %s (got %f, expected %f, eps %f)\n", \
                __FILE__, __LINE__, #a, #b, _a, _b, _e); \
        g_failed++; \
        return 1; \
    } \
    g_passed++; \
} while(0)

#define RUN_TEST(name) do { \
    printf("  test: %s ... ", #name); \
    fflush(stdout); \
    int _r = name(); \
    if (_r == 0) printf("PASS\n"); \
    else printf("FAIL\n"); \
    g_test_result |= _r; \
} while(0)

static int g_test_result = 0;

// ── World tests ─────────────────────────────────────────────────────

static int test_world_create_entity() {
    ure::World world;

    CHECK(world.entity_count() == 0);
    CHECK(world.invariant());

    ure::EntityId e1 = world.create_entity();
    CHECK(e1 == 1);
    CHECK(world.entity_count() == 1);
    CHECK(world.has_entity(e1));
    CHECK(world.index_of(e1) == 0);
    CHECK(world.invariant());

    ure::EntityId e2 = world.create_entity();
    CHECK(e2 == 2);
    CHECK(world.entity_count() == 2);
    CHECK(world.index_of(e2) == 1);

    return 0;
}

static int test_world_remove_entity() {
    ure::World world;
    ure::EntityId e1 = world.create_entity();
    ure::EntityId e2 = world.create_entity();
    ure::EntityId e3 = world.create_entity();
    CHECK(world.entity_count() == 3);

    // Remove middle entity
    world.remove_entity(e2);
    CHECK(world.entity_count() == 2);
    CHECK(!world.has_entity(e2));
    CHECK(world.has_entity(e1));
    CHECK(world.has_entity(e3));
    CHECK(world.invariant());

    // Remove first
    world.remove_entity(e1);
    CHECK(world.entity_count() == 1);
    CHECK(world.has_entity(e3));
    CHECK(world.invariant());

    // Remove last
    world.remove_entity(e3);
    CHECK(world.entity_count() == 0);
    CHECK(world.invariant());

    return 0;
}

static int test_world_transform_component() {
    ure::World world;
    ure::EntityId e = world.create_entity();

    // Default component values
    CHECK_FLOAT_EQ(world.transforms[0].position.x, 0.0f, 1e-6f);
    CHECK_FLOAT_EQ(world.transforms[0].scale.z, 1.0f, 1e-6f);
    CHECK_FLOAT_EQ(world.transforms[0].rotation.w, 1.0f, 1e-6f); // identity quat

    // Modify and verify
    world.transforms[0].position = {10, 20, 30};
    world.transforms[0].scale = {2, 2, 2};
    CHECK_FLOAT_EQ(world.transforms[0].position.x, 10.0f, 1e-6f);

    // to_matrix()
    auto m = world.transforms[0].to_matrix();
    // Identity rotation + scale(2) means m[0][0]=2, m[1][1]=2, m[2][2]=2
    CHECK_FLOAT_EQ(m.m[0][0], 2.0f, 1e-6f);
    CHECK_FLOAT_EQ(m.m[1][1], 2.0f, 1e-6f);
    CHECK_FLOAT_EQ(m.m[2][2], 2.0f, 1e-6f);
    // Translation
    CHECK_FLOAT_EQ(m.m[0][3], 10.0f, 1e-6f);
    CHECK_FLOAT_EQ(m.m[1][3], 20.0f, 1e-6f);
    CHECK_FLOAT_EQ(m.m[2][3], 30.0f, 1e-6f);

    return 0;
}

static int test_world_component_pools() {
    ure::World world;

    // Create entities and set different component values
    ure::EntityId e1 = world.create_entity();
    ure::EntityId e2 = world.create_entity();
    ure::EntityId e3 = world.create_entity();

    world.transforms[0].position = {1, 2, 3};
    world.transforms[1].position = {4, 5, 6};
    world.transforms[2].position = {7, 8, 9};

    world.geometries[0].material_index = 10;
    world.geometries[2].material_index = 30;

    world.physics[1].config_id = 100;

    // Remove e2 (index 1) and verify compaction
    world.remove_entity(e2);
    CHECK(world.entity_count() == 2);

    // After removal, e3 (was at index 2) should now be at index 1
    // The pool should have entries for e1 and e3 only
    CHECK(world.has_entity(e1));
    CHECK(world.has_entity(e3));
    size_t idx_e3 = world.index_of(e3);
    CHECK(idx_e3 == 1);

    // e3's position should still be correct
    CHECK_FLOAT_EQ(world.transforms[idx_e3].position.x, 7.0f, 1e-6f);
    CHECK_FLOAT_EQ(world.transforms[idx_e3].position.y, 8.0f, 1e-6f);
    CHECK_FLOAT_EQ(world.transforms[idx_e3].position.z, 9.0f, 1e-6f);

    // e3's geometry material should still be 30
    CHECK(world.geometries[idx_e3].material_index == 30);

    return 0;
}

int main() {
    printf("[World/ECS Test]\n");
    RUN_TEST(test_world_create_entity);
    RUN_TEST(test_world_remove_entity);
    RUN_TEST(test_world_transform_component);
    RUN_TEST(test_world_component_pools);
    printf("  passed: %d, failed: %d\n", g_passed, g_failed);
    return g_test_result;
}
