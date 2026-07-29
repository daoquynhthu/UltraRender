#include <ure/wave_optics.hpp>
#include <ure/render.hpp>
#include <ure/scene_ir.hpp>

#include <cmath>
#include <cstdio>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>

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

static int test_circular_airy_psf_kernel() {
    ure::wave::PsfKernelConfig config;
    config.aperture.wavelength_m = 550.0e-9;
    config.aperture.aperture_diameter_m = 1.8e-3;
    config.aperture.focal_length_m = 50.0e-3;
    config.pixel_pitch_m = 3.5e-6;
    config.radius_pixels = 9;

    const auto kernel = ure::wave::make_circular_airy_psf_kernel(config);
    CHECK(kernel.width == 19);
    CHECK(kernel.height == 19);
    CHECK(kernel.weights.size() == 361);
    CHECK(kernel.unnormalized_sum > 0.0);
    CHECK_NEAR(kernel.first_zero_radius_m,
               ure::wave::airy_first_zero_radius_on_sensor_m(config.aperture),
               0.0);

    double sum = 0.0;
    for (double weight : kernel.weights) {
        CHECK(weight >= 0.0);
        sum += weight;
    }
    CHECK_NEAR(sum, 1.0, 1.0e-12);

    const int center = config.radius_pixels;
    const double center_weight = kernel.at(center, center);
    CHECK(center_weight > kernel.at(center + 1, center));
    CHECK_NEAR(kernel.at(center + 2, center + 1),
               kernel.at(center - 2, center - 1),
               1.0e-15);
    CHECK_NEAR(kernel.at(center + 3, center),
               kernel.at(center, center + 3),
               1.0e-15);
    CHECK_NEAR(kernel.at(-1, center), 0.0, 0.0);

    ure::wave::PsfKernelConfig red_config = config;
    red_config.aperture.wavelength_m = 700.0e-9;
    const auto red = ure::wave::make_circular_airy_psf_kernel(red_config);
    CHECK(red.first_zero_radius_m > kernel.first_zero_radius_m);
    CHECK(red.at(center, center) < center_weight);
    return 0;
}

static int test_invalid_psf_kernel_fails_closed() {
    ure::wave::PsfKernelConfig config;
    config.pixel_pitch_m = 0.0;
    const auto kernel = ure::wave::make_circular_airy_psf_kernel(config);
    CHECK(kernel.width == 0);
    CHECK(kernel.height == 0);
    CHECK(kernel.weights.empty());
    CHECK_NEAR(kernel.unnormalized_sum, 0.0, 0.0);
    return 0;
}

static int test_circular_aperture_mtf_oracle() {
    ure::wave::CircularAperture aperture;
    aperture.wavelength_m = 550.0e-9;
    aperture.aperture_diameter_m = 4.0e-3;
    aperture.focal_length_m = 50.0e-3;

    const double cutoff = ure::wave::circular_aperture_cutoff_frequency_cycles_per_m(aperture);
    CHECK_NEAR(cutoff,
               aperture.aperture_diameter_m / (aperture.wavelength_m * aperture.focal_length_m),
               1.0e-8);
    CHECK_NEAR(ure::wave::circular_aperture_mtf_from_normalized_frequency(0.0), 1.0, 0.0);
    CHECK_NEAR(ure::wave::circular_aperture_mtf_from_normalized_frequency(1.0), 0.0, 0.0);
    CHECK_NEAR(ure::wave::circular_aperture_mtf(aperture, 0.0), 1.0, 0.0);
    CHECK_NEAR(ure::wave::circular_aperture_mtf(aperture, cutoff), 0.0, 0.0);
    CHECK_NEAR(ure::wave::circular_aperture_mtf(aperture, -0.25 * cutoff),
               ure::wave::circular_aperture_mtf(aperture, 0.25 * cutoff),
               0.0);
    CHECK(ure::wave::circular_aperture_mtf(aperture, 0.25 * cutoff) >
          ure::wave::circular_aperture_mtf(aperture, 0.5 * cutoff));
    CHECK(ure::wave::circular_aperture_mtf(aperture, 0.5 * cutoff) >
          ure::wave::circular_aperture_mtf(aperture, 0.75 * cutoff));

    ure::wave::CircularAperture red = aperture;
    red.wavelength_m = 700.0e-9;
    CHECK(ure::wave::circular_aperture_cutoff_frequency_cycles_per_m(red) < cutoff);
    return 0;
}

static int test_circular_aperture_mtf_samples() {
    ure::wave::CircularAperture aperture;
    aperture.wavelength_m = 550.0e-9;
    aperture.aperture_diameter_m = 4.0e-3;
    aperture.focal_length_m = 50.0e-3;

    const auto samples = ure::wave::sample_circular_aperture_mtf(aperture, 9);
    CHECK(samples.size() == 9);
    CHECK_NEAR(samples.front().spatial_frequency_cycles_per_m, 0.0, 0.0);
    CHECK_NEAR(samples.front().value, 1.0, 0.0);
    CHECK_NEAR(samples.back().spatial_frequency_cycles_per_m,
               ure::wave::circular_aperture_cutoff_frequency_cycles_per_m(aperture),
               1.0e-8);
    CHECK_NEAR(samples.back().value, 0.0, 0.0);
    for (size_t i = 1; i < samples.size(); ++i) {
        CHECK(samples[i].spatial_frequency_cycles_per_m > samples[i - 1].spatial_frequency_cycles_per_m);
        CHECK(samples[i].value <= samples[i - 1].value);
    }

    ure::wave::CircularAperture invalid = aperture;
    invalid.focal_length_m = 0.0;
    CHECK(ure::wave::sample_circular_aperture_mtf(invalid, 9).empty());
    CHECK(ure::wave::sample_circular_aperture_mtf(aperture, 0).empty());
    const auto one = ure::wave::sample_circular_aperture_mtf(aperture, 1);
    CHECK(one.size() == 1);
    CHECK_NEAR(one.front().spatial_frequency_cycles_per_m, 0.0, 0.0);
    CHECK_NEAR(one.front().value, 1.0, 0.0);
    return 0;
}

static int test_knife_edge_fresnel_reference() {
    CHECK_NEAR(ure::wave::knife_edge_fresnel_intensity(0.0), 0.25, 0.0);
    CHECK(ure::wave::knife_edge_fresnel_intensity(-1.0) <
          ure::wave::knife_edge_fresnel_intensity(0.0));
    CHECK(ure::wave::knife_edge_fresnel_intensity(1.0) >
          ure::wave::knife_edge_fresnel_intensity(0.0));
    CHECK(ure::wave::knife_edge_fresnel_intensity(1.0) > 1.0);
    return 0;
}

