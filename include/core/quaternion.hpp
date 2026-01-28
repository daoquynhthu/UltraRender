#pragma once

#include "vector.hpp"
#include "matrix.hpp"
#include <cmath>

namespace ure::core {

template <typename T>
requires std::floating_point<T>
class Quaternion {
public:
    T w, x, y, z;

    constexpr Quaternion() : w(1), x(0), y(0), z(0) {}
    constexpr Quaternion(T w, T x, T y, T z) : w(w), x(x), y(y), z(z) {}

    // From axis-angle
    static Quaternion from_axis_angle(const Vec3<T>& axis, T angle) {
        T half_angle = angle * static_cast<T>(0.5);
        T s = std::sin(half_angle);
        return Quaternion(
            std::cos(half_angle),
            axis.x * s,
            axis.y * s,
            axis.z * s
        );
    }

    Quaternion normalized() const {
        T mag_sq = w * w + x * x + y * y + z * z;
        if (mag_sq < static_cast<T>(1e-6)) return Quaternion();
        T inv_mag = static_cast<T>(1) / std::sqrt(mag_sq);
        return Quaternion(w * inv_mag, x * inv_mag, y * inv_mag, z * inv_mag);
    }

    Quaternion conjugate() const {
        return Quaternion(w, -x, -y, -z);
    }

    Quaternion inverse() const {
        T mag_sq = w * w + x * x + y * y + z * z;
        if (mag_sq < static_cast<T>(1e-6)) return Quaternion(); // or identity?
        T inv_mag_sq = static_cast<T>(1) / mag_sq;
        return Quaternion(w * inv_mag_sq, -x * inv_mag_sq, -y * inv_mag_sq, -z * inv_mag_sq);
    }

    Quaternion operator*(const Quaternion& other) const {
        return Quaternion(
            w * other.w - x * other.x - y * other.y - z * other.z,
            w * other.x + x * other.w + y * other.z - z * other.y,
            w * other.y - x * other.z + y * other.w + z * other.x,
            w * other.z + x * other.y - y * other.x + z * other.w
        );
    }

    Vec3<T> rotate(const Vec3<T>& v) const {
        // q * v * q_inv
        // Optimized implementation
        T tx = static_cast<T>(2) * (y * v.z - z * v.y);
        T ty = static_cast<T>(2) * (z * v.x - x * v.z);
        T tz = static_cast<T>(2) * (x * v.y - y * v.x);

        return Vec3<T>(
            v.x + w * tx + (y * tz - z * ty),
            v.y + w * ty + (z * tx - x * tz),
            v.z + w * tz + (x * ty - y * tx)
        );
    }
    
    Matrix4x4<T> to_matrix() const {
        T xx = x * x; T yy = y * y; T zz = z * z;
        T xy = x * y; T xz = x * z; T yz = y * z;
        T wx = w * x; T wy = w * y; T wz = w * z;

        return Matrix4x4<T>(
            1 - 2 * (yy + zz), 2 * (xy - wz),     2 * (xz + wy),     0,
            2 * (xy + wz),     1 - 2 * (xx + zz), 2 * (yz - wx),     0,
            2 * (xz - wy),     2 * (yz + wx),     1 - 2 * (xx + yy), 0,
            0,                 0,                 0,                 1
        );
    }
};

using Quat = Quaternion<float>;

} // namespace ure::core
