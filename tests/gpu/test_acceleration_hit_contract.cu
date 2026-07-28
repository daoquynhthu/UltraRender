#include <cuda_runtime.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <vector>

#include "ure/detail/cuda_bvh_builder.cuh"
#include "ure/detail/cuda_structs.cuh"

namespace ure::gpu {

#include "path_tracer_decl.cuh"
#include "path_tracer_intersect.cuh"

struct ContractHit {
    float t;
    GpuVec3 position;
    GpuVec3 shading_normal;
    GpuVec3 geometric_normal;
    GpuVec2 uv;
    int material_index;
    int hit_type;
    int hit_index;
    int primitive_index;
};

__global__ void acceleration_contract_kernel(
    GpuScene scene,
    const GpuRay* rays,
    ContractHit* hits,
    int* shadow_hits) {
    const int index =
        static_cast<int>(threadIdx.x);
    if (index >= 3) return;
    ContractHit result{};
    result.t = -1.0f;
    result.material_index = -1;
    result.hit_type = -1;
    result.hit_index = -1;
    result.primitive_index = -1;
    world_hit(
        scene,
        rays[index],
        rays[index].t_min,
        rays[index].t_max,
        result.t,
        result.position,
        result.shading_normal,
        result.geometric_normal,
        result.uv,
        result.material_index,
        result.hit_type,
        result.hit_index,
        result.primitive_index);
    hits[index] = result;
    float shadow_t;
    GpuVec3 shadow_position;
    GpuVec3 shadow_normal;
    GpuVec3 shadow_geometric_normal;
    GpuVec2 shadow_uv;
    int shadow_material;
    int shadow_type;
    int shadow_index;
    int shadow_primitive;
    shadow_hits[index] = world_hit(
        scene,
        rays[index],
        rays[index].t_min,
        rays[index].t_max,
        shadow_t,
        shadow_position,
        shadow_normal,
        shadow_geometric_normal,
        shadow_uv,
        shadow_material,
        shadow_type,
        shadow_index,
        shadow_primitive,
        true) ? 1 : 0;
}

__global__ void robust_intersection_kernel(int* results) {
    if (threadIdx.x != 0) return;
    GpuRay boundary_ray;
    boundary_ray.origin = GpuVec3(-1.0f, 0.0f, 2.0f);
    boundary_ray.direction = GpuVec3(0.0f, 0.0f, -1.0f);
    boundary_ray.t_min = 0.0f;
    boundary_ray.t_max = 10.0f;
    GpuRay parallel_miss = boundary_ray;
    parallel_miss.origin = GpuVec3(-2.0f, 0.0f, 2.0f);
    const GpuVec3 bounds_min(-1.0f, -1.0f, -0.001f);
    const GpuVec3 bounds_max(1.0f, 1.0f, 0.001f);
    results[0] = hit_aabb(
        boundary_ray,
        bounds_min,
        bounds_max,
        boundary_ray.t_min,
        boundary_ray.t_max) ? 1 : 0;
    results[1] = hit_aabb(
        parallel_miss,
        bounds_min,
        bounds_max,
        parallel_miss.t_min,
        parallel_miss.t_max) ? 1 : 0;

    GpuRay tiny_ray;
    tiny_ray.origin = GpuVec3(
        2.5e-10f, 2.5e-10f, 1.0f);
    tiny_ray.direction = GpuVec3(0.0f, 0.0f, -1.0f);
    tiny_ray.t_min = 0.0f;
    tiny_ray.t_max = 10.0f;
    float t;
    GpuVec3 geometric;
    GpuVec3 shading;
    float u;
    float v;
    results[2] = hit_triangle(
        tiny_ray,
        GpuVec3(0.0f, 0.0f, 0.0f),
        GpuVec3(1.0e-9f, 0.0f, 0.0f),
        GpuVec3(0.0f, 1.0e-9f, 0.0f),
        nullptr,
        nullptr,
        nullptr,
        0.0f,
        10.0f,
        t,
        geometric,
        shading,
        u,
        v) ? 1 : 0;
    results[3] = hit_triangle(
        tiny_ray,
        GpuVec3(0.0f, 0.0f, 0.0f),
        GpuVec3(1.0f, 0.0f, 0.0f),
        GpuVec3(2.0f, 0.0f, 0.0f),
        nullptr,
        nullptr,
        nullptr,
        0.0f,
        10.0f,
        t,
        geometric,
        shading,
        u,
        v) ? 1 : 0;
}

__global__ void traversal_failure_kernel(
    GpuMesh deep_mesh,
    GpuMesh missing_mesh,
    GpuRay ray,
    GpuAccelerationTelemetry* telemetry,
    int* results) {
    if (threadIdx.x != 0) return;
    float t;
    GpuVec3 geometric;
    GpuVec3 shading;
    GpuVec2 uv;
    int primitive = -1;
    results[0] = static_cast<int>(hit_bvh(
        deep_mesh,
        ray,
        ray.t_min,
        ray.t_max,
        t,
        geometric,
        shading,
        uv,
        primitive,
        telemetry,
        false,
        true));
    results[1] = static_cast<int>(hit_bvh(
        missing_mesh,
        ray,
        ray.t_min,
        ray.t_max,
        t,
        geometric,
        shading,
        uv,
        primitive,
        telemetry,
        false,
        true));
}

}

