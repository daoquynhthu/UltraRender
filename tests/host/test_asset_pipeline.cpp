#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <vector>
#include <string>

#include <ure/image_loader.hpp>
#include <ure/scene_io.hpp>
#include <ure/spd_loader.hpp>

static int g_passed = 0, g_failed = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        g_failed++; \
        return 1; \
    } \
    g_passed++; \
} while(0)

#define CHECK_FLOAT_EQ(a, b, eps) do { \
    float _a = (a), _b = (b), _e = (eps); \
    if (fabsf(_a - _b) > _e) { \
        fprintf(stderr, "  FAIL: %s:%d: %s == %s (%.6f vs %.6f)\n", \
                __FILE__, __LINE__, #a, #b, _a, _b); \
        g_failed++; \
        return 1; \
    } \
    g_passed++; \
} while(0)

// 2x2 24-bit top-down BMP with known pattern.
// Layout: R(1,0,0) G(0,1,0)
//         B(0,0,1) W(1,1,1)
// Negative height = top-down (row order matches file order).
static const unsigned char kTestBmp[] = {
    0x42, 0x4D,                // "BM"
    0x4A, 0x00, 0x00, 0x00,    // file size = 74
    0x00, 0x00,                 // reserved
    0x00, 0x00,                 // reserved
    0x36, 0x00, 0x00, 0x00,    // pixel offset = 54
    0x28, 0x00, 0x00, 0x00,    // DIB header size = 40
    0x02, 0x00, 0x00, 0x00,    // width = 2
    0xFE, 0xFF, 0xFF, 0xFF,    // height = -2 (top-down)
    0x01, 0x00,                 // planes = 1
    0x18, 0x00,                 // bits per pixel = 24
    0x00, 0x00, 0x00, 0x00,    // compression = 0
    0x14, 0x00, 0x00, 0x00,    // image size = 20
    0x13, 0x0B, 0x00, 0x00,    // x ppm
    0x13, 0x0B, 0x00, 0x00,    // y ppm
    0x00, 0x00, 0x00, 0x00,    // colors used
    0x00, 0x00, 0x00, 0x00,    // important colors
    // Row 0 (top): R(1,0,0) G(0,1,0) + 2 padding bytes
    0x00, 0x00, 0xFF,  0x00, 0xFF, 0x00,  0x00, 0x00,
    // Row 1 (bot): B(0,0,1) W(1,1,1) + 2 padding bytes
    0xFF, 0x00, 0x00,  0xFF, 0xFF, 0xFF,  0x00, 0x00,
};

static int test_load_image_bmp() {
    // Write test BMP to temp file
    const char* tmp = "test_asset_temp.bmp";
    {
        std::ofstream f(tmp, std::ios::binary);
        f.write(reinterpret_cast<const char*>(kTestBmp), sizeof(kTestBmp));
    }

    ure::gpu::HostTexture tex;
    bool ok = ure::io::load_image_rgb32f(tmp, tex);
    CHECK(ok);
    CHECK(tex.width == 2);
    CHECK(tex.height == 2);
    CHECK(tex.data.size() == 12);

    // File row 0 → output row 0: R(1,0,0) G(0,1,0)
    // File row 1 → output row 1: B(0,0,1) W(1,1,1)
    CHECK_FLOAT_EQ(tex.data[0], 1.0f, 1e-6f);
    CHECK_FLOAT_EQ(tex.data[1], 0.0f, 1e-6f);
    CHECK_FLOAT_EQ(tex.data[2], 0.0f, 1e-6f);
    CHECK_FLOAT_EQ(tex.data[3], 0.0f, 1e-6f);
    CHECK_FLOAT_EQ(tex.data[4], 1.0f, 1e-6f);
    CHECK_FLOAT_EQ(tex.data[5], 0.0f, 1e-6f);
    CHECK_FLOAT_EQ(tex.data[6], 0.0f, 1e-6f);
    CHECK_FLOAT_EQ(tex.data[7], 0.0f, 1e-6f);
    CHECK_FLOAT_EQ(tex.data[8], 1.0f, 1e-6f);
    CHECK_FLOAT_EQ(tex.data[9], 1.0f, 1e-6f);
    CHECK_FLOAT_EQ(tex.data[10], 1.0f, 1e-6f);
    CHECK_FLOAT_EQ(tex.data[11], 1.0f, 1e-6f);

    std::remove(tmp);
    return 0;
}

