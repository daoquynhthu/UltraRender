#include "ure/wave_optics.hpp"

#include <cmath>
#include <numbers>

namespace ure::wave {

bool is_valid(const CircularAperture& aperture) {
    return aperture.wavelength_m > 0.0 &&
           aperture.aperture_diameter_m > 0.0 &&
           aperture.focal_length_m > 0.0;
}

double airy_argument_from_angle(const CircularAperture& aperture, double theta_rad) {
    if (!is_valid(aperture)) return 0.0;
    return std::numbers::pi * aperture.aperture_diameter_m * std::sin(theta_rad) /
           aperture.wavelength_m;
}

double airy_intensity_from_argument(double x) {
    const double ax = std::abs(x);
    if (ax < 1.0e-8) return 1.0;
    const double j1 = std::cyl_bessel_j(1.0, ax);
    const double amplitude = 2.0 * j1 / ax;
    return amplitude * amplitude;
}

double airy_intensity_at_angle(const CircularAperture& aperture, double theta_rad) {
    return airy_intensity_from_argument(airy_argument_from_angle(aperture, theta_rad));
}

double airy_intensity_on_sensor(const CircularAperture& aperture, double sensor_radius_m) {
    if (!is_valid(aperture)) return 0.0;
    const double theta = std::atan2(std::abs(sensor_radius_m), aperture.focal_length_m);
    return airy_intensity_at_angle(aperture, theta);
}

double airy_first_zero_angle_rad(const CircularAperture& aperture) {
    if (!is_valid(aperture)) return 0.0;
    const double sin_theta = kAiryFirstZero * aperture.wavelength_m /
                             (std::numbers::pi * aperture.aperture_diameter_m);
    if (sin_theta >= 1.0) return std::numbers::pi / 2.0;
    return std::asin(sin_theta);
}

double airy_first_zero_radius_on_sensor_m(const CircularAperture& aperture) {
    if (!is_valid(aperture)) return 0.0;
    return aperture.focal_length_m * std::tan(airy_first_zero_angle_rad(aperture));
}

double airy_encircled_energy_from_argument(double x) {
    const double ax = std::abs(x);
    const double j0 = std::cyl_bessel_j(0.0, ax);
    const double j1 = std::cyl_bessel_j(1.0, ax);
    return 1.0 - j0 * j0 - j1 * j1;
}

PointSpreadSample sample_circular_aperture_psf(const CircularAperture& aperture,
                                               double sensor_x_m,
                                               double sensor_y_m) {
    const double radius = std::hypot(sensor_x_m, sensor_y_m);
    return {sensor_x_m, sensor_y_m, airy_intensity_on_sensor(aperture, radius)};
}

}