namespace {

template <typename T>
class DeviceArray {
public:
    explicit DeviceArray(std::size_t count) {
        if (cudaMalloc(
                reinterpret_cast<void**>(&pointer_),
                count * sizeof(T)) != cudaSuccess) {
            throw std::runtime_error("cudaMalloc failed");
        }
    }

    ~DeviceArray() {
        if (pointer_) cudaFree(pointer_);
    }

    DeviceArray(const DeviceArray&) = delete;
    DeviceArray& operator=(const DeviceArray&) = delete;

    T* get() const { return pointer_; }

    void upload(const T* source, std::size_t count) {
        if (cudaMemcpy(
                pointer_,
                source,
                count * sizeof(T),
                cudaMemcpyHostToDevice) != cudaSuccess) {
            throw std::runtime_error(
                "cudaMemcpy upload failed");
        }
    }

    void download(T* destination, std::size_t count) {
        if (cudaMemcpy(
                destination,
                pointer_,
                count * sizeof(T),
                cudaMemcpyDeviceToHost) != cudaSuccess) {
            throw std::runtime_error(
                "cudaMemcpy download failed");
        }
    }

private:
    T* pointer_ = nullptr;
};

bool close(float left, float right) {
    return std::abs(left - right) <= 1.0e-5f;
}

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

}

