#include <vector>

#include "test_framework.cuh"
#include "ure/gpu_structs.hpp"
#include "ure/gpu_driver.hpp"
#include "ure/gpu_context.hpp"
#include "ure/log.hpp"

using namespace ure::gpu;

static int test_tangent_upload_readback() {
    REQUIRE_GPU();

    RenderMesh mesh;
    mesh.vertices = {0.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f};
    mesh.normals  = {0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f};
    mesh.uvs      = {0.0f, 0.0f,         1.0f, 0.0f,         0.0f, 1.0f};
    mesh.tangents = {1.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f};
    mesh.indices  = {0, 1, 2};
    mesh.material_index = 0;

    std::vector<RenderMesh> meshes = {mesh};
    std::vector<GpuMaterialData> materials(1);
    materials[0].header.type = MaterialType::Lambertian;
    materials[0].albedo = SpectralPacket(0.8f, 0.8f, 0.8f);

    std::vector<GpuInstance> instances(1);
    instances[0].mesh_index = 0;
    instances[0].material_index = 0;
    instances[0].transform = GpuMat4::identity();
    instances[0].inverse_transform = GpuMat4::identity();
    instances[0].min_pt = {-1, -1, -1};
    instances[0].max_pt = {1, 1, 1};

    GpuContext* ctx = init_gpu_renderer(
        64, 64, meshes, instances, {}, materials, {});
    CHECK(ctx != nullptr);

    // Allocate readback buffer
    GpuVec3* d_readback;
    GpuVec3 h_readback[3];
    CHECK_CUDA(cudaMalloc(&d_readback, 3 * sizeof(GpuVec3)));
    DeviceMem _d(d_readback);
    CHECK_CUDA(cudaMemset(d_readback, 0, 3 * sizeof(GpuVec3)));

    render_pass_gpu(ctx, 1);

    // Read back tangents via cudaMemcpy from device mesh directly.
    // We know d_meshes is a device pointer from the context.
    // Copy d_meshes[0] to host so we can read mesh.tangents (which is a device ptr).
    GpuMesh h_mesh;
    CHECK_CUDA(cudaMemcpy(&h_mesh, ctx->d_meshes, sizeof(GpuMesh), cudaMemcpyDeviceToHost));
    CHECK(h_mesh.tangents != nullptr);

    // Now copy the tangent data from device to host
    CHECK_CUDA(cudaMemcpy(h_readback, h_mesh.tangents, 3 * sizeof(GpuVec3), cudaMemcpyDeviceToHost));

    for (int i = 0; i < 3; ++i) {
        CHECK_FLOAT_EQ(h_readback[i].x, 1.0f, 1e-4f);
        CHECK_FLOAT_EQ(h_readback[i].y, 0.0f, 1e-4f);
        CHECK_FLOAT_EQ(h_readback[i].z, 0.0f, 1e-4f);
    }

    free_gpu_renderer(ctx);
    return 0;
}

// Test mesh without tangents (should still upload with mesh.tangents empty)
static int test_no_tangents_fallback() {
    REQUIRE_GPU();

    RenderMesh mesh;
    mesh.vertices = {0.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f};
    mesh.normals  = {0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f};
    mesh.uvs      = {0.0f, 0.0f,  1.0f, 0.0f,  0.0f, 1.0f};
    // Intentionally empty tangents
    mesh.indices  = {0, 1, 2};
    mesh.material_index = 0;

    std::vector<RenderMesh> meshes = {mesh};
    std::vector<GpuMaterialData> materials(1);
    materials[0].header.type = MaterialType::Lambertian;
    materials[0].albedo = SpectralPacket(0.8f, 0.8f, 0.8f);

    std::vector<GpuInstance> instances(1);
    instances[0].mesh_index = 0;
    instances[0].material_index = 0;
    instances[0].transform = GpuMat4::identity();
    instances[0].inverse_transform = GpuMat4::identity();
    instances[0].min_pt = {-1, -1, -1};
    instances[0].max_pt = {1, 1, 1};

    GpuContext* ctx = init_gpu_renderer(
        64, 64, meshes, instances, {}, materials, {});
    CHECK(ctx != nullptr);

    // Render without tangents (should not crash)
    render_pass_gpu(ctx, 1);

    // Just verify rendering works (tangents path is nullptr, no crash)
    std::vector<float> fb(64 * 64 * 3);
    copy_frame_buffer_gpu(ctx, fb.data());

    bool has_radiance = false;
    for (int i = 0; i < 64 * 64; ++i) {
        if (fb[i*3] > 0.0f || fb[i*3+1] > 0.0f || fb[i*3+2] > 0.0f) {
            has_radiance = true;
            break;
        }
    }
    CHECK(has_radiance);

    free_gpu_renderer(ctx);
    return 0;
}

static int test_no_tangents_pointer_null() {
    REQUIRE_GPU();

    RenderMesh mesh;
    mesh.vertices = {0.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f};
    mesh.normals  = {0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f};
    mesh.uvs      = {0.0f, 0.0f,  1.0f, 0.0f,  0.0f, 1.0f};
    mesh.indices  = {0, 1, 2};
    mesh.material_index = 0;

    std::vector<RenderMesh> meshes = {mesh};
    std::vector<GpuMaterialData> materials(1);
    materials[0].header.type = MaterialType::Lambertian;
    materials[0].albedo = SpectralPacket(0.8f, 0.8f, 0.8f);

    std::vector<GpuInstance> instances(1);
    instances[0].mesh_index = 0;
    instances[0].material_index = 0;
    instances[0].transform = GpuMat4::identity();
    instances[0].inverse_transform = GpuMat4::identity();
    instances[0].min_pt = {-1, -1, -1};
    instances[0].max_pt = {1, 1, 1};

    GpuContext* ctx = init_gpu_renderer(
        64, 64, meshes, instances, {}, materials, {});
    CHECK(ctx != nullptr);

    render_pass_gpu(ctx, 1);

    // Verify that mesh.tangents is nullptr on the GPU
    GpuMesh h_mesh;
    CHECK_CUDA(cudaMemcpy(&h_mesh, ctx->d_meshes, sizeof(GpuMesh), cudaMemcpyDeviceToHost));
    CHECK(h_mesh.tangents == nullptr);

    free_gpu_renderer(ctx);
    return 0;
}

