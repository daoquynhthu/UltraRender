#include "ure/native_adapter.hpp"
#include "ure/render.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <numeric>
#include <string_view>
#include <vector>

static int failures = 0;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", \
                         __FILE__, __LINE__, #condition); \
            ++failures; \
        } \
    } while (false)

struct RenderEvidence {
    double mean = 0.0;
    double maximum_absolute = 0.0;
    std::vector<float> framebuffer;
    ure::AutomaticIntegratorReport report;
};

static double mean(const std::vector<float>& values) {
    return std::accumulate(values.begin(), values.end(), 0.0) /
        static_cast<double>(values.size());
}

static double variance(const std::vector<double>& values) {
    const auto average = std::accumulate(
        values.begin(), values.end(), 0.0) /
        static_cast<double>(values.size());
    double squared = 0.0;
    for (const auto value : values) {
        const auto delta = value - average;
        squared += delta * delta;
    }
    return squared / static_cast<double>(values.size() - 1);
}

static RenderEvidence render_automatic(
    const ure::scene_ir::SceneIR& input,
    std::uint64_t sample_offset) {
    auto scene = input;
    scene.width = 4;
    scene.height = 4;
    ure::RenderConfig config;
    config.integrator.mode = ure::IntegratorMode::Automatic;
    config.automatic_integrator.enabled = true;
    config.automatic_integrator.pilot_spp = 2;
    config.automatic_integrator.maximum_techniques = 2;
    config.automatic_integrator.minimum_wavefront_fraction = 0.25f;
    config.automatic_integrator.target_relative_standard_error = 0.5;
    config.sample_index_offset = sample_offset;
    auto engine = ure::RenderEngineFactory::create_gpu_renderer(config);
    engine->load_scene_ir(scene);
    ure::RenderSettings settings;
    settings.width = 4;
    settings.height = 4;
    settings.spp = 8;
    engine->render(settings);
    RenderEvidence result;
    result.framebuffer = engine->get_framebuffer();
    result.mean = mean(result.framebuffer);
    for (const auto value : result.framebuffer) {
        result.maximum_absolute = std::max(
            result.maximum_absolute,
            std::abs(static_cast<double>(value)));
    }
    result.report = engine->get_automatic_integrator_report();
    return result;
}

static double render_reference(
    const ure::scene_ir::SceneIR& input,
    std::uint64_t sample_offset) {
    auto scene = input;
    scene.width = 4;
    scene.height = 4;
    ure::RenderConfig config;
    config.sample_index_offset = sample_offset;
    auto engine = ure::RenderEngineFactory::create_gpu_renderer(config);
    engine->load_scene_ir(scene);
    ure::RenderSettings settings;
    settings.width = 4;
    settings.height = 4;
    settings.spp = 32;
    engine->render(settings);
    return mean(engine->get_framebuffer());
}

static void check_single_report(const RenderEvidence& evidence) {
    CHECK(evidence.framebuffer.size() == 4u * 4u * 3u);
    CHECK(std::ranges::all_of(
        evidence.framebuffer,
        [](float value) { return std::isfinite(value); }));
    const auto& report = evidence.report;
    CHECK(report.automatic);
    CHECK(report.complete);
    CHECK(report.total_allocated_spp == 8);
    CHECK(report.techniques.size() == 8);
    CHECK(report.techniques.front().mode ==
          ure::IntegratorMode::Wavefront);
    CHECK(report.techniques.front().selected);
    CHECK(report.techniques.front().allocated_spp >= 2);
    const auto mlt = std::ranges::find(
        report.techniques, ure::IntegratorMode::MLT,
        &ure::AutomaticTechniqueReport::mode);
    CHECK(mlt != report.techniques.end());
    CHECK(!mlt->qualified);
    CHECK(!mlt->selected);
    CHECK(mlt->reason.find("finite-sample unbiased") !=
          std::string::npos);
    const auto weight_sum = std::accumulate(
        report.techniques.begin(), report.techniques.end(), 0.0,
        [](double sum, const ure::AutomaticTechniqueReport& value) {
            return sum + value.aggregation_weight;
        });
    CHECK(std::abs(weight_sum - 1.0) < 1e-12);
    CHECK(std::isfinite(report.estimated_relative_standard_error));
    CHECK(std::ranges::all_of(
        report.techniques,
        [](const ure::AutomaticTechniqueReport& value) {
            return std::isfinite(
                value.maximum_absolute_pilot_contribution);
        }));
    CHECK(report.elapsed_nanoseconds > 0);
    CHECK(report.peak_memory_budget_bytes > 0);
    CHECK(report.measured_peak_resident_device_bytes > 0);
    CHECK(report.estimated_peak_device_bytes >=
          report.measured_peak_resident_device_bytes);
    CHECK(report.estimated_peak_device_bytes <=
          report.peak_memory_budget_bytes);
    CHECK(report.memory_budget_met);
    CHECK((report.technique_coverage_mask & 1ull) != 0);
    CHECK(report.independent_endpoint_ensemble);
    CHECK(report.pilot_precision_weighted);
    CHECK(report.conservative_uncertainty_bound);
    CHECK(report.auxiliary_outputs_wavefront_only);
    CHECK(std::isfinite(evidence.maximum_absolute));
}

