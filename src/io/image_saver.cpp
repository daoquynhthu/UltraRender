#include "../../include/io/image_saver.hpp"
#include <fstream>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace ure::io {

// Helper Functions for Tone Mapping
static float aces_film(float x) {
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return std::clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0f, 1.0f);
}

static float reinhard(float x) {
    return x / (1.0f + x);
}

static float linear_clamp(float x) {
    return std::clamp(x, 0.0f, 1.0f);
}

// Gamma correction (sRGB approximation)
static float gamma_correct(float x) {
    return std::pow(x, 1.0f / 2.2f);
}

static float apply_tone_map(float val, ToneMapType type, float exposure) {
    float exposed = val * exposure;
    float mapped = 0.0f;
    
    switch (type) {
        case ToneMapType::Reinhard:
            mapped = reinhard(exposed);
            break;
        case ToneMapType::ACES:
            mapped = aces_film(exposed);
            break;
        case ToneMapType::Linear:
        default:
            mapped = linear_clamp(exposed);
            break;
    }
    
    // Apply Gamma Correction after Tone Mapping
    return gamma_correct(mapped);
}

bool ImageSaver::save_ppm(const std::string& filename, int width, int height, const std::vector<core::Vec3f>& pixels, ToneMapType tm_type, float exposure) {
    std::ofstream ofs(filename, std::ios::binary);
    if (!ofs) return false;
    ofs << "P6\n" << width << " " << height << "\n255\n";
    
    for (const auto& p : pixels) {
        unsigned char r = static_cast<unsigned char>(apply_tone_map(p.x, tm_type, exposure) * 255.0f);
        unsigned char g = static_cast<unsigned char>(apply_tone_map(p.y, tm_type, exposure) * 255.0f);
        unsigned char b = static_cast<unsigned char>(apply_tone_map(p.z, tm_type, exposure) * 255.0f);
        ofs.write(reinterpret_cast<const char*>(&r), 1);
        ofs.write(reinterpret_cast<const char*>(&g), 1);
        ofs.write(reinterpret_cast<const char*>(&b), 1);
    }
    ofs.close();
    return true;
}

bool ImageSaver::save_bmp(const std::string& filename, int width, int height, const std::vector<core::Vec3f>& pixels, ToneMapType tm_type, float exposure) {
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

    for (int y = height - 1; y >= 0; --y) {
        for (int x = 0; x < width; ++x) {
            const auto& p = pixels[y * width + x];
            // BMP uses BGR order
            unsigned char b = static_cast<unsigned char>(std::clamp(apply_tone_map(p.z, tm_type, exposure) * 255.0f, 0.0f, 255.0f));
            unsigned char g = static_cast<unsigned char>(std::clamp(apply_tone_map(p.y, tm_type, exposure) * 255.0f, 0.0f, 255.0f));
            unsigned char r = static_cast<unsigned char>(std::clamp(apply_tone_map(p.x, tm_type, exposure) * 255.0f, 0.0f, 255.0f));
            ofs.write(reinterpret_cast<const char*>(&b), 1);
            ofs.write(reinterpret_cast<const char*>(&g), 1);
            ofs.write(reinterpret_cast<const char*>(&r), 1);
        }
        // Padding for 4-byte alignment
        unsigned char padding[3] = {0, 0, 0};
        int padding_size = (4 - (width * 3) % 4) % 4;
        if (padding_size > 0) ofs.write(reinterpret_cast<const char*>(padding), padding_size);
    }

    ofs.close();
    return true;
}

} // namespace ure::io