static int test_slit_diffraction_reference() {
    ure::wave::SlitAperture slit;
    slit.wavelength_m = 500.0e-9;
    slit.width_m = 10.0e-6;

    CHECK(ure::wave::is_valid(slit));
    CHECK_NEAR(ure::wave::slit_diffraction_intensity(slit, 0.0), 1.0, 0.0);

    const double first_zero = ure::wave::slit_first_zero_angle_rad(slit);
    CHECK_NEAR(first_zero, std::asin(slit.wavelength_m / slit.width_m), 0.0);
    CHECK_NEAR(ure::wave::slit_diffraction_intensity(slit, first_zero), 0.0, 1.0e-30);
    CHECK_NEAR(ure::wave::slit_diffraction_intensity(slit, 0.25 * first_zero),
               ure::wave::slit_diffraction_intensity(slit, -0.25 * first_zero),
               0.0);

    ure::wave::SlitAperture red = slit;
    red.wavelength_m = 700.0e-9;
    CHECK(ure::wave::slit_first_zero_angle_rad(red) >
          ure::wave::slit_first_zero_angle_rad(slit));

    slit.width_m = 0.0;
    CHECK(!ure::wave::is_valid(slit));
    CHECK_NEAR(ure::wave::slit_diffraction_intensity(slit, 0.0), 0.0, 0.0);
    CHECK_NEAR(ure::wave::slit_first_zero_angle_rad(slit), 0.0, 0.0);
    return 0;
}

static int test_rectangular_aperture_reference() {
    ure::wave::RectangularAperture aperture;
    aperture.wavelength_m = 500.0e-9;
    aperture.width_m = 20.0e-6;
    aperture.height_m = 10.0e-6;

    CHECK(ure::wave::is_valid(aperture));
    CHECK_NEAR(ure::wave::rectangular_aperture_intensity(aperture, 0.0, 0.0), 1.0, 0.0);

    const double x_zero = std::asin(aperture.wavelength_m / aperture.width_m);
    const double y_zero = std::asin(aperture.wavelength_m / aperture.height_m);
    CHECK_NEAR(ure::wave::rectangular_aperture_intensity(aperture, x_zero, 0.0), 0.0, 1.0e-30);
    CHECK_NEAR(ure::wave::rectangular_aperture_intensity(aperture, 0.0, y_zero), 0.0, 1.0e-30);

    ure::wave::SlitAperture x_slit;
    x_slit.wavelength_m = aperture.wavelength_m;
    x_slit.width_m = aperture.width_m;
    ure::wave::SlitAperture y_slit;
    y_slit.wavelength_m = aperture.wavelength_m;
    y_slit.width_m = aperture.height_m;
    const double tx = 0.25 * x_zero;
    const double ty = 0.25 * y_zero;
    CHECK_NEAR(ure::wave::rectangular_aperture_intensity(aperture, tx, ty),
               ure::wave::slit_diffraction_intensity(x_slit, tx) *
                   ure::wave::slit_diffraction_intensity(y_slit, ty),
               1.0e-15);

    aperture.height_m = 0.0;
    CHECK(!ure::wave::is_valid(aperture));
    CHECK_NEAR(ure::wave::rectangular_aperture_intensity(aperture, 0.0, 0.0), 0.0, 0.0);
    return 0;
}

static int test_grating_order_reference() {
    ure::wave::DiffractionGrating grating;
    grating.wavelength_m = 500.0e-9;
    grating.period_m = 2.0e-6;
    grating.slit_width_m = 0.5e-6;
    grating.slit_count = 32;

    CHECK(ure::wave::is_valid(grating));
    const auto zero = ure::wave::grating_order(grating, 0);
    CHECK(zero.propagating);
    CHECK_NEAR(zero.angle_rad, 0.0, 0.0);
    CHECK_NEAR(zero.relative_intensity, 1.0, 0.0);

    const auto plus_two = ure::wave::grating_order(grating, 2);
    CHECK(plus_two.propagating);
    CHECK_NEAR(std::sin(plus_two.angle_rad), 0.5, 1.0e-15);
    CHECK_NEAR(plus_two.relative_intensity, 4.0 / (std::numbers::pi * std::numbers::pi), 1.0e-15);

    const auto minus_two = ure::wave::grating_order(grating, -2);
    CHECK(minus_two.propagating);
    CHECK_NEAR(minus_two.angle_rad, -plus_two.angle_rad, 1.0e-15);
    CHECK_NEAR(minus_two.relative_intensity, plus_two.relative_intensity, 1.0e-15);

    const auto cutoff = ure::wave::grating_order(grating, 4);
    CHECK(cutoff.propagating);
    CHECK_NEAR(cutoff.angle_rad, std::numbers::pi / 2.0, 1.0e-15);

    const auto evanescent = ure::wave::grating_order(grating, 5);
    CHECK(!evanescent.propagating);
    CHECK_NEAR(evanescent.relative_intensity, 0.0, 0.0);

    const auto orders = ure::wave::grating_orders(grating, -5, 5);
    CHECK(orders.size() == 11);
    CHECK(!orders.front().propagating);
    CHECK(!orders.back().propagating);
    CHECK(orders[5].order == 0);
    CHECK(orders[5].propagating);

    grating.slit_width_m = grating.period_m * 2.0;
    CHECK(!ure::wave::is_valid(grating));
    CHECK(ure::wave::grating_orders(grating, -1, 1).empty());
    return 0;
}

static int test_circular_pupil_function_defocus_phase() {
    ure::wave::CircularPupil pupil;
    pupil.aperture.wavelength_m = 550.0e-9;
    pupil.aperture.aperture_diameter_m = 2.0e-3;
    pupil.aperture.focal_length_m = 35.0e-3;
    pupil.defocus_waves_at_edge = 0.25;

    CHECK(ure::wave::is_valid(pupil));
    const auto center = ure::wave::sample_circular_pupil(pupil, 0.0, 0.0);
    CHECK_NEAR(center.real, 1.0, 0.0);
    CHECK_NEAR(center.imag, 0.0, 0.0);
    CHECK_NEAR(center.power(), 1.0, 0.0);

    const double radius = 0.5 * pupil.aperture.aperture_diameter_m;
    const auto edge = ure::wave::sample_circular_pupil(pupil, radius, 0.0);
    CHECK_NEAR(edge.real, 0.0, 1.0e-15);
    CHECK_NEAR(edge.imag, 1.0, 1.0e-15);
    CHECK_NEAR(edge.power(), 1.0, 1.0e-15);

    const auto outside = ure::wave::sample_circular_pupil(pupil, 1.01 * radius, 0.0);
    CHECK_NEAR(outside.real, 0.0, 0.0);
    CHECK_NEAR(outside.imag, 0.0, 0.0);
    CHECK_NEAR(outside.power(), 0.0, 0.0);

    const auto a = ure::wave::sample_circular_pupil(pupil, 0.3 * radius, 0.4 * radius);
    const auto b = ure::wave::sample_circular_pupil(pupil, -0.4 * radius, 0.3 * radius);
    CHECK_NEAR(a.real, b.real, 1.0e-15);
    CHECK_NEAR(a.imag, b.imag, 1.0e-15);
    CHECK_NEAR(a.power(), 1.0, 1.0e-15);

    pupil.aperture.aperture_diameter_m = 0.0;
    CHECK(!ure::wave::is_valid(pupil));
    const auto invalid = ure::wave::sample_circular_pupil(pupil, 0.0, 0.0);
    CHECK_NEAR(invalid.power(), 0.0, 0.0);
    return 0;
}

