#include "test_framework.cuh"
#include <ure/gpu_context.hpp>
#include <ure/gpu_driver.hpp>
#include <ure/gpu_structs.hpp>
#include <ure/log.hpp>
#include <ure/transform_ring_buffer.hpp>

// --- Layout verification (host-side) ---
// GpuInstance layout:
//   mesh_index(4B) + material_index(4B) + transform(64B) + inverse_transform(64B) + min_pt(12B) + max_pt(12B) = 160B
// GpuInstanceDesc:
//   mesh_index(4B) + material_index(4B) = 8B  (matches start of GpuInstance)
// GpuInstanceTransform:
//   transform(64B) + inverse_transform(64B) + min_pt(12B) + max_pt(12B) = 152B

static_assert(sizeof(ure::gpu::GpuInstanceDesc) == 2 * sizeof(int),
    "GpuInstanceDesc should be exactly mesh_index + material_index");

static int test_instance_layout() {
    // Verify fundamental sizes
    CHECK(sizeof(ure::gpu::GpuInstanceDesc) == 8);
    CHECK(sizeof(ure::gpu::GpuInstanceTransform) == 152);
    CHECK(sizeof(ure::gpu::GpuInstance) == 160);
    
    // Verify that reinterpret_cast<GpuInstanceDesc*>(GpuInstance*) gives correct mesh/material indices
    // This is what the kernel does: scene.instance_descs = reinterpret_cast<GpuInstanceDesc*>(d_instances)
    {
        ure::gpu::GpuInstance inst;
        inst.mesh_index = 42;
        inst.material_index = 7;
        inst.transform = ure::gpu::GpuMat4::identity();
        inst.inverse_transform = ure::gpu::GpuMat4::identity();
        inst.min_pt = {-1, -1, -1};
        inst.max_pt = {1, 1, 1};
        
        const auto* desc = reinterpret_cast<const ure::gpu::GpuInstanceDesc*>(&inst);
        CHECK(desc->mesh_index == 42);
        CHECK(desc->material_index == 7);
    }
    
    return 0;
}

// --- GPU hot-update test ---
// Verify that after calling update_instance_transforms_gpu, the GPU scene uses the new transforms.

static int test_gpu_hot_update_identity() {
    REQUIRE_GPU();
    
    // Create a minimal scene: a single triangle mesh with one instance
    std::vector<ure::gpu::RenderMesh> meshes;
    {
        ure::gpu::RenderMesh mesh;
        // Single triangle at origin
        mesh.vertices = {-0.5f, 0.0f, 0.0f,  0.5f, 0.0f, 0.0f,  0.0f, 0.5f, 0.0f};
        mesh.normals = {0.0f, 1.0f, 0.0f,  0.0f, 1.0f, 0.0f,  0.0f, 1.0f, 0.0f};
        mesh.uvs = {0.0f, 0.0f,  1.0f, 0.0f,  0.5f, 1.0f};
        mesh.indices = {0, 1, 2};
        mesh.material_index = 0;
        meshes.push_back(mesh);
    }
    
    // Default material (Lambertian)
    std::vector<ure::gpu::GpuMaterialData> materials(1);
    materials[0].header.type = ure::gpu::MaterialType::Lambertian;
    materials[0].albedo = ure::gpu::SpectralPacket(0.8f, 0.8f, 0.8f);
    
    // One instance with known transform
    std::vector<ure::gpu::GpuInstance> instances(1);
    instances[0].mesh_index = 0;
    instances[0].material_index = 0;
    // Initial: identity
    instances[0].transform = ure::gpu::GpuMat4::identity();
    instances[0].inverse_transform = ure::gpu::GpuMat4::identity();
    instances[0].min_pt = {-1, -1, -1};
    instances[0].max_pt = {1, 1, 1};
    
    // No spheres, no textures
    std::vector<ure::gpu::GpuSphere> spheres;
    std::vector<ure::gpu::HostTexture> textures;
    
    // Initialize GPU context
    ure::gpu::GpuContext* ctx = ure::gpu::init_gpu_renderer(
        64, 64, meshes, instances, spheres, materials, textures
    );
    CHECK(ctx != nullptr);
    
    // Render a pass to warm up
    ure::gpu::render_pass_gpu(ctx, 1);
    
    // Now modify the transform on CPU and hot-update
    std::vector<ure::gpu::GpuInstanceTransform> new_transforms(1);
    new_transforms[0].transform = ure::gpu::GpuMat4::identity();
    new_transforms[0].transform.m[0][3] = 100.0f; // Translate by 100 units in X
    new_transforms[0].inverse_transform = new_transforms[0].transform;
    new_transforms[0].inverse_transform.m[0][3] = -100.0f;
    new_transforms[0].min_pt = {99, -1, -1};
    new_transforms[0].max_pt = {101, 1, 1};
    
    ure::gpu::update_instance_transforms_gpu(ctx, new_transforms.data(), 1);
    CHECK(ctx->has_scene_bounds);
    CHECK(ctx->scene_bounds_max.x >= 101.0f);
    
    // Render another pass (should use updated transform, no crash)
    int spp = ure::gpu::render_pass_gpu(ctx, 1);
    CHECK(spp == 2);
    
    // Copy frame buffer to host (should not crash with translated instance)
    std::vector<float> fb(64 * 64 * 3);
    ure::gpu::copy_frame_buffer_gpu(ctx, fb.data());
    
    // Note: With translation of 100 units, the triangle is far from camera,
    // so the buffer may be all zeros (no intersection). This is expected.
    // What matters is that the render completed without crash or CUDA error.
    
    ure::gpu::free_gpu_renderer(ctx);
    return 0;
}

