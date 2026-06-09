#pragma once

#include <cmath>
#include <concepts>
#include <type_traits>
#include <iostream>
#include <format>
#include <algorithm>
#include <string>

namespace ure::core {

template <typename T>
class Vec2 {
public:
    static_assert(std::is_floating_point_v<T>, "Vec2 requires a floating point type.");
    T x, y;
    constexpr Vec2() : x(0), y(0) {}
    constexpr Vec2(T v) : x(v), y(v) {}
    constexpr Vec2(T x, T y) : x(x), y(y) {}

    constexpr T operator[](int i) const {
        return (i == 0) ? x : y;
    }

    constexpr T& operator[](int i) {
        return (i == 0) ? x : y;
    }

    constexpr Vec2 operator*(T s) const { return Vec2(x * s, y * s); }
    constexpr Vec2 operator+(const Vec2& v) const { return Vec2(x + v.x, y + v.y); }
    constexpr Vec2 operator-(const Vec2& v) const { return Vec2(x - v.x, y - v.y); }
    constexpr T length_sq() const { return x * x + y * y; }
    T length() const { return std::sqrt(length_sq()); }
};

template <typename T>
class Vec3 {
public:
    static_assert(std::is_floating_point_v<T>, "Vec3 requires a floating point type.");
    T x, y, z;

    constexpr Vec3() : x(0), y(0), z(0) {}
    constexpr Vec3(T v) : x(v), y(v), z(v) {}
    constexpr Vec3(T x, T y, T z) : x(x), y(y), z(z) {}

    constexpr T operator[](int i) const {
        return (i == 0) ? x : (i == 1 ? y : z);
    }

    constexpr T& operator[](int i) {
        return (i == 0) ? x : (i == 1 ? y : z);
    }

    // Basic operations
    constexpr Vec3 operator+(const Vec3& v) const { return Vec3(x + v.x, y + v.y, z + v.z); }
    constexpr Vec3 operator-(const Vec3& v) const { return Vec3(x - v.x, y - v.y, z - v.z); }
    constexpr Vec3 operator*(const Vec3& v) const { return Vec3(x * v.x, y * v.y, z * v.z); }
    constexpr Vec3 operator/(const Vec3& v) const { return Vec3(x / v.x, y / v.y, z / v.z); }
    
    constexpr Vec3 operator*(T s) const { return Vec3(x * s, y * s, z * s); }
    constexpr Vec3 operator/(T s) const { 
        T inv = static_cast<T>(1) / s;
        return *this * inv;
    }

    constexpr Vec3& operator+=(const Vec3& v) { x += v.x; y += v.y; z += v.z; return *this; }
    constexpr Vec3& operator-=(const Vec3& v) { x -= v.x; y -= v.y; z -= v.z; return *this; }
    constexpr Vec3& operator*=(T s) { x *= s; y *= s; z *= s; return *this; }

    constexpr Vec3 operator-() const { return Vec3(-x, -y, -z); }

    // Vector operations
    constexpr T dot(const Vec3& v) const { return x * v.x + y * v.y + z * v.z; }
    
    constexpr Vec3 cross(const Vec3& v) const {
        return Vec3(
            y * v.z - z * v.y,
            z * v.x - x * v.z,
            x * v.y - y * v.x
        );
    }

    constexpr T length_sq() const { return dot(*this); }
    T length() const { return std::sqrt(length_sq()); }

    Vec3 normalize() const {
        T len = length();
        return len > 0 ? *this / len : Vec3(0);
    }

    // Common tools
    static constexpr Vec3 min(const Vec3& a, const Vec3& b) {
        return Vec3(std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z));
    }

    static constexpr Vec3 max(const Vec3& a, const Vec3& b) {
        return Vec3(std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z));
    }

    bool has_nan() const {
        return std::isnan(x) || std::isnan(y) || std::isnan(z);
    }

    std::string to_string() const {
        return std::format("[{:.4f}, {:.4f}, {:.4f}]", x, y, z);
    }
};

using Point2f = Vec2<float>;
using Point2d = Vec2<double>;
using Vec2f = Vec2<float>;
using Vec2d = Vec2<double>;

// Aliases
template <typename T>
using Point3 = Vec3<T>;

template <typename T>
using Normal3 = Vec3<T>;

using Vec3f = Vec3<float>;
using Vec3d = Vec3<double>;
using Point3f = Point3<float>;
using Point3d = Point3<double>;
using Normal3f = Normal3<float>;

// External operators
template <typename T>
constexpr Vec3<T> operator*(T s, const Vec3<T>& v) { return v * s; }

} // namespace ure::core