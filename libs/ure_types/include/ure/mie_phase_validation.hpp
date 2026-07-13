#pragma once

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>

#include "ure/mie_phase.hpp"

namespace ure::scene_ir {
namespace mie_validation_detail {

inline void hash_bytes(std::uint64_t& hash, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
}

inline void hash_u32(std::uint64_t& hash, std::uint32_t value) {
    unsigned char bytes[4];
    for (int i = 0; i < 4; ++i) {
        bytes[i] = static_cast<unsigned char>((value >> (8 * i)) & 0xffu);
    }
    hash_bytes(hash, bytes, sizeof(bytes));
}

inline void hash_u64(std::uint64_t& hash, std::uint64_t value) {
    unsigned char bytes[8];
    for (int i = 0; i < 8; ++i) {
        bytes[i] = static_cast<unsigned char>((value >> (8 * i)) & 0xffu);
    }
    hash_bytes(hash, bytes, sizeof(bytes));
}

inline void hash_vector(std::uint64_t& hash, const std::vector<float>& values) {
    hash_u64(hash, static_cast<std::uint64_t>(values.size()));
    for (float value : values) {
        hash_u32(hash, value == 0.0f ? 0u : std::bit_cast<std::uint32_t>(value));
    }
}

inline void require_finite_non_negative(float value, const char* field) {
    if (!std::isfinite(value) || value < 0.0f) {
        throw std::invalid_argument(std::string("Mie phase resource invalid ") + field);
    }
}

inline void require_strictly_increasing(const std::vector<float>& values, const char* field) {
    if (values.size() < 2) {
        throw std::invalid_argument(std::string("Mie phase resource requires at least two ") + field);
    }
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (!std::isfinite(values[i]) || (i > 0 && values[i] <= values[i - 1])) {
            throw std::invalid_argument(std::string("Mie phase resource non-monotone ") + field);
        }
    }
}

inline std::size_t checked_table_size(std::size_t wavelengths, std::size_t angles) {
    if (angles != 0 && wavelengths > std::numeric_limits<std::size_t>::max() / angles) {
        throw std::invalid_argument("Mie phase resource table size overflow");
    }
    return wavelengths * angles;
}

}

inline std::string mie_phase_content_hash(const MiePhaseResource& resource) {
    std::uint64_t hash = 14695981039346656037ull;
    mie_validation_detail::hash_vector(hash, resource.wavelengths_nm);
    mie_validation_detail::hash_vector(hash, resource.cos_theta);
    mie_validation_detail::hash_vector(hash, resource.phase);
    mie_validation_detail::hash_vector(hash, resource.cdf);
    mie_validation_detail::hash_vector(hash, resource.scattering_cross_section_m2);
    mie_validation_detail::hash_vector(hash, resource.extinction_cross_section_m2);
    mie_validation_detail::hash_vector(hash, resource.absorption_cross_section_m2);
    mie_validation_detail::hash_vector(hash, resource.asymmetry);
    mie_validation_detail::hash_u32(hash, static_cast<std::uint32_t>(resource.polarization_model));
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16) << hash;
    return stream.str();
}

