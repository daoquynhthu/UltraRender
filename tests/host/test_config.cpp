#include <cstdio>
#include <fstream>
#include <string>

#include <ure/config.hpp>

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        g_failed++; \
        return 1; \
    } \
    g_passed++; \
} while(0)

static int test_spectral_json_fields() {
    const char* path = "test_config_spectral.json";
    {
        std::ofstream f(path);
        f << R"({
  "spectral": {
    "bands": 64,
    "domain_bins": 1000000,
    "packet_lanes": 16,
    "max_resident_mb": 512,
    "sampling_mode": "stratified"
  }
})";
    }

    auto cfg = ure::config::load_config(path);
    std::remove(path);
    CHECK(cfg.spectral.bands == 64);
    CHECK(cfg.spectral.domain_bins == 1000000ULL);
    CHECK(cfg.spectral.packet_lanes == 16);
    CHECK(cfg.spectral.max_resident_mb == 512);
    CHECK(cfg.spectral.sampling_mode == "stratified");
    return 0;
}

static int test_spectral_cli_overrides() {
    const char* argv[] = {
        "ure_cli",
        "render",
        "scene.gltf",
        "--spectral-domain-bins",
        "1000000",
        "--spectral-packet-lanes",
        "8",
        "--spectral-max-resident-mb",
        "256",
        "--spectral-sampling",
        "importance",
    };
    auto cfg = ure::config::parse_cli(static_cast<int>(sizeof(argv) / sizeof(argv[0])),
                                      const_cast<char**>(argv)).config;
    CHECK(cfg.scene_path == "scene.gltf");
    CHECK(cfg.spectral.domain_bins == 1000000ULL);
    CHECK(cfg.spectral.packet_lanes == 8);
    CHECK(cfg.spectral.max_resident_mb == 256);
    CHECK(cfg.spectral.sampling_mode == "importance");
    return 0;
}

int main() {
    std::fprintf(stderr, "[Config Test]\n");
    auto run = [](const char* name, int (*fn)()) {
        std::fprintf(stderr, "  test: %s ... ", name);
        int r = fn();
        std::fprintf(stderr, "%s\n", r == 0 ? "PASS" : "FAIL");
        return r != 0;
    };

    int failed = 0;
    failed += run("test_spectral_json_fields", test_spectral_json_fields);
    failed += run("test_spectral_cli_overrides", test_spectral_cli_overrides);

    std::fprintf(stderr, "  passed: %d, failed: %d\n", g_passed, failed);
    g_failed += failed;
    return g_failed;
}
