#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>

#include <ure/gpu_scene_compiler.hpp>
#include <ure/render_config.hpp>
#include <ure/scene_ir.hpp>

static int g_passed = 0;
static int g_failed = 0;

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

static ure::scene_ir::MaterialGraphInput input(const char* name, ure::scene_ir::MaterialGraphNodeId node_id) {
    ure::scene_ir::MaterialGraphInput in;
    in.name = name;
    in.node_id = node_id;
    return in;
}

static ure::scene_ir::SceneIR scene_with_material(const std::shared_ptr<ure::scene_ir::MaterialNode>& material) {
    ure::scene_ir::SceneIR scene;
    ure::scene_ir::SphereNode sphere;
    sphere.material = material;
    scene.spheres.push_back(sphere);
    return scene;
}

static int test_lambert_graph_compiles_to_gpu_material() {
    auto material = std::make_shared<ure::scene_ir::MaterialNode>();
    material->model = ure::scene_ir::MaterialModel::Metal;
    material->roughness = 0.9f;
    material->graph = std::make_shared<ure::scene_ir::MaterialGraph>();

    ure::scene_ir::MaterialGraphNode color;
    color.id = 1;
    color.kind = ure::scene_ir::MaterialGraphNodeKind::ConstantColor;
    color.color = {0.15f, 0.35f, 0.75f};

    ure::scene_ir::MaterialGraphNode roughness;
    roughness.id = 2;
    roughness.kind = ure::scene_ir::MaterialGraphNodeKind::ConstantFloat;
    roughness.value = 0.37f;

    ure::scene_ir::MaterialGraphNode bsdf;
    bsdf.id = 3;
    bsdf.kind = ure::scene_ir::MaterialGraphNodeKind::BsdfLambert;
    bsdf.inputs.push_back(input("base_color", color.id));
    bsdf.inputs.push_back(input("roughness", roughness.id));

    ure::scene_ir::MaterialGraphNode output;
    output.id = 4;
    output.kind = ure::scene_ir::MaterialGraphNodeKind::OutputSurface;
    output.inputs.push_back(input("surface", bsdf.id));

    material->graph->nodes = {color, roughness, bsdf, output};
    material->graph->output_node_id = output.id;

    ure::RenderConfig config;
    config.num_wavelengths = 8;
    auto compiled = ure::GpuSceneCompiler::compile(scene_with_material(material), config);

    CHECK(compiled.materials.size() == 1);
    const auto& gpu_material = compiled.materials[0];
    CHECK(gpu_material.header.type == ure::gpu::MaterialType::Lambertian);
    CHECK_FLOAT_EQ(gpu_material.header.roughness, 0.37f, 1e-6f);
    for (int c = 0; c < config.num_wavelengths; ++c) {
        CHECK(gpu_material.albedo.wavelengths[c] > 0.0f);
        CHECK(std::isfinite(gpu_material.albedo.values[c]));
    }
    return 0;
}

static int test_dielectric_graph_overrides_scalar_parameters() {
    auto material = std::make_shared<ure::scene_ir::MaterialNode>();
    material->model = ure::scene_ir::MaterialModel::Lambertian;
    material->ior = 1.1f;
    material->graph = std::make_shared<ure::scene_ir::MaterialGraph>();

    ure::scene_ir::MaterialGraphNode ior;
    ior.id = 1;
    ior.kind = ure::scene_ir::MaterialGraphNodeKind::ConstantFloat;
    ior.value = 1.62f;

    ure::scene_ir::MaterialGraphNode bsdf;
    bsdf.id = 2;
    bsdf.kind = ure::scene_ir::MaterialGraphNodeKind::BsdfDielectric;
    bsdf.inputs.push_back(input("ior", ior.id));

    ure::scene_ir::MaterialGraphNode output;
    output.id = 3;
    output.kind = ure::scene_ir::MaterialGraphNodeKind::OutputSurface;
    output.inputs.push_back(input("surface", bsdf.id));

    material->graph->nodes = {ior, bsdf, output};
    material->graph->output_node_id = output.id;

    ure::RenderConfig config;
    config.num_wavelengths = 8;
    auto compiled = ure::GpuSceneCompiler::compile(scene_with_material(material), config);
    CHECK(compiled.materials.size() == 1);
    CHECK(compiled.materials[0].header.type == ure::gpu::MaterialType::Dielectric);
    CHECK_FLOAT_EQ(compiled.materials[0].header.ior, 1.62f, 1e-6f);
    return 0;
}

static int test_unsupported_graph_fails_loud() {
    auto material = std::make_shared<ure::scene_ir::MaterialNode>();
    material->graph = std::make_shared<ure::scene_ir::MaterialGraph>();

    ure::scene_ir::MaterialGraphNode mix;
    mix.id = 1;
    mix.kind = ure::scene_ir::MaterialGraphNodeKind::Mix;

    ure::scene_ir::MaterialGraphNode output;
    output.id = 2;
    output.kind = ure::scene_ir::MaterialGraphNodeKind::OutputSurface;
    output.inputs.push_back(input("surface", mix.id));

    material->graph->nodes = {mix, output};
    material->graph->output_node_id = output.id;

    bool rejected = false;
    try {
        ure::RenderConfig config;
        config.num_wavelengths = 8;
        (void)ure::GpuSceneCompiler::compile(scene_with_material(material), config);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    CHECK(rejected);
    return 0;
}

static int test_graph_rejects_legacy_texture_slots() {
    auto material = std::make_shared<ure::scene_ir::MaterialNode>();
    material->base_color_texture = std::make_shared<ure::scene_ir::TextureResource>();
    material->graph = std::make_shared<ure::scene_ir::MaterialGraph>();

    ure::scene_ir::MaterialGraphNode bsdf;
    bsdf.id = 1;
    bsdf.kind = ure::scene_ir::MaterialGraphNodeKind::BsdfLambert;
    bsdf.color = {0.2f, 0.4f, 0.6f};

    ure::scene_ir::MaterialGraphNode output;
    output.id = 2;
    output.kind = ure::scene_ir::MaterialGraphNodeKind::OutputSurface;
    output.inputs.push_back(input("surface", bsdf.id));

    material->graph->nodes = {bsdf, output};
    material->graph->output_node_id = output.id;

    bool rejected = false;
    try {
        ure::RenderConfig config;
        config.num_wavelengths = 8;
        (void)ure::GpuSceneCompiler::compile(scene_with_material(material), config);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    CHECK(rejected);
    return 0;
}

int main() {
    fprintf(stderr, "[Material Graph Test]\n");

    auto run = [](const char* name, int (*fn)()) {
        fprintf(stderr, "  test: %s ... ", name);
        int r = fn();
        fprintf(stderr, "%s\n", r == 0 ? "PASS" : "FAIL");
        return r != 0;
    };

    int failed = 0;
    failed += run("test_lambert_graph_compiles_to_gpu_material", test_lambert_graph_compiles_to_gpu_material);
    failed += run("test_dielectric_graph_overrides_scalar_parameters", test_dielectric_graph_overrides_scalar_parameters);
    failed += run("test_unsupported_graph_fails_loud", test_unsupported_graph_fails_loud);
    failed += run("test_graph_rejects_legacy_texture_slots", test_graph_rejects_legacy_texture_slots);

    fprintf(stderr, "  passed: %d, failed: %d\n", g_passed, failed);
    g_failed += failed;
    if (g_failed > 0) {
        fprintf(stderr, "  OVERALL: FAIL\n");
    } else {
        fprintf(stderr, "  OVERALL: PASS\n");
    }
    return g_failed;
}
