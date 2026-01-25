#include <gtest/gtest.h>
#include "core/ray.hpp"

using namespace ure::core;

TEST(RayTest, Constructor) {
    Point3f origin(1.0f, 2.0f, 3.0f);
    Vec3f direction(0.0f, 0.0f, 1.0f);
    Rayf r(origin, direction);
    
    EXPECT_EQ(r.origin.x, 1.0f);
    EXPECT_EQ(r.origin.y, 2.0f);
    EXPECT_EQ(r.origin.z, 3.0f);
    EXPECT_EQ(r.direction.z, 1.0f);
    EXPECT_EQ(r.t_max, std::numeric_limits<float>::infinity());
}

TEST(RayTest, At) {
    Point3f origin(0.0f, 0.0f, 0.0f);
    Vec3f direction(1.0f, 0.0f, 0.0f);
    Rayf r(origin, direction);
    
    Point3f p = r.at(2.0f);
    EXPECT_EQ(p.x, 2.0f);
    EXPECT_EQ(p.y, 0.0f);
    EXPECT_EQ(p.z, 0.0f);
}