static int test_circular_pupil_wave_field_grid() {
    ure::wave::CircularPupil pupil;
    pupil.aperture.wavelength_m = 550.0e-9;
    pupil.aperture.aperture_diameter_m = 2.0e-3;
    pupil.aperture.focal_length_m = 35.0e-3;
    pupil.defocus_waves_at_edge = 0.25;

    const int samples = 9;
    const auto field = ure::wave::make_circular_pupil_field(pupil, samples);
    CHECK(field.width == samples);
    CHECK(field.height == samples);
    CHECK(field.samples.size() == static_cast<size_t>(samples * samples));
    CHECK_NEAR(field.sample_pitch_m, pupil.aperture.aperture_diameter_m / static_cast<double>(samples), 0.0);
    CHECK_NEAR(field.wavelength_m, pupil.aperture.wavelength_m, 0.0);

    const auto center = field.at(samples / 2, samples / 2);
    CHECK_NEAR(center.real, 1.0, 0.0);
    CHECK_NEAR(center.imag, 0.0, 0.0);
    CHECK_NEAR(center.power(), 1.0, 0.0);

    int inside = 0;
    for (const auto& sample : field.samples) {
        if (sample.power() > 0.0) ++inside;
        CHECK_NEAR(sample.power(), sample.power() > 0.0 ? 1.0 : 0.0, 1.0e-15);
    }
    CHECK_NEAR(field.total_power(), static_cast<double>(inside), 1.0e-12);
    CHECK(field.at(0, 0).power() == 0.0);
    CHECK(field.at(-1, 0).power() == 0.0);
    CHECK(field.at(samples, samples).power() == 0.0);

    const auto a = field.at(2, 4);
    const auto b = field.at(6, 4);
    CHECK_NEAR(a.real, b.real, 1.0e-15);
    CHECK_NEAR(a.imag, b.imag, 1.0e-15);

    pupil.aperture.wavelength_m = 0.0;
    const auto invalid = ure::wave::make_circular_pupil_field(pupil, samples);
    CHECK(invalid.width == 0);
    CHECK(invalid.samples.empty());
    return 0;
}

static int test_fraunhofer_direct_uniform_field_oracle() {
    ure::wave::WaveFieldGrid field;
    field.width = 4;
    field.height = 4;
    field.sample_pitch_m = 2.0e-6;
    field.wavelength_m = 550.0e-9;
    field.samples.assign(16, {1.0, 0.0});

    const auto far_field = ure::wave::propagate_fraunhofer_direct(field);
    CHECK(far_field.width == 4);
    CHECK(far_field.height == 4);
    CHECK(far_field.amplitudes.size() == 16);
    CHECK_NEAR(far_field.frequency_pitch_x_cycles_per_m, 1.0 / (4.0 * field.sample_pitch_m), 0.0);
    CHECK_NEAR(far_field.frequency_pitch_y_cycles_per_m, 1.0 / (4.0 * field.sample_pitch_m), 0.0);
    CHECK_NEAR(far_field.wavelength_m, field.wavelength_m, 0.0);

    const auto center = far_field.at(2, 2);
    CHECK_NEAR(center.real, 16.0, 1.0e-12);
    CHECK_NEAR(center.imag, 0.0, 1.0e-12);
    CHECK_NEAR(far_field.intensity_at(2, 2), 256.0, 1.0e-9);

    for (int y = 0; y < far_field.height; ++y) {
        for (int x = 0; x < far_field.width; ++x) {
            if (x == 2 && y == 2) continue;
            CHECK_NEAR(far_field.intensity_at(x, y), 0.0, 1.0e-24);
        }
    }
    CHECK_NEAR(far_field.total_power(), 256.0, 1.0e-9);
    CHECK_NEAR(far_field.intensity_at(-1, 0), 0.0, 0.0);

    field.samples.pop_back();
    const auto invalid = ure::wave::propagate_fraunhofer_direct(field);
    CHECK(invalid.width == 0);
    CHECK(invalid.amplitudes.empty());
    return 0;
}

static int test_fraunhofer_direct_circular_pupil_peak() {
    ure::wave::CircularPupil pupil;
    pupil.aperture.wavelength_m = 550.0e-9;
    pupil.aperture.aperture_diameter_m = 2.0e-3;
    pupil.aperture.focal_length_m = 35.0e-3;

    const auto field = ure::wave::make_circular_pupil_field(pupil, 9);
    const auto far_field = ure::wave::propagate_fraunhofer_direct(field);
    CHECK(far_field.width == 9);
    CHECK(far_field.height == 9);
    CHECK(far_field.total_power() > 0.0);

    const double center = far_field.intensity_at(4, 4);
    CHECK(center > far_field.intensity_at(5, 4));
    CHECK(center > far_field.intensity_at(4, 5));
    CHECK_NEAR(far_field.intensity_at(5, 4), far_field.intensity_at(3, 4), 1.0e-9);
    CHECK_NEAR(far_field.intensity_at(4, 5), far_field.intensity_at(4, 3), 1.0e-9);
    CHECK_NEAR(far_field.total_power(),
               static_cast<double>(field.width * field.height) * field.total_power(),
               1.0e-8);
    return 0;
}

static int test_fresnel_direct_point_field_scale() {
    ure::wave::WaveFieldGrid field;
    field.width = 3;
    field.height = 3;
    field.sample_pitch_m = 1.0e-6;
    field.wavelength_m = 500.0e-9;
    field.samples.assign(9, {0.0, 0.0});
    field.samples[4] = {1.0, 0.0};

    ure::wave::FresnelPropagationConfig config;
    config.distance_m = 0.1;
    config.output_width = 5;
    config.output_height = 5;
    config.output_sample_pitch_m = 2.0e-6;

    const auto propagated = ure::wave::propagate_fresnel_direct(field, config);
    CHECK(propagated.width == 5);
    CHECK(propagated.height == 5);
    CHECK(propagated.samples.size() == 25);
    CHECK_NEAR(propagated.sample_pitch_m, config.output_sample_pitch_m, 0.0);
    CHECK_NEAR(propagated.wavelength_m, field.wavelength_m, 0.0);

    const double scale = field.sample_pitch_m * field.sample_pitch_m /
                         (field.wavelength_m * config.distance_m);
    const double expected_intensity = scale * scale;
    for (int y = 0; y < propagated.height; ++y) {
        for (int x = 0; x < propagated.width; ++x) {
            CHECK_NEAR(propagated.at(x, y).power(), expected_intensity, 1.0e-24);
        }
    }

    config.distance_m = 0.0;
    CHECK(ure::wave::propagate_fresnel_direct(field, config).samples.empty());
    return 0;
}

static int test_angular_spectrum_zero_distance_reconstructs_field() {
    ure::wave::WaveFieldGrid field;
    field.width = 4;
    field.height = 4;
    field.sample_pitch_m = 2.0e-6;
    field.wavelength_m = 550.0e-9;
    field.samples.reserve(16);
    for (int i = 0; i < 16; ++i) {
        field.samples.push_back({0.25 * static_cast<double>(i + 1),
                                 -0.125 * static_cast<double>(i % 5)});
    }

    const auto propagated = ure::wave::propagate_angular_spectrum_direct(field, 0.0);
    CHECK(propagated.width == field.width);
    CHECK(propagated.height == field.height);
    CHECK(propagated.samples.size() == field.samples.size());
    for (int y = 0; y < field.height; ++y) {
        for (int x = 0; x < field.width; ++x) {
            const auto expected = field.at(x, y);
            const auto actual = propagated.at(x, y);
            CHECK_NEAR(actual.real, expected.real, 1.0e-12);
            CHECK_NEAR(actual.imag, expected.imag, 1.0e-12);
        }
    }

    field.sample_pitch_m = 0.0;
    CHECK(ure::wave::propagate_angular_spectrum_direct(field, 0.0).samples.empty());
    return 0;
}