inline void validate_mie_phase_resource(MiePhaseResource& resource,
                                        float normalization_tolerance = 1.0e-4f) {
    using namespace mie_validation_detail;
    if (!std::isfinite(normalization_tolerance) || normalization_tolerance <= 0.0f) {
        throw std::invalid_argument("Mie phase resource invalid normalization tolerance");
    }
    require_strictly_increasing(resource.wavelengths_nm, "wavelength samples");
    if (resource.wavelengths_nm.front() <= 0.0f) {
        throw std::invalid_argument("Mie phase resource wavelengths must be positive");
    }
    if (resource.polarization_model != MiePolarizationModel::ScalarDepolarizing) {
        throw std::invalid_argument("Mie phase resource invalid polarization model");
    }
    require_strictly_increasing(resource.cos_theta, "cosine samples");
    if (std::abs(resource.cos_theta.front() + 1.0f) > 1.0e-6f ||
        std::abs(resource.cos_theta.back() - 1.0f) > 1.0e-6f) {
        throw std::invalid_argument("Mie phase resource cosine grid must span [-1, 1]");
    }
    const std::size_t wavelength_count = resource.wavelengths_nm.size();
    const std::size_t angle_count = resource.cos_theta.size();
    const std::size_t table_size = checked_table_size(wavelength_count, angle_count);
    if (resource.phase.size() != table_size) {
        throw std::invalid_argument("Mie phase resource phase table dimension mismatch");
    }
    if (resource.scattering_cross_section_m2.size() != wavelength_count ||
        resource.extinction_cross_section_m2.size() != wavelength_count) {
        throw std::invalid_argument("Mie phase resource cross section dimension mismatch");
    }
    for (float value : resource.phase) {
        require_finite_non_negative(value, "phase value");
    }
    for (float value : resource.scattering_cross_section_m2) {
        require_finite_non_negative(value, "scattering cross section");
    }
    for (float value : resource.extinction_cross_section_m2) {
        require_finite_non_negative(value, "extinction cross section");
    }
    resource.absorption_cross_section_m2.resize(wavelength_count);
    for (std::size_t wavelength = 0; wavelength < wavelength_count; ++wavelength) {
        const float scattering = resource.scattering_cross_section_m2[wavelength];
        const float extinction = resource.extinction_cross_section_m2[wavelength];
        if (scattering > extinction) {
            throw std::invalid_argument("Mie phase resource scattering exceeds extinction");
        }
        resource.absorption_cross_section_m2[wavelength] = std::max(0.0f, extinction - scattering);
    }
    resource.cdf.assign(table_size, 0.0f);
    resource.asymmetry.assign(wavelength_count, 0.0f);
    constexpr double kTwoPi = 2.0 * std::numbers::pi_v<double>;
    for (std::size_t wavelength = 0; wavelength < wavelength_count; ++wavelength) {
        const std::size_t offset = wavelength * angle_count;
        double integral = 0.0;
        double first_moment = 0.0;
        for (std::size_t angle = 1; angle < angle_count; ++angle) {
            const double mu0 = resource.cos_theta[angle - 1];
            const double mu1 = resource.cos_theta[angle];
            const double p0 = resource.phase[offset + angle - 1];
            const double p1 = resource.phase[offset + angle];
            const double width = mu1 - mu0;
            integral += kTwoPi * 0.5 * (p0 + p1) * width;
            const double slope = (p1 - p0) / width;
            const double moment = mu0 * p0 * width +
                                  (mu0 * slope + p0) * width * width * 0.5 +
                                  slope * width * width * width / 3.0;
            first_moment += kTwoPi * moment;
            resource.cdf[offset + angle] = static_cast<float>(integral);
        }
        if (!std::isfinite(integral) || std::abs(integral - 1.0) > normalization_tolerance) {
            throw std::invalid_argument("Mie phase resource phase row is not normalized");
        }
        const float inverse_integral = static_cast<float>(1.0 / integral);
        for (std::size_t angle = 0; angle < angle_count; ++angle) {
            resource.phase[offset + angle] *= inverse_integral;
        }
        resource.asymmetry[wavelength] = static_cast<float>(first_moment / integral);
        if (!std::isfinite(resource.asymmetry[wavelength]) ||
            resource.asymmetry[wavelength] < -1.0f - 1.0e-5f ||
            resource.asymmetry[wavelength] > 1.0f + 1.0e-5f) {
            throw std::invalid_argument("Mie phase resource invalid asymmetry");
        }
        resource.cdf[offset] = 0.0f;
        double canonical_integral = 0.0;
        for (std::size_t angle = 1; angle < angle_count; ++angle) {
            const double width = resource.cos_theta[angle] - resource.cos_theta[angle - 1];
            canonical_integral += kTwoPi * 0.5 *
                (resource.phase[offset + angle - 1] + resource.phase[offset + angle]) * width;
            resource.cdf[offset + angle] = static_cast<float>(canonical_integral);
        }
        for (std::size_t angle = 1; angle < angle_count; ++angle) {
            resource.cdf[offset + angle] = static_cast<float>(
                resource.cdf[offset + angle] / canonical_integral);
        }
        resource.cdf[offset + angle_count - 1] = 1.0f;
    }
    resource.content_hash = mie_phase_content_hash(resource);
}

}
