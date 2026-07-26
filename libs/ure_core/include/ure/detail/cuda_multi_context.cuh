#pragma once

#include <cstdint>

#include "ure/detail/cuda_multi_driver.cuh"
#include "ure/runtime/runtime.hpp"

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
    std::uint32_t execution_graph_schema = 0;
    std::uint32_t lowered_execution_node_count = 0;
    std::uint32_t lowered_dispatch_count = 0;
    std::uint32_t lowered_indirect_dispatch_count = 0;
    runtime::SubmissionId completed_submissions = 0;
};

}
