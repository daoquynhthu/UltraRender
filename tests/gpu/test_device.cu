#include "test_framework.cuh"

static int test_device_count() {
    REQUIRE_GPU();
    int count = 0;
    CHECK_CUDA(cudaGetDeviceCount(&count));
    CHECK(count > 0);
    printf("  device count: %d\n", count);
    return 0;
}

static int test_device_properties() {
    REQUIRE_GPU();
    cudaDeviceProp prop = {};
    CHECK_CUDA(cudaGetDeviceProperties(&prop, 0));
    printf("  device: %s\n", prop.name);
    printf("  compute capability: %d.%d\n", prop.major, prop.minor);
    printf("  global memory: %.1f GB\n", prop.totalGlobalMem / (1024.0*1024.0*1024.0));
    printf("  SMs: %d\n", prop.multiProcessorCount);
    printf("  max threads per block: %d\n", prop.maxThreadsPerBlock);
    CHECK(prop.major >= 6);
    CHECK(prop.totalGlobalMem > 0);
    CHECK(prop.multiProcessorCount > 0);
    return 0;
}

int main() {
    printf("[GPU Device Test]\n");
    RUN_TEST(test_device_count);
    RUN_TEST(test_device_properties);
    printf("  passed: %d, failed: %d\n", g_tests_passed, g_tests_failed);
    return g_test_result;
}
