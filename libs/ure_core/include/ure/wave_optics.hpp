#pragma once

namespace ure::wave {

struct CircularAperture {
    double wavelength_m = 550.0e-9;
    double aperture_diameter_m = 1.0e-3;
    double focal_length_m = 50.0e-3;
};

struct PointSpreadSample {
    double sensor_x_m = 0.0;
    double sensor_y_m = 0.0;
    double intensity = 0.0;
};

constexpr double kAiryFirstZero = 3.8317059702075125;

bool is_valid(const CircularAperture& aperture);
double airy_argument_from_angle(const CircularAperture& aperture, double theta_rad);
double airy_intensity_from_argument(double x);
double airy_intensity_at_angle(const CircularAperture& aperture, double theta_rad);
double airy_intensity_on_sensor(const CircularAperture& aperture, double sensor_radius_m);
double airy_first_zero_angle_rad(const CircularAperture& aperture);
double airy_first_zero_radius_on_sensor_m(const CircularAperture& aperture);
double airy_encircled_energy_from_argument(double x);
PointSpreadSample sample_circular_aperture_psf(const CircularAperture& aperture,
                                               double sensor_x_m,
                                               double sensor_y_m);

}
