#include <ure/distributed_file_io.hpp>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

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

#define CHECK_FLOAT_EQ(a, b, eps) do { \
    float _a = (a); \
    float _b = (b); \
    float _e = (eps); \
    if (std::fabs(_a - _b) > _e) { \
        std::fprintf(stderr, "  FAIL: %s:%d: %s == %s (%.6f vs %.6f)\n", \
                     __FILE__, __LINE__, #a, #b, _a, _b); \
        ++g_failed; \
        return 1; \
    } \
    ++g_passed; \
} while (0)

template <typename Fn>
static bool throws_exception(Fn&& fn) {
    try {
        fn();
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

static std::filesystem::path temp_path(const char* name) {
    return std::filesystem::temp_directory_path() / name;
}

static void remove_if_exists(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

static ure::gpu::DistributedFrameBuffer make_view(int width,
                                                  int height,
                                                  int samples,
                                                  std::vector<float>& data) {
    data.resize(static_cast<size_t>(width * height * 3));
    return {width, height, samples, data.data()};
}

static int test_sample_range_file_roundtrip() {
    const auto path = temp_path("ure_range_roundtrip.urd");
    remove_if_exists(path);
    ure::gpu::DistributedSampleRange range = ure::gpu::make_sample_range(2, 5, 17, 8, 4);
    ure::gpu::write_sample_range_file(path, range);

    ure::gpu::DistributedSampleRange loaded = ure::gpu::read_sample_range_file(path);
    CHECK(loaded.node_id == 2);
    CHECK(loaded.node_count == 5);
    CHECK(loaded.sample_start == 8);
    CHECK(loaded.sample_count == 3);
    CHECK(loaded.total_samples == 17);
    CHECK(loaded.width == 8);
    CHECK(loaded.height == 4);
    CHECK(ure::gpu::validate_sample_range(loaded));

    remove_if_exists(path);
    return 0;
}

static int test_framebuffer_file_roundtrip() {
    const auto path = temp_path("ure_frame_roundtrip.urf");
    remove_if_exists(path);
    std::vector<float> data;
    ure::gpu::DistributedFrameBuffer fb = make_view(2, 2, 7, data);
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<float>(i) * 0.25f;
    }

    ure::gpu::write_framebuffer_file(path, fb);
    ure::gpu::DistributedFrameBufferStorage loaded = ure::gpu::read_framebuffer_file(path);
    CHECK(loaded.width == 2);
    CHECK(loaded.height == 2);
    CHECK(loaded.total_samples == 7);
    CHECK(loaded.data.size() == data.size());
    for (size_t i = 0; i < data.size(); ++i) {
        CHECK_FLOAT_EQ(loaded.data[i], data[i], 1e-6f);
    }

    remove_if_exists(path);
    return 0;
}

static int test_framebuffer_file_merge() {
    const auto accum_path = temp_path("ure_frame_accum.urf");
    const auto incoming_a_path = temp_path("ure_frame_incoming_a.urf");
    const auto incoming_b_path = temp_path("ure_frame_incoming_b.urf");
    const auto output_path = temp_path("ure_frame_output.urf");
    remove_if_exists(accum_path);
    remove_if_exists(incoming_a_path);
    remove_if_exists(incoming_b_path);
    remove_if_exists(output_path);

    std::vector<float> accum_data;
    std::vector<float> incoming_a_data;
    std::vector<float> incoming_b_data;
    auto accum = make_view(2, 1, 1, accum_data);
    auto incoming_a = make_view(2, 1, 2, incoming_a_data);
    auto incoming_b = make_view(2, 1, 3, incoming_b_data);
    for (int i = 0; i < 6; ++i) {
        accum_data[static_cast<size_t>(i)] = 1.0f;
        incoming_a_data[static_cast<size_t>(i)] = 2.0f;
        incoming_b_data[static_cast<size_t>(i)] = 3.0f;
    }

    ure::gpu::write_framebuffer_file(accum_path, accum);
    ure::gpu::write_framebuffer_file(incoming_a_path, incoming_a);
    ure::gpu::write_framebuffer_file(incoming_b_path, incoming_b);
    ure::gpu::merge_framebuffer_files(accum_path, {incoming_a_path, incoming_b_path}, output_path);

    ure::gpu::DistributedFrameBufferStorage output = ure::gpu::read_framebuffer_file(output_path);
    CHECK(output.total_samples == 6);
    for (float value : output.data) {
        CHECK_FLOAT_EQ(value, 6.0f, 1e-6f);
    }

    remove_if_exists(accum_path);
    remove_if_exists(incoming_a_path);
    remove_if_exists(incoming_b_path);
    remove_if_exists(output_path);
    return 0;
}

static int test_invalid_range_file_inputs() {
    const auto path = temp_path("ure_invalid_range.urd");
    remove_if_exists(path);
    ure::gpu::DistributedSampleRange invalid = {0, 2, 3, 4, 5, 8, 8};
    CHECK(throws_exception([&] { ure::gpu::write_sample_range_file(path, invalid); }));

    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << "bad";
    }
    CHECK(throws_exception([&] { (void)ure::gpu::read_sample_range_file(path); }));

    remove_if_exists(path);
    return 0;
}

static int test_invalid_framebuffer_file_inputs() {
    const auto path = temp_path("ure_invalid_frame.urf");
    remove_if_exists(path);
    ure::gpu::DistributedFrameBuffer null_fb = {2, 2, 1, nullptr};
    CHECK(throws_exception([&] { ure::gpu::write_framebuffer_file(path, null_fb); }));

    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << "URDFRAME";
        int version = 1;
        int width = 2;
        int height = 2;
        int samples = 1;
        out.write(reinterpret_cast<const char*>(&version), sizeof(version));
        out.write(reinterpret_cast<const char*>(&width), sizeof(width));
        out.write(reinterpret_cast<const char*>(&height), sizeof(height));
        out.write(reinterpret_cast<const char*>(&samples), sizeof(samples));
        float partial = 1.0f;
        out.write(reinterpret_cast<const char*>(&partial), sizeof(partial));
    }
    CHECK(throws_exception([&] { (void)ure::gpu::read_framebuffer_file(path); }));

    remove_if_exists(path);
    return 0;
}

int main() {
    std::fprintf(stderr, "[Distributed File IO Test]\n");
    auto run = [](const char* name, int (*fn)()) {
        std::fprintf(stderr, "  test: %s ... ", name);
        int result = fn();
        std::fprintf(stderr, "%s\n", result == 0 ? "PASS" : "FAIL");
        return result;
    };

    int failed = 0;
    failed += run("test_sample_range_file_roundtrip", test_sample_range_file_roundtrip);
    failed += run("test_framebuffer_file_roundtrip", test_framebuffer_file_roundtrip);
    failed += run("test_framebuffer_file_merge", test_framebuffer_file_merge);
    failed += run("test_invalid_range_file_inputs", test_invalid_range_file_inputs);
    failed += run("test_invalid_framebuffer_file_inputs", test_invalid_framebuffer_file_inputs);

    std::fprintf(stderr, "  passed: %d, failed: %d\n", g_passed, g_failed);
    return failed || g_failed ? 1 : 0;
}
