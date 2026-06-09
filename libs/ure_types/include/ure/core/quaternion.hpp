#pragma once

#include "vector.hpp"
#include "matrix.hpp"
#include <cmath>
#include <concepts>

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

    // From Euler angles (degrees, ZYX convention: yaw, pitch, roll)
    static Quaternion from_euler_zyx(T yaw_deg, T pitch_deg, T roll_deg) {
        constexpr T PI = T(3.14159265358979323846);
        T cy = std::cos(yaw_deg   * T(0.5) * PI / T(180));
        T sy = std::sin(yaw_deg   * T(0.5) * PI / T(180));
        T cp = std::cos(pitch_deg * T(0.5) * PI / T(180));
        T sp = std::sin(pitch_deg * T(0.5) * PI / T(180));
        T cr = std::cos(roll_deg  * T(0.5) * PI / T(180));
        T sr = std::sin(roll_deg  * T(0.5) * PI / T(180));
        return Quaternion(
            cr * cp * cy + sr * sp * sy,
            sr * cp * cy - cr * sp * sy,
            cr * sp * cy + sr * cp * sy,
            cr * cp * sy - sr * sp * cy
        );
    }

    static Quaternion from_euler_zyx(const Vec3<T>& euler_deg) {
        return from_euler_zyx(euler_deg.z, euler_deg.y, euler_deg.x);
    }

    // To Euler angles (degrees, ZYX convention)
    Vec3<T> to_euler_zyx() const {
        constexpr T PI = T(3.14159265358979323846);
        Vec3<T> euler;
        T sinr_cosp = T(2) * (w * x + y * z);
        T cosr_cosp = T(1) - T(2) * (x * x + y * y);
        euler.x = std::atan2(sinr_cosp, cosr_cosp);
        T sinp = T(2) * (w * y - z * x);
        if (std::abs(sinp) >= T(1))
            euler.y = std::copysign(PI / T(2), sinp);
        else
            euler.y = std::asin(sinp);
        T siny_cosp = T(2) * (w * z + x * y);
        T cosy_cosp = T(1) - T(2) * (y * y + z * z);
        euler.z = std::atan2(siny_cosp, cosy_cosp);
        euler.x = euler.x * T(180) / PI;
        euler.y = euler.y * T(180) / PI;
        euler.z = euler.z * T(180) / PI;
        return euler;
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
