#include <cmath>
#include <cstdio>
#include <algorithm>
#include <memory>
#include <vector>

#include "test_framework.cuh"
#include "ure/render.hpp"
#include "ure/scene_ir.hpp"
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

static ure::scene_ir::SceneIR make_diffraction_scene() {
    ure::scene_ir::SceneIR scene;
    scene.width = 16;
    scene.height = 16;
    scene.camera.position = {0.0f, 0.0f, 4.0f};
    scene.camera.look_at = {0.0f, 0.0f, 0.0f};
    scene.camera.fov = 35.0f;
    auto light =
        std::make_shared<ure::scene_ir::MaterialNode>();
    light->model = ure::scene_ir::MaterialModel::Light;
    light->emission = {12.0f, 12.0f, 12.0f};
    scene.materials.push_back(light);
    ure::scene_ir::SphereNode emitter;
    emitter.center = {0.0f, 0.0f, 0.0f};
    emitter.radius = 0.22f;
    emitter.material = light;
    scene.spheres.push_back(emitter);
    return scene;
}

static std::vector<float> render_diffraction_fixture(
    const ure::RenderConfig& config,
    int pass_count) {
    auto engine =
        ure::RenderEngineFactory::create_gpu_renderer(
            config);
    engine->load_scene_ir(make_diffraction_scene());
    for (int pass = 0; pass < pass_count; ++pass) {
        engine->render_pass();
    }
    return engine->get_framebuffer();
}

static float framebuffer_sum(
    const std::vector<float>& framebuffer) {
    float sum = 0.0f;
    for (float value : framebuffer) {
        sum += value;
    }
    return sum;
}

static int test_gpu_diffraction_camera_film_integration() {
    REQUIRE_GPU();
    ure::RenderConfig radiometric;
    radiometric.queue_capacity = 512;
    radiometric.spectral_packet_lanes = 8;
    radiometric.spectral_domain_bins = 8;
    radiometric.max_trace_depth = 2;
    const auto reference =
        render_diffraction_fixture(radiometric, 16);

    auto diffraction = radiometric;
    diffraction.wave_optics.mode =
        ure::WaveOpticsMode::CameraDiffraction;
    diffraction.wave_optics.camera_diffraction_enabled =
        true;
    diffraction.wave_optics.camera_aperture_diameter_m =
        0.2e-3;
    diffraction.wave_optics.camera_focal_length_m =
        50.0e-3;
    diffraction.wave_optics.sensor_pixel_pitch_m =
        4.0e-6;
    diffraction.wave_optics.camera_psf_radius_pixels =
        4;
    diffraction.wave_optics.camera_wavelength_bin_count =
        8;
    diffraction.wave_optics.camera_pupil_sample_count =
        16;
    const auto filtered =
        render_diffraction_fixture(diffraction, 16);
    CHECK(filtered.size() == reference.size());
    const float reference_sum =
        framebuffer_sum(reference);
    const float filtered_sum =
        framebuffer_sum(filtered);
    CHECK(reference_sum > 0.0f);
    CHECK(filtered_sum > 0.0f);
    float difference = 0.0f;
    for (std::size_t index = 0;
         index < reference.size();
         ++index) {
        CHECK(std::isfinite(filtered[index]));
        difference +=
            std::abs(filtered[index] - reference[index]);
    }
    CHECK(difference > 0.01f);
    const float reference_peak =
        *std::max_element(
            reference.begin(),
            reference.end());
    const float filtered_peak =
        *std::max_element(
            filtered.begin(),
            filtered.end());
    CHECK(filtered_peak < reference_peak);
    CHECK_FLOAT_EQ(
        filtered_sum / reference_sum,
        1.0f,
        0.05f);
    return 0;
}

static int test_disabled_diffraction_parameters_are_inert() {
    REQUIRE_GPU();
    ure::RenderConfig first;
    first.queue_capacity = 512;
    first.spectral_packet_lanes = 8;
    first.spectral_domain_bins = 8;
    first.max_trace_depth = 2;
    auto second = first;
    second.wave_optics.camera_aperture_diameter_m =
        0.1e-3;
    second.wave_optics.camera_defocus_waves_at_edge =
        12.0;
    second.wave_optics.camera_aperture_blade_count = 9;
    second.wave_optics.camera_psf_radius_pixels = 17;
    second.wave_optics.camera_wavelength_bin_count = 31;
    const auto a = render_diffraction_fixture(first, 2);
    const auto b = render_diffraction_fixture(second, 2);
    CHECK(a.size() == b.size());
    for (std::size_t index = 0; index < a.size(); ++index) {
        CHECK_FLOAT_EQ(a[index], b[index], 0.0f);
    }
    return 0;
}

int main() {
    std::printf("[GPU Wave Optics Test]\n");
    RUN_TEST(test_gpu_fraunhofer_uniform_field);
    RUN_TEST(test_gpu_fraunhofer_matches_cpu_reference);
    RUN_TEST(test_gpu_fraunhofer_invalid_fails_closed);
    RUN_TEST(test_gpu_diffraction_camera_film_integration);
    RUN_TEST(test_disabled_diffraction_parameters_are_inert);
    std::printf("  passed: %d, failed: %d\n", g_tests_passed, g_tests_failed);
    return g_test_result;
}
