#include <ure/wave_optics.hpp>

#include <cmath>
#include <cstdio>

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond) do { if (cond) ++g_passed; else { ++g_failed; std::fprintf(stderr, "CHECK failed: %s at line %d\n", #cond, __LINE__); } } while (0)
#define CHECK_NEAR(a, b, eps) do { const double _a = (a); const double _b = (b); if (std::abs(_a - _b) <= (eps)) ++g_passed; else { ++g_failed; std::fprintf(stderr, "CHECK_NEAR failed: %.17g vs %.17g at line %d\n", _a, _b, __LINE__); } } while (0)

static int test_circular_airy_oracle() {
    ure::wave::CircularAperture aperture;
    aperture.wavelength_m = 550.0e-9;
    aperture.aperture_diameter_m = 5.0e-3;
    aperture.focal_length_m = 50.0e-3;

    CHECK(ure::wave::is_valid(aperture));
    CHECK_NEAR(ure::wave::airy_intensity_at_angle(aperture, 0.0), 1.0, 1.0e-12);

    const double zero_angle = ure::wave::airy_first_zero_angle_rad(aperture);
    const double expected_zero_angle = std::asin(1.2196698912665045 *
                                                aperture.wavelength_m /
                                                aperture.aperture_diameter_m);
    CHECK_NEAR(zero_angle, expected_zero_angle, 1.0e-15);
    CHECK(ure::wave::airy_intensity_at_angle(aperture, zero_angle) < 1.0e-12);

    const double zero_radius = ure::wave::airy_first_zero_radius_on_sensor_m(aperture);
    CHECK_NEAR(ure::wave::airy_intensity_on_sensor(aperture, zero_radius),
               ure::wave::airy_intensity_at_angle(aperture, zero_angle),
               1.0e-12);
    return 0;
}

static int test_circular_airy_scaling_and_symmetry() {
    ure::wave::CircularAperture green;
    green.wavelength_m = 550.0e-9;
    green.aperture_diameter_m = 4.0e-3;
    green.focal_length_m = 35.0e-3;

    ure::wave::CircularAperture red = green;
    red.wavelength_m = 700.0e-9;
    CHECK(ure::wave::airy_first_zero_radius_on_sensor_m(red) >
          ure::wave::airy_first_zero_radius_on_sensor_m(green));

    const double x = 2.0e-6;
    const double y = -3.0e-6;
    const auto a = ure::wave::sample_circular_aperture_psf(green, x, y);
    const auto b = ure::wave::sample_circular_aperture_psf(green, -y, x);
    CHECK_NEAR(a.intensity, b.intensity, 1.0e-15);
    CHECK_NEAR(a.sensor_x_m, x, 0.0);
    CHECK_NEAR(a.sensor_y_m, y, 0.0);

    const double first_ring_energy =
        ure::wave::airy_encircled_energy_from_argument(ure::wave::kAiryFirstZero);
    CHECK_NEAR(first_ring_energy, 0.8377848691733143, 1.0e-12);
    return 0;
}

static int test_invalid_aperture_fails_closed() {
    ure::wave::CircularAperture aperture;
    aperture.wavelength_m = 0.0;
    CHECK(!ure::wave::is_valid(aperture));
    CHECK_NEAR(ure::wave::airy_first_zero_angle_rad(aperture), 0.0, 0.0);
    CHECK_NEAR(ure::wave::airy_intensity_on_sensor(aperture, 1.0e-6), 0.0, 0.0);
    CHECK_NEAR(ure::wave::airy_argument_from_angle(aperture, 0.1), 0.0, 0.0);
    return 0;
}

int main() {
    std::fprintf(stderr, "[Wave Optics Test]\n");
    auto run = [](const char* name, int (*fn)()) {
        std::fprintf(stderr, "  test: %s ... ", name);
        const int result = fn();
        std::fprintf(stderr, "%s\n", result == 0 ? "PASS" : "FAIL");
        return result;
    };

    int failed = 0;
    failed += run("test_circular_airy_oracle", test_circular_airy_oracle);
    failed += run("test_circular_airy_scaling_and_symmetry", test_circular_airy_scaling_and_symmetry);
    failed += run("test_invalid_aperture_fails_closed", test_invalid_aperture_fails_closed);

    std::fprintf(stderr, "  passed: %d, failed: %d\n", g_passed, failed);
    g_failed += failed;
    return g_failed == 0 ? 0 : 1;
}
