#pragma once

#include <string>
#include <vector>
#include "../core/vector.hpp"

namespace ure::io {

/**
 * @brief Simple Image Saver (PPM/BMP)
 * Used for quick verification without external dependencies.
 */
enum class ToneMapType {
    Linear,
    Reinhard,
    ACES
};

class ImageSaver {
public:
    static bool save_ppm(const std::string& filename, int width, int height, const std::vector<core::Vec3f>& pixels, ToneMapType tm_type = ToneMapType::Linear, float exposure = 1.0f);
    static bool save_bmp(const std::string& filename, int width, int height, const std::vector<core::Vec3f>& pixels, ToneMapType tm_type = ToneMapType::Linear, float exposure = 1.0f);
};

} // namespace ure::io
