#include <gtest/gtest.h>
#include "core/matrix.hpp"
#include "core/vector.hpp"

using namespace ure::core;

TEST(MatrixTest, Identity) {
    Matrix4x4f m = Matrix4x4f::identity();
    
    Point3f p(1.0f, 2.0f, 3.0f);
    Point3f result = m.transform_point(p);
    EXPECT_FLOAT_EQ(result.x, 1.0f);
    EXPECT_FLOAT_EQ(result.y, 2.0f);
    EXPECT_FLOAT_EQ(result.z, 3.0f);
}

TEST(MatrixTest, ManualTranslation) {
    // 1 0 0 10
    // 0 1 0 20
    // 0 0 1 30
    // 0 0 0 1
    Matrix4x4f m(
        1.0f, 0.0f, 0.0f, 10.0f,
        0.0f, 1.0f, 0.0f, 20.0f,
        0.0f, 0.0f, 1.0f, 30.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    );
    
    Point3f p(1.0f, 2.0f, 3.0f);
    Point3f result = m.transform_point(p);
    EXPECT_FLOAT_EQ(result.x, 11.0f);
    EXPECT_FLOAT_EQ(result.y, 22.0f);
    EXPECT_FLOAT_EQ(result.z, 33.0f);
}
