#include "ure/gpu_hardware.hpp"
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
    printf("[GPU Hardware]\n");
    printf("  device_count:          %d\n", hw.device_count);
    printf("  sm_count:              %d\n", hw.sm_count);
    printf("  compute_capability:    %d.%d\n", hw.cc_major, hw.cc_minor);
    printf("  global_memory:         %.1f GB\n",
           hw.total_global_memory / (1024.0 * 1024.0 * 1024.0));
    printf("  l1_cache_per_sm:       %zu KB\n", hw.l1_cache_per_sm / 1024);
    printf("  max_threads_per_block: %d\n", hw.max_threads_per_block);
    printf("  warp_size:             %d\n", hw.warp_size);
    printf("  memory_bandwidth:      %.1f GB/s\n", hw.memory_bandwidth_gb_s);
}

}
