#include "ure/gpu_hardware.hpp"

#include <ure/log.hpp>

#include <algorithm>

namespace ure::gpu {

static int get_attr(cudaDeviceAttr attr, int device) {
    int val = 0;
    cudaDeviceGetAttribute(&val, attr, device);
    return val;
}

GpuHardwareInfo query_hardware(int device_id) {
    GpuHardwareInfo info = {};

    cudaError_t err = cudaGetDeviceCount(&info.device_count);
    if (err != cudaSuccess || info.device_count == 0) {
        info.device_count = 0;
        return info;
    }

    int actual_device = std::min(device_id, info.device_count - 1);
    cudaSetDevice(actual_device);

    info.sm_count = get_attr(cudaDevAttrMultiProcessorCount, actual_device);
    info.cc_major = get_attr(cudaDevAttrComputeCapabilityMajor, actual_device);
    info.cc_minor = get_attr(cudaDevAttrComputeCapabilityMinor, actual_device);
    info.max_threads_per_block = get_attr(cudaDevAttrMaxThreadsPerBlock, actual_device);
    info.warp_size = get_attr(cudaDevAttrWarpSize, actual_device);

    size_t free_mem = 0, total_mem = 0;
    cudaMemGetInfo(&free_mem, &total_mem);
    info.total_global_memory = total_mem;

    info.l1_cache_per_sm = 0;

    int mem_clock = get_attr(cudaDevAttrMemoryClockRate, actual_device);
    int bus_width = get_attr(cudaDevAttrGlobalMemoryBusWidth, actual_device);
    float mem_clock_mhz = (float)mem_clock / 1000.0f;
    info.memory_bandwidth_gb_s = mem_clock_mhz * bus_width * 2.0f / 8.0f / 1000.0f;

    return info;
}

void print_hardware_info(const GpuHardwareInfo& hw) {
    UR_LOG_INFO(GPU, "Hardware info:");
    UR_LOG_INFO(GPU, "  device_count:           {}", hw.device_count);
    UR_LOG_INFO(GPU, "  sm_count:               {}", hw.sm_count);
    UR_LOG_INFO(GPU, "  compute_capability:     {}.{}", hw.cc_major, hw.cc_minor);
    UR_LOG_INFO(GPU, "  global_memory:          {:.1f} GB",
                static_cast<double>(hw.total_global_memory) / (1024.0 * 1024.0 * 1024.0));
    UR_LOG_INFO(GPU, "  l1_cache_per_sm:        {} KB", hw.l1_cache_per_sm / 1024);
    UR_LOG_INFO(GPU, "  max_threads_per_block:  {}", hw.max_threads_per_block);
    UR_LOG_INFO(GPU, "  warp_size:              {}", hw.warp_size);
    UR_LOG_INFO(GPU, "  memory_bandwidth:       {:.1f} GB/s", static_cast<double>(hw.memory_bandwidth_gb_s));
}

}
