#include <ure/mie_phase.hpp>
#include <ure/mie_phase_io.hpp>
#include <ure/mie_phase_validation.hpp>
#include <ure/mie_solver.hpp>
#include <ure/detail/cuda_scene_compiler.hpp>
#include <ure/scene_ir.hpp>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <string>
#include <type_traits>

static int g_passed = 0;
static int g_failed = 0;

static_assert(std::is_same_v<decltype(ure::scene_ir::SceneIR{}.medium_mie_resource),
                             std::shared_ptr<const ure::scene_ir::MiePhaseResource>>);
static_assert(std::is_same_v<decltype(ure::scene_ir::MaterialNode{}.medium_mie_resource),
                             std::shared_ptr<const ure::scene_ir::MiePhaseResource>>);

#define CHECK(cond) do { if (cond) ++g_passed; else { ++g_failed; std::fprintf(stderr, "CHECK failed: %s at line %d\n", #cond, __LINE__); } } while (0)

static ure::scene_ir::MiePhaseResource make_isotropic_resource() {
    ure::scene_ir::MiePhaseResource resource;
    resource.wavelengths_nm = {450.0f, 650.0f};
    resource.cos_theta = {-1.0f, 0.0f, 1.0f};
    const float isotropic = 1.0f / (4.0f * std::numbers::pi_v<float>);
    resource.phase.assign(6, isotropic);
    resource.scattering_cross_section_m2 = {1.0e-12f, 2.0e-12f};
    resource.extinction_cross_section_m2 = {1.5e-12f, 2.5e-12f};
    resource.provenance = "unit-test";
    return resource;
}

