#pragma once
#include <cstddef>
#include <algorithm>

namespace ure {

struct RenderConfig {
    int queue_capacity = 0;       // 0 = auto (width * height)
    int max_trace_depth = 50;
    int num_wavelengths = 4;
    int wg_size = 32;
    int rays_per_block = 256;
    int samples_per_pass = 1;
    int num_gpus_to_use = 1;
};

}
