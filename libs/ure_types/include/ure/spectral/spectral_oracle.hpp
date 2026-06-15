#pragma once

#include "cie_data.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace ure::spectral {

struct Xyz {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct SpectralDomain {
    double lambda_min = 360.0;
    double lambda_max = 830.0;
    std::uint64_t bins = 1'000'000;

    double step() const {
        if (bins == 0 || lambda_max <= lambda_min) {
            throw std::invalid_argument("invalid spectral domain");
        }
        return (lambda_max - lambda_min) / static_cast<double>(bins);
    }

    double center(std::uint64_t index) const {
        return lambda_min + (static_cast<double>(index) + 0.5) * step();
    }
};

struct UniformSpectralTable {
    SpectralDomain domain;
    std::vector<double> values;
};

inline double cie_x(double lambda) {
    return static_cast<double>(interpolate_cie(static_cast<float>(lambda), kCieX));
}

inline double cie_y(double lambda) {
    return static_cast<double>(interpolate_cie(static_cast<float>(lambda), kCieY));
}

inline double cie_z(double lambda) {
    return static_cast<double>(interpolate_cie(static_cast<float>(lambda), kCieZ));
}

inline constexpr double kCieYIntegral = 106.857039252350;

inline Xyz operator+(const Xyz& a, const Xyz& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

inline Xyz operator-(const Xyz& a, const Xyz& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

inline Xyz operator*(const Xyz& a, double s) {
    return {a.x * s, a.y * s, a.z * s};
}

inline double max_abs_delta(const Xyz& a, const Xyz& b) {
    return std::max({std::abs(a.x - b.x), std::abs(a.y - b.y), std::abs(a.z - b.z)});
}

inline std::array<double, 2> xy_chromaticity(const Xyz& xyz) {
    double sum = xyz.x + xyz.y + xyz.z;
    if (sum <= 0.0) {
        return {0.0, 0.0};
    }
    return {xyz.x / sum, xyz.y / sum};
}

template <typename Evaluator>
Xyz integrate_xyz(const SpectralDomain& domain, Evaluator&& evaluator) {
    double step = domain.step();
    Xyz sum;
    for (std::uint64_t i = 0; i < domain.bins; ++i) {
        double lambda = domain.lambda_min + (static_cast<double>(i) + 0.5) * step;
        double value = evaluator(lambda);
        sum.x += value * cie_x(lambda);
        sum.y += value * cie_y(lambda);
        sum.z += value * cie_z(lambda);
    }
    double scale = step / kCieYIntegral;
    return sum * scale;
}

template <typename Evaluator>
UniformSpectralTable make_uniform_table(const SpectralDomain& domain, Evaluator&& evaluator) {
    UniformSpectralTable table;
    table.domain = domain;
    table.values.resize(static_cast<std::size_t>(domain.bins));
    double step = domain.step();
    for (std::uint64_t i = 0; i < domain.bins; ++i) {
        double lambda = domain.lambda_min + (static_cast<double>(i) + 0.5) * step;
        table.values[static_cast<std::size_t>(i)] = evaluator(lambda);
    }
    return table;
}

inline Xyz integrate_xyz(const UniformSpectralTable& table) {
    if (table.values.size() != table.domain.bins) {
        throw std::invalid_argument("uniform spectral table size does not match domain bins");
    }
    double step = table.domain.step();
    Xyz sum;
    for (std::uint64_t i = 0; i < table.domain.bins; ++i) {
        double lambda = table.domain.lambda_min + (static_cast<double>(i) + 0.5) * step;
        double value = table.values[static_cast<std::size_t>(i)];
        sum.x += value * cie_x(lambda);
        sum.y += value * cie_y(lambda);
        sum.z += value * cie_z(lambda);
    }
    double scale = step / kCieYIntegral;
    return sum * scale;
}

inline double eval_uniform_table(const UniformSpectralTable& table, double lambda) {
    if (table.values.empty()) {
        return 0.0;
    }
    if (lambda <= table.domain.lambda_min) {
        return table.values.front();
    }
    if (lambda >= table.domain.lambda_max) {
        return table.values.back();
    }
    double t = (lambda - table.domain.lambda_min) / table.domain.step() - 0.5;
    auto i0 = static_cast<std::int64_t>(std::floor(t));
    double frac = t - static_cast<double>(i0);
    if (i0 < 0) {
        return table.values.front();
    }
    auto i1 = i0 + 1;
    if (i1 >= static_cast<std::int64_t>(table.values.size())) {
        return table.values.back();
    }
    double a = table.values[static_cast<std::size_t>(i0)];
    double b = table.values[static_cast<std::size_t>(i1)];
    return a * (1.0 - frac) + b * frac;
}

template <typename Evaluator>
Xyz estimate_uniform_sampled_xyz(const SpectralDomain& domain, std::uint64_t sample_count, Evaluator&& evaluator) {
    if (sample_count == 0) {
        throw std::invalid_argument("sample_count must be positive");
    }
    SpectralDomain sampled{domain.lambda_min, domain.lambda_max, sample_count};
    return integrate_xyz(sampled, evaluator);
}

inline double equal_energy(double) {
    return 1.0;
}

inline double narrowband(double lambda, double center_nm, double sigma_nm) {
    double x = (lambda - center_nm) / sigma_nm;
    return std::exp(-0.5 * x * x);
}

template <typename Table>
double interpolate_uniform_table(double lambda, const Table& table) {
    if (lambda <= static_cast<double>(kCieStart)) {
        return table.front();
    }
    if (lambda >= static_cast<double>(kCieEnd)) {
        return table.back();
    }
    double t = (lambda - static_cast<double>(kCieStart)) / static_cast<double>(kCieStep);
    auto idx = static_cast<std::size_t>(std::floor(t));
    double frac = t - static_cast<double>(idx);
    return table[idx] * (1.0 - frac) + table[idx + 1] * frac;
}

inline double d65(double lambda) {
    static constexpr std::array<double, kCieCount> kD65 = {
        46.6383, 49.3637, 52.0891, 51.0323, 49.9755, 52.3118, 54.6482, 68.7015,
        82.7549, 87.1204, 91.4860, 92.4589, 93.4318, 90.0570, 86.6823, 95.7736,
        104.8650, 110.9360, 117.0080, 117.4100, 117.8120, 116.3360, 114.8610, 115.3920,
        115.9230, 112.3670, 108.8110, 109.0820, 109.3540, 108.5780, 107.8020, 106.2960,
        104.7900, 106.2390, 107.6890, 106.0470, 104.4050, 104.2250, 104.0460, 102.0230,
        100.0000, 98.1671, 96.3342, 96.0611, 95.7880, 92.2368, 88.6856, 89.3459,
        90.0062, 89.8026, 89.5991, 88.6489, 87.6987, 85.4936, 83.2886, 83.4939,
        83.6992, 81.8630, 80.0268, 80.1207, 80.2146, 81.2462, 82.2778, 80.2810,
        78.2842, 74.0027, 69.7213, 70.6652, 71.6091, 72.9790, 74.3490, 67.9765,
        61.6040, 65.7448, 69.8856, 72.4863, 75.0870, 69.3398, 63.5927, 55.0054,
        46.4182, 56.6118, 66.8054, 65.0941, 63.3828, 63.8434, 64.3040, 61.8779,
        59.4519, 55.7054, 51.9590, 54.6998, 57.4406, 58.8765, 60.3125
    };
    return interpolate_uniform_table(lambda, kD65) / 100.0;
}

inline double gaussian_basis(double lambda, double center_nm) {
    return narrowband(lambda, center_nm, 12.0);
}

inline std::array<double, 4> metamer_null_coefficients(const SpectralDomain& domain) {
    std::array<double, 4> centers{450.0, 530.0, 610.0, 700.0};
    std::array<Xyz, 4> columns{};
    for (std::size_t i = 0; i < centers.size(); ++i) {
        columns[i] = integrate_xyz(domain, [center = centers[i]](double lambda) {
            return gaussian_basis(lambda, center);
        });
    }

    double a00 = columns[0].x, a01 = columns[1].x, a02 = columns[2].x;
    double a10 = columns[0].y, a11 = columns[1].y, a12 = columns[2].y;
    double a20 = columns[0].z, a21 = columns[1].z, a22 = columns[2].z;
    double b0 = -columns[3].x, b1 = -columns[3].y, b2 = -columns[3].z;

    double det = a00 * (a11 * a22 - a12 * a21) -
                 a01 * (a10 * a22 - a12 * a20) +
                 a02 * (a10 * a21 - a11 * a20);
    if (std::abs(det) < 1e-14) {
        throw std::runtime_error("metamer basis is singular");
    }

    double dx = b0 * (a11 * a22 - a12 * a21) -
                a01 * (b1 * a22 - a12 * b2) +
                a02 * (b1 * a21 - a11 * b2);
    double dy = a00 * (b1 * a22 - a12 * b2) -
                b0 * (a10 * a22 - a12 * a20) +
                a02 * (a10 * b2 - b1 * a20);
    double dz = a00 * (a11 * b2 - b1 * a21) -
                a01 * (a10 * b2 - b1 * a20) +
                b0 * (a10 * a21 - a11 * a20);

    return {dx / det, dy / det, dz / det, 1.0};
}

inline double metamer_delta(double lambda, const std::array<double, 4>& coeffs) {
    std::array<double, 4> centers{450.0, 530.0, 610.0, 700.0};
    double value = 0.0;
    for (std::size_t i = 0; i < centers.size(); ++i) {
        value += coeffs[i] * gaussian_basis(lambda, centers[i]);
    }
    return value;
}

inline std::array<UniformSpectralTable, 2> make_metamer_pair(const SpectralDomain& domain, double scale) {
    auto coeffs = metamer_null_coefficients(domain);
    UniformSpectralTable a;
    UniformSpectralTable b;
    a.domain = domain;
    b.domain = domain;
    a.values.resize(static_cast<std::size_t>(domain.bins));
    b.values.resize(static_cast<std::size_t>(domain.bins));

    double step = domain.step();
    for (std::uint64_t i = 0; i < domain.bins; ++i) {
        double lambda = domain.lambda_min + (static_cast<double>(i) + 0.5) * step;
        double delta = scale * metamer_delta(lambda, coeffs);
        a.values[static_cast<std::size_t>(i)] = 1.0 + std::max(delta, 0.0);
        b.values[static_cast<std::size_t>(i)] = 1.0 + std::max(-delta, 0.0);
    }
    return {a, b};
}

} // namespace ure::spectral
