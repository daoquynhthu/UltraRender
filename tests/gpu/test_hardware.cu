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
    CHECK(cfg.spectral_domain_bins == 128);
    CHECK(cfg.spectral_packet_lanes == 8);
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
    CHECK(cfg.spectral_domain_bins == 128);
    CHECK(cfg.spectral_packet_lanes == 16);
    CHECK(cfg.num_wavelengths == cfg.spectral_packet_lanes);
    CHECK(cfg.queue_capacity > 0);
    CHECK(cfg.wg_size == 16);
    return 0;
}

static int test_auto_configure_high_vram() {
    ure::gpu::GpuHardwareInfo hw = {};
    hw.total_global_memory = 24ULL * 1024 * 1024 * 1024;
    hw.sm_count = 128;
    hw.max_threads_per_block = 1024;
    hw.warp_size = 32;

    auto cfg = ure::auto_configure(hw, 1920, 1080, 256);
    CHECK(cfg.spectral_domain_bins == 256);
    CHECK(cfg.spectral_packet_lanes == 32);
    CHECK(cfg.num_wavelengths == cfg.spectral_packet_lanes);
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
    CHECK(cfg.spectral_domain_bins == 512);
    CHECK(cfg.spectral_packet_lanes == 32);
    CHECK(cfg.num_wavelengths == cfg.spectral_packet_lanes);
    CHECK(cfg.wg_size == 32);
    return 0;
}

static int test_auto_configure_scene_N_lower_than_hw_max() {
    ure::gpu::GpuHardwareInfo hw = {};
    hw.total_global_memory = 24ULL * 1024 * 1024 * 1024;
    hw.sm_count = 128;

    auto cfg = ure::auto_configure(hw, 1920, 1080, 16);
    CHECK(cfg.spectral_domain_bins == 16);
    CHECK(cfg.spectral_packet_lanes == 16);
    return 0;
}

static int test_spectral_runtime_plan_million_domain() {
    ure::gpu::GpuHardwareInfo hw = {};
    hw.total_global_memory = 8ULL * 1024 * 1024 * 1024;
    hw.sm_count = 26;

    ure::RenderConfig cfg;
    cfg.spectral_domain_bins = 1000000ULL;
    cfg.spectral_packet_lanes = 16;
    cfg.num_wavelengths = cfg.spectral_packet_lanes;
    auto plan = ure::plan_spectral_runtime(hw, cfg, 4);
    CHECK(plan.domain_bins == 1000000ULL);
    CHECK(plan.packet_lanes == 16);
    CHECK(plan.sampled_domain);
    CHECK(plan.max_resident_bins > 0);
    CHECK(plan.sampler_preset == ure::SpectralSamplerDesktop);
    CHECK(plan.cache_preset == ure::SpectralCacheResident ||
          plan.cache_preset == ure::SpectralCacheCompactStreaming);
    CHECK(plan.max_cuda_streams >= 1);
    return 0;
}

static int test_spectral_runtime_plan_low_end_reject_signal() {
    ure::gpu::GpuHardwareInfo hw = {};
    hw.total_global_memory = 4ULL * 1024 * 1024 * 1024;
    hw.sm_count = 8;

    ure::RenderConfig cfg;
    cfg.spectral_domain_bins = 1'000'000ULL;
    cfg.spectral_packet_lanes = 8;
    cfg.num_wavelengths = cfg.spectral_packet_lanes;
    cfg.spectral_max_resident_mb = 1;

    ure::SpectralSceneResourceStats stats;
    stats.material_count = 8;
    stats.sampled_resource_floats = 512ULL * 1024ULL;
    stats.spectral_texture_floats = 512ULL * 1024ULL;
    stats.spectral_texture_count = 1;

    auto plan = ure::plan_spectral_runtime(hw, cfg, stats);
    CHECK(plan.sampler_preset == ure::SpectralSamplerLowEnd);
    CHECK(plan.cache_preset == ure::SpectralCacheCompactStreaming);
    CHECK(plan.stream_preset == ure::SpectralStreamSingle);
    CHECK(plan.exceeds_resident_budget);
    CHECK(plan.requires_streaming);
    CHECK(plan.max_cuda_streams == 1);
    return 0;
}

static int test_spectral_runtime_plan_high_end_and_farm_presets() {
    ure::gpu::GpuHardwareInfo hw = {};
    hw.total_global_memory = 80ULL * 1024 * 1024 * 1024;
    hw.sm_count = 132;

    ure::RenderConfig cfg;
    cfg.spectral_domain_bins = 1'000'000ULL;
    cfg.spectral_packet_lanes = 32;
    cfg.num_wavelengths = cfg.spectral_packet_lanes;

    ure::SpectralSceneResourceStats stats;
    stats.material_count = 16;
    stats.sampled_resource_floats = 4096;
    auto plan = ure::plan_spectral_runtime(hw, cfg, stats);
    CHECK(plan.sampler_preset == ure::SpectralSamplerHighEnd);
    CHECK(plan.stream_preset == ure::SpectralStreamMulti);
    CHECK(plan.max_cuda_streams == 4);

    cfg.spectral_sampling_mode = ure::SpectralSamplingMode::FarmShard;
    auto farm_plan = ure::plan_spectral_runtime(hw, cfg, stats);
    CHECK(farm_plan.sampler_preset == ure::SpectralSamplerFarmShard);
    CHECK(farm_plan.cache_preset == ure::SpectralCacheFarmShard);
    CHECK(farm_plan.stream_preset == ure::SpectralStreamFarmWorker);
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
    RUN_TEST(test_spectral_runtime_plan_million_domain);
    RUN_TEST(test_spectral_runtime_plan_low_end_reject_signal);
    RUN_TEST(test_spectral_runtime_plan_high_end_and_farm_presets);
    printf("  passed: %d, failed: %d\n", g_tests_passed, g_tests_failed);
    return g_test_result;
}
