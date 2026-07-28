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

__global__ void wide_bvh_benchmark_kernel(
    GpuMesh mesh,
    const GpuRay* rays,
    int ray_count,
    ContractHit* hits,
    GpuAccelerationTelemetry* telemetry,
    bool collect_stats) {
    const int index =
        static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    if (index >= ray_count) return;
    ContractHit result{};
    result.t = -1.0f;
    result.primitive_index = -1;
    const BvhTraversalResult traversal = hit_bvh(
        mesh, rays[index],
        rays[index].t_min, rays[index].t_max,
        result.t, result.geometric_normal,
        result.shading_normal, result.uv,
        result.primitive_index, telemetry,
        false, collect_stats);
    result.hit_type =
        traversal == BvhTraversalResult::Hit ? 1 :
        traversal == BvhTraversalResult::Miss ? 0 :
        -1;
    hits[index] = result;
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
        std::vector<GpuInstanceTransform>
            builder_instance_transforms(9);
        for (std::size_t index = 0;
             index < builder_instance_transforms.size();
             ++index) {
            const float x = static_cast<float>(index) * 3.0f;
            builder_instance_transforms[index].min_pt =
                {x, -1.0f, -1.0f};
            builder_instance_transforms[index].max_pt =
                {x + 1.0f, 1.0f, 1.0f};
        }
        std::vector<int> builder_instance_indices;
        std::vector<GpuBvhNode> builder_tlas_nodes;
        const auto builder_tlas_stats =
            InstanceTlasBuilder::build(
                builder_instance_transforms,
                builder_instance_indices,
                builder_tlas_nodes);
        require(
            builder_tlas_stats.instance_count == 9 &&
                builder_tlas_stats.node_count > 1 &&
                builder_tlas_stats.leaf_count > 1 &&
                builder_tlas_stats.max_depth > 1,
            "multi-instance TLAS build statistics mismatch");
        const std::vector<int> original_instance_order =
            builder_instance_indices;
        builder_instance_transforms[8].min_pt.x = 100.0f;
        builder_instance_transforms[8].max_pt.x = 101.0f;
        InstanceTlasBuilder::refit(
            builder_instance_transforms,
            builder_instance_indices,
            builder_tlas_nodes);
        require(
            builder_instance_indices == original_instance_order &&
                builder_tlas_nodes[0].max_pt.x == 101.0f,
            "TLAS refit changed topology or retained stale bounds");

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

        std::vector<GpuInstanceDesc> descs(9, {0, 5});
        descs[1].material_index = 7;
        std::vector<GpuInstanceTransform> transforms(9);
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
        for (std::size_t index = 2;
             index < transforms.size();
             ++index) {
            const float x =
                20.0f + static_cast<float>(index) * 3.0f;
            transforms[index].transform.m[0][3] = x;
            transforms[index].inverse_transform.m[0][3] = -x;
            transforms[index].min_pt =
                {x - 1.0f, -1.0f, -0.001f};
            transforms[index].max_pt =
                {x + 1.0f, 1.0f, 0.001f};
        }
        DeviceArray<GpuInstanceDesc> device_descs(descs.size());
        DeviceArray<GpuInstanceTransform> device_transforms(
            transforms.size());
        device_descs.upload(descs.data(), descs.size());
        device_transforms.upload(
            transforms.data(), transforms.size());
        std::vector<int> tlas_instance_indices;
        std::vector<GpuBvhNode> tlas_nodes;
        const auto tlas_stats = InstanceTlasBuilder::build(
            transforms, tlas_instance_indices,
            tlas_nodes);
        require(
            tlas_stats.instance_count == transforms.size() &&
                tlas_stats.node_count > 1 &&
                tlas_stats.leaf_count > 1,
            "self-compute TLAS build statistics mismatch");
        DeviceArray<GpuBvhNode> device_tlas_nodes(
            tlas_nodes.size());
        DeviceArray<int> device_tlas_instance_indices(
            tlas_instance_indices.size());
        device_tlas_nodes.upload(
            tlas_nodes.data(), tlas_nodes.size());
        device_tlas_instance_indices.upload(
            tlas_instance_indices.data(),
            tlas_instance_indices.size());

        GpuScene scene{};
        scene.meshes = device_meshes.get();
        scene.mesh_count = 1;
        scene.instance_descs = device_descs.get();
        scene.instance_transforms = device_transforms.get();
        scene.instance_count =
            static_cast<int>(transforms.size());
        scene.tlas_nodes = device_tlas_nodes.get();
        scene.tlas_node_count =
            static_cast<int>(tlas_nodes.size());
        scene.tlas_instance_indices =
            device_tlas_instance_indices.get();
        scene.tlas_instance_index_count =
            static_cast<int>(tlas_instance_indices.size());
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
                telemetry.closest_tlas_node_visits > 0 &&
                telemetry.shadow_tlas_node_visits > 0 &&
                telemetry.stack_overflow_count == 0 &&
                telemetry.invalid_acceleration_count == 0,
            "CUDA acceleration telemetry mismatch");

        constexpr int grid_size = 96;
        std::vector<float> large_vertices;
        large_vertices.reserve(
            static_cast<std::size_t>(
                (grid_size + 1) * (grid_size + 1) * 3));
        for (int y = 0; y <= grid_size; ++y) {
            for (int x = 0; x <= grid_size; ++x) {
                const float z =
                    0.15f * std::sin(
                        static_cast<float>(x) * 0.17f) *
                    std::cos(
                        static_cast<float>(y) * 0.13f);
                large_vertices.push_back(
                    static_cast<float>(x));
                large_vertices.push_back(
                    static_cast<float>(y));
                large_vertices.push_back(z);
            }
        }
        std::vector<int> large_indices;
        large_indices.reserve(
            static_cast<std::size_t>(
                grid_size * grid_size * 6));
        for (int y = 0; y < grid_size; ++y) {
            for (int x = 0; x < grid_size; ++x) {
                const int row = grid_size + 1;
                const int v00 = y * row + x;
                const int v10 = v00 + 1;
                const int v01 = v00 + row;
                const int v11 = v01 + 1;
                large_indices.insert(
                    large_indices.end(),
                    {v00, v10, v11, v00, v11, v01});
            }
        }
        std::vector<int> fast_indices = large_indices;
        std::vector<int> balanced_indices = large_indices;
        std::vector<int> high_quality_indices = large_indices;
        std::vector<GpuBvhNode> fast_binary;
        std::vector<GpuBvhNode> unused_binary;
        std::vector<GpuBvh4Node> unused_bvh4;
        std::vector<GpuBvh4Node> balanced_bvh4;
        std::vector<GpuWideBvhNode> unused_wide;
        std::vector<GpuWideBvhNode> high_quality_wide;
        std::vector<int> unused_references;
        std::vector<int> balanced_references;
        std::vector<int> high_quality_references;
        const auto fast_stats = MeshBvhBuilder::build(
            large_vertices, fast_indices,
            ure::AccelerationBuildQuality::FastBuild,
            fast_binary, unused_bvh4,
            unused_wide, unused_references);
        const auto balanced_stats = MeshBvhBuilder::build(
            large_vertices, balanced_indices,
            ure::AccelerationBuildQuality::Balanced,
            unused_binary, balanced_bvh4, unused_wide,
            balanced_references);
        const auto high_quality_stats = MeshBvhBuilder::build(
            large_vertices, high_quality_indices,
            ure::AccelerationBuildQuality::HighQuality,
            unused_binary, unused_bvh4, high_quality_wide,
            high_quality_references);
        const std::uint64_t fast_bytes =
            fast_binary.size() * sizeof(GpuBvhNode);
        const std::uint64_t balanced_bytes =
            balanced_bvh4.size() * sizeof(GpuBvh4Node) +
            balanced_references.size() * sizeof(int);
        const std::uint64_t high_quality_bytes =
            high_quality_wide.size() * sizeof(GpuWideBvhNode) +
            high_quality_references.size() * sizeof(int);
        require(
            fast_stats.layout == GpuBvhLayout::Binary &&
                balanced_stats.layout == GpuBvhLayout::Wide4 &&
                high_quality_stats.layout ==
                    GpuBvhLayout::Wide8,
            "acceleration quality did not select the required layout");
        std::printf(
            "V.4 layout bytes: node_size=%zu/%zu "
            "fast=%llu balanced=%llu high=%llu "
            "nodes=%zu/%zu/%zu refs=%zu/%zu\n",
            sizeof(GpuBvh4Node), sizeof(GpuWideBvhNode),
            static_cast<unsigned long long>(fast_bytes),
            static_cast<unsigned long long>(balanced_bytes),
            static_cast<unsigned long long>(high_quality_bytes),
            fast_binary.size(), balanced_bvh4.size(),
            high_quality_wide.size(),
            balanced_references.size(),
            high_quality_references.size());
        require(
            balanced_bytes < fast_bytes &&
                high_quality_bytes < fast_bytes &&
                balanced_stats.build_nanoseconds > 0 &&
                high_quality_stats.build_nanoseconds > 0 &&
                fast_stats.temporary_bytes > 0 &&
                balanced_stats.temporary_bytes >
                    fast_stats.temporary_bytes &&
                high_quality_stats.temporary_bytes >
                    balanced_stats.temporary_bytes &&
                fast_stats.compacted_bytes == fast_bytes &&
                balanced_stats.compacted_bytes ==
                    balanced_bytes &&
                high_quality_stats.compacted_bytes ==
                    high_quality_bytes &&
                balanced_stats.uncompacted_bytes >
                    balanced_stats.compacted_bytes &&
                high_quality_stats.uncompacted_bytes >
                    high_quality_stats.compacted_bytes &&
                balanced_stats.compaction_nanoseconds > 0 &&
                high_quality_stats.compaction_nanoseconds > 0,
            "wide BVH build did not produce compact measured output");
        std::vector<float> spatial_vertices;
        std::vector<int> spatial_indices;
        for (int index = 0; index < 64; ++index) {
            const float extent =
                1.0f + static_cast<float>(index);
            const float thickness = 0.01f;
            const int first =
                static_cast<int>(spatial_vertices.size() / 3);
            spatial_vertices.insert(
                spatial_vertices.end(),
                {-extent, -thickness, 0.0f,
                 extent, -thickness, 0.0f,
                 0.0f, 2.0f * thickness, 0.0f});
            spatial_indices.insert(
                spatial_indices.end(),
                {first, first + 1, first + 2});
            const int second =
                static_cast<int>(spatial_vertices.size() / 3);
            spatial_vertices.insert(
                spatial_vertices.end(),
                {-thickness, -extent, 0.0f,
                 -thickness, extent, 0.0f,
                 2.0f * thickness, 0.0f, 0.0f});
            spatial_indices.insert(
                spatial_indices.end(),
                {second, second + 1, second + 2});
        }
        std::vector<GpuWideBvhNode> spatial_wide;
        std::vector<int> spatial_references;
        const auto spatial_stats = MeshBvhBuilder::build(
            spatial_vertices, spatial_indices,
            ure::AccelerationBuildQuality::HighQuality,
            unused_binary, unused_bvh4,
            spatial_wide, spatial_references);
        require(
            spatial_stats.spatial_split_count > 0 &&
                spatial_stats.primitive_reference_count >
                    spatial_stats.triangle_count,
            "high-quality SBVH did not retain spatial references");

        std::size_t benchmark_free_before = 0;
        std::size_t benchmark_total_memory = 0;
        require(
            cudaMemGetInfo(
                &benchmark_free_before,
                &benchmark_total_memory) == cudaSuccess,
            "wide BVH benchmark VRAM baseline failed");
        const std::size_t large_vertex_count =
            large_vertices.size() / 3;
        DeviceArray<GpuVec3> device_large_vertices(
            large_vertex_count);
        device_large_vertices.upload(
            reinterpret_cast<const GpuVec3*>(
                large_vertices.data()),
            large_vertex_count);
        DeviceArray<int> device_fast_indices(
            fast_indices.size());
        DeviceArray<int> device_balanced_indices(
            balanced_indices.size());
        DeviceArray<int> device_high_quality_indices(
            high_quality_indices.size());
        device_fast_indices.upload(
            fast_indices.data(), fast_indices.size());
        device_balanced_indices.upload(
            balanced_indices.data(), balanced_indices.size());
        device_high_quality_indices.upload(
            high_quality_indices.data(),
            high_quality_indices.size());
        DeviceArray<GpuBvhNode> device_fast_binary(
            fast_binary.size());
        DeviceArray<GpuBvh4Node> device_balanced_bvh4(
            balanced_bvh4.size());
        DeviceArray<GpuWideBvhNode> device_high_quality_wide(
            high_quality_wide.size());
        DeviceArray<int> device_balanced_references(
            balanced_references.size());
        DeviceArray<int> device_high_quality_references(
            high_quality_references.size());
        device_fast_binary.upload(
            fast_binary.data(), fast_binary.size());
        device_balanced_bvh4.upload(
            balanced_bvh4.data(), balanced_bvh4.size());
        device_high_quality_wide.upload(
            high_quality_wide.data(),
            high_quality_wide.size());
        device_balanced_references.upload(
            balanced_references.data(),
            balanced_references.size());
        device_high_quality_references.upload(
            high_quality_references.data(),
            high_quality_references.size());

        GpuMesh fast_mesh{};
        fast_mesh.vertices = device_large_vertices.get();
        fast_mesh.indices = device_fast_indices.get();
        fast_mesh.triangle_count =
            static_cast<int>(fast_indices.size() / 3);
        fast_mesh.bvh_nodes = device_fast_binary.get();
        fast_mesh.bvh_node_count =
            static_cast<int>(fast_binary.size());
        fast_mesh.bvh_layout = GpuBvhLayout::Binary;
        GpuMesh balanced_mesh{};
        balanced_mesh.vertices = device_large_vertices.get();
        balanced_mesh.indices = device_balanced_indices.get();
        balanced_mesh.triangle_count =
            static_cast<int>(balanced_indices.size() / 3);
        balanced_mesh.bvh4_nodes =
            device_balanced_bvh4.get();
        balanced_mesh.bvh4_node_count =
            static_cast<int>(balanced_bvh4.size());
        balanced_mesh.primitive_references =
            device_balanced_references.get();
        balanced_mesh.primitive_reference_count =
            static_cast<int>(balanced_references.size());
        balanced_mesh.bvh_layout = GpuBvhLayout::Wide4;
        GpuMesh high_quality_mesh{};
        high_quality_mesh.vertices =
            device_large_vertices.get();
        high_quality_mesh.indices =
            device_high_quality_indices.get();
        high_quality_mesh.triangle_count =
            static_cast<int>(high_quality_indices.size() / 3);
        high_quality_mesh.wide_bvh_nodes =
            device_high_quality_wide.get();
        high_quality_mesh.wide_bvh_node_count =
            static_cast<int>(high_quality_wide.size());
        high_quality_mesh.primitive_references =
            device_high_quality_references.get();
        high_quality_mesh.primitive_reference_count =
            static_cast<int>(high_quality_references.size());
        high_quality_mesh.bvh_layout = GpuBvhLayout::Wide8;

        constexpr int benchmark_ray_count = 4096;
        std::vector<GpuRay> benchmark_rays;
        benchmark_rays.reserve(benchmark_ray_count);
        for (int index = 0;
             index < benchmark_ray_count;
             ++index) {
            const int x = index % 64;
            const int y = index / 64;
            GpuRay ray;
            ray.origin = {
                0.37f +
                    static_cast<float>(
                        x * (grid_size - 1)) / 63.0f,
                0.43f +
                    static_cast<float>(
                        y * (grid_size - 1)) / 63.0f,
                4.0f};
            ray.direction = {0.0f, 0.0f, -1.0f};
            ray.t_min = 0.001f;
            ray.t_max = 10.0f;
            benchmark_rays.push_back(ray);
        }
        DeviceArray<GpuRay> device_benchmark_rays(
            benchmark_rays.size());
        device_benchmark_rays.upload(
            benchmark_rays.data(), benchmark_rays.size());
        DeviceArray<ContractHit> device_fast_hits(
            benchmark_rays.size());
        DeviceArray<ContractHit> device_balanced_hits(
            benchmark_rays.size());
        DeviceArray<ContractHit> device_high_quality_hits(
            benchmark_rays.size());
        std::array<GpuAccelerationTelemetry, 3>
            benchmark_telemetry{};
        std::array<DeviceArray<GpuAccelerationTelemetry>, 3>
            device_benchmark_telemetry = {
                DeviceArray<GpuAccelerationTelemetry>(1),
                DeviceArray<GpuAccelerationTelemetry>(1),
                DeviceArray<GpuAccelerationTelemetry>(1)};
        for (std::size_t index = 0;
             index < benchmark_telemetry.size();
             ++index) {
            device_benchmark_telemetry[index].upload(
                &benchmark_telemetry[index], 1);
        }
        std::size_t benchmark_free_after = 0;
        require(
            cudaMemGetInfo(
                &benchmark_free_after,
                &benchmark_total_memory) == cudaSuccess &&
                benchmark_free_before >= benchmark_free_after,
            "wide BVH benchmark VRAM measurement failed");
        const std::uint64_t benchmark_vram_bytes =
            static_cast<std::uint64_t>(
                benchmark_free_before - benchmark_free_after);
        const int blocks =
            (benchmark_ray_count + 255) / 256;
        const auto measure_traversal =
            [&](GpuMesh benchmark_mesh,
                ContractHit* output,
                GpuAccelerationTelemetry* output_telemetry) {
                constexpr int repetitions = 32;
                cudaEvent_t start;
                cudaEvent_t finish;
                require(
                    cudaEventCreate(&start) == cudaSuccess &&
                        cudaEventCreate(&finish) == cudaSuccess,
                    "wide BVH benchmark event creation failed");
                require(
                    cudaEventRecord(start) == cudaSuccess,
                    "wide BVH benchmark start failed");
                for (int repetition = 0;
                     repetition < repetitions;
                     ++repetition) {
                    wide_bvh_benchmark_kernel<<<blocks, 256>>>(
                        benchmark_mesh,
                        device_benchmark_rays.get(),
                        benchmark_ray_count, output,
                        output_telemetry, false);
                }
                require(
                    cudaEventRecord(finish) == cudaSuccess &&
                        cudaEventSynchronize(finish) ==
                            cudaSuccess,
                    "wide BVH benchmark traversal failed");
                float milliseconds = 0.0f;
                require(
                    cudaEventElapsedTime(
                        &milliseconds, start, finish) ==
                        cudaSuccess,
                    "wide BVH benchmark timing failed");
                cudaEventDestroy(start);
                cudaEventDestroy(finish);
                return milliseconds /
                    static_cast<float>(repetitions);
            };
        const float fast_trace_ms = measure_traversal(
            fast_mesh, device_fast_hits.get(),
            device_benchmark_telemetry[0].get());
        const float balanced_trace_ms = measure_traversal(
            balanced_mesh, device_balanced_hits.get(),
            device_benchmark_telemetry[1].get());
        const float high_quality_trace_ms = measure_traversal(
            high_quality_mesh, device_high_quality_hits.get(),
            device_benchmark_telemetry[2].get());
        wide_bvh_benchmark_kernel<<<blocks, 256>>>(
            fast_mesh, device_benchmark_rays.get(),
            benchmark_ray_count, device_fast_hits.get(),
            device_benchmark_telemetry[0].get(), true);
        wide_bvh_benchmark_kernel<<<blocks, 256>>>(
            balanced_mesh, device_benchmark_rays.get(),
            benchmark_ray_count, device_balanced_hits.get(),
            device_benchmark_telemetry[1].get(), true);
        wide_bvh_benchmark_kernel<<<blocks, 256>>>(
            high_quality_mesh, device_benchmark_rays.get(),
            benchmark_ray_count, device_high_quality_hits.get(),
            device_benchmark_telemetry[2].get(), true);
        require(
            cudaDeviceSynchronize() == cudaSuccess,
            "wide BVH telemetry traversal failed");
        std::vector<ContractHit> fast_hits(
            benchmark_rays.size());
        std::vector<ContractHit> balanced_hits(
            benchmark_rays.size());
        std::vector<ContractHit> high_quality_hits(
            benchmark_rays.size());
        device_fast_hits.download(
            fast_hits.data(), fast_hits.size());
        device_balanced_hits.download(
            balanced_hits.data(), balanced_hits.size());
        device_high_quality_hits.download(
            high_quality_hits.data(), high_quality_hits.size());
        for (std::size_t index = 0;
             index < fast_hits.size();
             ++index) {
            require(
                fast_hits[index].hit_type == 1 &&
                    balanced_hits[index].hit_type == 1 &&
                    high_quality_hits[index].hit_type == 1 &&
                    close(
                        fast_hits[index].t,
                        balanced_hits[index].t) &&
                    close(
                        fast_hits[index].t,
                        high_quality_hits[index].t),
                "wide BVH result diverged from reference traversal");
        }
        for (std::size_t index = 0;
             index < benchmark_telemetry.size();
             ++index) {
            device_benchmark_telemetry[index].download(
                &benchmark_telemetry[index], 1);
            require(
                benchmark_telemetry[index]
                        .stack_overflow_count == 0 &&
                    benchmark_telemetry[index]
                        .invalid_acceleration_count == 0,
                "wide BVH benchmark reported traversal failure");
        }
        require(
            benchmark_telemetry[1].closest_node_visits <
                benchmark_telemetry[0].closest_node_visits &&
                benchmark_telemetry[2].closest_node_visits <
                    benchmark_telemetry[0]
                        .closest_node_visits &&
                std::isfinite(fast_trace_ms) &&
                std::isfinite(balanced_trace_ms) &&
                std::isfinite(high_quality_trace_ms) &&
                fast_trace_ms > 0.0f &&
                balanced_trace_ms > 0.0f &&
                high_quality_trace_ms > 0.0f,
            "wide BVH did not reduce large-mesh traversal visits");
        std::printf(
            "V.4 large mesh: triangles=%llu "
            "build_ms=[%.3f,%.3f,%.3f] "
            "trace_ms=[%.3f,%.3f,%.3f] "
            "node_bytes=[%llu,%llu,%llu] "
            "node_visits=[%llu,%llu,%llu] "
            "triangle_tests=[%llu,%llu,%llu] "
            "sbvh_stress_splits=%llu "
            "vram_bytes=%llu\n",
            static_cast<unsigned long long>(
                fast_stats.triangle_count),
            static_cast<double>(
                fast_stats.build_nanoseconds) / 1.0e6,
            static_cast<double>(
                balanced_stats.build_nanoseconds) / 1.0e6,
            static_cast<double>(
                high_quality_stats.build_nanoseconds) / 1.0e6,
            static_cast<double>(fast_trace_ms),
            static_cast<double>(balanced_trace_ms),
            static_cast<double>(high_quality_trace_ms),
            static_cast<unsigned long long>(fast_bytes),
            static_cast<unsigned long long>(balanced_bytes),
            static_cast<unsigned long long>(high_quality_bytes),
            benchmark_telemetry[0].closest_node_visits,
            benchmark_telemetry[1].closest_node_visits,
            benchmark_telemetry[2].closest_node_visits,
            benchmark_telemetry[0].closest_triangle_tests,
            benchmark_telemetry[1].closest_triangle_tests,
            benchmark_telemetry[2].closest_triangle_tests,
            static_cast<unsigned long long>(
                spatial_stats.spatial_split_count),
            static_cast<unsigned long long>(
                benchmark_vram_bytes));

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
