#include "test_framework.cuh"
#include "ure/gpu_hardware.hpp"
#include "ure/gpu_auto_config.hpp"
#include "ure/log.hpp"

static int test_query_hardware() {
    REQUIRE_GPU();
    auto hw = ure::gpu::query_hardware(0);
    CHECK(hw.device_count > 0);
    CHECK(hw.sm_count > 0);
    CHECK(hw.total_global_memory > 0);
    CHECK(hw.max_threads_per_block > 0);
    CHECK(hw.warp_size == 32);
    ure::gpu::print_hardware_info(hw);
    return 0;
}

static int test_auto_configure_low_vram() {
    ure::gpu::GpuHardwareInfo hw = {};
    hw.total_global_memory = 4ULL * 1024 * 1024 * 1024;
    hw.sm_count = 8;
    hw.max_threads_per_block = 1024;
    hw.warp_size = 32;

    auto cfg = ure::auto_configure(hw, 1920, 1080, 128);
    CHECK(cfg.num_wavelengths == 8);
    CHECK(cfg.queue_capacity > 0);
    CHECK(cfg.wg_size == 8);
    return 0;
}

static int test_auto_configure_medium_vram() {
    ure::gpu::GpuHardwareInfo hw = {};
    hw.total_global_memory = 8ULL * 1024 * 1024 * 1024;
    hw.sm_count = 26;
    hw.max_threads_per_block = 1024;
    hw.warp_size = 32;

    auto cfg = ure::auto_configure(hw, 1920, 1080, 128);
    CHECK(cfg.num_wavelengths == 64);
    CHECK(cfg.queue_capacity > 0);
    CHECK(cfg.wg_size == 32);
    return 0;
}

static int test_auto_configure_high_vram() {
    ure::gpu::GpuHardwareInfo hw = {};
    hw.total_global_memory = 24ULL * 1024 * 1024 * 1024;
    hw.sm_count = 128;
    hw.max_threads_per_block = 1024;
    hw.warp_size = 32;

    auto cfg = ure::auto_configure(hw, 1920, 1080, 256);
    CHECK(cfg.num_wavelengths == 128);
    CHECK(cfg.queue_capacity > 0);
    CHECK(cfg.wg_size == 32);
    return 0;
}

static int test_auto_configure_ultra_vram() {
    ure::gpu::GpuHardwareInfo hw = {};
    hw.total_global_memory = 80ULL * 1024 * 1024 * 1024;
    hw.sm_count = 132;
    hw.max_threads_per_block = 1024;
    hw.warp_size = 32;

    auto cfg = ure::auto_configure(hw, 3840, 2160, 512);
    CHECK(cfg.num_wavelengths == 512);
    CHECK(cfg.wg_size == 32);
    return 0;
}

static int test_auto_configure_scene_N_lower_than_hw_max() {
    ure::gpu::GpuHardwareInfo hw = {};
    hw.total_global_memory = 24ULL * 1024 * 1024 * 1024;

    auto cfg = ure::auto_configure(hw, 1920, 1080, 16);
    CHECK(cfg.num_wavelengths == 16);
    return 0;
}

int main() {
    ure::log::set_min_level(ure::log::Level::Warn);
    printf("[Hardware Config Test]\n");
    RUN_TEST(test_query_hardware);
    RUN_TEST(test_auto_configure_low_vram);
    RUN_TEST(test_auto_configure_medium_vram);
    RUN_TEST(test_auto_configure_high_vram);
    RUN_TEST(test_auto_configure_ultra_vram);
    RUN_TEST(test_auto_configure_scene_N_lower_than_hw_max);
    printf("  passed: %d, failed: %d\n", g_tests_passed, g_tests_failed);
    return g_test_result;
}