static int test_angular_spectrum_uniform_field_preserves_intensity() {
    ure::wave::WaveFieldGrid field;
    field.width = 4;
    field.height = 4;
    field.sample_pitch_m = 2.0e-6;
    field.wavelength_m = 550.0e-9;
    field.samples.assign(16, {1.0, 0.0});

    const auto propagated = ure::wave::propagate_angular_spectrum_direct(field, 0.25);
    CHECK(propagated.width == 4);
    CHECK(propagated.height == 4);
    for (int y = 0; y < propagated.height; ++y) {
        for (int x = 0; x < propagated.width; ++x) {
            CHECK_NEAR(propagated.at(x, y).power(), 1.0, 1.0e-12);
        }
    }
    CHECK_NEAR(propagated.total_power(), field.total_power(), 1.0e-10);
    return 0;
}

static int test_huygens_fresnel_and_rayleigh_sommerfeld_point_field() {
    ure::wave::WaveFieldGrid field;
    field.width = 3;
    field.height = 3;
    field.sample_pitch_m = 1.0e-6;
    field.wavelength_m = 500.0e-9;
    field.samples.assign(9, {0.0, 0.0});
    field.samples[4] = {1.0, 0.0};

    ure::wave::FresnelPropagationConfig config;
    config.distance_m = 0.1;
    config.output_width = 5;
    config.output_height = 5;
    config.output_sample_pitch_m = 2.0e-6;

    const auto hf = ure::wave::propagate_huygens_fresnel_direct(field, config);
    const auto rs = ure::wave::propagate_rayleigh_sommerfeld_direct(field, config);
    CHECK(hf.width == 5);
    CHECK(rs.width == 5);
    CHECK(hf.samples.size() == 25);
    CHECK(rs.samples.size() == 25);

    const double center_scale = field.sample_pitch_m * field.sample_pitch_m /
                                (field.wavelength_m * config.distance_m);
    CHECK_NEAR(hf.at(2, 2).power(), center_scale * center_scale, 1.0e-24);
    CHECK_NEAR(rs.at(2, 2).power(), center_scale * center_scale, 1.0e-24);

    const double corner_x = (0.5 - 2.5) * config.output_sample_pitch_m;
    const double corner_y = (0.5 - 2.5) * config.output_sample_pitch_m;
    const double r = std::sqrt(corner_x * corner_x + corner_y * corner_y +
                               config.distance_m * config.distance_m);
    const double hf_corner_scale = field.sample_pitch_m * field.sample_pitch_m /
                                   (field.wavelength_m * r);
    const double obliquity = config.distance_m / r;
    CHECK_NEAR(hf.at(0, 0).power(), hf_corner_scale * hf_corner_scale, 1.0e-24);
    CHECK_NEAR(rs.at(0, 0).power(),
               hf_corner_scale * hf_corner_scale * obliquity * obliquity,
               1.0e-24);
    CHECK(rs.at(0, 0).power() <= hf.at(0, 0).power());

    config.distance_m = 0.0;
    CHECK(ure::wave::propagate_huygens_fresnel_direct(field, config).samples.empty());
    CHECK(ure::wave::propagate_rayleigh_sommerfeld_direct(field, config).samples.empty());
    return 0;
}

static int test_propagation_operator_dispatches_supported_oracles() {
    ure::wave::WaveFieldGrid field;
    field.width = 4;
    field.height = 4;
    field.sample_pitch_m = 2.0e-6;
    field.wavelength_m = 550.0e-9;
    field.samples.assign(16, {1.0, 0.0});

    ure::wave::PropagationConfig config;
    config.kind = ure::wave::PropagationOperatorKind::Fraunhofer;
    auto result = ure::wave::propagate_direct(field, config);
    CHECK(result.status == ure::wave::PropagationStatus::Ready);
    CHECK(ure::wave::is_ready(result.status));
    CHECK(result.kind == ure::wave::PropagationOperatorKind::Fraunhofer);
    CHECK(result.far_field.width == 4);
    CHECK_NEAR(result.far_field.intensity_at(2, 2), 256.0, 1.0e-9);
    CHECK(result.field.samples.empty());

    config.kind = ure::wave::PropagationOperatorKind::Fresnel;
    config.distance_m = 0.1;
    config.output_width = 5;
    config.output_height = 5;
    config.output_sample_pitch_m = 3.0e-6;
    result = ure::wave::propagate_direct(field, config);
    CHECK(result.status == ure::wave::PropagationStatus::Ready);
    CHECK(result.kind == ure::wave::PropagationOperatorKind::Fresnel);
    CHECK(result.field.width == 5);
    CHECK(result.field.height == 5);
    CHECK_NEAR(result.field.sample_pitch_m, 3.0e-6, 0.0);
    CHECK(result.far_field.amplitudes.empty());

    config.kind = ure::wave::PropagationOperatorKind::AngularSpectrum;
    config.distance_m = 0.0;
    result = ure::wave::propagate_direct(field, config);
    CHECK(result.status == ure::wave::PropagationStatus::Ready);
    CHECK(result.kind == ure::wave::PropagationOperatorKind::AngularSpectrum);
    CHECK(result.field.width == field.width);
    CHECK_NEAR(result.field.at(0, 0).real, 1.0, 1.0e-12);
    CHECK_NEAR(result.field.at(0, 0).imag, 0.0, 1.0e-12);

    config.kind = ure::wave::PropagationOperatorKind::RayleighSommerfeld;
    config.distance_m = 0.1;
    config.output_width = 3;
    config.output_height = 3;
    config.output_sample_pitch_m = 2.0e-6;
    result = ure::wave::propagate_direct(field, config);
    CHECK(result.status == ure::wave::PropagationStatus::Ready);
    CHECK(result.kind == ure::wave::PropagationOperatorKind::RayleighSommerfeld);
    CHECK(result.field.width == 3);
    CHECK(result.field.height == 3);

    config.kind = ure::wave::PropagationOperatorKind::HuygensFresnel;
    result = ure::wave::propagate_direct(field, config);
    CHECK(result.status == ure::wave::PropagationStatus::Ready);
    CHECK(result.kind == ure::wave::PropagationOperatorKind::HuygensFresnel);
    CHECK(result.field.width == 3);
    CHECK(result.field.height == 3);
    return 0;
}