template <typename Function>
static bool throws_invalid_argument(Function&& function) {
    try {
        function();
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

template <typename Function>
static bool throws_runtime_error(Function&& function) {
    try {
        function();
    } catch (const std::runtime_error&) {
        return true;
    }
    return false;
}

static void test_valid_resource_builds_derived_data() {
    auto resource = make_isotropic_resource();
    ure::sceneio::validate_mie_phase_resource(resource, 1.0e-5f);
    CHECK(resource.cdf.size() == resource.phase.size());
    CHECK(resource.absorption_cross_section_m2.size() == 2);
    CHECK(resource.asymmetry.size() == 2);
    CHECK(std::abs(resource.cdf[2] - 1.0f) < 1.0e-6f);
    CHECK(std::abs(resource.cdf[5] - 1.0f) < 1.0e-6f);
    CHECK(std::abs(resource.asymmetry[0]) < 1.0e-6f);
    CHECK(!resource.content_hash.empty());
    CHECK(resource.content_hash == ure::sceneio::mie_phase_content_hash(resource));
}

static void test_invalid_resource_rejected() {
    auto non_positive_wavelength = make_isotropic_resource();
    non_positive_wavelength.wavelengths_nm = {-1.0f, 650.0f};
    CHECK(throws_invalid_argument([&] {
        ure::sceneio::validate_mie_phase_resource(non_positive_wavelength);
    }));

    auto invalid_polarization = make_isotropic_resource();
    invalid_polarization.polarization_model =
        static_cast<ure::scene_ir::MiePolarizationModel>(99);
    CHECK(throws_invalid_argument([&] {
        ure::sceneio::validate_mie_phase_resource(invalid_polarization);
    }));

    auto wavelengths = make_isotropic_resource();
    wavelengths.wavelengths_nm = {650.0f, 450.0f};
    CHECK(throws_invalid_argument([&] { ure::sceneio::validate_mie_phase_resource(wavelengths); }));

    auto angular_coverage = make_isotropic_resource();
    angular_coverage.cos_theta.front() = -0.9f;
    CHECK(throws_invalid_argument([&] { ure::sceneio::validate_mie_phase_resource(angular_coverage); }));

    auto negative = make_isotropic_resource();
    negative.phase[1] = -1.0f;
    CHECK(throws_invalid_argument([&] { ure::sceneio::validate_mie_phase_resource(negative); }));

    auto dimensions = make_isotropic_resource();
    dimensions.phase.pop_back();
    CHECK(throws_invalid_argument([&] { ure::sceneio::validate_mie_phase_resource(dimensions); }));

    auto inconsistent_cross_sections = make_isotropic_resource();
    inconsistent_cross_sections.scattering_cross_section_m2[0] =
        std::nextafter(inconsistent_cross_sections.extinction_cross_section_m2[0],
                       std::numeric_limits<float>::infinity());
    CHECK(throws_invalid_argument([&] {
        ure::sceneio::validate_mie_phase_resource(inconsistent_cross_sections);
    }));
}

static void test_tolerance_input_is_canonicalized() {
    auto resource = make_isotropic_resource();
    for (float& value : resource.phase) value *= 1.0f + 5.0e-5f;
    ure::sceneio::validate_mie_phase_resource(resource, 1.0e-4f);
    for (std::size_t wavelength = 0; wavelength < resource.wavelengths_nm.size(); ++wavelength) {
        const std::size_t offset = wavelength * resource.cos_theta.size();
        double integral = 0.0;
        for (std::size_t angle = 1; angle < resource.cos_theta.size(); ++angle) {
            integral += std::numbers::pi_v<double> *
                (resource.phase[offset + angle - 1] + resource.phase[offset + angle]) *
                (resource.cos_theta[angle] - resource.cos_theta[angle - 1]);
        }
        CHECK(std::abs(integral - 1.0) < 1.0e-6);
        CHECK(resource.cdf[offset + resource.cos_theta.size() - 1] == 1.0f);
    }
}

static void test_exact_first_moment_and_canonical_hash() {
    auto resource = make_isotropic_resource();
    for (std::size_t wavelength = 0; wavelength < resource.wavelengths_nm.size(); ++wavelength) {
        const std::size_t offset = wavelength * resource.cos_theta.size();
        for (std::size_t angle = 0; angle < resource.cos_theta.size(); ++angle) {
            const float mu = resource.cos_theta[angle];
            resource.phase[offset + angle] = (1.0f + 0.5f * mu) /
                                             (4.0f * std::numbers::pi_v<float>);
        }
    }
    ure::sceneio::validate_mie_phase_resource(resource, 1.0e-5f);
    CHECK(std::abs(resource.asymmetry[0] - 1.0f / 6.0f) < 1.0e-6f);

    auto different_provenance = resource;
    different_provenance.provenance = "different-source";
    CHECK(ure::sceneio::mie_phase_content_hash(resource) ==
          ure::sceneio::mie_phase_content_hash(different_provenance));

    auto positive_zero = resource;
    auto negative_zero = resource;
    positive_zero.absorption_cross_section_m2[0] = 0.0f;
    negative_zero.absorption_cross_section_m2[0] = -0.0f;
    CHECK(ure::sceneio::mie_phase_content_hash(positive_zero) ==
          ure::sceneio::mie_phase_content_hash(negative_zero));
}

static void test_radius_distribution_compilation() {
    ure::scene_ir::MieRadiusDistribution mono;
    mono.kind = ure::scene_ir::MieRadiusDistributionKind::Monodisperse;
    mono.median_radius_m = 1.0e-6;
    const auto mono_samples = ure::mie::compile_mie_radius_distribution(mono);
    CHECK(mono_samples.size() == 1);
    CHECK(mono_samples[0].radius_m == mono.median_radius_m);
    CHECK(mono_samples[0].number_weight == 1.0);

    ure::scene_ir::MieRadiusDistribution discrete;
    discrete.kind = ure::scene_ir::MieRadiusDistributionKind::Discrete;
    discrete.samples = {{1.0e-6, 1.0}, {2.0e-6, 3.0}};
    const auto discrete_samples = ure::mie::compile_mie_radius_distribution(discrete);
    CHECK(discrete_samples.size() == 2);
    CHECK(std::abs(discrete_samples[0].number_weight - 0.25) < 1.0e-12);
    CHECK(std::abs(discrete_samples[1].number_weight - 0.75) < 1.0e-12);

    auto invalid = discrete;
    invalid.samples[0].radius_m = 0.0;
    CHECK(throws_invalid_argument([&] { ure::mie::compile_mie_radius_distribution(invalid); }));
    invalid = discrete;
    invalid.samples[0].number_weight = -1.0;
    CHECK(throws_invalid_argument([&] { ure::mie::compile_mie_radius_distribution(invalid); }));

    ure::scene_ir::MieRadiusDistribution log_normal;
    log_normal.kind = ure::scene_ir::MieRadiusDistributionKind::LogNormal;
    log_normal.median_radius_m = 1.0e-6;
    log_normal.geometric_standard_deviation = 1.5;
    log_normal.quadrature_sample_count = 9;
    const auto log_a = ure::mie::compile_mie_radius_distribution(log_normal);
    const auto log_b = ure::mie::compile_mie_radius_distribution(log_normal);
    CHECK(log_a.size() == 9);
    CHECK(log_a == log_b);
    double weight_sum = 0.0;
    for (const auto& sample : log_a) {
        CHECK(sample.radius_m > 0.0 && sample.number_weight > 0.0);
        weight_sum += sample.number_weight;
    }
    CHECK(std::abs(weight_sum - 1.0) < 1.0e-12);
}

static void test_lorenz_mie_reference_and_rayleigh_limit() {
    ure::scene_ir::MieGenerationConfig reference;
    reference.optical_samples = {
        {632.8, {1.5, 1.0}, 1.0},
        {700.0, {1.5, 1.0}, 1.0}
    };
    reference.radius_distribution.median_radius_m = 632.8e-9 / (2.0 * std::numbers::pi);
    reference.initial_angular_sample_count = 257;
    reference.maximum_angular_sample_count = 4097;
    const auto resource = ure::mie::generate_mie_phase_resource(reference);
    const double area = std::numbers::pi * reference.radius_distribution.median_radius_m *
                        reference.radius_distribution.median_radius_m;
    CHECK(std::abs(resource->extinction_cross_section_m2[0] / area - 2.336321) < 2.0e-5);
    CHECK(std::abs(resource->scattering_cross_section_m2[0] / area - 0.663454) < 2.0e-5);
    CHECK(std::abs(resource->asymmetry[0] - 0.192136f) < 2.0e-5f);
    CHECK(resource->extinction_cross_section_m2[0] >=
          resource->scattering_cross_section_m2[0]);

    auto rayleigh = reference;
    rayleigh.optical_samples = {
        {500.0, {1.5, 0.0}, 1.0},
        {600.0, {1.5, 0.0}, 1.0}
    };
    rayleigh.radius_distribution.median_radius_m = 1.0e-9;
    const auto small = ure::mie::generate_mie_phase_resource(rayleigh);
    const std::size_t middle = small->cos_theta.size() / 2;
    CHECK(std::abs(small->asymmetry[0]) < 1.0e-3f);
    CHECK(std::abs(small->phase.front() / small->phase[middle] - 2.0f) < 2.0e-3f);
    const auto repeated = ure::mie::generate_mie_phase_resource(rayleigh);
    CHECK(small->phase == repeated->phase);
    CHECK(small->content_hash == repeated->content_hash);
    CHECK(std::abs(small->extinction_cross_section_m2[0] -
                   small->scattering_cross_section_m2[0]) < 1.0e-6f *
                  small->scattering_cross_section_m2[0]);
}

static ure::scene_ir::MieGenerationConfig make_mixture_config() {
    ure::scene_ir::MieGenerationConfig config;
    config.optical_samples = {
        {500.0, {1.45, 0.0}, 1.0},
        {600.0, {1.45, 0.0}, 1.0}
    };
    config.initial_angular_sample_count = 513;
    config.maximum_angular_sample_count = 1025;
    config.angular_cross_section_tolerance = 2.0e-4;
    return config;
}

static void test_scattering_weighted_radius_mixture_and_budget() {
    auto first_config = make_mixture_config();
    first_config.radius_distribution.median_radius_m = 0.1e-6;
    auto second_config = make_mixture_config();
    second_config.radius_distribution.median_radius_m = 0.2e-6;
    const auto first = ure::mie::generate_mie_phase_resource(first_config);
    const auto second = ure::mie::generate_mie_phase_resource(second_config);

    auto mixture_config = make_mixture_config();
    mixture_config.radius_distribution.kind =
        ure::scene_ir::MieRadiusDistributionKind::Discrete;
    mixture_config.radius_distribution.samples = {{0.1e-6, 1.0}, {0.2e-6, 3.0}};
    const auto mixture = ure::mie::generate_mie_phase_resource(mixture_config);
    const float expected_cross_section =
        0.25f * first->scattering_cross_section_m2[0] +
        0.75f * second->scattering_cross_section_m2[0];
    CHECK(std::abs(mixture->scattering_cross_section_m2[0] - expected_cross_section) <
          2.0e-6f * expected_cross_section);
    const std::size_t angle = mixture->cos_theta.size() * 3 / 4;
    const float expected_phase =
        (0.25f * first->scattering_cross_section_m2[0] * first->phase[angle] +
         0.75f * second->scattering_cross_section_m2[0] * second->phase[angle]) /
        expected_cross_section;
    CHECK(std::abs(mixture->phase[angle] - expected_phase) < 2.0e-6f * expected_phase);

    auto constrained = mixture_config;
    constrained.maximum_resource_bytes = 1;
    CHECK(throws_runtime_error([&] { ure::mie::generate_mie_phase_resource(constrained); }));
    constrained = mixture_config;
    constrained.maximum_working_set_bytes = 1;
    CHECK(throws_runtime_error([&] { ure::mie::generate_mie_phase_resource(constrained); }));
    constrained = mixture_config;
    constrained.maximum_angular_evaluations = 1;
    CHECK(throws_runtime_error([&] { ure::mie::generate_mie_phase_resource(constrained); }));

    auto unresolved = make_mixture_config();
    unresolved.radius_distribution.median_radius_m = 8.0e-6;
    unresolved.initial_angular_sample_count = 33;
    unresolved.maximum_angular_sample_count = 33;
    unresolved.angular_cross_section_tolerance = 1.0e-6;
    CHECK(throws_runtime_error([&] { ure::mie::generate_mie_phase_resource(unresolved); }));
}

static void test_high_size_absorbing_mie_converges() {
    ure::scene_ir::MieGenerationConfig config;
    config.optical_samples = {
        {500.0, {1.5, 0.1}, 1.0},
        {600.0, {1.5, 0.1}, 1.0}};
    config.radius_distribution.median_radius_m =
        100.0 * 500.0e-9 / (2.0 * std::numbers::pi_v<double>);
    config.initial_angular_sample_count = 257;
    config.maximum_angular_sample_count = 8193;
    config.angular_cross_section_tolerance = 2.0e-3;
    config.angular_asymmetry_tolerance = 2.0e-3;
    config.angular_distribution_tolerance = 2.0e-3;
    const auto resource = ure::mie::generate_mie_phase_resource(config);
    CHECK(resource->cos_theta.size() <= config.maximum_angular_sample_count);
    for (std::size_t i = 1; i < resource->cos_theta.size(); ++i) {
        CHECK(resource->cos_theta[i] > resource->cos_theta[i - 1]);
    }
    CHECK(resource->extinction_cross_section_m2[0] >
          resource->scattering_cross_section_m2[0]);
    CHECK(resource->asymmetry[0] > 0.0f && resource->asymmetry[0] < 1.0f);

    auto float_limit = config;
    float_limit.radius_distribution.median_radius_m = 1.0e-7;
    float_limit.initial_angular_sample_count = 8193;
    float_limit.maximum_angular_sample_count = 16385;
    float_limit.angular_cross_section_tolerance = 1.0;
    float_limit.angular_asymmetry_tolerance = 1.0;
    float_limit.angular_distribution_tolerance = 1.0;
    const auto maximum_grid = ure::mie::generate_mie_phase_resource(float_limit);
    CHECK(maximum_grid->cos_theta.size() == 16385);
    for (std::size_t i = 1; i < maximum_grid->cos_theta.size(); ++i) {
        CHECK(maximum_grid->cos_theta[i] > maximum_grid->cos_theta[i - 1]);
    }
}

static void test_external_table_roundtrip() {
    const std::filesystem::path fixture =
        std::filesystem::path(URE_TEST_ASSET_DIR) / "mie" / "isotropic_v1.mie.json";
    const auto loaded = ure::sceneio::load_mie_phase_table(fixture.string());
    CHECK(loaded.phase.size() == 6);
    CHECK(loaded.cdf.size() == 6);
    CHECK(loaded.source_hash == "fixture-v1");
    CHECK(throws_invalid_argument([&] {
        ure::sceneio::load_mie_phase_table(fixture.string(), 32);
    }));

    const auto temporary = std::filesystem::temp_directory_path();
    const auto first = temporary / "ure_mie_roundtrip_a.json";
    const auto second = temporary / "ure_mie_roundtrip_b.json";
    ure::sceneio::save_mie_phase_table(loaded, first.string());
    ure::sceneio::save_mie_phase_table(loaded, second.string());
    std::ifstream first_stream(first, std::ios::binary);
    std::ifstream second_stream(second, std::ios::binary);
    const std::string first_bytes((std::istreambuf_iterator<char>(first_stream)), {});
    const std::string second_bytes((std::istreambuf_iterator<char>(second_stream)), {});
    CHECK(first_bytes == second_bytes);
    CHECK(ure::sceneio::load_mie_phase_table(first.string()).content_hash == loaded.content_hash);
    first_stream.close();
    second_stream.close();
    const auto invalid = temporary / "ure_mie_invalid.json";
    auto write_invalid = [&](std::string bytes) {
        std::ofstream stream(invalid, std::ios::binary | std::ios::trunc);
        stream << bytes;
    };
    auto invalid_version = first_bytes;
    invalid_version.replace(invalid_version.find("\"version\": 1"), 12, "\"version\": 2");
    write_invalid(invalid_version);
    CHECK(throws_invalid_argument([&] { ure::sceneio::load_mie_phase_table(invalid.string()); }));
    auto invalid_units = first_bytes;
    invalid_units.replace(invalid_units.find("\"wavelength\": \"nm\""), 18,
                          "\"wavelength\": \"um\"");
    write_invalid(invalid_units);
    CHECK(throws_invalid_argument([&] { ure::sceneio::load_mie_phase_table(invalid.string()); }));
    auto unknown_field = first_bytes;
    unknown_field.insert(unknown_field.find('{') + 1, "\n  \"unknown_required\": true,");
    write_invalid(unknown_field);
    CHECK(throws_invalid_argument([&] { ure::sceneio::load_mie_phase_table(invalid.string()); }));
    std::filesystem::remove(first);
    std::filesystem::remove(second);
    std::filesystem::remove(invalid);
}

static std::shared_ptr<const ure::scene_ir::MiePhaseResource> make_compiler_resource(
    float wavelength_min = 360.0f, float wavelength_max = 830.0f) {
    auto resource = std::make_shared<ure::scene_ir::MiePhaseResource>(make_isotropic_resource());
    resource->wavelengths_nm = {wavelength_min, wavelength_max};
    ure::scene_ir::validate_mie_phase_resource(*resource);
    return resource;
}

static void test_scene_compiler_mie_contract() {
    ure::scene_ir::SceneIR missing;
    missing.medium_phase = ure::scene_ir::VolumePhaseFunction::Mie;
    missing.medium_density = 1.0f;
    CHECK(throws_invalid_argument([&] { ure::GpuSceneCompiler::compile(missing); }));

    ure::scene_ir::SceneIR scene;
    scene.medium_phase = ure::scene_ir::VolumePhaseFunction::Mie;
    scene.medium_density = 1.0f;
    scene.medium_mie_resource = make_compiler_resource();
    auto material = std::make_shared<ure::scene_ir::MaterialNode>();
    material->medium_phase = ure::scene_ir::VolumePhaseFunction::Mie;
    material->medium_density = 2.0f;
    auto equal_resource = std::make_shared<ure::scene_ir::MiePhaseResource>(
        *scene.medium_mie_resource);
    equal_resource->content_hash = "forged-stale-hash";
    material->medium_mie_resource = equal_resource;
    scene.spheres.push_back({"mie-volume", {0.0f, 0.0f, 0.0f}, 1.0f, material});
    const auto compiled = ure::GpuSceneCompiler::compile(scene);
    CHECK(compiled.mie_phase_resources.size() == 1);
    CHECK(compiled.medium_phase_resource_index == 0);
    CHECK(compiled.materials[0].header.medium_phase_resource_index == 0);

    auto gap = scene;
    gap.medium_mie_resource = make_compiler_resource(400.0f, 700.0f);
    CHECK(throws_invalid_argument([&] { ure::GpuSceneCompiler::compile(gap); }));
    auto ambiguous = scene;
    ambiguous.medium_anisotropy = 0.1f;
    CHECK(throws_invalid_argument([&] { ure::GpuSceneCompiler::compile(ambiguous); }));
    ambiguous = scene;
    ambiguous.medium_scattering = {0.1f, 0.0f, 0.0f};
    CHECK(throws_invalid_argument([&] { ure::GpuSceneCompiler::compile(ambiguous); }));
    auto invalid_phase = scene;
    invalid_phase.medium_phase = static_cast<ure::scene_ir::VolumePhaseFunction>(99);
    invalid_phase.medium_mie_resource.reset();
    CHECK(throws_invalid_argument([&] { ure::GpuSceneCompiler::compile(invalid_phase); }));
    ambiguous = scene;
    ambiguous.medium_density = -1.0f;
    CHECK(throws_invalid_argument([&] { ure::GpuSceneCompiler::compile(ambiguous); }));
    ambiguous = scene;
    ambiguous.medium_phase = ure::scene_ir::VolumePhaseFunction::HenyeyGreenstein;
    CHECK(throws_invalid_argument([&] { ure::GpuSceneCompiler::compile(ambiguous); }));

    auto overflow_resource = std::make_shared<ure::scene_ir::MiePhaseResource>(
        *scene.medium_mie_resource);
    overflow_resource->scattering_cross_section_m2 = {1.0e30f, 1.0e30f};
    overflow_resource->extinction_cross_section_m2 = {1.0e30f, 1.0e30f};
    auto overflow = scene;
    overflow.medium_density = 1.0e20f;
    overflow.medium_mie_resource = overflow_resource;
    CHECK(throws_invalid_argument([&] { ure::GpuSceneCompiler::compile(overflow); }));
}

int main() {
    try {
        test_valid_resource_builds_derived_data();
        test_invalid_resource_rejected();
        test_tolerance_input_is_canonicalized();
        test_exact_first_moment_and_canonical_hash();
        test_radius_distribution_compilation();
        test_lorenz_mie_reference_and_rayleigh_limit();
        test_scattering_weighted_radius_mixture_and_budget();
        test_high_size_absorbing_mie_converges();
        test_external_table_roundtrip();
        test_scene_compiler_mie_contract();
    } catch (const std::exception& error) {
        ++g_failed;
        std::fprintf(stderr, "Unhandled exception: %s\n", error.what());
    }
    std::printf("[Mie Phase Resource Test] passed: %d, failed: %d\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
