#pragma once
#include <cuda_runtime.h>
#include <stdio.h>

namespace ure::gpu {

struct GpuHardwareInfo {
    int device_count;
    int sm_count;
    int cc_major;
    int cc_minor;
    size_t total_global_memory;
    size_t l1_cache_per_sm;
    int max_threads_per_block;
    int warp_size;
    float memory_bandwidth_gb_s;
};

GpuHardwareInfo query_hardware(int device_id = 0);

void print_hardware_info(const GpuHardwareInfo& hw);

}
