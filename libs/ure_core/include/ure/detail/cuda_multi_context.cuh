#pragma once

#include "ure/gpu_multi_driver.hpp"

namespace ure::gpu {

struct MultiGpuContext {
    int num_gpus = 0;
    int width = 0;
    int height = 0;
    GpuContext** contexts = nullptr;
    GpuVec3* d_merged_accum = nullptr;
    int* d_merged_counts = nullptr;
    float* d_path_guiding_baseline_light = nullptr;
    float* d_path_guiding_baseline_spatial = nullptr;
    float* d_path_guiding_temp_light = nullptr;
    float* d_path_guiding_temp_spatial = nullptr;
    size_t path_guiding_light_count = 0;
    size_t path_guiding_spatial_count = 0;
};

}
