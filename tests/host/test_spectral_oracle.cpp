#include <cmath>
#include <cstdio>

#include <ure/spectral/spectral_oracle.hpp>

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        ++g_failed; \
        return 1; \
    } \
    ++g_passed; \
} while (0)

#define CHECK_NEAR(a, b, eps) do { \
    double _a = (a), _b = (b), _e = (eps); \
    if (std::abs(_a - _b) > _e) { \
        std::fprintf(stderr, "  FAIL: %s:%d: %s ~= %s (%.12f vs %.12f)\n", \
            __FILE__, __LINE__, #a, #b, _a, _b); \
        ++g_failed; \
        return 1; \
    } \
    ++g_passed; \
} while (0)

static int test_equal_energy_million_bin_normalization() {
    ure::spectral::SpectralDomain domain;
    auto xyz = ure::spectral::integrate_xyz(domain, ure::spectral::equal_energy);

    CHECK_NEAR(xyz.y, 1.0, 2e-3);
    CHECK(xyz.x > 0.9);
    CHECK(xyz.z > 0.9);
    CHECK(std::isfinite(xyz.x));
    CHECK(std::isfinite(xyz.y));
    CHECK(std::isfinite(xyz.z));
    return 0;
}

static int test_d65_million_bin_chromaticity() {
    ure::spectral::SpectralDomain domain;
    auto xyz = ure::spectral::integrate_xyz(domain, ure::spectral::d65);
    auto xy = ure::spectral::xy_chromaticity(xyz);

    CHECK_NEAR(xy[0], 0.3127, 8e-4);
    CHECK_NEAR(xy[1], 0.3290, 8e-4);
    CHECK(xyz.y > 0.95);
    CHECK(xyz.y < 1.05);
    return 0;
}

static int test_narrowband_fixtures_are_spectral() {
    ure::spectral::SpectralDomain domain;
    auto blue = ure::spectral::integrate_xyz(domain, [](double lambda) {
        return ure::spectral::narrowband(lambda, 450.0, 4.0);
    });
    auto red = ure::spectral::integrate_xyz(domain, [](double lambda) {
        return ure::spectral::narrowband(lambda, 650.0, 4.0);
    });

    CHECK(blue.z > blue.x);
    CHECK(blue.z > blue.y);
    CHECK(red.x > red.y);
    CHECK(red.x > red.z);
    CHECK(blue.y > 0.0);
    CHECK(red.y > 0.0);
    return 0;
}

static int test_metamer_pair_matches_xyz_but_not_spectrum() {
    ure::spectral::SpectralDomain domain{360.0, 830.0, 131'072};
    auto pair = ure::spectral::make_metamer_pair(domain, 0.08);
    auto a_xyz = ure::spectral::integrate_xyz(pair[0]);
    auto b_xyz = ure::spectral::integrate_xyz(pair[1]);

    double max_spectral_delta = 0.0;
    for (std::size_t i = 0; i < pair[0].values.size(); ++i) {
        max_spectral_delta = std::max(max_spectral_delta, std::abs(pair[0].values[i] - pair[1].values[i]));
    }

    CHECK(ure::spectral::max_abs_delta(a_xyz, b_xyz) < 2e-8);
    CHECK(max_spectral_delta > 1e-3);
    return 0;
}

static int test_high_res_resource_sampled_estimator_converges() {
    ure::spectral::SpectralDomain reference_domain;
    auto reference_table = ure::spectral::make_uniform_table(reference_domain, [](double lambda) {
        return ure::spectral::d65(lambda) * (0.8 + 0.2 * ure::spectral::narrowband(lambda, 610.0, 18.0));
    });
    auto reference = ure::spectral::integrate_xyz(reference_table);

    auto sampled = ure::spectral::estimate_uniform_sampled_xyz(reference_domain, 4096, [&](double lambda) {
        return ure::spectral::eval_uniform_table(reference_table, lambda);
    });

    CHECK(ure::spectral::max_abs_delta(reference, sampled) < 2e-3);
    CHECK(reference_table.values.size() == 1'000'000);
    return 0;
}

static int run(const char* name, int (*fn)()) {
    std::printf("  test: %s ... ", name);
    int before_failed = g_failed;
    int result = fn();
    if (result == 0 && g_failed == before_failed) {
        std::printf("PASS\n");
        return 0;
    }
    std::printf("FAIL\n");
    return 1;
}

int main() {
    std::printf("[Phase L Spectral Oracle Test]\n");
    int failed = 0;
    failed += run("test_equal_energy_million_bin_normalization", test_equal_energy_million_bin_normalization);
    failed += run("test_d65_million_bin_chromaticity", test_d65_million_bin_chromaticity);
    failed += run("test_narrowband_fixtures_are_spectral", test_narrowband_fixtures_are_spectral);
    failed += run("test_metamer_pair_matches_xyz_but_not_spectrum", test_metamer_pair_matches_xyz_but_not_spectrum);
    failed += run("test_high_res_resource_sampled_estimator_converges", test_high_res_resource_sampled_estimator_converges);

    std::printf("  passed: %d, failed: %d\n", g_passed, g_failed);
    return failed == 0 && g_failed == 0 ? 0 : 1;
}
