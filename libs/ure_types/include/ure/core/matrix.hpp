#pragma once

#include "vector.hpp"
#include <array>

namespace ure::core {

template <typename T>
requires std::floating_point<T>
class Matrix4x4 {
public:
    std::array<std::array<T, 4>, 4> m;

    constexpr Matrix4x4() {
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                m[i][j] = (i == j) ? static_cast<T>(1) : 0;
    }

    constexpr Matrix4x4(T t00, T t01, T t02, T t03,
                       T t10, T t11, T t12, T t13,
                       T t20, T t21, T t22, T t23,
                       T t30, T t31, T t32, T t33) {
        m[0][0] = t00; m[0][1] = t01; m[0][2] = t02; m[0][3] = t03;
        m[1][0] = t10; m[1][1] = t11; m[1][2] = t12; m[1][3] = t13;
        m[2][0] = t20; m[2][1] = t21; m[2][2] = t22; m[2][3] = t23;
        m[3][0] = t30; m[3][1] = t31; m[3][2] = t32; m[3][3] = t33;
    }

    static constexpr Matrix4x4 identity() {
        return Matrix4x4();
    }

    constexpr Matrix4x4 operator*(const Matrix4x4& other) const {
        Matrix4x4 result;
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                result.m[i][j] = m[i][0] * other.m[0][j] +
                                 m[i][1] * other.m[1][j] +
                                 m[i][2] * other.m[2][j] +
                                 m[i][3] * other.m[3][j];
            }
        }
        return result;
    }

    constexpr Matrix4x4 transpose() const {
        Matrix4x4 result;
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                result.m[i][j] = m[j][i];
            }
        }
        return result;
    }

    // Vector transformation
    constexpr Vec3<T> transform_vector(const Vec3<T>& v) const {
        return {
            m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
            m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
            m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z
        };
    }

    constexpr Point3<T> transform_point(const Point3<T>& p) const {
        T x = m[0][0] * p.x + m[0][1] * p.y + m[0][2] * p.z + m[0][3];
        T y = m[1][0] * p.x + m[1][1] * p.y + m[1][2] * p.z + m[1][3];
        T z = m[2][0] * p.x + m[2][1] * p.y + m[2][2] * p.z + m[2][3];
        T w = m[3][0] * p.x + m[3][1] * p.y + m[3][2] * p.z + m[3][3];
        if (w == 1) return {x, y, z};
        return {x / w, y / w, z / w};
    }
};

using Matrix4x4f = Matrix4x4<float>;
using Matrix4x4d = Matrix4x4<double>;

} // namespace ure::core
