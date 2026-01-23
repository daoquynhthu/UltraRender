#include "../../include/io/image_saver.hpp"
#include <fstream>
#include <cmath>
#include <algorithm>

namespace ure::io {

bool ImageSaver::save_ppm(const std::string& filename, int width, int height, const std::vector<core::Vec3f>& pixels) {
    std::ofstream ofs(filename, std::ios::binary);
    if (!ofs) return false;
    ofs << "P6\n" << width << " " << height << "\n255\n";
    
    auto tone_map = [](float x) { return std::pow(std::clamp(x, 0.0f, 1.0f), 1.0f / 2.2f); };

    for (const auto& p : pixels) {
        unsigned char r = static_cast<unsigned char>(tone_map(p.x) * 255.0f);
        unsigned char g = static_cast<unsigned char>(tone_map(p.y) * 255.0f);
        unsigned char b = static_cast<unsigned char>(tone_map(p.z) * 255.0f);
        ofs.write(reinterpret_cast<const char*>(&r), 1);
        ofs.write(reinterpret_cast<const char*>(&g), 1);
        ofs.write(reinterpret_cast<const char*>(&b), 1);
    }
    ofs.close();
    return true;
}

bool ImageSaver::save_bmp(const std::string& filename, int width, int height, const std::vector<core::Vec3f>& pixels) {
    std::ofstream ofs(filename, std::ios::out | std::ios::binary);
    if (!ofs.is_open()) return false;

    uint32_t file_size = 54 + 3 * width * height;
    uint32_t reserved = 0;
    uint32_t offset = 54;
    uint32_t header_size = 40;
    uint16_t planes = 1;
    uint16_t bpp = 24;
    uint32_t compression = 0;
    uint32_t img_size = 3 * width * height;
    uint32_t x_ppm = 2835;
    uint32_t y_ppm = 2835;
    uint32_t colors = 0;
    uint32_t important_colors = 0;

    ofs.write("BM", 2);
    ofs.write(reinterpret_cast<char*>(&file_size), 4);
    ofs.write(reinterpret_cast<char*>(&reserved), 4);
    ofs.write(reinterpret_cast<char*>(&offset), 4);
    ofs.write(reinterpret_cast<char*>(&header_size), 4);
    ofs.write(reinterpret_cast<char*>(&width), 4);
    ofs.write(reinterpret_cast<char*>(&height), 4);
    ofs.write(reinterpret_cast<char*>(&planes), 2);
    ofs.write(reinterpret_cast<char*>(&bpp), 2);
    ofs.write(reinterpret_cast<char*>(&compression), 4);
    ofs.write(reinterpret_cast<char*>(&img_size), 4);
    ofs.write(reinterpret_cast<char*>(&x_ppm), 4);
    ofs.write(reinterpret_cast<char*>(&y_ppm), 4);
    ofs.write(reinterpret_cast<char*>(&colors), 4);
    ofs.write(reinterpret_cast<char*>(&important_colors), 4);

    auto tone_map = [](float x) { return std::pow(std::clamp(x, 0.0f, 1.0f), 1.0f / 2.2f); };

    for (int y = height - 1; y >= 0; --y) {
        for (int x = 0; x < width; ++x) {
            const auto& p = pixels[y * width + x];
            // BMP 存储顺序是 BGR，且需要钳制到 [0, 255]
            unsigned char b = static_cast<unsigned char>(std::clamp(tone_map(p.z) * 255.0f, 0.0f, 255.0f));
            unsigned char g = static_cast<unsigned char>(std::clamp(tone_map(p.y) * 255.0f, 0.0f, 255.0f));
            unsigned char r = static_cast<unsigned char>(std::clamp(tone_map(p.x) * 255.0f, 0.0f, 255.0f));
            ofs.write(reinterpret_cast<const char*>(&b), 1);
            ofs.write(reinterpret_cast<const char*>(&g), 1);
            ofs.write(reinterpret_cast<const char*>(&r), 1);
        }
        // BMP 扫描线需 4 字节对齐
        unsigned char padding[3] = {0, 0, 0};
        int padding_size = (4 - (width * 3) % 4) % 4;
        if (padding_size > 0) ofs.write(reinterpret_cast<const char*>(padding), padding_size);
    }

    ofs.close();
    return true;
}

} // namespace ure::io