static int test_propagation_operator_rejects_invalid_inputs() {
    ure::wave::WaveFieldGrid field;
    field.width = 4;
    field.height = 4;
    field.sample_pitch_m = 2.0e-6;
    field.wavelength_m = 550.0e-9;
    field.samples.assign(16, {1.0, 0.0});

    ure::wave::PropagationConfig config;
    config.kind = ure::wave::PropagationOperatorKind::Fresnel;
    config.distance_m = 0.0;
    auto result = ure::wave::propagate_direct(field, config);
    CHECK(result.status == ure::wave::PropagationStatus::InvalidInput);
    CHECK(!ure::wave::is_ready(result.status));
    CHECK(result.field.samples.empty());

    config.kind = ure::wave::PropagationOperatorKind::RayleighSommerfeld;
    result = ure::wave::propagate_direct(field, config);
    CHECK(result.status == ure::wave::PropagationStatus::InvalidInput);

    config.kind = ure::wave::PropagationOperatorKind::HuygensFresnel;
    result = ure::wave::propagate_direct(field, config);
    CHECK(result.status == ure::wave::PropagationStatus::InvalidInput);

    field.samples.pop_back();
    config.kind = ure::wave::PropagationOperatorKind::AngularSpectrum;
    result = ure::wave::propagate_direct(field, config);
    CHECK(result.status == ure::wave::PropagationStatus::InvalidInput);
    CHECK(result.field.samples.empty());
    CHECK(result.far_field.amplitudes.empty());
    return 0;
}

static int test_complex_spectrum_optical_path_phase_accumulation() {
    ure::wave::ComplexSpectrum spectrum;
    spectrum.wavelengths_m = {500.0e-9, 1000.0e-9};
    spectrum.amplitudes = {{1.0, 0.0}, {1.0, 0.0}};
    spectrum.coherence = {7, 11, 13, 1.0e-3, true};

    CHECK(ure::wave::is_valid(spectrum));
    CHECK(spectrum.size() == 2);
    CHECK_NEAR(spectrum.total_power(), 2.0, 0.0);

    const auto shifted = ure::wave::apply_optical_path_phase(spectrum, 250.0e-9);
    CHECK(ure::wave::is_valid(shifted));
    CHECK_NEAR(shifted.optical_path_length_m, 250.0e-9, 0.0);
    CHECK_NEAR(shifted.at(0).real, -1.0, 1.0e-12);
    CHECK_NEAR(shifted.at(0).imag, 0.0, 1.0e-12);
    CHECK_NEAR(shifted.at(1).real, 0.0, 1.0e-12);
    CHECK_NEAR(shifted.at(1).imag, 1.0, 1.0e-12);
    CHECK_NEAR(shifted.total_power(), spectrum.total_power(), 1.0e-12);
    CHECK(shifted.coherence.group_id == 11);
    CHECK(shifted.coherence.realization_id == 13);

    const auto invalid_lane = shifted.at(7);
    CHECK_NEAR(invalid_lane.power(), 0.0, 0.0);

    spectrum.coherence.coherence_length_m = -1.0;
    CHECK(!ure::wave::is_valid(spectrum));
    CHECK(ure::wave::apply_optical_path_phase(spectrum, 1.0e-6).amplitudes.empty());
    return 0;
}

static int test_jones_field_optical_path_phase_preserves_polarized_power() {
    ure::wave::JonesSpectrum spectrum;
    spectrum.wavelengths_m = {600.0e-9};
    spectrum.fields = {{{1.0, 0.0}, {0.0, 1.0}}};
    spectrum.coherence = {1, 2, 3, 2.0e-3, true};

    CHECK(ure::wave::is_valid(spectrum));
    CHECK_NEAR(spectrum.total_power(), 2.0, 0.0);

    const auto shifted = ure::wave::apply_optical_path_phase(spectrum, 150.0e-9);
    CHECK(ure::wave::is_valid(shifted));
    CHECK_NEAR(shifted.optical_path_length_m, 150.0e-9, 0.0);
    const auto field = shifted.at(0);
    CHECK_NEAR(field.x.real, 0.0, 1.0e-12);
    CHECK_NEAR(field.x.imag, 1.0, 1.0e-12);
    CHECK_NEAR(field.y.real, -1.0, 1.0e-12);
    CHECK_NEAR(field.y.imag, 0.0, 1.0e-12);
    CHECK_NEAR(field.power(), 2.0, 1.0e-12);
    CHECK_NEAR(shifted.at(9).power(), 0.0, 0.0);
    return 0;
}

static int test_complex_field_film_coherent_and_incoherent_order() {
    auto film = ure::wave::make_complex_field_film(2, 1, {550.0e-9});
    CHECK(film.is_valid());
    CHECK(film.lane_count() == 1);

    film.add_coherent_sample(0, 0, 0, {1.0, 0.0});
    film.add_coherent_sample(0, 0, 0, {1.0, 0.0});
    film.add_incoherent_sample(0, 0, 0, 1.0);
    film.add_incoherent_sample(0, 0, 0, 1.0);
    CHECK_NEAR(film.coherent_amplitude_at(0, 0, 0).real, 2.0, 0.0);
    CHECK_NEAR(film.coherent_power_at(0, 0, 0), 4.0, 0.0);
    CHECK_NEAR(film.incoherent_power_at(0, 0, 0), 2.0, 0.0);
    CHECK_NEAR(film.resolved_power_at(0, 0, 0), 6.0, 0.0);

    film.add_coherent_sample(1, 0, 0, {1.0, 0.0});
    film.add_coherent_sample(1, 0, 0, {-1.0, 0.0});
    film.add_incoherent_sample(1, 0, 0, 1.0);
    film.add_incoherent_sample(1, 0, 0, 1.0);
    CHECK_NEAR(film.coherent_power_at(1, 0, 0), 0.0, 0.0);
    CHECK_NEAR(film.incoherent_power_at(1, 0, 0), 2.0, 0.0);
    CHECK_NEAR(film.resolved_power_at(1, 0, 0), 2.0, 0.0);

    film.add_coherent_sample(7, 0, 0, {8.0, 0.0});
    film.add_incoherent_sample(0, 0, 4, 8.0);
    film.add_incoherent_sample(0, 0, 0, -8.0);
    CHECK_NEAR(film.resolved_power_at(0, 0, 0), 6.0, 0.0);
    CHECK_NEAR(film.resolved_power_at(-1, 0, 0), 0.0, 0.0);

    CHECK(!ure::wave::make_complex_field_film(0, 1, {550.0e-9}).is_valid());
    CHECK(!ure::wave::make_complex_field_film(1, 1, {0.0}).is_valid());
    return 0;
}

static int test_diffraction_camera_plan_requires_feature_gate() {
    ure::wave::DiffractionCameraConfig camera;
    camera.pupil.aperture.wavelength_m = 550.0e-9;
    camera.pupil.aperture.aperture_diameter_m = 2.0e-3;
    camera.pupil.aperture.focal_length_m = 35.0e-3;

    ure::WaveOpticsConfig wave;
    auto plan = ure::wave::make_diffraction_camera_plan(wave, camera);
    CHECK(plan.status == ure::wave::DiffractionCameraPlanStatus::Disabled);
    CHECK(!ure::wave::is_ready(plan.status));
    CHECK(plan.psf.weights.empty());
    CHECK(plan.mtf.empty());

    wave.mode = ure::WaveOpticsMode::CameraDiffraction;
    plan = ure::wave::make_diffraction_camera_plan(wave, camera);
    CHECK(plan.status == ure::wave::DiffractionCameraPlanStatus::FeatureDisabled);
    CHECK(!ure::wave::is_ready(plan.status));

    wave.mode = ure::WaveOpticsMode::Radiometric;
    wave.camera_diffraction_enabled = true;
    plan = ure::wave::make_diffraction_camera_plan(wave, camera);
    CHECK(plan.status == ure::wave::DiffractionCameraPlanStatus::FeatureDisabled);
    CHECK(!ure::wave::is_ready(plan.status));
    return 0;
}

