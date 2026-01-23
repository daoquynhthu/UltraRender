#pragma once

#include <string>
#include <vector>
#include "../core/vector.hpp"

namespace ure::io {

/**
 * @brief 简单的 PPM 图像保存器 (无需外部依赖)
 * 用于在集成高级库前快速验证结果。
 */
class ImageSaver {
public:
    static bool save_ppm(const std::string& filename, int width, int height, const std::vector<core::Vec3f>& pixels);
    static bool save_bmp(const std::string& filename, int width, int height, const std::vector<core::Vec3f>& pixels);
};

} // namespace ure::io
