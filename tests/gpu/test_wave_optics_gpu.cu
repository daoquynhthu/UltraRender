#include <cmath>
#include <cstdio>

#include "test_framework.cuh"
#include "ure/wave_optics.hpp"

static int test_gpu_fraunhofer_uniform_field() {
    REQUIRE_GPU();

    ure::wave::WaveFieldGrid field;
    field.width = 4;
    field.height = 4;
    field.sample_pitch_m = 2.0e-6;
    field.wavelength_m = 550.0e-9;
    field.samples.assign(16, {1.0, 0.0});

    const auto gpu = ure::wave::propagate_fraunhofer_gpu(field);
    CHECK(gpu.width == 4);
    CHECK(gpu.height == 4);
    CHECK(gpu.amplitudes.size() == 16);
    CHECK_FLOAT_EQ(static_cast<float>(gpu.frequency_pitch_x_cycles_per_m),
                   static_cast<float>(1.0 / (4.0 * field.sample_pitch_m)),
                   1.0e-3f);

    CHECK_FLOAT_EQ(static_cast<float>(gpu.at(2, 2).real), 16.0f, 1.0e-4f);
    CHECK_FLOAT_EQ(static_cast<float>(gpu.at(2, 2).imag), 0.0f, 1.0e-4f);
    CHECK_FLOAT_EQ(static_cast<float>(gpu.intensity_at(2, 2)), 256.0f, 1.0e-3f);
    for (int y = 0; y < gpu.height; ++y) {
        for (int x = 0; x < gpu.width; ++x) {
            if (x == 2 && y == 2) continue;
            CHECK_FLOAT_EQ(static_cast<float>(gpu.intensity_at(x, y)), 0.0f, 1.0e-8f);
        }
    }
    return 0;
}

static int test_gpu_fraunhofer_matches_cpu_reference() {
    REQUIRE_GPU();

    ure::wave::CircularPupil pupil;
    pupil.aperture.wavelength_m = 550.0e-9;
    pupil.aperture.aperture_diameter_m = 2.0e-3;
    pupil.aperture.focal_length_m = 35.0e-3;
    pupil.defocus_waves_at_edge = 0.125;

    const auto field = ure::wave::make_circular_pupil_field(pupil, 7);
    const auto cpu = ure::wave::propagate_fraunhofer_direct(field);
    const auto gpu = ure::wave::propagate_fraunhofer_gpu(field);
    CHECK(gpu.width == cpu.width);
    CHECK(gpu.height == cpu.height);
    CHECK(gpu.amplitudes.size() == cpu.amplitudes.size());

    for (std::size_t i = 0; i < cpu.amplitudes.size(); ++i) {
        CHECK_FLOAT_EQ(static_cast<float>(gpu.amplitudes[i].real),
                       static_cast<float>(cpu.amplitudes[i].real),
                       1.0e-4f);
        CHECK_FLOAT_EQ(static_cast<float>(gpu.amplitudes[i].imag),
                       static_cast<float>(cpu.amplitudes[i].imag),
                       1.0e-4f);
    }
    return 0;
}

static int test_gpu_fraunhofer_invalid_fails_closed() {
    ure::wave::WaveFieldGrid field;
    field.width = 4;
    field.height = 4;
    field.sample_pitch_m = 0.0;
    field.wavelength_m = 550.0e-9;
    field.samples.assign(16, {1.0, 0.0});

    const auto gpu = ure::wave::propagate_fraunhofer_gpu(field);
    CHECK(gpu.width == 0);
    CHECK(gpu.amplitudes.empty());
    return 0;
}

int main() {
    std::printf("[GPU Wave Optics Test]\n");
    RUN_TEST(test_gpu_fraunhofer_uniform_field);
    RUN_TEST(test_gpu_fraunhofer_matches_cpu_reference);
    RUN_TEST(test_gpu_fraunhofer_invalid_fails_closed);
    std::printf("  passed: %d, failed: %d\n", g_tests_passed, g_tests_failed);
    return g_test_result;
}
