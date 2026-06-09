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

static int g_passed = 0, g_failed = 0;
static int g_counter = 0;

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

#define CHECK_VEC3_EQ(a, b, eps) do { \
    CHECK_FLOAT_EQ((a).x, (b).x, eps); \
    CHECK_FLOAT_EQ((a).y, (b).y, eps); \
    CHECK_FLOAT_EQ((a).z, (b).z, eps); \
} while(0)

static std::string write_temp(const std::string& content, const std::string& ext) {
    std::string name = "test_gltf_" + std::to_string(g_counter++) + ext;
    std::ofstream f(name, std::ios::binary);
    f.write(content.data(), content.size());
    return name;
}

// Pre-encoded base64 for a single triangle:
// Positions (3 * 3 * 4 = 36 bytes): v0=(0,0,0) v1=(1,0,0) v2=(0,1,0)
// Normals  (3 * 3 * 4 = 36 bytes): all (0,0,1)
// UVs      (3 * 2 * 4 = 24 bytes): (0,0) (1,0) (0,1)
// Indices  (3 * 2 = 6 bytes): 0,1,2 (uint16)
// Total: 102 bytes
static const char kTriangleBase64[] = "AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAABAAIA";

static const char kGltfHeader[] = R"({
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [{"mesh": 0}],
  "meshes": [{
    "name": "test_mesh",
    "primitives": [{
      "attributes": {
        "POSITION": 0,
        "NORMAL": 1,
        "TEXCOORD_0": 2
      },
      "indices": 3,
      "material": 0
    }]
  }],
  "materials": [{
    "name": "test_mat",
    "pbrMetallicRoughness": {
      "baseColorFactor": [0.2, 0.4, 0.6, 1.0],
      "metallicFactor": 0.8,
      "roughnessFactor": 0.3
    }
  }],
  "textures": [],
  "images": [],
  "buffers": [{
    "uri": "data:application/octet-stream;base64,)";

static const char kGltfAccessors[] = R"(],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36},
    {"buffer": 0, "byteOffset": 36, "byteLength": 36},
    {"buffer": 0, "byteOffset": 72, "byteLength": 24},
    {"buffer": 0, "byteOffset": 96, "byteLength": 6}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
    {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"},
    {"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2"},
    {"bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR"}
  ]
})";

static std::string make_gltf() {
    std::string s = kGltfHeader;
    s += kTriangleBase64;
    s += "\", \"byteLength\": 102}";
    s += kGltfAccessors;
    return s;
}
// ============ Test: tangent generation ============
static int test_empty_scene() {
    std::string gltf = R"({
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"nodes": []}]
})";
    std::string path = write_temp(gltf, ".gltf");
    auto scene = ure::GltfSceneFrontend::parse_file_to_ir(path);
    std::filesystem::remove(path);
    CHECK(scene.instances.empty());
    CHECK(scene.materials.empty());
    return 0;
}

static int test_tangent_generation() {
    std::string gltf = make_gltf();
    std::string path = write_temp(gltf, ".gltf");
    auto scene = ure::GltfSceneFrontend::parse_file_to_ir(path);
    std::filesystem::remove(path);

    CHECK(!scene.instances.empty());
    CHECK(scene.instances[0].mesh != nullptr);
    CHECK(scene.instances[0].mesh->mesh != nullptr);
    auto& mesh = *scene.instances[0].mesh->mesh;
    CHECK(mesh.vertices.size() == 3);
    CHECK(mesh.indices.size() == 3);

    for (const auto& v : mesh.vertices) {
        float len = sqrtf(v.tangent.x * v.tangent.x +
                          v.tangent.y * v.tangent.y +
                          v.tangent.z * v.tangent.z);
        CHECK_FLOAT_EQ(len, 1.0f, 1e-5f);

        float dot = v.tangent.x * v.normal.x +
                    v.tangent.y * v.normal.y +
                    v.tangent.z * v.normal.z;
        CHECK_FLOAT_EQ(dot, 0.0f, 1e-5f);

        CHECK_FLOAT_EQ(v.tangent.y, 0.0f, 1e-5f);
        CHECK_FLOAT_EQ(v.tangent.z, 0.0f, 1e-5f);
    }
    return 0;
}

// ============ Test: PBR material parsing ============
static int test_pbr_material() {
    std::string gltf = make_gltf();
    std::string path = write_temp(gltf, ".gltf");
    auto scene = ure::GltfSceneFrontend::parse_file_to_ir(path);
    std::filesystem::remove(path);

    CHECK(!scene.materials.empty());
    auto mat = scene.materials[0];
    CHECK(mat->name == "test_mat");
    CHECK_FLOAT_EQ(mat->base_color.x, 0.2f, 1e-6f);
    CHECK_FLOAT_EQ(mat->base_color.y, 0.4f, 1e-6f);
    CHECK_FLOAT_EQ(mat->base_color.z, 0.6f, 1e-6f);
    CHECK_FLOAT_EQ(mat->roughness, 0.3f, 1e-6f);
    CHECK(mat->model == ure::scene_ir::MaterialModel::Metal);
    return 0;
}

