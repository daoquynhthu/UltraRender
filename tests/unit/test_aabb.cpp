#include <gtest/gtest.h>
#include "core/aabb.hpp"
#include "core/ray.hpp"

using namespace ure::core;

TEST(AABBTest, Constructor) {
    Point3f p1(0.0f, 0.0f, 0.0f);
    Point3f p2(1.0f, 1.0f, 1.0f);
    AABB box(p1, p2);
    
    EXPECT_EQ(box.min.x, 0.0f);
    EXPECT_EQ(box.max.x, 1.0f);
}

TEST(AABBTest, ExpandPoint) {
    AABB box(Point3f(0.0f, 0.0f, 0.0f), Point3f(1.0f, 1.0f, 1.0f));
    box.expand(Point3f(2.0f, 2.0f, 2.0f));
    
    EXPECT_EQ(box.max.x, 2.0f);
    EXPECT_EQ(box.max.y, 2.0f);
    EXPECT_EQ(box.max.z, 2.0f);
}

TEST(AABBTest, Intersect) {
    AABB box(Point3f(-1.0f, -1.0f, -1.0f), Point3f(1.0f, 1.0f, 1.0f));
    Rayf r(Point3f(0.0f, 0.0f, -5.0f), Vec3f(0.0f, 0.0f, 1.0f));
    
    float t_min, t_max;
    bool hit = box.intersect(r, &t_min, &t_max);
    
    EXPECT_TRUE(hit);
    EXPECT_FLOAT_EQ(t_min, 4.0f);
    EXPECT_FLOAT_EQ(t_max, 6.0f);
}

TEST(AABBTest, NoIntersect) {
    AABB box(Point3f(-1.0f, -1.0f, -1.0f), Point3f(1.0f, 1.0f, 1.0f));
    Rayf r(Point3f(0.0f, 0.0f, -5.0f), Vec3f(1.0f, 0.0f, 0.0f)); // Parallel to face
    
    bool hit = box.intersect(r);
    EXPECT_FALSE(hit);
}
