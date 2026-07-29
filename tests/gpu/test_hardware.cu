#include <exception>

#include "test_framework.cuh"
#include "ure/backend.hpp"
#include "ure/gpu_hardware.hpp"
#include "ure/gpu_auto_config.hpp"
#include "ure/log.hpp"

template <typename Fn>
static bool throws_exception(Fn&& fn) {
    try {
        fn();
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

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

static int test_backend_identity_and_capability_contract() {
    REQUIRE_GPU();
    CHECK(ure::parse_backend_kind("auto") == ure::BackendKind::Auto);
    CHECK(ure::parse_backend_kind("cuda") == ure::BackendKind::Cuda);
    CHECK(ure::parse_backend_kind("vulkan") == ure::BackendKind::Vulkan);
    CHECK(!ure::parse_backend_kind("invalid"));
    CHECK(
        ure::parse_acceleration_provider("self_compute") ==
        ure::AccelerationProviderKind::SelfCompute);
    CHECK(
        ure::parse_acceleration_provider("optix") ==
        ure::AccelerationProviderKind::Optix);
    CHECK(
        ure::parse_acceleration_quality("high_quality") ==
        ure::AccelerationBuildQuality::HighQuality);
    CHECK(
        ure::parse_acceleration_update_policy("refit") ==
        ure::AccelerationUpdatePolicy::Refit);
    CHECK(!ure::parse_acceleration_provider("invalid"));
    CHECK(
        ure::parse_backend_feature("ray_query") ==
        ure::BackendFeature::RayQuery);
    CHECK(
        ure::parse_backend_feature("ray_tracing_pipeline") ==
        ure::BackendFeature::RayTracingPipeline);
    const auto adapters = ure::enumerate_backend_adapters();
    CHECK(!adapters.empty());
    const auto& adapter = adapters.front();
    CHECK(adapter.kind == ure::BackendKind::Cuda);
    CHECK(adapter.vendor_id != 0);
    CHECK(adapter.device_id != 0);
    CHECK(adapter.adapter_id.starts_with("cuda:"));
    CHECK(!adapter.name.empty());
    CHECK(adapter.vendor_id == 0x10de);
    CHECK(adapter.features != 0);
    CHECK(ure::backend_has_features(
        adapter.features,
        ure::backend_feature_bit(
            ure::BackendFeature::SpectralTransport) |
        ure::backend_feature_bit(
            ure::BackendFeature::Polarization) |
        ure::backend_feature_bit(
            ure::BackendFeature::SelfComputeTraversal)));
    CHECK(adapter.limits.max_workgroup_threads > 0);
    CHECK(adapter.limits.subgroup_size == 32);
    CHECK(adapter.limits.max_spectral_packet_lanes == 32);
    CHECK(adapter.memory.total_bytes > 0);
    CHECK(adapter.memory.available_bytes > 0);
    CHECK(!adapter.driver_identity.empty());
    CHECK(!adapter.compiler_identity.empty());
    const auto vulkan_adapters =
        ure::enumerate_backend_adapters(
            ure::BackendKind::Vulkan);
    CHECK(!vulkan_adapters.empty());
    for (const auto& vulkan_adapter : vulkan_adapters) {
        CHECK(vulkan_adapter.kind ==
              ure::BackendKind::Vulkan);
        CHECK(vulkan_adapter.adapter_id.starts_with(
            "vulkan:"));
        CHECK(ure::backend_has_features(
            vulkan_adapter.features,
            ure::backend_feature_bit(
                ure::BackendFeature::Compute) |
            ure::backend_feature_bit(
                ure::BackendFeature::Subgroup) |
            ure::backend_feature_bit(
                ure::BackendFeature::SpectralTransport) |
            ure::backend_feature_bit(
                ure::BackendFeature::Polarization) |
            ure::backend_feature_bit(
                ure::BackendFeature::WaveReference)));
        CHECK(!ure::backend_has_features(
            vulkan_adapter.features,
            ure::backend_feature_bit(
                ure::BackendFeature::SelfComputeTraversal)));
    }

    ure::RenderConfig config;
    auto automatic = ure::select_backend(config);
    CHECK(automatic.adapter.kind == ure::BackendKind::Cuda);
    CHECK(automatic.memory_budget_bytes > 0);
    CHECK(automatic.memory_budget_bytes <=
          automatic.adapter.memory.available_bytes);

    config.backend.kind = ure::BackendKind::Cuda;
    config.backend.adapter_id = adapter.adapter_id;
    config.backend.adapter_ordinal =
        std::numeric_limits<std::uint32_t>::max();
    config.backend.memory_budget_bytes = 64ull * 1024ull * 1024ull;
    const auto explicit_selection = ure::select_backend(config);
    CHECK(explicit_selection.adapter.adapter_id == adapter.adapter_id);
    CHECK(explicit_selection.memory_budget_bytes ==
          config.backend.memory_budget_bytes);
    config.acceleration.provider =
        ure::AccelerationProviderKind::SelfCompute;
    config.acceleration.update_policy =
        ure::AccelerationUpdatePolicy::Static;
    CHECK(
        ure::select_backend(config).adapter.kind ==
        ure::BackendKind::Cuda);
    config.acceleration.quality =
        ure::AccelerationBuildQuality::FastBuild;
    CHECK(
        ure::select_backend(config).adapter.kind ==
        ure::BackendKind::Cuda);
    config.acceleration.quality =
        ure::AccelerationBuildQuality::Balanced;
    CHECK(
        ure::select_backend(config).adapter.kind ==
        ure::BackendKind::Cuda);
    config.acceleration.quality =
        ure::AccelerationBuildQuality::HighQuality;
    CHECK(
        ure::select_backend(config).adapter.kind ==
        ure::BackendKind::Cuda);
    config.acceleration.quality =
        ure::AccelerationBuildQuality::Automatic;
    config.acceleration.update_policy =
        ure::AccelerationUpdatePolicy::Refit;
    CHECK(
        ure::select_backend(config).adapter.kind ==
        ure::BackendKind::Cuda);
    config.acceleration.update_policy =
        ure::AccelerationUpdatePolicy::Rebuild;
    CHECK(
        ure::select_backend(config).adapter.kind ==
        ure::BackendKind::Cuda);
    config.acceleration.update_policy =
        ure::AccelerationUpdatePolicy::Automatic;
    config.acceleration.clustered_geometry_enabled = true;
    CHECK(throws_exception([&] { (void)ure::select_backend(config); }));
    config.acceleration.clustered_geometry_enabled = false;
    config.acceleration.collect_stats = true;
    CHECK(
        ure::select_backend(config).adapter.kind ==
        ure::BackendKind::Cuda);
    config.acceleration.collect_stats = false;
    config.acceleration.scratch_budget_bytes = 1024;
    CHECK(
        ure::select_backend(config).adapter.kind ==
        ure::BackendKind::Cuda);
    config.acceleration.scratch_budget_bytes = 0;
    config.acceleration.provider =
        ure::AccelerationProviderKind::Optix;
    CHECK(throws_exception([&] { (void)ure::select_backend(config); }));
    config.acceleration.provider =
        ure::AccelerationProviderKind::Automatic;

    config.backend.adapter_id = "cuda:missing";
    CHECK(throws_exception([&] { (void)ure::select_backend(config); }));
    config.backend.adapter_id.clear();
    config.backend.adapter_ordinal =
        std::numeric_limits<std::uint32_t>::max();
    CHECK(throws_exception([&] { (void)ure::select_backend(config); }));
    config.backend.adapter_ordinal = 0;
    config.backend.required_features = 1ull << 63;
    CHECK(throws_exception([&] { (void)ure::select_backend(config); }));
    config.backend.required_features = 0;
    config.backend.kind = ure::BackendKind::Vulkan;
    CHECK(throws_exception([&] { (void)ure::select_backend(config); }));
    config.backend.kind = ure::BackendKind::D3D12;
    CHECK(throws_exception([&] { (void)ure::select_backend(config); }));
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
    RUN_TEST(test_backend_identity_and_capability_contract);
    printf("  passed: %d, failed: %d\n", g_tests_passed, g_tests_failed);
    return g_test_result;
}