static int test_diffraction_camera_plan_builds_reference_products() {
    ure::wave::DiffractionCameraConfig camera;
    camera.pupil.aperture.wavelength_m = 550.0e-9;
    camera.pupil.aperture.aperture_diameter_m = 2.0e-3;
    camera.pupil.aperture.focal_length_m = 35.0e-3;
    camera.pupil.defocus_waves_at_edge = 0.5;
    camera.sensor_pixel_pitch_m = 2.5e-6;
    camera.psf_radius_pixels = 5;
    camera.mtf_sample_count = 17;

    ure::WaveOpticsConfig wave;
    wave.mode = ure::WaveOpticsMode::CameraDiffraction;
    wave.camera_diffraction_enabled = true;
    const auto plan = ure::wave::make_diffraction_camera_plan(wave, camera);

    CHECK(plan.status == ure::wave::DiffractionCameraPlanStatus::Ready);
    CHECK(ure::wave::is_ready(plan.status));
    CHECK_NEAR(plan.pupil.defocus_waves_at_edge, 0.5, 0.0);
    CHECK(plan.psf.width == 11);
    CHECK(plan.psf.height == 11);
    CHECK(plan.psf.weights.size() == 121);
    CHECK(plan.mtf.size() == 17);
    CHECK_NEAR(plan.mtf.front().value, 1.0, 0.0);
    CHECK_NEAR(plan.mtf.back().value, 0.0, 0.0);

    double sum = 0.0;
    for (double weight : plan.psf.weights) {
        sum += weight;
    }
    CHECK_NEAR(sum, 1.0, 1.0e-12);
    return 0;
}

static int test_diffraction_camera_plan_rejects_invalid_optics() {
    ure::wave::DiffractionCameraConfig camera;
    camera.pupil.aperture.wavelength_m = 550.0e-9;
    camera.pupil.aperture.aperture_diameter_m = 2.0e-3;
    camera.pupil.aperture.focal_length_m = 35.0e-3;
    camera.sensor_pixel_pitch_m = 0.0;

    ure::WaveOpticsConfig wave;
    wave.mode = ure::WaveOpticsMode::CameraDiffraction;
    wave.camera_diffraction_enabled = true;
    const auto plan = ure::wave::make_diffraction_camera_plan(wave, camera);
    CHECK(plan.status == ure::wave::DiffractionCameraPlanStatus::InvalidOptics);
    CHECK(!ure::wave::is_ready(plan.status));
    CHECK(plan.psf.weights.empty());
    CHECK(plan.mtf.empty());
    return 0;
}

static double psf_second_moment(
    const ure::wave::DiffractionPsfBank& bank,
    int wavelength_index) {
    double moment = 0.0;
    const int width = bank.kernel_width();
    for (int y = 0; y < width; ++y) {
        for (int x = 0; x < width; ++x) {
            const double dx =
                static_cast<double>(x - bank.radius_pixels);
            const double dy =
                static_cast<double>(y - bank.radius_pixels);
            moment +=
                (dx * dx + dy * dy) *
                bank.at(wavelength_index, x, y);
        }
    }
    return moment;
}

static int test_diffraction_psf_bank_normalization_and_scaling() {
    ure::WaveOpticsConfig config;
    config.mode = ure::WaveOpticsMode::CameraDiffraction;
    config.camera_diffraction_enabled = true;
    config.camera_aperture_diameter_m = 1.0e-3;
    config.camera_focal_length_m = 50.0e-3;
    config.sensor_pixel_pitch_m = 4.0e-6;
    config.camera_psf_radius_pixels = 8;
    config.camera_wavelength_bin_count = 8;
    const auto bank =
        ure::wave::make_diffraction_psf_bank(config);
    CHECK(bank.is_valid());
    CHECK(bank.kernel_width() == 17);
    CHECK(bank.weights.size() ==
          static_cast<std::size_t>(8 * 17 * 17));
    for (int wavelength = 0;
         wavelength < bank.wavelength_count;
         ++wavelength) {
        double sum = 0.0;
        for (int y = 0; y < bank.kernel_width(); ++y) {
            for (int x = 0; x < bank.kernel_width(); ++x) {
                const float weight =
                    bank.at(wavelength, x, y);
                CHECK(weight >= 0.0f);
                sum += weight;
            }
        }
        CHECK_NEAR(sum, 1.0, 2.0e-6);
    }
    CHECK(psf_second_moment(
              bank,
              bank.wavelength_count - 1) >
          psf_second_moment(bank, 0));
    return 0;
}

static int test_diffraction_psf_bank_blades_and_defocus() {
    ure::WaveOpticsConfig focused;
    focused.mode =
        ure::WaveOpticsMode::CameraDiffraction;
    focused.camera_diffraction_enabled = true;
    focused.camera_aperture_diameter_m = 1.2e-3;
    focused.camera_focal_length_m = 35.0e-3;
    focused.sensor_pixel_pitch_m = 3.5e-6;
    focused.camera_psf_radius_pixels = 5;
    focused.camera_wavelength_bin_count = 2;
    focused.camera_pupil_sample_count = 16;
    const auto circular =
        ure::wave::make_diffraction_psf_bank(focused);

    auto hexagonal = focused;
    hexagonal.camera_aperture_blade_count = 6;
    hexagonal.camera_aperture_rotation_rad = 0.2;
    const auto blade_bank =
        ure::wave::make_diffraction_psf_bank(hexagonal);
    CHECK(blade_bank.is_valid());
    double shape_difference = 0.0;
    for (std::size_t i = 0;
         i < circular.weights.size();
         ++i) {
        shape_difference += std::abs(
            static_cast<double>(circular.weights[i]) -
            static_cast<double>(blade_bank.weights[i]));
    }
    CHECK(shape_difference > 0.05);

    auto defocused = focused;
    defocused.camera_defocus_waves_at_edge = 1.0;
    const auto defocused_bank =
        ure::wave::make_diffraction_psf_bank(defocused);
    CHECK(defocused_bank.is_valid());
    const int center = focused.camera_psf_radius_pixels;
    CHECK(defocused_bank.at(0, center, center) <
          circular.at(0, center, center));
    return 0;
}

static int test_diffraction_psf_bank_invalid_fails_closed() {
    ure::WaveOpticsConfig config;
    config.mode =
        ure::WaveOpticsMode::CameraDiffraction;
    config.camera_diffraction_enabled = true;
    config.camera_aperture_blade_count = 2;
    CHECK(!ure::wave::is_valid_diffraction_camera_config(
        config));
    CHECK(ure::wave::make_diffraction_psf_bank(
              config).weights.empty());
    config.camera_aperture_blade_count = 0;
    config.camera_wavelength_bin_count = 33;
    CHECK(!ure::wave::is_valid_diffraction_camera_config(
        config));
    return 0;
}