static int test_spd_loader() {
    const char* tmp = "test_spd_temp.spd";
    {
        std::ofstream f(tmp);
        f << "# test SPD file\n";
        f << "400.0 0.1\n";
        f << "500.0 0.5\n";
        f << "600.0 0.8\n";
        f << "700.0 0.3\n";
    }

    auto data = ure::spectral::load_spd_file(tmp);
    CHECK(!data.lambdas.empty());
    CHECK(data.lambdas.size() == 4);

    // Verify values are in order
    CHECK_FLOAT_EQ(data.lambdas[0], 400.0f, 1e-6f);
    CHECK_FLOAT_EQ(data.values[0], 0.1f, 1e-6f);
    CHECK_FLOAT_EQ(data.lambdas[1], 500.0f, 1e-6f);
    CHECK_FLOAT_EQ(data.values[1], 0.5f, 1e-6f);

    // Resample to 4 uniform wavelengths in [400, 700]
    auto uniform = ure::spectral::resample_uniform(data, 4);
    CHECK(uniform.size() == 4);
    // At 400nm -> spd value is 0.1
    CHECK_FLOAT_EQ(uniform[0], 0.1f, 1e-4f);
    // At 500nm -> spd value is 0.5
    CHECK_FLOAT_EQ(uniform[1], 0.5f, 1e-4f);
    // At 600nm -> spd value is 0.8
    CHECK_FLOAT_EQ(uniform[2], 0.8f, 1e-4f);
    // At 700nm -> spd value is 0.3
    CHECK_FLOAT_EQ(uniform[3], 0.3f, 1e-4f);

    std::remove(tmp);
    return 0;
}

static int test_scene_io_load_spd_runtime_n() {
    const char* tmp = "test_spd_runtime_n.spd";
    {
        std::ofstream f(tmp);
        f << "400.0 0.0\n";
        f << "500.0 1.0\n";
        f << "600.0 0.5\n";
        f << "700.0 0.25\n";
    }

    auto n8 = ure::scene_io::load_spd(tmp, 8);
    CHECK(n8.size() == 8);
    CHECK_FLOAT_EQ(n8[0], 0.0f, 1e-6f);
    CHECK_FLOAT_EQ(n8[7], 0.25f, 1e-6f);

    ure::RenderConfig cfg;
    cfg.num_wavelengths = 6;
    auto n6 = ure::scene_io::load_spd(tmp, cfg);
    CHECK(n6.size() == 6);
    CHECK_FLOAT_EQ(n6[0], 0.0f, 1e-6f);
    CHECK_FLOAT_EQ(n6[5], 0.25f, 1e-6f);

    std::remove(tmp);
    return 0;
}

static int test_missing_texture() {
    ure::gpu::HostTexture tex;
    bool ok = ure::io::load_image_rgb32f("nonexistent_file_xyz.bmp", tex);
    CHECK(!ok);
    return 0;
}

static int test_empty_spd() {
    auto data = ure::spectral::load_spd_file("nonexistent.spd");
    CHECK(data.lambdas.empty());
    CHECK(data.values.empty());

    auto uniform = ure::spectral::resample_uniform(data, 4);
    CHECK(uniform.size() == 4);
    CHECK_FLOAT_EQ(uniform[0], 0.0f, 1e-6f);
    return 0;
}

static int test_spd_unsorted() {
    const char* tmp = "test_spd_unsorted.tmp";
    {
        std::ofstream f(tmp);
        f << "700.0 0.9\n";
        f << "400.0 0.1\n";
        f << "550.0 0.5\n";
    }

    auto data = ure::spectral::load_spd_file(tmp);
    CHECK(data.lambdas.size() == 3);
    CHECK_FLOAT_EQ(data.lambdas[0], 400.0f, 1e-6f);
    CHECK_FLOAT_EQ(data.values[0], 0.1f, 1e-6f);
    CHECK_FLOAT_EQ(data.lambdas[1], 550.0f, 1e-6f);
    CHECK_FLOAT_EQ(data.values[1], 0.5f, 1e-6f);
    CHECK_FLOAT_EQ(data.lambdas[2], 700.0f, 1e-6f);
    CHECK_FLOAT_EQ(data.values[2], 0.9f, 1e-6f);

    std::remove(tmp);
    return 0;
}

static int test_spd_malformed_line() {
    const char* tmp = "test_spd_bad.tmp";
    {
        std::ofstream f(tmp);
        f << "400.0 0.1\n";
        f << "garbage\n";
        f << "500.0 0.5\n";
    }

    auto data = ure::spectral::load_spd_file(tmp);
    CHECK(data.lambdas.size() == 2);
    CHECK_FLOAT_EQ(data.lambdas[0], 400.0f, 1e-6f);
    CHECK_FLOAT_EQ(data.lambdas[1], 500.0f, 1e-6f);

    std::remove(tmp);
    return 0;
}

int main() {
    fprintf(stderr, "[Asset Pipeline Test]\n");

    auto run = [](const char* name, int (*fn)()) {
        fprintf(stderr, "  test: %s ... ", name);
        int r = fn();
        fprintf(stderr, "%s\n", r == 0 ? "PASS" : "FAIL");
        return r != 0;
    };

    int failed = 0;
    failed += run("test_load_image_bmp", test_load_image_bmp);
    failed += run("test_spd_loader", test_spd_loader);
    failed += run("test_scene_io_load_spd_runtime_n", test_scene_io_load_spd_runtime_n);
    failed += run("test_missing_texture", test_missing_texture);
    failed += run("test_empty_spd", test_empty_spd);
    failed += run("test_spd_unsorted", test_spd_unsorted);
    failed += run("test_spd_malformed_line", test_spd_malformed_line);

    fprintf(stderr, "  passed: %d, failed: %d\n", g_passed, failed);
    g_failed += failed;
    if (g_failed > 0) {
        fprintf(stderr, "  OVERALL: FAIL\n");
    } else {
        fprintf(stderr, "  OVERALL: PASS\n");
    }
    return g_failed;
}