// ============ Test: URE_spectral_material extension ============
static int test_ure_spectral_extension() {
    std::string gltf = R"({
  "asset": {"version": "2.0"},
  "extensionsUsed": ["URE_spectral_material"],
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [{"mesh": 0}],
  "meshes": [{
    "primitives": [{
      "attributes": {"POSITION": 0, "NORMAL": 1},
      "indices": 2,
      "material": 0
    }]
  }],
  "materials": [{
    "name": "spectral_gold",
    "extensions": {
      "URE_spectral_material": {
        "spectralBands": 64,
        "albedoSPD": "textures/gold.spd",
        "emissionSPD": "lights/d65.spd",
        "dispersion": 0.15
      }
    }
  }],
  "buffers": [{
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAABAAIA",
    "byteLength": 78
  }],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36},
    {"buffer": 0, "byteOffset": 36, "byteLength": 36},
    {"buffer": 0, "byteOffset": 72, "byteLength": 6}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
    {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"},
    {"bufferView": 2, "componentType": 5123, "count": 3, "type": "SCALAR"}
  ]
})";
    std::string path = write_temp(gltf, ".gltf");
    auto scene = ure::GltfSceneFrontend::parse_file_to_ir(path);
    std::filesystem::remove(path);

    CHECK(!scene.materials.empty());
    auto mat = scene.materials[0];
    CHECK(mat->name == "spectral_gold");
    CHECK(mat->spectral_extension != nullptr);
    CHECK(mat->spectral_extension->spectral_bands == 64);
    CHECK(mat->spectral_extension->albedo_spd == "textures/gold.spd");
    CHECK(mat->spectral_extension->emission_spd == "lights/d65.spd");
    CHECK_FLOAT_EQ(mat->dispersion, 0.15f, 1e-6f);
    return 0;
}

// ============ Test: extensionsRequired rejects unsupported ============
static int test_extensions_required_rejected() {
    std::string gltf = R"({
  "asset": {"version": "2.0"},
  "extensionsRequired": ["KHR_unknown_unsupported"],
  "scene": 0,
  "scenes": [{"nodes": []}]
})";
    std::string path = write_temp(gltf, ".gltf");
    auto scene = ure::GltfSceneFrontend::parse_file_to_ir(path);
    std::filesystem::remove(path);
    CHECK(scene.instances.empty());
    CHECK(scene.materials.empty());
    return 0;
}

// ============ Test: extensionsUsed spectral passes ============
static int test_extensions_used_spectral() {
    std::string gltf = R"({
  "asset": {"version": "2.0"},
  "extensionsUsed": ["URE_spectral_material"],
  "scene": 0,
  "scenes": [{"nodes": []}]
})";
    std::string path = write_temp(gltf, ".gltf");
    auto scene = ure::GltfSceneFrontend::parse_file_to_ir(path);
    std::filesystem::remove(path);
    CHECK(scene.instances.empty());
    CHECK(scene.materials.empty());
    return 0;
}

// ============ Test: camera parsing ============
static int test_camera_parsing() {
    std::string gltf = R"({
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [{
    "camera": 0,
    "translation": [0.0, 2.0, 5.0]
  }],
  "cameras": [{
    "type": "perspective",
    "perspective": {
      "yfov": 0.785398,
      "aspectRatio": 1.7778
    }
  }]
})";
    std::string path = write_temp(gltf, ".gltf");
    auto scene = ure::GltfSceneFrontend::parse_file_to_ir(path);
    std::filesystem::remove(path);

    CHECK_FLOAT_EQ(scene.camera.position.x, 0.0f, 1e-4f);
    CHECK_FLOAT_EQ(scene.camera.position.y, 2.0f, 1e-4f);
    CHECK_FLOAT_EQ(scene.camera.position.z, 5.0f, 1e-4f);
    CHECK_FLOAT_EQ(scene.camera.look_at.z, 4.0f, 1e-4f);
    CHECK_FLOAT_EQ(scene.camera.fov, 45.0f, 1.0f);
    CHECK_FLOAT_EQ(scene.camera.aspect_ratio, 1.7778f, 1e-4f);
    return 0;
}