static int test_diffractive_analytic_orders() {
    ure::scene_ir::DiffractiveOperator grating;
    grating.kind =
        ure::scene_ir::DiffractiveOperatorKind::Grating;
    grating.period_m = 1.0e-6;
    grating.duty_cycle = 0.5;
    grating.max_order = 3;
    CHECK(ure::wave::is_valid(grating));
    const auto orders =
        ure::wave::diffractive_orders(
            grating,
            550.0,
            0.0);
    CHECK(orders.size() == 7);
    double energy = 0.0;
    bool found_positive_first = false;
    bool found_evanescent = false;
    for (const auto& order : orders) {
        energy += order.unpolarized_efficiency;
        if (order.order == 1) {
            found_positive_first = true;
            CHECK(order.propagating);
            CHECK_NEAR(
                order.tangential_sine,
                0.55,
                1.0e-12);
        }
        if (order.order == 2) {
            found_evanescent = true;
            CHECK(!order.propagating);
            CHECK(order.evanescent_decay_per_m > 0.0);
            CHECK_NEAR(
                order.unpolarized_efficiency,
                0.0,
                0.0);
        }
    }
    CHECK(found_positive_first);
    CHECK(found_evanescent);
    CHECK_NEAR(energy, 0.5, 1.0e-12);

    ure::scene_ir::DiffractiveOperator phase_mask;
    phase_mask.kind =
        ure::scene_ir::DiffractiveOperatorKind::
            PhaseMask;
    phase_mask.phase_depth_rad = 1.7;
    phase_mask.max_order = 5;
    const auto phase_orders =
        ure::wave::diffractive_orders(
            phase_mask,
            500.0,
            0.0);
    double phase_energy = 0.0;
    for (const auto& order : phase_orders) {
        phase_energy += order.unpolarized_efficiency;
    }
    CHECK_NEAR(phase_energy, 1.0, 1.0e-12);

    ure::scene_ir::DiffractiveOperator zone_plate;
    zone_plate.kind =
        ure::scene_ir::DiffractiveOperatorKind::
            ZonePlate;
    zone_plate.max_order = 2;
    const auto zone_orders =
        ure::wave::diffractive_orders(
            zone_plate,
            550.0,
            0.0,
            0.002);
    double zone_energy = 0.0;
    for (const auto& order : zone_orders) {
        zone_energy += order.unpolarized_efficiency;
        if (order.order == 1) {
            CHECK(order.tangential_sine < 0.0);
        }
    }
    CHECK_NEAR(zone_energy, 1.0, 1.0e-12);
    return 0;
}

static int test_diffractive_scattering_table_contract() {
    ure::scene_ir::DiffractiveOperator table;
    table.kind =
        ure::scene_ir::DiffractiveOperatorKind::
            ScatteringTable;
    table.table_id = "rcwa/interpolation";
    table.max_order = 0;
    for (const float wavelength : {500.0f, 600.0f}) {
        ure::scene_ir::DiffractiveScatteringEntry entry;
        entry.wavelength_nm = wavelength;
        entry.incident_cosine = 1.0f;
        entry.order = 0;
        entry.jones_ss.real =
            wavelength == 500.0f ? 0.2f : 0.6f;
        entry.jones_pp.real =
            entry.jones_ss.real;
        table.table.push_back(entry);
    }
    CHECK(ure::wave::is_valid(table));
    const auto interpolated =
        ure::wave::diffractive_orders(
            table,
            550.0,
            0.0);
    CHECK(interpolated.size() == 1);
    CHECK_NEAR(
        interpolated[0].amplitude.ss.real,
        0.4,
        1.0e-7);
    CHECK_NEAR(
        interpolated[0].unpolarized_efficiency,
        0.16,
        1.0e-7);

    ure::scene_ir::DiffractiveOperator polarized;
    polarized.kind =
        ure::scene_ir::DiffractiveOperatorKind::
            ScatteringTable;
    polarized.table_id = "rcwa/non-passive";
    polarized.max_order = 0;
    for (int side = 0; side < 2; ++side) {
        ure::scene_ir::DiffractiveScatteringEntry entry;
        entry.wavelength_nm = 550.0f;
        entry.incident_cosine = 1.0f;
        entry.order = 0;
        entry.side =
            static_cast<
                ure::scene_ir::DiffractiveScatterSide>(
                side);
        entry.jones_ss.real = 0.8f;
        polarized.table.push_back(entry);
    }
    CHECK(!ure::wave::is_valid(polarized));

    auto incomplete = table;
    incomplete.table.push_back(incomplete.table[0]);
    incomplete.table.back().order = 1;
    incomplete.max_order = 1;
    CHECK(!ure::wave::is_valid(incomplete));

    auto oversized = table;
    oversized.table.resize(
        ure::scene_ir::kMaxDiffractiveScatteringEntries +
        1,
        oversized.table.front());
    CHECK(!ure::wave::is_valid(oversized));
    return 0;
}

static int test_diffractive_config_boundary() {
    ure::RenderConfig config;
    CHECK(!ure::wave::
              is_supported_diffractive_material_config(
                  config));
    config.wave_optics.diffractive_materials_enabled =
        true;
    CHECK(ure::wave::
              is_supported_diffractive_material_config(
                  config));
    config.restir_di.enabled = true;
    CHECK(!ure::wave::
              is_supported_diffractive_material_config(
                  config));
    return 0;
}

static ure::scene_ir::FluorescenceResource
fluorescence_fixture() {
    ure::scene_ir::FluorescenceResource resource;
    resource.resource_id = "fluorescence/reference";
    resource.excitation_wavelengths_nm = {
        400.0f,
        500.0f};
    resource.emission_wavelengths_nm = {
        600.0f,
        700.0f};
    resource.excitation_efficiency = {
        0.8f,
        0.6f};
    resource.quantum_yield = {
        0.5f,
        0.4f};
    resource.emission_pdf_per_nm = {
        0.01f,
        0.01f,
        0.01f,
        0.01f};
    resource.lifetime_seconds = 0.002;
    return resource;
}

static int test_fluorescence_resource_and_sampling() {
    const auto resource = fluorescence_fixture();
    CHECK(ure::wave::is_valid(resource));
    CHECK_NEAR(
        ure::wave::fluorescence_emission_pdf(
            resource,
            450.0,
            650.0),
        0.01,
        1.0e-9);
    CHECK_NEAR(
        ure::wave::fluorescence_emission_pdf(
            resource,
            550.0,
            650.0),
        0.0,
        0.0);
    const auto sample =
        ure::wave::sample_fluorescence(
            resource,
            450.0,
            0.25,
            0.25,
            1.0 - std::exp(-1.0));
    CHECK_NEAR(
        sample.emission_wavelength_nm,
        625.0,
        1.0e-5);
    CHECK_NEAR(
        sample.emission_pdf_per_nm,
        0.01,
        1.0e-9);
    CHECK_NEAR(
        sample.excitation_efficiency,
        0.7,
        1.0e-7);
    CHECK_NEAR(
        sample.quantum_yield,
        0.45,
        1.0e-7);
    CHECK_NEAR(
        sample.radiant_energy_scale,
        0.7 * 0.45 * 450.0 / 625.0,
        1.0e-7);
    CHECK(sample.radiant_energy_scale <=
          sample.excitation_efficiency *
              sample.quantum_yield);
    CHECK_NEAR(
        sample.delay_seconds,
        resource.lifetime_seconds,
        1.0e-12);
    const auto adjoint =
        ure::wave::sample_fluorescence_adjoint(
            resource,
            650.0,
            0.5,
            1.0 - std::exp(-1.0));
    CHECK(adjoint.excitation_wavelength_nm >= 400.0);
    CHECK(adjoint.excitation_wavelength_nm <= 500.0);
    CHECK(adjoint.excitation_wavelength_nm < 650.0);
    CHECK(adjoint.transition_pdf_per_nm > 0.0);
    CHECK(adjoint.kernel_density_per_nm > 0.0);
    CHECK_NEAR(
        adjoint.estimator_weight,
        0.5 *
            (0.8 * 0.5 * 400.0 / 650.0 *
                 0.01 +
             0.6 * 0.4 * 500.0 / 650.0 *
                 0.01) *
            100.0,
        1.0e-7);
    CHECK_NEAR(
        adjoint.delay_seconds,
        resource.lifetime_seconds,
        1.0e-12);
    return 0;
}