static int test_gpu_hot_update_resets_spatial_guiding_epoch() {
    REQUIRE_GPU();

    ure::gpu::RenderMesh mesh;
    mesh.vertices = {-0.5f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f};
    mesh.normals = {0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    mesh.uvs = {0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 1.0f};
    mesh.indices = {0, 1, 2};
    mesh.material_index = 0;

    std::vector<ure::gpu::GpuMaterialData> materials(2);
    materials[0].header.type = ure::gpu::MaterialType::Lambertian;
    materials[0].albedo = ure::gpu::SpectralPacket(0.8f);
    materials[1].header.type = ure::gpu::MaterialType::Light;
    materials[1].emission = ure::gpu::SpectralPacket(4.0f);

    ure::gpu::GpuInstance instance = {};
    instance.mesh_index = 0;
    instance.material_index = 0;
    instance.transform = ure::gpu::GpuMat4::identity();
    instance.inverse_transform = ure::gpu::GpuMat4::identity();
    instance.min_pt = {-1.0f, -1.0f, -1.0f};
    instance.max_pt = {1.0f, 1.0f, 1.0f};

    ure::gpu::GpuSphere light_sphere;
    light_sphere.center = {0.0f, 3.0f, 0.0f};
    light_sphere.radius = 0.5f;
    light_sphere.material_index = 8;

    ure::RenderConfig config;
    config.path_guiding.enabled = true;
    config.path_guiding.spatial_cell_count = 8;
    config.path_guiding.directional_bin_count = 4;
    ure::gpu::GpuContext* ctx = ure::gpu::init_gpu_renderer(
        4, 4, {mesh}, {instance}, {light_sphere}, materials, {}, config);
    CHECK(ctx != nullptr);
    CHECK(ctx->d_path_guiding_light_weights != nullptr);
    CHECK(ctx->d_path_guiding_spatial_directional_weights != nullptr);

    float weight = 2.0f;
    CHECK_CUDA(cudaMemcpy(ctx->d_path_guiding_light_weights, &weight, sizeof(float), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(ctx->d_path_guiding_spatial_directional_weights, &weight, sizeof(float), cudaMemcpyHostToDevice));

    ure::gpu::GpuInstanceTransform transform = {};
    transform.transform = ure::gpu::GpuMat4::identity();
    transform.inverse_transform = ure::gpu::GpuMat4::identity();
    transform.min_pt = {9.0f, -1.0f, -1.0f};
    transform.max_pt = {11.0f, 1.0f, 1.0f};
    ure::gpu::update_instance_transforms_gpu(ctx, &transform, 1);

    CHECK(ctx->scene_bounds_max.x >= 11.0f);
    CHECK_CUDA(cudaMemcpy(&weight, ctx->d_path_guiding_light_weights, sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(weight, 0.0f, 1e-6f);
    CHECK_CUDA(cudaMemcpy(&weight, ctx->d_path_guiding_spatial_directional_weights, sizeof(float), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(weight, 0.0f, 1e-6f);

    ure::gpu::free_gpu_renderer(ctx);
    return 0;
}

// Test that the hot-update path actually changes transform data on GPU
// by reading back through a simple kernel

__global__ void read_instance_transform_kernel(ure::gpu::GpuInstanceTransform* out,
                                                const ure::gpu::GpuScene scene) {
    if (threadIdx.x == 0 && blockIdx.x == 0) {
        if (scene.instance_count > 0) {
            out[0] = scene.instance_transforms[0];
        }
    }
}

static int test_gpu_transform_readback() {
    REQUIRE_GPU();
    
    // Minimal scene setup (same as above)
    std::vector<ure::gpu::RenderMesh> meshes;
    {
        ure::gpu::RenderMesh mesh;
        mesh.vertices = {0,0,0, 1,0,0, 0,1,0};
        mesh.normals = {0,1,0, 0,1,0, 0,1,0};
        mesh.uvs = {0,0, 1,0, 0.5f,1};
        mesh.indices = {0,1,2};
        mesh.material_index = 0;
        meshes.push_back(mesh);
    }
    std::vector<ure::gpu::GpuMaterialData> materials(1);
    materials[0].header.type = ure::gpu::MaterialType::Lambertian;
    materials[0].albedo = ure::gpu::SpectralPacket(0.5f,0.5f,0.5f);
    
    std::vector<ure::gpu::GpuInstance> instances(1);
    instances[0].mesh_index = 0;
    instances[0].material_index = 0;
    instances[0].transform = ure::gpu::GpuMat4::identity();
    instances[0].inverse_transform = ure::gpu::GpuMat4::identity();
    instances[0].min_pt = {-1,-1,-1};
    instances[0].max_pt = {1,1,1};
    
    ure::gpu::GpuContext* ctx = ure::gpu::init_gpu_renderer(64, 64, meshes, instances, {}, materials, {});
    CHECK(ctx != nullptr);
    
    // Allocate host+device memory for transform readback
    ure::gpu::GpuInstanceTransform* d_readback;
    ure::gpu::GpuInstanceTransform h_readback;
    ure::gpu::GpuInstanceTransform h_previous;
    CHECK_CUDA(cudaMalloc(&d_readback, sizeof(ure::gpu::GpuInstanceTransform)));
    DeviceMem _d(d_readback);
    CHECK_CUDA(cudaMemset(d_readback, 0, sizeof(ure::gpu::GpuInstanceTransform)));
    
    // Build scene on host to pass to kernel
    // We can't easily get the internal GpuScene from ctx, but we can verify indirectly:
    // 1. Update transforms with a known non-identity value
    // 2. Render a pass (triggers internal use of transforms)  
    // 3. Verify CUDA state is clean
    
    std::vector<ure::gpu::GpuInstanceTransform> updated(1);
    updated[0].transform.m[0][0] = 2.0f; // Scale X by 2
    updated[0].transform.m[1][1] = 1.0f;
    updated[0].transform.m[2][2] = 1.0f;
    updated[0].transform.m[0][3] = 0.0f;
    updated[0].transform.m[1][3] = 0.0f;
    updated[0].transform.m[2][3] = 0.0f;
    updated[0].inverse_transform = updated[0].transform;
    updated[0].inverse_transform.m[0][0] = 0.5f;
    updated[0].min_pt = {-2, -1, -1};
    updated[0].max_pt = {2, 1, 1};
    
    ure::gpu::update_instance_transforms_gpu(ctx, updated.data(), 1);

    CHECK_CUDA(cudaMemcpy(&h_previous, ctx->d_previous_instance_transforms, sizeof(ure::gpu::GpuInstanceTransform), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(h_previous.transform.m[0][0], 1.0f, 1e-6f);
    CHECK_FLOAT_EQ(h_previous.inverse_transform.m[0][0], 1.0f, 1e-6f);
    CHECK_FLOAT_EQ(h_previous.min_pt.x, -1.0f, 1e-6f);
    CHECK_FLOAT_EQ(h_previous.max_pt.x, 1.0f, 1e-6f);

    CHECK_CUDA(cudaMemcpy(&h_readback, ctx->d_instance_transforms, sizeof(ure::gpu::GpuInstanceTransform), cudaMemcpyDeviceToHost));
    CHECK_FLOAT_EQ(h_readback.transform.m[0][0], 2.0f, 1e-6f);
    CHECK_FLOAT_EQ(h_readback.inverse_transform.m[0][0], 0.5f, 1e-6f);
    CHECK_FLOAT_EQ(h_readback.min_pt.x, -2.0f, 1e-6f);
    CHECK_FLOAT_EQ(h_readback.max_pt.x, 2.0f, 1e-6f);
    
    // Render and verify no CUDA error
    int spp = ure::gpu::render_pass_gpu(ctx, 1);
    CHECK(spp == 1);
    
    CHECK_CUDA(cudaPeekAtLastError());
    CHECK_CUDA(cudaDeviceSynchronize());
    
    // Get frame buffer - should have rendered content since the triangle is
    // at origin facing the default camera
    std::vector<float> fb(64 * 64 * 3);
    ure::gpu::copy_frame_buffer_gpu(ctx, fb.data());
    
    // Verify at least one pixel has non-zero radiance (the triangle should be visible)
    bool has_radiance = false;
    for (int i = 0; i < 64 * 64; ++i) {
        if (fb[i*3] > 0.0f || fb[i*3+1] > 0.0f || fb[i*3+2] > 0.0f) {
            has_radiance = true;
            break;
        }
    }
    CHECK(has_radiance);
    
    ure::gpu::free_gpu_renderer(ctx);
    return 0;
}

// --- TransformRingBuffer tests ---

static int test_ring_buffer_basic() {
    ure::gpu::TransformRingBuffer rb;
    
    CHECK(rb.instance_count == 0);
    CHECK(rb.write_index.load() == 0);
    
    rb.resize(4);
    CHECK(rb.instance_count == 4);
    CHECK(rb.write_index.load() == 0);
    
    // After resize: write_index=0, read frame=(0+2)%3=2 (identity data).
    int count_a = 0;
    const ure::gpu::GpuInstanceTransform* ra = rb.begin_read(count_a);
    CHECK(count_a == 4);
    for (int i = 0; i < 4; ++i) {
        CHECK_FLOAT_EQ(ra[i].transform.m[0][0], 1.0f, 1e-6f);
        CHECK_FLOAT_EQ(ra[i].transform.m[0][1], 0.0f, 1e-6f);
    }
    rb.end_read();
    
    // Write frame 0 with values 1..4, then advance (write_index 0->1)
    ure::gpu::GpuInstanceTransform* w = rb.begin_write();
    CHECK(w != nullptr);
    CHECK(rb.write_count() == 4);
    for (int i = 0; i < 4; ++i)
        w[i].transform.m[0][0] = (float)(i + 1);
    rb.end_write();
    rb.advance();
    CHECK(rb.write_index.load() == 1);
    
    // Reader now sees frame 0 (just completed): values 1..4
    int count_b = 0;
    const ure::gpu::GpuInstanceTransform* rb1 = rb.begin_read(count_b);
    CHECK(count_b == 4);
    for (int i = 0; i < 4; ++i)
        CHECK_FLOAT_EQ(rb1[i].transform.m[0][0], (float)(i + 1), 1e-6f);
    rb.end_read();
    
    // Write frame 1 with values 10..13, then advance (write_index 1->2)
    w = rb.begin_write();
    for (int i = 0; i < 4; ++i)
        w[i].transform.m[0][0] = (float)(i + 10);
    rb.end_write();
    rb.advance();
    
    // Reader now sees frame 1 (just completed): values 10..13
    int count_c = 0;
    const ure::gpu::GpuInstanceTransform* rc = rb.begin_read(count_c);
    CHECK(count_c == 4);
    for (int i = 0; i < 4; ++i)
        CHECK_FLOAT_EQ(rc[i].transform.m[0][0], (float)(i + 10), 1e-6f);
    rb.end_read();
    
    return 0;
}

static int test_ring_buffer_init_from_instances() {
    std::vector<ure::gpu::GpuInstance> insts(2);
    insts[0].mesh_index = 0;
    insts[0].transform.m[0][3] = 1.0f;
    insts[0].inverse_transform.m[0][3] = -1.0f;
    insts[0].min_pt = {0,0,0};
    insts[0].max_pt = {1,1,1};
    
    insts[1].mesh_index = 1;
    insts[1].transform.m[0][3] = 5.0f;
    insts[1].inverse_transform.m[0][3] = -5.0f;
    insts[1].min_pt = {10,10,10};
    insts[1].max_pt = {20,20,20};
    
    ure::gpu::TransformRingBuffer rb;
    rb.init_from_instances(insts);
    
    CHECK(rb.instance_count == 2);
    
    // All 3 frames should have the same initial data
    for (int f = 0; f < 3; ++f) {
        CHECK_FLOAT_EQ(rb.frames[f][0].transform.m[0][3], 1.0f, 1e-6f);
        CHECK_FLOAT_EQ(rb.frames[f][0].inverse_transform.m[0][3], -1.0f, 1e-6f);
        CHECK_FLOAT_EQ(rb.frames[f][1].transform.m[0][3], 5.0f, 1e-6f);
        CHECK_FLOAT_EQ(rb.frames[f][1].inverse_transform.m[0][3], -5.0f, 1e-6f);
    }
    
    // Write index at 0
    CHECK(rb.write_index.load() == 0);
    
    return 0;
}

static int test_ring_buffer_wraparound() {
    ure::gpu::TransformRingBuffer rb;
    rb.resize(1);
    
    for (int cycle = 0; cycle < 6; ++cycle) {
        ure::gpu::GpuInstanceTransform* w = rb.begin_write();
        w[0].transform.m[0][0] = (float)cycle;
        rb.end_write();
        rb.advance();
    }
    CHECK(rb.write_index.load() == (6 % 3));
    
    int count_a = 0;
    const ure::gpu::GpuInstanceTransform* ra = rb.begin_read(count_a);
    CHECK(count_a == 1);
    rb.end_read();
    
    int count_b = 0;
    const ure::gpu::GpuInstanceTransform* rb1 = rb.begin_read(count_b);
    CHECK(count_b == 1);
    rb.end_read();
    
    int count_c = 0;
    const ure::gpu::GpuInstanceTransform* rc = rb.begin_read(count_c);
    CHECK(count_c == 1);
    rb.end_read();
    
    return 0;
}

int main() {
    ure::log::set_min_level(ure::log::Level::Warn);
    printf("[GPU Instance Hot-Update Test]\n");
    RUN_TEST(test_instance_layout);
    RUN_TEST(test_gpu_hot_update_identity);
    RUN_TEST(test_gpu_hot_update_resets_spatial_guiding_epoch);
    RUN_TEST(test_gpu_transform_readback);
    RUN_TEST(test_ring_buffer_basic);
    RUN_TEST(test_ring_buffer_init_from_instances);
    RUN_TEST(test_ring_buffer_wraparound);
    printf("  passed: %d, failed: %d\n", g_tests_passed, g_tests_failed);
    return g_test_result;
}
