#pragma once
#include <vector>
#include "ure/gpu_structs.hpp"

namespace ure::gpu {

struct HostMesh {
    std::vector<float> vertices;
    std::vector<float> normals; // Vertex Normals
    std::vector<float> uvs; // u, v pairs
    std::vector<float> tangents; // Vertex Tangents (normal mapping)
    std::vector<int> indices;
    int material_index;
};

struct HostTexture {
    int width;
    int height;
    std::vector<float> data; // RGB floats (3 floats per pixel)
};

struct GpuHostScene {
    std::vector<GpuMaterial> materials;
    std::vector<GpuSphere> spheres;
    std::vector<HostMesh> meshes;
    std::vector<HostTexture> textures;
};

GpuHostScene load_default_scene(bool has_mesh);

}
