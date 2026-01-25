#include <gtest/gtest.h>
#include "io/image_saver.hpp"
#include <vector>
#include <filesystem>

using namespace ure::io;
using namespace ure::core;

class ImageSaverTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create output directory if not exists
        std::filesystem::create_directories("test_output");
    }

    std::vector<Vec3f> create_hdr_pattern(int width, int height) {
        std::vector<Vec3f> pixels(width * height);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                float u = (float)x / width;
                float v = (float)y / height;
                // Gradient from 0 to 5.0 (HDR)
                pixels[y * width + x] = Vec3f(u * 5.0f, v * 5.0f, 0.5f);
            }
        }
        return pixels;
    }
};

TEST_F(ImageSaverTest, SaveBmpLinear) {
    int width = 100;
    int height = 100;
    auto pixels = create_hdr_pattern(width, height);
    
    bool result = ImageSaver::save_bmp("test_output/linear.bmp", width, height, pixels, ToneMapType::Linear, 1.0f);
    EXPECT_TRUE(result);
    EXPECT_TRUE(std::filesystem::exists("test_output/linear.bmp"));
}

TEST_F(ImageSaverTest, SaveBmpReinhard) {
    int width = 100;
    int height = 100;
    auto pixels = create_hdr_pattern(width, height);
    
    bool result = ImageSaver::save_bmp("test_output/reinhard.bmp", width, height, pixels, ToneMapType::Reinhard, 1.0f);
    EXPECT_TRUE(result);
    EXPECT_TRUE(std::filesystem::exists("test_output/reinhard.bmp"));
}

TEST_F(ImageSaverTest, SaveBmpACES) {
    int width = 100;
    int height = 100;
    auto pixels = create_hdr_pattern(width, height);
    
    bool result = ImageSaver::save_bmp("test_output/aces.bmp", width, height, pixels, ToneMapType::ACES, 1.0f);
    EXPECT_TRUE(result);
    EXPECT_TRUE(std::filesystem::exists("test_output/aces.bmp"));
}

TEST_F(ImageSaverTest, SaveBmpExposure) {
    int width = 100;
    int height = 100;
    auto pixels = create_hdr_pattern(width, height);
    
    // Low exposure should darken the image
    bool result = ImageSaver::save_bmp("test_output/exposure_0.5.bmp", width, height, pixels, ToneMapType::Linear, 0.5f);
    EXPECT_TRUE(result);
    EXPECT_TRUE(std::filesystem::exists("test_output/exposure_0.5.bmp"));
}
