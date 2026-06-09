#pragma once
#include <cstddef>
#include <algorithm>

namespace ure {

struct RenderConfig {
    int queue_capacity;
    int max_trace_depth;
    int num_wavelengths;
    int wg_size;
    int rays_per_block;
    int samples_per_pass;
    int num_gpus_to_use;
};

}
