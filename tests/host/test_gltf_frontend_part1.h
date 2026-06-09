#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>

#include <ure/gltf_scene_frontend.hpp>
#include <ure/scene_ir.hpp>
#include <ure/scene_parser.hpp>
#include <ure/log.hpp>

static int g_passed = 0, g_failed = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        g_failed++; \
        return 1; \
    } \
    g_passed++; \
} while(0)

#define CHECK_FLOAT_EQ(a, b, eps) do { \
    float _a = (a), _b = (b), _e = (eps); \
    if (fabsf(_a - _b) > _e) { \
        fprintf(stderr, "  FAIL: %s:%d: %s == %s (%.6f vs %.6f)\n", \
                __FILE__, __LINE__, #a, #b, (double)_a, (double)_b); \
        g_failed++; \
        return 1; \
    } \
    g_passed++; \
} while(0)

static int g_counter = 0;

static std::string write_temp(const std::string& content, const std::string& ext) {
    std::string name = "test_gltf_" + std::to_string(g_counter++) + ext;
    std::ofstream f(name, std::ios::binary);
    f.write(content.data(), content.size());
    return name;
}