int main() {
    try {
        using namespace ure::gpu;
        std::vector<float> builder_vertices = {
            -1.0f, -1.0f, 0.0f,
             1.0f, -1.0f, 0.0f,
             1.0f,  1.0f, 0.0f,
            -1.0f,  1.0f, 0.0f};
        std::vector<int> builder_indices = {
            0, 1, 2, 0, 2, 3};
        std::vector<GpuBvhNode> builder_nodes;
        const auto builder_stats = MeshBvhBuilder::build(
            builder_vertices, builder_indices, builder_nodes);
        require(
            builder_stats.triangle_count == 2 &&
                builder_stats.node_count == 1 &&
                builder_stats.leaf_count == 1 &&
                builder_stats.max_depth == 1,
            "self-compute BVH build statistics mismatch");

        bool invalid_index_rejected = false;
        try {
            std::vector<int> invalid_indices = {0, 1, 9};
            MeshBvhBuilder::build(
                builder_vertices, invalid_indices, builder_nodes);
        } catch (const std::invalid_argument&) {
            invalid_index_rejected = true;
        }
        require(
            invalid_index_rejected,
            "self-compute BVH accepted an out-of-range index");

        const std::array vertices = {
            GpuVec3{-1.0f, -1.0f, 0.0f},
            GpuVec3{1.0f, -1.0f, 0.0f},
            GpuVec3{1.0f, 1.0f, 0.0f},
            GpuVec3{-1.0f, 1.0f, 0.0f}};
        std::array<GpuVec3, 4> normals;
        normals.fill({0.0f, 0.0f, 1.0f});
        const std::array texcoords = {
            GpuVec2{0.0f, 0.0f},
            GpuVec2{1.0f, 0.0f},
            GpuVec2{1.0f, 1.0f},
            GpuVec2{0.0f, 1.0f}};
        const std::array indices = {0, 1, 2, 0, 2, 3};
        const std::array bvh = {
            GpuBvhNode{
                {-1.0f, -1.0f, -0.001f},
                0,
                {1.0f, 1.0f, 0.001f},
                2}};
        GpuMesh mesh{};
        DeviceArray<GpuVec3> device_vertices(vertices.size());
        DeviceArray<GpuVec3> device_normals(normals.size());
        DeviceArray<GpuVec2> device_texcoords(texcoords.size());
        DeviceArray<int> device_indices(indices.size());
        DeviceArray<GpuBvhNode> device_bvh(bvh.size());
        device_vertices.upload(vertices.data(), vertices.size());
        device_normals.upload(normals.data(), normals.size());
        device_texcoords.upload(texcoords.data(), texcoords.size());
        device_indices.upload(indices.data(), indices.size());
        device_bvh.upload(bvh.data(), bvh.size());
        mesh.vertices = device_vertices.get();
        mesh.normals = device_normals.get();
        mesh.uvs = device_texcoords.get();
        mesh.indices = device_indices.get();
        mesh.triangle_count = 2;
        mesh.material_index = -1;
        mesh.min_pt = {-1.0f, -1.0f, -0.001f};
        mesh.max_pt = {1.0f, 1.0f, 0.001f};
        mesh.bvh_nodes = device_bvh.get();
        mesh.bvh_node_count = 1;
        DeviceArray<GpuMesh> device_meshes(1);
        device_meshes.upload(&mesh, 1);
        GpuAccelerationTelemetry telemetry{};
        DeviceArray<GpuAccelerationTelemetry> device_telemetry(1);
        device_telemetry.upload(&telemetry, 1);

        std::array<GpuInstanceDesc, 2> descs = {{
            {0, 5},
            {0, 7}}};
        std::array<GpuInstanceTransform, 2> transforms;
        transforms[0].transform.m[0][0] = 1.5f;
        transforms[0].transform.m[0][3] = -2.0f;
        transforms[0].transform.m[1][1] = 0.75f;
        transforms[0].inverse_transform.m[0][0] =
            2.0f / 3.0f;
        transforms[0].inverse_transform.m[0][3] =
            4.0f / 3.0f;
        transforms[0].inverse_transform.m[1][1] =
            4.0f / 3.0f;
        transforms[0].min_pt =
            {-3.5f, -0.75f, -0.001f};
        transforms[0].max_pt =
            {-0.5f, 0.75f, 0.001f};
        transforms[1].transform.m[0][0] = 0.75f;
        transforms[1].transform.m[0][3] = 2.0f;
        transforms[1].transform.m[1][1] = 1.5f;
        transforms[1].inverse_transform.m[0][0] =
            4.0f / 3.0f;
        transforms[1].inverse_transform.m[0][3] =
            -8.0f / 3.0f;
        transforms[1].inverse_transform.m[1][1] =
            2.0f / 3.0f;
        transforms[1].min_pt =
            {1.25f, -1.5f, -0.001f};
        transforms[1].max_pt =
            {2.75f, 1.5f, 0.001f};
        DeviceArray<GpuInstanceDesc> device_descs(descs.size());
        DeviceArray<GpuInstanceTransform> device_transforms(
            transforms.size());
        device_descs.upload(descs.data(), descs.size());
        device_transforms.upload(
            transforms.data(), transforms.size());

        GpuScene scene{};
        scene.meshes = device_meshes.get();
        scene.mesh_count = 1;
        scene.instance_descs = device_descs.get();
        scene.instance_transforms = device_transforms.get();
        scene.instance_count = 2;
        scene.acceleration_telemetry = device_telemetry.get();
        scene.acceleration_collect_stats = 1;
        std::array<GpuRay, 3> rays;
        rays[0].origin =
            {-2.75f, 0.1875f, 4.0f};
        rays[1].origin =
            {2.3f, -0.45f, 4.0f};
        rays[2].origin =
            {0.5f, 0.0f, 4.0f};
        for (auto& ray : rays) {
            ray.direction = {0.0f, 0.0f, -1.0f};
            ray.t_min = 0.001f;
            ray.t_max = 100.0f;
        }
        DeviceArray<GpuRay> device_rays(rays.size());
        DeviceArray<ContractHit> device_hits(rays.size());
        DeviceArray<int> device_shadow_hits(rays.size());
        device_rays.upload(rays.data(), rays.size());
        acceleration_contract_kernel<<<1, 32>>>(
            scene,
            device_rays.get(),
            device_hits.get(),
            device_shadow_hits.get());
        require(
            cudaGetLastError() == cudaSuccess,
            "CUDA acceleration kernel launch failed");
        require(
            cudaDeviceSynchronize() == cudaSuccess,
            "CUDA acceleration kernel failed");
        std::array<ContractHit, 3> hits;
        std::array<int, 3> shadow_hits;
        device_hits.download(hits.data(), hits.size());
        device_shadow_hits.download(
            shadow_hits.data(), shadow_hits.size());
        for (std::size_t index = 0; index < 2; ++index) {
            require(
                close(hits[index].t, 4.0f),
                "CUDA hit distance mismatch");
            require(
                close(hits[index].shading_normal.z, 1.0f) &&
                    close(
                        hits[index].geometric_normal.z,
                        1.0f),
                "CUDA hit normal mismatch");
            require(
                hits[index].hit_type == 2,
                "CUDA hit type mismatch");
        }
        require(
            hits[0].material_index == 5 &&
                hits[0].hit_index == 0 &&
                hits[0].primitive_index == 1 &&
                close(hits[0].uv.u, 0.25f) &&
                close(hits[0].uv.v, 0.625f),
            "CUDA first hit metadata mismatch");
        require(
            hits[1].material_index == 7 &&
                hits[1].hit_index == 1 &&
                hits[1].primitive_index == 0 &&
                close(hits[1].uv.u, 0.7f) &&
                close(hits[1].uv.v, 0.35f),
            "CUDA second hit metadata mismatch");
        require(
            hits[2].t < 0.0f &&
                hits[2].material_index == -1,
            "CUDA visibility miss mismatch");
        require(
            shadow_hits[0] == 1 &&
                shadow_hits[1] == 1 &&
                shadow_hits[2] == 0,
            "CUDA closest/shadow instance parity mismatch");
        device_telemetry.download(&telemetry, 1);
        require(
            telemetry.closest_node_visits > 0 &&
                telemetry.closest_triangle_tests > 0 &&
                telemetry.shadow_node_visits > 0 &&
                telemetry.shadow_triangle_tests > 0 &&
                telemetry.stack_overflow_count == 0 &&
                telemetry.invalid_acceleration_count == 0,
            "CUDA acceleration telemetry mismatch");

        DeviceArray<int> robust_results(4);
        robust_intersection_kernel<<<1, 1>>>(
            robust_results.get());
        require(
            cudaDeviceSynchronize() == cudaSuccess,
            "CUDA robust intersection kernel failed");
        std::array<int, 4> robust{};
        robust_results.download(robust.data(), robust.size());
        require(
            robust[0] == 1 && robust[1] == 0 &&
                robust[2] == 1 && robust[3] == 0,
            "CUDA robust AABB/triangle contract mismatch");

        std::array<GpuBvhNode, 67> deep_nodes;
        for (int index = 0; index < 65; ++index) {
            deep_nodes[index] = {
                {-1.0f, -1.0f, -0.001f},
                66,
                {1.0f, 1.0f, 0.001f},
                0};
        }
        deep_nodes[65] = {
            {-1.0f, -1.0f, -0.001f},
            0,
            {1.0f, 1.0f, 0.001f},
            1};
        deep_nodes[66] = deep_nodes[65];
        DeviceArray<GpuBvhNode> device_deep_nodes(
            deep_nodes.size());
        device_deep_nodes.upload(
            deep_nodes.data(), deep_nodes.size());
        GpuMesh deep_mesh = mesh;
        deep_mesh.bvh_nodes = device_deep_nodes.get();
        deep_mesh.bvh_node_count =
            static_cast<int>(deep_nodes.size());
        GpuMesh missing_mesh = mesh;
        missing_mesh.bvh_nodes = nullptr;
        missing_mesh.bvh_node_count = 0;
        telemetry = {};
        device_telemetry.upload(&telemetry, 1);
        DeviceArray<int> traversal_results(2);
        traversal_failure_kernel<<<1, 1>>>(
            deep_mesh,
            missing_mesh,
            rays[2],
            device_telemetry.get(),
            traversal_results.get());
        require(
            cudaDeviceSynchronize() == cudaSuccess,
            "CUDA traversal failure kernel failed");
        std::array<int, 2> traversal{};
        traversal_results.download(
            traversal.data(), traversal.size());
        device_telemetry.download(&telemetry, 1);
        require(
            traversal[0] ==
                static_cast<int>(
                    BvhTraversalResult::StackOverflow) &&
                traversal[1] ==
                static_cast<int>(
                    BvhTraversalResult::InvalidAcceleration) &&
                telemetry.stack_overflow_count == 1 &&
                telemetry.invalid_acceleration_count == 1,
            "CUDA traversal failures were not fail-loud");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
