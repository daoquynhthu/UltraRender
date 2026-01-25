#include <gtest/gtest.h>
#include "../../include/scene/scene_factory.hpp"
#include "../../include/scene/scene.hpp"
#include "../../include/scene/camera.hpp"
#include "../../include/scene/light.hpp"

namespace ure {
namespace tests {

class SceneFactoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup code if needed
    }
};

// Test creation of the standard "test" scene
TEST_F(SceneFactoryTest, CreateTestScene) {
    auto data = scene::SceneFactory::create_scene("test");

    // Verify SceneData properties
    EXPECT_EQ(data.name, "test");
    EXPECT_GT(data.width, 0);
    EXPECT_GT(data.height, 0);
    EXPECT_GT(data.spp, 0);

    // Verify Camera
    ASSERT_NE(data.camera, nullptr);
    // Ideally we would check camera properties, but Camera interface is limited.
    // We can verify it generates valid rays.
    auto ray = data.camera->generate_ray({0.5f, 0.5f});
    EXPECT_FLOAT_EQ(ray.direction.length(), 1.0f);

    // Verify Scene
    ASSERT_NE(data.scene, nullptr);
    
    // Check Lights
    const auto& lights = data.scene->lights();
    EXPECT_FALSE(lights.empty()) << "Test scene should have lights";
    
    // Verify Intersection (Ground or Spheres)
    // Ray pointing down should hit the ground sphere
    // Ground is at y=-1000, radius 1000. Center (0, -1000, 0).
    // Ray from (0, 10, 0) down (0, -1, 0) should hit it.
    core::Rayf down_ray(core::Point3f(0, 10, 0), core::Vec3f(0, -1, 0));
    auto isect = data.scene->intersect(down_ray);
    EXPECT_TRUE(isect.has_value()) << "Scene should have intersectable geometry (ground)";
}

// Test creation of the "quick" scene
TEST_F(SceneFactoryTest, CreateQuickScene) {
    auto data = scene::SceneFactory::create_scene("quick");

    EXPECT_EQ(data.name, "quick");
    EXPECT_EQ(data.width, 640);
    EXPECT_EQ(data.height, 480);
    
    ASSERT_NE(data.scene, nullptr);
    ASSERT_NE(data.camera, nullptr);
    
    // Quick scene has a sphere at (0,0,0) radius 1.
    // Ray from (0,0,5) towards (0,0,0) should hit it.
    core::Rayf hit_ray(core::Point3f(0, 0, 5), core::Vec3f(0, 0, -1));
    auto isect = data.scene->intersect(hit_ray);
    EXPECT_TRUE(isect.has_value()) << "Quick scene should have a sphere at origin";
}

// Test fallback for unknown scene names
TEST_F(SceneFactoryTest, UnknownSceneFallback) {
    // Should default to "test" scene
    auto data = scene::SceneFactory::create_scene("non_existent_scene_name");
    
    EXPECT_EQ(data.name, "test");
    ASSERT_NE(data.scene, nullptr);
}

} // namespace tests
} // namespace ure
