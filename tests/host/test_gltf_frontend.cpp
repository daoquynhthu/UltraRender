#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <stdexcept>

#include <ure/gpu_scene_compiler.hpp>
#include <ure/gltf_scene_frontend.hpp>
#include <ure/render.hpp>
#include <ure/render_config.hpp>
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
    CHECK(mat->graph != nullptr);
    CHECK(!mat->graph->empty());
    CHECK(mat->graph->nodes.size() >= 4);
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
    CHECK(mat->spectral_extension->albedo_spd.find("textures") != std::string::npos);
    CHECK(mat->spectral_extension->albedo_spd.find("gold.spd") != std::string::npos);
    CHECK(mat->spectral_extension->emission_spd.find("lights") != std::string::npos);
    CHECK(mat->spectral_extension->emission_spd.find("d65.spd") != std::string::npos);
    CHECK_FLOAT_EQ(mat->dispersion, 0.15f, 1e-6f);
    return 0;
}

static int test_spectral_spd_compiles_runtime_n() {
    std::filesystem::create_directories("textures");
    std::filesystem::create_directories("lights");
    {
        std::ofstream spd("textures/gold.spd");
        spd << "360 0.36\n830 0.83\n";
    }
    {
        std::ofstream spd("lights/d65.spd");
        spd << "360 1.0\n830 2.0\n";
    }

    std::string gltf = R"({
  "asset": {"version": "2.0"},
  "extensionsUsed": ["URE_spectral_material"],
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [{"mesh": 0}],
  "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1}, "indices": 2, "material": 0}]}],
  "materials": [{
    "name": "spectral_runtime_n",
    "emissiveFactor": [1.0, 1.0, 1.0],
    "extensions": {
      "URE_spectral_material": {
        "spectralBands": 64,
        "albedoSPD": "textures/gold.spd",
        "emissionSPD": "lights/d65.spd"
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
    scene.width = 4;
    scene.height = 4;
    ure::RenderConfig config;
    config.num_wavelengths = 8;
    auto compiled = ure::GpuSceneCompiler::compile(scene, config);

    CHECK(compiled.materials.size() == 1);
    const auto& mat = compiled.materials[0];
    CHECK(mat.albedo_resource.kind == ure::gpu::SpectralResourceKind::SampledTable);
    CHECK(mat.emission_resource.kind == ure::gpu::SpectralResourceKind::SampledTable);
    CHECK(mat.albedo_resource.wavelengths.size() == 2);
    CHECK(mat.albedo_resource.values.size() == 2);
    CHECK_FLOAT_EQ(mat.albedo_resource.wavelengths[0], 360.0f, 1e-6f);
    CHECK_FLOAT_EQ(mat.albedo_resource.values[0], 0.36f, 1e-6f);
    CHECK_FLOAT_EQ(mat.albedo_resource.wavelengths[1], 830.0f, 1e-6f);
    CHECK_FLOAT_EQ(mat.albedo_resource.values[1], 0.83f, 1e-6f);
    CHECK_FLOAT_EQ(mat.emission_resource.wavelengths[0], 360.0f, 1e-6f);
    CHECK_FLOAT_EQ(mat.emission_resource.values[0], 1.0f, 1e-6f);
    CHECK_FLOAT_EQ(mat.emission_resource.wavelengths[1], 830.0f, 1e-6f);
    CHECK_FLOAT_EQ(mat.emission_resource.values[1], 2.0f, 1e-6f);
    for (int c = 0; c < config.num_wavelengths; ++c) {
        float lambda = ure::gpu::kSpectralLambdaMin +
                       (static_cast<float>(c) + 0.5f) *
                           ((ure::gpu::kSpectralLambdaMax - ure::gpu::kSpectralLambdaMin) /
                            static_cast<float>(config.num_wavelengths));
        CHECK_FLOAT_EQ(mat.albedo.values[c], lambda * 0.001f, 1e-5f);
        CHECK_FLOAT_EQ(mat.emission.values[c],
                       1.0f + (lambda - ure::gpu::kSpectralLambdaMin) /
                                  (ure::gpu::kSpectralLambdaMax - ure::gpu::kSpectralLambdaMin),
                       1e-5f);
        CHECK_FLOAT_EQ(mat.albedo.wavelengths[c], lambda, 1e-5f);
        CHECK_FLOAT_EQ(mat.emission.wavelengths[c], lambda, 1e-5f);
    }

    auto engine = ure::RenderEngineFactory::create_gpu_renderer(config);
    engine->load_scene_ir(scene);
    CHECK(engine->render_pass() == 1);
    const auto& framebuffer = engine->get_framebuffer();
    CHECK(framebuffer.size() == 4 * 4 * 3);
    for (float value : framebuffer) {
        CHECK(std::isfinite(value));
    }

    bool rejected = false;
    try {
        ure::RenderConfig invalid = config;
        invalid.num_wavelengths = ure::gpu::kMaxPacketLanes + 1;
        (void)ure::GpuSceneCompiler::compile(scene, invalid);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    CHECK(rejected);

    rejected = false;
    try {
        ure::RenderConfig invalid = config;
        invalid.num_wavelengths = 4;
        (void)ure::GpuSceneCompiler::compile(scene, invalid);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    CHECK(rejected);

    std::filesystem::remove(path);
    std::filesystem::remove("textures/gold.spd");
    std::filesystem::remove("lights/d65.spd");
    std::filesystem::remove("textures");
    std::filesystem::remove("lights");
    return 0;
}

static int test_rgb_fallback_compiles_to_runtime_n_spectrum() {
    auto mat = std::make_shared<ure::scene_ir::MaterialNode>();
    mat->model = ure::scene_ir::MaterialModel::Lambertian;
    mat->base_color = {1.0f, 0.0f, 0.0f};
    mat->emission = {1.0f, 0.0f, 0.0f};

    ure::scene_ir::SceneIR scene;
    ure::scene_ir::SphereNode sphere;
    sphere.material = mat;
    scene.spheres.push_back(sphere);

    ure::RenderConfig config;
    config.num_wavelengths = 8;
    auto compiled = ure::GpuSceneCompiler::compile(scene, config);
    CHECK(compiled.materials.size() == 1);

    const auto& compiled_mat = compiled.materials[0];
    CHECK(compiled_mat.albedo.values[0] < 1e-5f);
    CHECK(compiled_mat.albedo.values[1] < 1e-5f);
    CHECK(compiled_mat.albedo.values[4] > 0.9f);
    CHECK(compiled_mat.albedo.values[7] > 0.9f);
    CHECK(compiled_mat.emission.values[4] > compiled_mat.emission.values[1]);
    for (int c = 0; c < config.num_wavelengths; ++c) {
        CHECK(compiled_mat.albedo.wavelengths[c] > 0.0f);
        CHECK(compiled_mat.emission.wavelengths[c] == compiled_mat.albedo.wavelengths[c]);
    }
    return 0;
}

static int test_metal_coefficients_compile_as_physical_carriers() {
    auto measured = std::make_shared<ure::scene_ir::MaterialNode>();
    measured->model = ure::scene_ir::MaterialModel::Metal;
    measured->base_color = {0.9f, 0.2f, 0.1f};
    measured->metal_eta = {2.0f, 3.0f, 4.0f};
    measured->metal_k = {5.0f, 6.0f, 7.0f};

    auto fallback = std::make_shared<ure::scene_ir::MaterialNode>();
    fallback->model = ure::scene_ir::MaterialModel::Metal;
    fallback->base_color = {0.25f, 0.5f, 0.75f};
    fallback->metal_eta = {8.0f, 9.0f, 10.0f};
    fallback->metal_k = {0.0f, 0.0f, 0.0f};

    ure::scene_ir::SceneIR scene;
    ure::scene_ir::SphereNode s0;
    s0.material = measured;
    scene.spheres.push_back(s0);
    ure::scene_ir::SphereNode s1;
    s1.material = fallback;
    scene.spheres.push_back(s1);

    ure::RenderConfig config;
    config.num_wavelengths = 8;
    auto compiled = ure::GpuSceneCompiler::compile(scene, config);
    CHECK(compiled.materials.size() == 2);

    const auto& measured_gpu = compiled.materials[0];
    CHECK_FLOAT_EQ(measured_gpu.metal_eta.values[0], 4.0f, 1e-6f);
    CHECK_FLOAT_EQ(measured_gpu.extinction.values[0], 7.0f, 1e-6f);
    CHECK_FLOAT_EQ(measured_gpu.metal_eta.values[7], 2.0f, 1e-6f);
    CHECK_FLOAT_EQ(measured_gpu.extinction.values[7], 5.0f, 1e-6f);
    CHECK(measured_gpu.extinction.values[3] > 5.0f);

    const auto& fallback_gpu = compiled.materials[1];
    for (int c = 0; c < config.num_wavelengths; ++c) {
        CHECK_FLOAT_EQ(fallback_gpu.extinction.values[c], 0.0f, 1e-6f);
        CHECK(fallback_gpu.albedo.values[c] >= 0.25f);
    }
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

// ============ Test: non-glTF rejected ============
static int test_non_gltf_rejected() {
    std::string obj = "v 0 0 0\nv 1 0 0\nv 0 1 0\n";
    std::string path = write_temp(obj, ".obj");
    bool rejected = false;
    try {
        (void)ure::GltfSceneFrontend::parse_file_to_ir(path);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    std::filesystem::remove(path);
    CHECK(rejected);
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
    CHECK(mat->graph != nullptr);
    bool found_texture_node = false;
    for (const auto& node : mat->graph->nodes) {
        if (node.kind == ure::scene_ir::MaterialGraphNodeKind::Texture2D &&
            node.texture == mat->roughness_texture) {
            found_texture_node = true;
        }
    }
    CHECK(found_texture_node);
    return 0;
}

static int test_scene_ir_quad_light_compiles_to_direct_emissive_mesh() {
    auto light = std::make_shared<ure::scene_ir::MaterialNode>();
    light->model = ure::scene_ir::MaterialModel::Light;
    light->emission = {2.0f, 3.0f, 4.0f};

    ure::scene_ir::SceneIR scene;
    ure::scene_ir::QuadLightNode quad;
    quad.corner = {0.0f, 0.0f, 0.0f};
    quad.edge_u = {2.0f, 0.0f, 0.0f};
    quad.edge_v = {0.0f, 1.0f, 0.0f};
    quad.material = light;
    scene.quad_lights.push_back(quad);

    ure::RenderConfig config;
    config.num_wavelengths = 8;
    auto compiled = ure::GpuSceneCompiler::compile(scene, config);
    CHECK(compiled.meshes.size() == 1);
    CHECK(compiled.instances.empty());
    CHECK(compiled.materials.size() == 1);
    CHECK(compiled.materials[0].header.type == ure::gpu::MaterialType::Light);

    const auto& mesh = compiled.meshes[0];
    CHECK(mesh.material_index == ure::gpu::kDefaultMaterialCount);
    CHECK(mesh.vertices.size() == 12);
    CHECK(mesh.normals.size() == 12);
    CHECK(mesh.uvs.size() == 8);
    CHECK(mesh.tangents.size() == 12);
    CHECK(mesh.indices.size() == 6);
    CHECK(mesh.indices[0] == 0 && mesh.indices[1] == 1 && mesh.indices[2] == 2);
    CHECK(mesh.indices[3] == 0 && mesh.indices[4] == 2 && mesh.indices[5] == 3);
    CHECK_FLOAT_EQ(mesh.normals[2], 1.0f, 1e-6f);
    CHECK_FLOAT_EQ(mesh.tangents[0], 1.0f, 1e-6f);
    return 0;
}

static int test_scene_ir_quad_light_rejects_degenerate_area() {
    auto light = std::make_shared<ure::scene_ir::MaterialNode>();
    light->model = ure::scene_ir::MaterialModel::Light;
    light->emission = {1.0f, 1.0f, 1.0f};

    ure::scene_ir::SceneIR scene;
    ure::scene_ir::QuadLightNode quad;
    quad.edge_u = {1.0f, 0.0f, 0.0f};
    quad.edge_v = {2.0f, 0.0f, 0.0f};
    quad.material = light;
    scene.quad_lights.push_back(quad);

    bool rejected = false;
    try {
        ure::RenderConfig config;
        config.num_wavelengths = 8;
        (void)ure::GpuSceneCompiler::compile(scene, config);
    } catch (const std::runtime_error& e) {
        rejected = std::string(e.what()).find("zero area") != std::string::npos;
    }
    CHECK(rejected);
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
    failed += run("test_spectral_spd_compiles_runtime_n",  test_spectral_spd_compiles_runtime_n);
    failed += run("test_rgb_fallback_compiles_to_runtime_n_spectrum", test_rgb_fallback_compiles_to_runtime_n_spectrum);
    failed += run("test_metal_coefficients_compile_as_physical_carriers", test_metal_coefficients_compile_as_physical_carriers);
    failed += run("test_extensions_required_rejected",     test_extensions_required_rejected);
    failed += run("test_extensions_used_spectral",         test_extensions_used_spectral);
    failed += run("test_camera_parsing",                  test_camera_parsing);
    failed += run("test_normal_texture",                  test_normal_texture);
    failed += run("test_non_gltf_rejected",               test_non_gltf_rejected);
    failed += run("test_file_not_found",                  test_file_not_found);
    failed += run("test_metallic_roughness_texture_linear", test_metallic_roughness_texture_linear);
    failed += run("test_scene_ir_quad_light_compiles_to_direct_emissive_mesh", test_scene_ir_quad_light_compiles_to_direct_emissive_mesh);
    failed += run("test_scene_ir_quad_light_rejects_degenerate_area", test_scene_ir_quad_light_rejects_degenerate_area);

    fprintf(stderr, "  passed: %d, failed: %d\n", g_passed, failed);
    g_failed += failed;
    if (g_failed > 0) {
        fprintf(stderr, "  OVERALL: FAIL\n");
    } else {
        fprintf(stderr, "  OVERALL: PASS\n");
    }
    return g_failed;
}
