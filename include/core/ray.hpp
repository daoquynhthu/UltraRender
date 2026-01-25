#pragma once

#include "vector.hpp"
#include <limits>
#include <concepts>
#include <type_traits>

namespace ure::core {

template <typename T>
class Ray {
public:
    static_assert(std::is_floating_point_v<T>, "Ray requires a floating point type.");
    Point3<T> origin;
    Vec3<T> direction;
    mutable T t_max;
    T time;

    constexpr Ray() : t_max(std::numeric_limits<T>::infinity()), time(0) {}
    constexpr Ray(const Point3<T>& o, const Vec3<T>& d, T t_max = std::numeric_limits<T>::infinity(), T time = 0)
        : origin(o), direction(d), t_max(t_max), time(time) {}

    constexpr Point3<T> at(T t) const {
        return origin + direction * t;
    }
};

// Aliases
using Rayf = Ray<float>;
using Rayd = Ray<double>;

// Point3 aliases are defined in vector.hpp

} // namespace ure::core
