#include <cuda_runtime.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <stdexcept>

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
    ContractHit* hits) {
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
        device_rays.upload(rays.data(), rays.size());
        acceleration_contract_kernel<<<1, 32>>>(
            scene,
            device_rays.get(),
            device_hits.get());
        require(
            cudaGetLastError() == cudaSuccess,
            "CUDA acceleration kernel launch failed");
        require(
            cudaDeviceSynchronize() == cudaSuccess,
            "CUDA acceleration kernel failed");
        std::array<ContractHit, 3> hits;
        device_hits.download(hits.data(), hits.size());
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
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
