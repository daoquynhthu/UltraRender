#include <gtest/gtest.h>
#include "core/vector.hpp"

using namespace ure::core;

TEST(VectorTest, Constructor) {
    Vec3f v(1.0f, 2.0f, 3.0f);
    EXPECT_FLOAT_EQ(v.x, 1.0f);
    EXPECT_FLOAT_EQ(v.y, 2.0f);
    EXPECT_FLOAT_EQ(v.z, 3.0f);
}

TEST(VectorTest, Addition) {
    Vec3f v1(1.0f, 2.0f, 3.0f);
    Vec3f v2(4.0f, 5.0f, 6.0f);
    Vec3f result = v1 + v2;
    EXPECT_FLOAT_EQ(result.x, 5.0f);
    EXPECT_FLOAT_EQ(result.y, 7.0f);
    EXPECT_FLOAT_EQ(result.z, 9.0f);
}

TEST(VectorTest, Subtraction) {
    Vec3f v1(4.0f, 5.0f, 6.0f);
    Vec3f v2(1.0f, 2.0f, 3.0f);
    Vec3f result = v1 - v2;
    EXPECT_FLOAT_EQ(result.x, 3.0f);
    EXPECT_FLOAT_EQ(result.y, 3.0f);
    EXPECT_FLOAT_EQ(result.z, 3.0f);
}

TEST(VectorTest, ScalarMultiplication) {
    Vec3f v(1.0f, 2.0f, 3.0f);
    Vec3f result = v * 2.0f;
    EXPECT_FLOAT_EQ(result.x, 2.0f);
    EXPECT_FLOAT_EQ(result.y, 4.0f);
    EXPECT_FLOAT_EQ(result.z, 6.0f);
}

TEST(VectorTest, DotProduct) {
    Vec3f v1(1.0f, 2.0f, 3.0f);
    Vec3f v2(4.0f, 5.0f, 6.0f);
    float dot = v1.dot(v2);
    // 1*4 + 2*5 + 3*6 = 4 + 10 + 18 = 32
    EXPECT_FLOAT_EQ(dot, 32.0f);
}

TEST(VectorTest, CrossProduct) {
    Vec3f v1(1.0f, 0.0f, 0.0f);
    Vec3f v2(0.0f, 1.0f, 0.0f);
    Vec3f cross = v1.cross(v2);
    EXPECT_FLOAT_EQ(cross.x, 0.0f);
    EXPECT_FLOAT_EQ(cross.y, 0.0f);
    EXPECT_FLOAT_EQ(cross.z, 1.0f);
}

TEST(VectorTest, Normalization) {
    Vec3f v(3.0f, 0.0f, 0.0f);
    Vec3f n = v.normalize();
    EXPECT_FLOAT_EQ(n.x, 1.0f);
    EXPECT_FLOAT_EQ(n.y, 0.0f);
    EXPECT_FLOAT_EQ(n.z, 0.0f);
    EXPECT_FLOAT_EQ(n.length(), 1.0f);
}
