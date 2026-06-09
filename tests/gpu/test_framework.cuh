#pragma once

#include <cuda_runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static int g_tests_passed = 0;
static int g_tests_failed = 0;
static int g_test_result = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        g_tests_failed++; \
        return 1; \
    } \
    g_tests_passed++; \
} while(0)

#define CHECK_FLOAT_EQ(a, b, eps) do { \
    float _a = (a), _b = (b), _e = (eps); \
    if (fabsf(_a - _b) > _e) { \
        fprintf(stderr, "  FAIL: %s:%d: %s ≈ %s (got %f, expected %f, eps %f)\n", \
                __FILE__, __LINE__, #a, #b, _a, _b, _e); \
        g_tests_failed++; \
        return 1; \
    } \
    g_tests_passed++; \
} while(0)

#define CHECK_VEC3_EQ(a, b, eps) do { \
    CHECK_FLOAT_EQ((a).x, (b).x, eps); \
    CHECK_FLOAT_EQ((a).y, (b).y, eps); \
    CHECK_FLOAT_EQ((a).z, (b).z, eps); \
} while(0)

#define CHECK_CUDA(call) do { \
    cudaError_t _err = (call); \
    if (_err != cudaSuccess) { \
        fprintf(stderr, "  FAIL: %s:%d: %s => %s\n", \
                __FILE__, __LINE__, #call, cudaGetErrorString(_err)); \
        g_tests_failed++; \
        return 1; \
    } \
    g_tests_passed++; \
} while(0)

// RAII guard for device memory (ensures cleanup on early return)
struct DeviceMem {
    void* ptr = nullptr;
    explicit DeviceMem(void* p) : ptr(p) {}
    ~DeviceMem() { if (ptr) cudaFree(ptr); }
    DeviceMem(const DeviceMem&) = delete;
    DeviceMem& operator=(const DeviceMem&) = delete;
};

inline bool has_cuda_gpu() {
    int count = 0;
    cudaError_t err = cudaGetDeviceCount(&count);
    return (err == cudaSuccess && count > 0);
}

#define REQUIRE_GPU() do { \
    if (!has_cuda_gpu()) { \
        printf("  SKIP: no CUDA device available\n"); \
        return 77; \
    } \
} while(0)

#define RUN_TEST(name) do { \
    printf("  test: %s ... ", #name); \
    fflush(stdout); \
    int _r = name(); \
    if (_r == 0) printf("PASS\n"); \
    else if (_r == 77) printf("SKIP\n"); \
    else printf("FAIL\n"); \
    g_test_result |= _r; \
} while(0)
