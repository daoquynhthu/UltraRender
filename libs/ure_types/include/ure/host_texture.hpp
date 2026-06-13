#pragma once

#include <vector>

namespace ure::gpu {

// Host-side texture data: width * height pixels, 3 floats per pixel (RGB).
// No CUDA dependency — usable from ure_types (header-only INTERFACE library).
struct HostTexture {
    int width;
    int height;
    int channels = 3;
    std::vector<float> data;
};

} // namespace ure::gpu