static void test_fixed_multiscene_replicates() {
    constexpr std::array<std::string_view, 3> scenes{
        "test_plane_sphere.gltf",
        "cornell_box.gltf",
        "textured_quad_validation.gltf"};
    for (const auto name : scenes) {
        const auto imported = ure::native_scene::import_gltf_native(
            std::filesystem::path(URE_TEST_SCENE_DIR) / name);
        CHECK(imported.ok());
        if (!imported.ok()) continue;
        std::vector<RenderEvidence> automatic;
        std::vector<double> references;
        for (std::uint64_t replicate = 0; replicate < 3; ++replicate) {
            automatic.push_back(render_automatic(
                imported.archive.scene, replicate * 128));
            references.push_back(render_reference(
                imported.archive.scene, 10000 + replicate * 128));
            check_single_report(automatic.back());
        }
        std::vector<double> automatic_means;
        for (const auto& value : automatic) {
            automatic_means.push_back(value.mean);
        }
        CHECK(*std::ranges::max_element(automatic_means) !=
              *std::ranges::min_element(automatic_means));
        const auto automatic_mean = std::accumulate(
            automatic_means.begin(), automatic_means.end(), 0.0) / 3.0;
        const auto reference_mean = std::accumulate(
            references.begin(), references.end(), 0.0) / 3.0;
        const auto standard_error = std::sqrt(
            variance(automatic_means) / 3.0 + variance(references) / 3.0);
        CHECK(std::abs(automatic_mean - reference_mean) <=
              5.0 * standard_error + 1e-4);
        std::vector<float> merged(
            automatic.front().framebuffer.size(), 0.0f);
        for (const auto index : std::array{2u, 0u, 1u}) {
            for (std::size_t value = 0; value < merged.size(); ++value) {
                merged[value] += automatic[index].framebuffer[value] / 3.0f;
            }
        }
        CHECK(std::abs(mean(merged) - automatic_mean) < 1e-6);
    }
}

int main() {
    auto imported = ure::native_scene::import_gltf_native(
        std::filesystem::path(URE_TEST_SCENE_DIR) /
        "test_plane_sphere.gltf");
    CHECK(imported.ok());
    if (!imported.ok()) return 1;
    const auto evidence = render_automatic(imported.archive.scene, 0);
    check_single_report(evidence);
    auto metadata_config = ure::RenderConfig{};
    metadata_config.integrator.mode = ure::IntegratorMode::Automatic;
    metadata_config.automatic_integrator.enabled = true;
    const auto metadata_engine =
        ure::RenderEngineFactory::create_gpu_renderer(metadata_config);
    const auto metadata = metadata_engine->get_estimator_metadata();
    CHECK(metadata.mode == ure::IntegratorMode::Automatic);
    CHECK(metadata.policy ==
          ure::IntegratorEstimatorPolicy::AutomaticPortfolio);
    CHECK(ure::validate_integrator_estimator_metadata(metadata));
    test_fixed_multiscene_replicates();
    if (failures == 0) {
        std::puts(
            "Automatic CUDA portfolio executed wavefront and path-guided estimators.");
    }
    return failures == 0 ? 0 : 1;
}