static int test_fluorescence_resource_rejects_invalid_energy() {
    auto resource = fluorescence_fixture();
    resource.quantum_yield[0] = 1.01f;
    CHECK(!ure::wave::is_valid(resource));
    resource = fluorescence_fixture();
    resource.emission_pdf_per_nm[0] = 0.02f;
    CHECK(!ure::wave::is_valid(resource));
    resource = fluorescence_fixture();
    resource.emission_wavelengths_nm[0] = 450.0f;
    CHECK(!ure::wave::is_valid(resource));
    resource = fluorescence_fixture();
    resource.emission_wavelengths_nm = {
        490.0f,
        510.0f};
    resource.emission_pdf_per_nm = {
        0.0f,
        0.1f,
        0.0f,
        0.1f};
    CHECK(!ure::wave::is_valid(resource));
    resource = fluorescence_fixture();
    resource.lifetime_seconds =
        std::numeric_limits<double>::max();
    CHECK(!ure::wave::is_valid(resource));
    resource = fluorescence_fixture();
    resource.emission_pdf_per_nm.resize(
        ure::scene_ir::kMaxFluorescenceMatrixEntries +
        1,
        0.0f);
    CHECK(!ure::wave::is_valid(resource));
    return 0;
}

static int test_fluorescence_config_boundary() {
    ure::RenderConfig config;
    CHECK(!ure::wave::
              is_supported_fluorescence_config(
                  config));
    config.wave_optics.fluorescence_enabled =
        true;
    CHECK(ure::wave::
              is_supported_fluorescence_config(
                  config));
    config.mlt.enabled = true;
    CHECK(!ure::wave::
              is_supported_fluorescence_config(
                  config));
    return 0;
}

static int test_gpu_renderer_rejects_unsupported_wave_combination() {
    ure::RenderConfig config;
    config.wave_optics.mode = ure::WaveOpticsMode::CameraDiffraction;
    config.wave_optics.camera_diffraction_enabled = true;
    config.wave_optics.coherent_field_enabled = true;
    auto engine = ure::RenderEngineFactory::create_gpu_renderer(config);

    bool rejected = false;
    try {
        ure::scene_ir::SceneIR scene;
        engine->load_scene_ir(scene);
    } catch (const std::runtime_error& e) {
        rejected =
            std::string(e.what()).find(
                "unsupported") != std::string::npos;
    }
    CHECK(rejected);

    int width = -1;
    int height = -1;
    engine->get_framebuffer_size(width, height);
    CHECK(width == 0);
    CHECK(height == 0);
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
    failed += run("test_circular_airy_psf_kernel", test_circular_airy_psf_kernel);
    failed += run("test_invalid_psf_kernel_fails_closed", test_invalid_psf_kernel_fails_closed);
    failed += run("test_circular_aperture_mtf_oracle", test_circular_aperture_mtf_oracle);
    failed += run("test_circular_aperture_mtf_samples", test_circular_aperture_mtf_samples);
    failed += run("test_knife_edge_fresnel_reference", test_knife_edge_fresnel_reference);
    failed += run("test_slit_diffraction_reference", test_slit_diffraction_reference);
    failed += run("test_rectangular_aperture_reference", test_rectangular_aperture_reference);
    failed += run("test_grating_order_reference", test_grating_order_reference);
    failed += run("test_circular_pupil_function_defocus_phase", test_circular_pupil_function_defocus_phase);
    failed += run("test_circular_pupil_wave_field_grid", test_circular_pupil_wave_field_grid);
    failed += run("test_fraunhofer_direct_uniform_field_oracle", test_fraunhofer_direct_uniform_field_oracle);
    failed += run("test_fraunhofer_direct_circular_pupil_peak", test_fraunhofer_direct_circular_pupil_peak);
    failed += run("test_fresnel_direct_point_field_scale", test_fresnel_direct_point_field_scale);
    failed += run("test_angular_spectrum_zero_distance_reconstructs_field", test_angular_spectrum_zero_distance_reconstructs_field);
    failed += run("test_angular_spectrum_uniform_field_preserves_intensity", test_angular_spectrum_uniform_field_preserves_intensity);
    failed += run("test_huygens_fresnel_and_rayleigh_sommerfeld_point_field", test_huygens_fresnel_and_rayleigh_sommerfeld_point_field);
    failed += run("test_propagation_operator_dispatches_supported_oracles", test_propagation_operator_dispatches_supported_oracles);
    failed += run("test_propagation_operator_rejects_invalid_inputs", test_propagation_operator_rejects_invalid_inputs);
    failed += run("test_complex_spectrum_optical_path_phase_accumulation", test_complex_spectrum_optical_path_phase_accumulation);
    failed += run("test_jones_field_optical_path_phase_preserves_polarized_power", test_jones_field_optical_path_phase_preserves_polarized_power);
    failed += run("test_complex_field_film_coherent_and_incoherent_order", test_complex_field_film_coherent_and_incoherent_order);
    failed += run("test_diffraction_camera_plan_requires_feature_gate", test_diffraction_camera_plan_requires_feature_gate);
    failed += run("test_diffraction_camera_plan_builds_reference_products", test_diffraction_camera_plan_builds_reference_products);
    failed += run("test_diffraction_camera_plan_rejects_invalid_optics", test_diffraction_camera_plan_rejects_invalid_optics);
    failed += run("test_diffraction_psf_bank_normalization_and_scaling", test_diffraction_psf_bank_normalization_and_scaling);
    failed += run("test_diffraction_psf_bank_blades_and_defocus", test_diffraction_psf_bank_blades_and_defocus);
    failed += run("test_diffraction_psf_bank_invalid_fails_closed", test_diffraction_psf_bank_invalid_fails_closed);
    failed += run("test_diffractive_analytic_orders", test_diffractive_analytic_orders);
    failed += run("test_diffractive_scattering_table_contract", test_diffractive_scattering_table_contract);
    failed += run("test_diffractive_config_boundary", test_diffractive_config_boundary);
    failed += run("test_fluorescence_resource_and_sampling", test_fluorescence_resource_and_sampling);
    failed += run("test_fluorescence_resource_rejects_invalid_energy", test_fluorescence_resource_rejects_invalid_energy);
    failed += run("test_fluorescence_config_boundary", test_fluorescence_config_boundary);
    failed += run("test_gpu_renderer_rejects_unsupported_wave_combination", test_gpu_renderer_rejects_unsupported_wave_combination);

    std::fprintf(stderr, "  passed: %d, failed: %d\n", g_passed, failed);
    g_failed += failed;
    return g_failed == 0 ? 0 : 1;
}