// ============ Test: normalTexture parsing ============
static int test_normal_texture() {
    std::string gltf = R"({
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [{"mesh": 0}],
  "meshes": [{
    "primitives": [{
      "attributes": {"POSITION": 0, "NORMAL": 1},
      "indices": 2,
      "material": 0
    }]
  }],
  "materials": [{
    "name": "normal_mat",
    "normalTexture": {
      "index": 0,
      "scale": 2.5
    }
  }],
  "textures": [{"source": 0}],
  "images": [{"uri": "nonexistent.png"}],
  "buffers": [{
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAABAAIA",
    "byteLength": 78
  }],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36},
    {"buffer": 0, "byteOffset": 36, "byteLength": 36},
    {"buffer": 0, "byteOffset": 72, "byteLength": 6}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
    {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"},
    {"bufferView": 2, "componentType": 5123, "count": 3, "type": "SCALAR"}
  ]
})";
    std::string path = write_temp(gltf, ".gltf");
    auto scene = ure::GltfSceneFrontend::parse_file_to_ir(path);
    std::filesystem::remove(path);

    CHECK(!scene.materials.empty());
    auto mat = scene.materials[0];
    CHECK(mat->normal_texture != nullptr);
    CHECK(mat->normal_texture->image != nullptr);
    CHECK(mat->normal_texture->image->uri.find("nonexistent.png") != std::string::npos);
    CHECK_FLOAT_EQ(mat->normal_scale, 2.5f, 1e-6f);
    return 0;
}

// ============ Test: non-glTF fallback ============
static int test_non_gltf_fallback() {
    std::string obj = "v 0 0 0\nv 1 0 0\nv 0 1 0\n";
    std::string path = write_temp(obj, ".obj");
    auto scene = ure::GltfSceneFrontend::parse_file_to_ir(path);
    std::filesystem::remove(path);
    return 0;
}

// ============ Test: file not found ============
static int test_file_not_found() {
    auto scene = ure::GltfSceneFrontend::parse_file_to_ir("nonexistent_file_xyz.gltf");
    CHECK(scene.instances.empty());
    CHECK(scene.materials.empty());
    return 0;
}

// ============ Test: metallicRoughnessTexture color space ============
static int test_metallic_roughness_texture_linear() {
    std::string gltf = R"({
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [{"mesh": 0}],
  "meshes": [{
    "primitives": [{
      "attributes": {"POSITION": 0, "NORMAL": 1},
      "indices": 2,
      "material": 0
    }]
  }],
  "materials": [{
    "name": "mr_mat",
    "pbrMetallicRoughness": {
      "metallicRoughnessTexture": {"index": 0}
    }
  }],
  "textures": [{"source": 0}],
  "images": [{"uri": "nonexistent_mr.png"}],
  "buffers": [{
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAABAAIA",
    "byteLength": 78
  }],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36},
    {"buffer": 0, "byteOffset": 36, "byteLength": 36},
    {"buffer": 0, "byteOffset": 72, "byteLength": 6}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
    {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"},
    {"bufferView": 2, "componentType": 5123, "count": 3, "type": "SCALAR"}
  ]
})";
    std::string path = write_temp(gltf, ".gltf");
    auto scene = ure::GltfSceneFrontend::parse_file_to_ir(path);
    std::filesystem::remove(path);

    CHECK(!scene.materials.empty());
    auto mat = scene.materials[0];
    CHECK(mat->roughness_texture != nullptr);
    CHECK(mat->roughness_texture->image != nullptr);
    CHECK(mat->roughness_texture->image->uri.find("nonexistent_mr.png") != std::string::npos);
    CHECK(mat->roughness_texture->image->color_space == ure::scene_ir::ImageColorSpace::Linear);
    return 0;
}
int main() {
    fprintf(stderr, "[glTF Frontend Test]\n");

    auto run = [](const char* name, int (*fn)()) {
        fprintf(stderr, "  test: %s ... ", name);
        int r = fn();
        fprintf(stderr, "%s\n", r == 0 ? "PASS" : "FAIL");
        return r != 0;
    };

    int failed = 0;
    failed += run("test_empty_scene",                     test_empty_scene);
    failed += run("test_tangent_generation",              test_tangent_generation);
    failed += run("test_pbr_material",                    test_pbr_material);
    failed += run("test_ure_spectral_extension",           test_ure_spectral_extension);
    failed += run("test_extensions_required_rejected",     test_extensions_required_rejected);
    failed += run("test_extensions_used_spectral",         test_extensions_used_spectral);
    failed += run("test_camera_parsing",                  test_camera_parsing);
    failed += run("test_normal_texture",                  test_normal_texture);
    failed += run("test_non_gltf_fallback",               test_non_gltf_fallback);
    failed += run("test_file_not_found",                  test_file_not_found);
    failed += run("test_metallic_roughness_texture_linear", test_metallic_roughness_texture_linear);

    fprintf(stderr, "  passed: %d, failed: %d\n", g_passed, failed);
    g_failed += failed;
    if (g_failed > 0) {
        fprintf(stderr, "  OVERALL: FAIL\n");
    } else {
        fprintf(stderr, "  OVERALL: PASS\n");
    }
    return g_failed;
}
