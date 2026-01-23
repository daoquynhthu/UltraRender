#include "../../include/gpu/gpu_driver.hpp"
#include <iostream>

namespace ure::gpu {

bool is_gpu_available() {
    return false;
}

void run_gpu_test() {
    std::cout << "[GPU] CUDA support not compiled in. Running in CPU-only mode.\n";
}

} // namespace ure::gpu
