#pragma once

#include <vector>

namespace ure::gpu {

// Host-side texture data: RGB textures use 3 channels; explicit spectral
// textures use channels as source spectral sample count over the visible domain.
// No CUDA dependency — usable from ure_types (header-only INTERFACE library).
struct HostTexture {
    int width;
    int height;
    int channels = 3;
    std::vector<float> data;
};

} // namespace ure::gpu