static int test_update_camera_gpu() {
    REQUIRE_GPU();

    ure::gpu::RenderMesh mesh;
    mesh.vertices = {-0.5f, 0.0f, 0.0f,  0.5f, 0.0f, 0.0f,  0.0f, 0.5f, 0.0f};
    mesh.normals = {0.0f, 1.0f, 0.0f,  0.0f, 1.0f, 0.0f,  0.0f, 1.0f, 0.0f};
    mesh.uvs = {0.0f, 0.0f,  1.0f, 0.0f,  0.5f, 1.0f};
    mesh.indices = {0, 1, 2};
    mesh.material_index = 0;
    std::vector<ure::gpu::RenderMesh> meshes = {mesh};

    std::vector<ure::gpu::GpuMaterialData> materials(1);
    materials[0].header.type = ure::gpu::MaterialType::Light;
    materials[0].emission = ure::gpu::SpectralPacket(5.0f);

    std::vector<ure::gpu::GpuInstance> instances(1);
    instances[0].mesh_index = 0;
    instances[0].material_index = 0;
    instances[0].transform = ure::gpu::GpuMat4::identity();
    instances[0].inverse_transform = ure::gpu::GpuMat4::identity();
    instances[0].min_pt = {-1, -1, -1};
    instances[0].max_pt = {1, 1, 1};

    GpuContext* ctx = init_gpu_renderer(64, 64, meshes, instances, {}, materials, {});
    CHECK(ctx != nullptr);

    // Move camera closer — should still render
    float cam_pos[] = {0.0f, 0.25f, 1.5f};
    float cam_look[] = {0.0f, 0.25f, 0.0f};
    update_camera_gpu(ctx, cam_pos, cam_look, 45.0f);

    int spp = render_pass_gpu(ctx, 1);
    CHECK(spp == 1);

    std::vector<float> fb(64 * 64 * 3);
    copy_frame_buffer_gpu(ctx, fb.data());

    // Triangle should be visible
    bool has_radiance = false;
    for (int i = 0; i < 64 * 64; ++i) {
        if (fb[i*3] > 0.0f || fb[i*3+1] > 0.0f || fb[i*3+2] > 0.0f) {
            has_radiance = true;
            break;
        }
    }
    CHECK(has_radiance);

    free_gpu_renderer(ctx);
    return 0;
}

static int test_update_medium_gpu() {
    REQUIRE_GPU();

    ure::gpu::RenderMesh mesh;
    mesh.vertices = {-0.5f, 0.0f, 0.0f,  0.5f, 0.0f, 0.0f,  0.0f, 0.5f, 0.0f};
    mesh.normals = {0.0f, 1.0f, 0.0f,  0.0f, 1.0f, 0.0f,  0.0f, 1.0f, 0.0f};
    mesh.uvs = {0.0f, 0.0f,  1.0f, 0.0f,  0.5f, 1.0f};
    mesh.indices = {0, 1, 2};
    mesh.material_index = 0;
    std::vector<ure::gpu::RenderMesh> meshes = {mesh};

    std::vector<ure::gpu::GpuMaterialData> materials(1);
    materials[0].header.type = ure::gpu::MaterialType::Light;
    materials[0].emission = ure::gpu::SpectralPacket(5.0f);

    std::vector<ure::gpu::GpuInstance> instances(1);
    instances[0].mesh_index = 0;
    instances[0].material_index = 0;
    instances[0].transform = ure::gpu::GpuMat4::identity();
    instances[0].inverse_transform = ure::gpu::GpuMat4::identity();
    instances[0].min_pt = {-1, -1, -1};
    instances[0].max_pt = {1, 1, 1};

    GpuContext* ctx = init_gpu_renderer(64, 64, meshes, instances, {}, materials, {});
    CHECK(ctx != nullptr);

    // Move camera closer
    float cam_pos[] = {0.0f, 0.25f, 1.5f};
    float cam_look[] = {0.0f, 0.25f, 0.0f};
    update_camera_gpu(ctx, cam_pos, cam_look, 45.0f);

    // Set medium parameters (very thin medium)
    update_medium_gpu(ctx, 0.001f, 0.0f,
                       SpectralPacket(0.1f), SpectralPacket(0.01f), 10.0f);

    int spp = render_pass_gpu(ctx, 1);
    CHECK(spp == 1);

    // Verify no CUDA error after medium-aware rendering
    CHECK_CUDA(cudaPeekAtLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    // Medium exists — radiance may be modulated, verify no crash is sufficient

    free_gpu_renderer(ctx);
    return 0;
}

int main() {
    ure::log::set_min_level(ure::log::Level::Warn);
    printf("[GPU Tangent Upload Test]\n");
    RUN_TEST(test_tangent_upload_readback);
    RUN_TEST(test_no_tangents_fallback);
    RUN_TEST(test_no_tangents_pointer_null);
    RUN_TEST(test_update_camera_gpu);
    RUN_TEST(test_update_medium_gpu);
    printf("  passed: %d, failed: %d\n", g_tests_passed, g_tests_failed);
    return g_test_result;
}
