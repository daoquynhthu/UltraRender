#pragma once

#include <complex>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace ure::scene_ir {

enum class VolumePhaseFunction {
    HenyeyGreenstein = 0,
    Rayleigh = 1,
    Mie = 2
};

enum class MieRadiusDistributionKind {
    Monodisperse = 0,
    Discrete = 1,
    LogNormal = 2
};

enum class MiePolarizationModel {
    ScalarDepolarizing = 0
};

struct MieRadiusSample {
    double radius_m = 0.0;
    double number_weight = 0.0;

    bool operator==(const MieRadiusSample&) const = default;
};

struct MieOpticalSample {
    double wavelength_nm = 0.0;
    std::complex<double> particle_ior = {1.0, 0.0};
    double host_ior = 1.0;
};

struct MieRadiusDistribution {
    MieRadiusDistributionKind kind = MieRadiusDistributionKind::Monodisperse;
    std::vector<MieRadiusSample> samples;
    double median_radius_m = 0.0;
    double geometric_standard_deviation = 1.0;
    double standard_deviation_extent = 4.0;
    std::size_t quadrature_sample_count = 32;
};

struct MieGenerationConfig {
    std::vector<MieOpticalSample> optical_samples;
    MieRadiusDistribution radius_distribution;
    std::size_t initial_angular_sample_count = 257;
    std::size_t maximum_angular_sample_count = 16385;
    std::size_t maximum_series_terms = 100000;
    std::size_t maximum_resource_bytes = 256ull * 1024ull * 1024ull;
    std::size_t maximum_working_set_bytes = 512ull * 1024ull * 1024ull;
    std::size_t maximum_angular_evaluations = 1000000000ull;
    double angular_cross_section_tolerance = 1.0e-4;
    double angular_asymmetry_tolerance = 1.0e-4;
    double angular_distribution_tolerance = 1.0e-4;
};

struct MiePhaseResource {
    std::vector<float> wavelengths_nm;
    std::vector<float> cos_theta;
    std::vector<float> phase;
    std::vector<float> cdf;
    std::vector<float> scattering_cross_section_m2;
    std::vector<float> extinction_cross_section_m2;
    std::vector<float> absorption_cross_section_m2;
    std::vector<float> asymmetry;
    MiePolarizationModel polarization_model = MiePolarizationModel::ScalarDepolarizing;
    std::string provenance;
    std::string source_hash;
    std::string content_hash;
};

}
