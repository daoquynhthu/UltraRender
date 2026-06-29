#include <cmath>
#include <cstdio>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>

#include <ure/gpu_scene_compiler.hpp>
#include <ure/material_presets.hpp>
#include <ure/render_config.hpp>
#include <ure/scene_ir.hpp>

static int g_passed = 0;
static int g_failed = 0;

static const unsigned char kTestBmp[] = {
    0x42, 0x4D, 0x4A, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x36, 0x00, 0x00, 0x00, 0x28, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0xFE, 0xFF,
    0xFF, 0xFF, 0x01, 0x00, 0x18, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x13, 0x0B,
    0x00, 0x00, 0x13, 0x0B, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF, 0x00,
    0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
};

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
    return ure::scene_ir::material_graph_input(name, node_id);
}

static ure::scene_ir::MaterialGraphNodeId add_constant_float(ure::scene_ir::MaterialGraph& graph, float value) {
    ure::scene_ir::MaterialGraphNode node;
    node.kind = ure::scene_ir::MaterialGraphNodeKind::ConstantFloat;
    node.value = value;
    return graph.add_node(node);
}

static ure::scene_ir::SceneIR scene_with_material(const std::shared_ptr<ure::scene_ir::MaterialNode>& material) {
    ure::scene_ir::SceneIR scene;
    ure::scene_ir::SphereNode sphere;
    sphere.material = material;
    scene.spheres.push_back(sphere);
    return scene;
}

static std::shared_ptr<ure::scene_ir::TextureResource> write_texture_resource(const std::string& path) {
    {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(kTestBmp), sizeof(kTestBmp));
    }
    auto image = std::make_shared<ure::scene_ir::ImageResource>();
    image->name = path;
    image->uri = path;
    image->color_space = ure::scene_ir::ImageColorSpace::Linear;

    auto texture = std::make_shared<ure::scene_ir::TextureResource>();
    texture->name = path;
    texture->image = image;
    return texture;
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
    CHECK(gpu_material.header.roughness_expression_root >= 0);
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

static int test_graph_overrides_scene_ir_texture_fields() {
    auto material = std::make_shared<ure::scene_ir::MaterialNode>();
    material->base_color_texture = std::make_shared<ure::scene_ir::TextureResource>();
    material->base_color_texture->image = std::make_shared<ure::scene_ir::ImageResource>();
    material->base_color_texture->image->uri = "missing_scalar_texture_must_not_load.bmp";
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

    ure::RenderConfig config;
    config.num_wavelengths = 8;
    auto compiled = ure::GpuSceneCompiler::compile(scene_with_material(material), config);
    CHECK(compiled.textures.empty());
    CHECK(compiled.materials.size() == 1);
    CHECK(compiled.materials[0].header.texture_index == -1);
    return 0;
}

static int test_graph_texture2d_compiles_to_texture_slot() {
    const std::string texture_path = "test_material_graph_texture.bmp";
    auto texture = write_texture_resource(texture_path);

    auto material = std::make_shared<ure::scene_ir::MaterialNode>();
    material->graph = std::make_shared<ure::scene_ir::MaterialGraph>();

    ure::scene_ir::MaterialGraphNode tint;
    tint.id = 1;
    tint.kind = ure::scene_ir::MaterialGraphNodeKind::ConstantColor;
    tint.color = {0.25f, 0.5f, 0.75f};

    ure::scene_ir::MaterialGraphNode tex;
    tex.id = 2;
    tex.kind = ure::scene_ir::MaterialGraphNodeKind::Texture2D;
    tex.texture = texture;

    ure::scene_ir::MaterialGraphNode multiplied;
    multiplied.id = 3;
    multiplied.kind = ure::scene_ir::MaterialGraphNodeKind::Multiply;
    multiplied.inputs.push_back(input("a", tint.id));
    multiplied.inputs.push_back(input("b", tex.id));

    ure::scene_ir::MaterialGraphNode bsdf;
    bsdf.id = 4;
    bsdf.kind = ure::scene_ir::MaterialGraphNodeKind::BsdfLambert;
    bsdf.inputs.push_back(input("base_color", multiplied.id));

    ure::scene_ir::MaterialGraphNode output;
    output.id = 5;
    output.kind = ure::scene_ir::MaterialGraphNodeKind::OutputSurface;
    output.inputs.push_back(input("surface", bsdf.id));

    material->graph->nodes = {tint, tex, multiplied, bsdf, output};
    material->graph->output_node_id = output.id;

    ure::RenderConfig config;
    config.num_wavelengths = 8;
    auto compiled = ure::GpuSceneCompiler::compile(scene_with_material(material), config);
    std::remove(texture_path.c_str());

    CHECK(compiled.textures.size() == 1);
    CHECK(compiled.textures[0].width == 2);
    CHECK(compiled.textures[0].height == 2);
    CHECK(compiled.materials.size() == 1);
    CHECK(compiled.materials[0].header.texture_index == -1);
    CHECK(compiled.materials[0].header.albedo_expression_root >= 0);
    CHECK(!compiled.materials[0].expression_nodes.empty());
    CHECK(compiled.materials[0].header.roughness_texture_index == -1);
    return 0;
}

static int test_graph_roughness_texture_compiles_to_texture_slot() {
    const std::string texture_path = "test_material_graph_roughness.bmp";
    auto texture = write_texture_resource(texture_path);

    auto material = std::make_shared<ure::scene_ir::MaterialNode>();
    material->graph = std::make_shared<ure::scene_ir::MaterialGraph>();

    ure::scene_ir::MaterialGraphNode roughness_factor;
    roughness_factor.id = 1;
    roughness_factor.kind = ure::scene_ir::MaterialGraphNodeKind::ConstantFloat;
    roughness_factor.value = 0.6f;

    ure::scene_ir::MaterialGraphNode tex;
    tex.id = 2;
    tex.kind = ure::scene_ir::MaterialGraphNodeKind::Texture2D;
    tex.texture = texture;

    ure::scene_ir::MaterialGraphNode multiplied;
    multiplied.id = 3;
    multiplied.kind = ure::scene_ir::MaterialGraphNodeKind::Multiply;
    multiplied.inputs.push_back(input("a", roughness_factor.id));
    multiplied.inputs.push_back(input("b", tex.id));

    ure::scene_ir::MaterialGraphNode bsdf;
    bsdf.id = 4;
    bsdf.kind = ure::scene_ir::MaterialGraphNodeKind::BsdfLambert;
    bsdf.inputs.push_back(input("roughness", multiplied.id));

    ure::scene_ir::MaterialGraphNode output;
    output.id = 5;
    output.kind = ure::scene_ir::MaterialGraphNodeKind::OutputSurface;
    output.inputs.push_back(input("surface", bsdf.id));

    material->graph->nodes = {roughness_factor, tex, multiplied, bsdf, output};
    material->graph->output_node_id = output.id;

    ure::RenderConfig config;
    config.num_wavelengths = 8;
    auto compiled = ure::GpuSceneCompiler::compile(scene_with_material(material), config);
    std::remove(texture_path.c_str());

    CHECK(compiled.textures.size() == 1);
    CHECK(compiled.materials.size() == 1);
    CHECK(compiled.materials[0].header.roughness_expression_root >= 0);
    CHECK(compiled.materials[0].header.texture_index == -1);
    CHECK(compiled.materials[0].header.roughness_texture_index == -1);
    return 0;
}

static int test_graph_add_and_mix_constants_compile() {
    auto material = std::make_shared<ure::scene_ir::MaterialNode>();
    material->graph = std::make_shared<ure::scene_ir::MaterialGraph>();

    ure::scene_ir::MaterialGraphNode low;
    low.id = 1;
    low.kind = ure::scene_ir::MaterialGraphNodeKind::ConstantFloat;
    low.value = 0.2f;

    ure::scene_ir::MaterialGraphNode high;
    high.id = 2;
    high.kind = ure::scene_ir::MaterialGraphNodeKind::ConstantFloat;
    high.value = 0.8f;

    ure::scene_ir::MaterialGraphNode factor;
    factor.id = 3;
    factor.kind = ure::scene_ir::MaterialGraphNodeKind::ConstantFloat;
    factor.value = 0.25f;

    ure::scene_ir::MaterialGraphNode mixed;
    mixed.id = 4;
    mixed.kind = ure::scene_ir::MaterialGraphNodeKind::Mix;
    mixed.inputs.push_back(input("a", low.id));
    mixed.inputs.push_back(input("b", high.id));
    mixed.inputs.push_back(input("factor", factor.id));

    ure::scene_ir::MaterialGraphNode offset;
    offset.id = 5;
    offset.kind = ure::scene_ir::MaterialGraphNodeKind::ConstantFloat;
    offset.value = 0.1f;

    ure::scene_ir::MaterialGraphNode added;
    added.id = 6;
    added.kind = ure::scene_ir::MaterialGraphNodeKind::Add;
    added.inputs.push_back(input("a", mixed.id));
    added.inputs.push_back(input("b", offset.id));

    ure::scene_ir::MaterialGraphNode bsdf;
    bsdf.id = 7;
    bsdf.kind = ure::scene_ir::MaterialGraphNodeKind::BsdfLambert;
    bsdf.inputs.push_back(input("roughness", added.id));

    ure::scene_ir::MaterialGraphNode output;
    output.id = 8;
    output.kind = ure::scene_ir::MaterialGraphNodeKind::OutputSurface;
    output.inputs.push_back(input("surface", bsdf.id));

    material->graph->nodes = {low, high, factor, mixed, offset, added, bsdf, output};
    material->graph->output_node_id = output.id;

    ure::RenderConfig config;
    config.num_wavelengths = 8;
    auto compiled = ure::GpuSceneCompiler::compile(scene_with_material(material), config);

    CHECK(compiled.materials.size() == 1);
    CHECK(compiled.materials[0].header.roughness_expression_root >= 0);
    CHECK(!compiled.materials[0].expression_nodes.empty());
    return 0;
}

static int test_graph_builder_assigns_node_ids() {
    ure::scene_ir::MaterialGraph graph;
    ure::scene_ir::MaterialGraphNodeId a = add_constant_float(graph, 0.25f);
    ure::scene_ir::MaterialGraphNodeId b = add_constant_float(graph, 0.75f);

    ure::scene_ir::MaterialGraphNode add;
    add.kind = ure::scene_ir::MaterialGraphNodeKind::Add;
    add.inputs.push_back(ure::scene_ir::material_graph_input("a", a));
    add.inputs.push_back(ure::scene_ir::material_graph_input("b", b));
    ure::scene_ir::MaterialGraphNodeId c = graph.add_node(add);

    CHECK(a == 1);
    CHECK(b == 2);
    CHECK(c == 3);
    CHECK(graph.nodes.size() == 3);
    CHECK(graph.nodes[2].inputs[0].output == "out");
    bool rejected = false;
    try {
        graph.validate();
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    CHECK(rejected);
    return 0;
}

static int test_graph_duplicate_node_ids_rejected() {
    auto material = std::make_shared<ure::scene_ir::MaterialNode>();
    material->graph = std::make_shared<ure::scene_ir::MaterialGraph>();

    ure::scene_ir::MaterialGraphNode a;
    a.id = 1;
    a.kind = ure::scene_ir::MaterialGraphNodeKind::BsdfLambert;

    ure::scene_ir::MaterialGraphNode b;
    b.id = 1;
    b.kind = ure::scene_ir::MaterialGraphNodeKind::ConstantFloat;
    b.value = 0.5f;

    ure::scene_ir::MaterialGraphNode output;
    output.id = 2;
    output.kind = ure::scene_ir::MaterialGraphNodeKind::OutputSurface;
    output.inputs.push_back(input("surface", a.id));

    material->graph->nodes = {a, b, output};
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

static int test_graph_cycles_rejected() {
    auto material = std::make_shared<ure::scene_ir::MaterialNode>();
    material->graph = std::make_shared<ure::scene_ir::MaterialGraph>();

    ure::scene_ir::MaterialGraphNode a;
    a.id = 1;
    a.kind = ure::scene_ir::MaterialGraphNodeKind::ConstantFloat;
    a.value = 0.5f;
    a.inputs.push_back(input("cycle", 2));

    ure::scene_ir::MaterialGraphNode b;
    b.id = 2;
    b.kind = ure::scene_ir::MaterialGraphNodeKind::ConstantFloat;
    b.value = 0.5f;
    b.inputs.push_back(input("cycle", 1));

    ure::scene_ir::MaterialGraphNode bsdf;
    bsdf.id = 3;
    bsdf.kind = ure::scene_ir::MaterialGraphNodeKind::BsdfLambert;
    bsdf.inputs.push_back(input("roughness", a.id));

    ure::scene_ir::MaterialGraphNode output;
    output.id = 4;
    output.kind = ure::scene_ir::MaterialGraphNodeKind::OutputSurface;
    output.inputs.push_back(input("surface", bsdf.id));

    material->graph->nodes = {a, b, bsdf, output};
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

static int test_texture_add_and_mix_compile_to_expression_graph() {
    const std::string texture_path = "test_material_graph_texture_expr.bmp";
    auto texture = write_texture_resource(texture_path);

    auto make_material = [&](ure::scene_ir::MaterialGraphNodeKind op) {
        auto material = std::make_shared<ure::scene_ir::MaterialNode>();
        material->graph = std::make_shared<ure::scene_ir::MaterialGraph>();

        ure::scene_ir::MaterialGraphNode tint;
        tint.id = 1;
        tint.kind = ure::scene_ir::MaterialGraphNodeKind::ConstantColor;
        tint.color = {0.1f, 0.2f, 0.3f};

        ure::scene_ir::MaterialGraphNode tex;
        tex.id = 2;
        tex.kind = ure::scene_ir::MaterialGraphNodeKind::Texture2D;
        tex.texture = texture;

        ure::scene_ir::MaterialGraphNode factor;
        factor.id = 3;
        factor.kind = ure::scene_ir::MaterialGraphNodeKind::ConstantFloat;
        factor.value = 0.5f;

        ure::scene_ir::MaterialGraphNode expr;
        expr.id = 4;
        expr.kind = op;
        expr.inputs.push_back(input("a", tint.id));
        expr.inputs.push_back(input("b", tex.id));
        if (op == ure::scene_ir::MaterialGraphNodeKind::Mix) {
            expr.inputs.push_back(input("factor", factor.id));
        }

        ure::scene_ir::MaterialGraphNode bsdf;
        bsdf.id = 5;
        bsdf.kind = ure::scene_ir::MaterialGraphNodeKind::BsdfLambert;
        bsdf.inputs.push_back(input("base_color", expr.id));

        ure::scene_ir::MaterialGraphNode output;
        output.id = 6;
        output.kind = ure::scene_ir::MaterialGraphNodeKind::OutputSurface;
        output.inputs.push_back(input("surface", bsdf.id));

        material->graph->nodes = {tint, tex, factor, expr, bsdf, output};
        material->graph->output_node_id = output.id;
        return material;
    };

    {
        ure::RenderConfig config;
        config.num_wavelengths = 8;
        auto compiled = ure::GpuSceneCompiler::compile(scene_with_material(make_material(ure::scene_ir::MaterialGraphNodeKind::Add)), config);
        CHECK(compiled.textures.size() == 1);
        CHECK(compiled.materials.size() == 1);
        CHECK(compiled.materials[0].header.albedo_expression_root >= 0);
        CHECK(compiled.materials[0].header.texture_index == -1);
        CHECK(!compiled.materials[0].expression_nodes.empty());
    }
    {
        ure::RenderConfig config;
        config.num_wavelengths = 8;
        auto compiled = ure::GpuSceneCompiler::compile(scene_with_material(make_material(ure::scene_ir::MaterialGraphNodeKind::Mix)), config);
        CHECK(compiled.textures.size() == 1);
        CHECK(compiled.materials.size() == 1);
        CHECK(compiled.materials[0].header.albedo_expression_root >= 0);
        CHECK(compiled.materials[0].header.texture_index == -1);
        CHECK(!compiled.materials[0].expression_nodes.empty());
    }
    std::remove(texture_path.c_str());
    return 0;
}

static int test_optical_parameter_textures_compile_to_typed_expression_roots() {
    const std::string texture_path = "test_material_graph_optical.bmp";
    auto texture = write_texture_resource(texture_path);

    auto metal = std::make_shared<ure::scene_ir::MaterialNode>();
    metal->graph = std::make_shared<ure::scene_ir::MaterialGraph>();
    ure::scene_ir::MaterialGraphNode eta;
    eta.id = 1;
    eta.kind = ure::scene_ir::MaterialGraphNodeKind::Texture2D;
    eta.texture = texture;
    ure::scene_ir::MaterialGraphNode k = eta;
    k.id = 2;
    ure::scene_ir::MaterialGraphNode metal_bsdf;
    metal_bsdf.id = 3;
    metal_bsdf.kind = ure::scene_ir::MaterialGraphNodeKind::BsdfMetal;
    metal_bsdf.inputs.push_back(input("eta", eta.id));
    metal_bsdf.inputs.push_back(input("k", k.id));
    ure::scene_ir::MaterialGraphNode metal_output;
    metal_output.id = 4;
    metal_output.kind = ure::scene_ir::MaterialGraphNodeKind::OutputSurface;
    metal_output.inputs.push_back(input("surface", metal_bsdf.id));
    metal->graph->nodes = {eta, k, metal_bsdf, metal_output};
    metal->graph->output_node_id = metal_output.id;

    ure::RenderConfig config;
    config.num_wavelengths = 8;
    auto compiled_metal = ure::GpuSceneCompiler::compile(scene_with_material(metal), config);
    CHECK(compiled_metal.materials[0].header.metal_eta_expression_root >= 0);
    CHECK(compiled_metal.materials[0].header.extinction_expression_root >= 0);
    CHECK(compiled_metal.materials[0].expression_nodes[0].semantic == ure::gpu::SpectralExpressionSemantic::OpticalConstant);

    auto dielectric = std::make_shared<ure::scene_ir::MaterialNode>();
    dielectric->graph = std::make_shared<ure::scene_ir::MaterialGraph>();
    ure::scene_ir::MaterialGraphNode ior = eta;
    ior.id = 1;
    ure::scene_ir::MaterialGraphNode dielectric_bsdf;
    dielectric_bsdf.id = 2;
    dielectric_bsdf.kind = ure::scene_ir::MaterialGraphNodeKind::BsdfDielectric;
    dielectric_bsdf.inputs.push_back(input("ior", ior.id));
    ure::scene_ir::MaterialGraphNode dielectric_output;
    dielectric_output.id = 3;
    dielectric_output.kind = ure::scene_ir::MaterialGraphNodeKind::OutputSurface;
    dielectric_output.inputs.push_back(input("surface", dielectric_bsdf.id));
    dielectric->graph->nodes = {ior, dielectric_bsdf, dielectric_output};
    dielectric->graph->output_node_id = dielectric_output.id;
    auto compiled_dielectric = ure::GpuSceneCompiler::compile(scene_with_material(dielectric), config);
    std::remove(texture_path.c_str());
    CHECK(compiled_dielectric.materials[0].header.ior_expression_root >= 0);
    CHECK(compiled_dielectric.materials[0].expression_nodes[0].semantic == ure::gpu::SpectralExpressionSemantic::OpticalConstant);
    return 0;
}

static int test_procedural_nodes_compile_to_device_expression_graph() {
    auto material = std::make_shared<ure::scene_ir::MaterialNode>();
    material->graph = std::make_shared<ure::scene_ir::MaterialGraph>();
    ure::scene_ir::MaterialGraphNode a;
    a.id = 1;
    a.kind = ure::scene_ir::MaterialGraphNodeKind::ConstantColor;
    a.color = {0.1f, 0.2f, 0.3f};
    ure::scene_ir::MaterialGraphNode b = a;
    b.id = 2;
    b.color = {0.7f, 0.8f, 0.9f};
    ure::scene_ir::MaterialGraphNode scale;
    scale.id = 3;
    scale.kind = ure::scene_ir::MaterialGraphNodeKind::ConstantFloat;
    scale.value = 8.0f;
    ure::scene_ir::MaterialGraphNode checker;
    checker.id = 4;
    checker.kind = ure::scene_ir::MaterialGraphNodeKind::Checker2D;
    checker.inputs = {input("a", a.id), input("b", b.id), input("scale", scale.id)};
    ure::scene_ir::MaterialGraphNode noise;
    noise.id = 5;
    noise.kind = ure::scene_ir::MaterialGraphNodeKind::Noise2D;
    noise.inputs = {input("a", checker.id), input("b", b.id), input("scale", scale.id)};
    ure::scene_ir::MaterialGraphNode bsdf;
    bsdf.id = 6;
    bsdf.kind = ure::scene_ir::MaterialGraphNodeKind::BsdfLambert;
    bsdf.inputs.push_back(input("base_color", noise.id));
    ure::scene_ir::MaterialGraphNode output;
    output.id = 7;
    output.kind = ure::scene_ir::MaterialGraphNodeKind::OutputSurface;
    output.inputs.push_back(input("surface", bsdf.id));
    material->graph->nodes = {a, b, scale, checker, noise, bsdf, output};
    material->graph->output_node_id = output.id;

    ure::RenderConfig config;
    config.num_wavelengths = 8;
    auto compiled = ure::GpuSceneCompiler::compile(scene_with_material(material), config);
    CHECK(compiled.materials.size() == 1);
    CHECK(compiled.materials[0].header.albedo_expression_root >= 0);
    CHECK(compiled.materials[0].expression_nodes.size() == 5);
    CHECK(compiled.materials[0].expression_nodes[3].kind == ure::gpu::SpectralExpressionNodeKind::Checker2D);
    CHECK(compiled.materials[0].expression_nodes[4].kind == ure::gpu::SpectralExpressionNodeKind::Noise2D);
    return 0;
}

static int test_bsdf_mix_compiles_explicit_lobes_and_weight() {
    auto material = std::make_shared<ure::scene_ir::MaterialNode>();
    material->roughness = 0.3f;
    material->metal_eta = {0.2f, 0.4f, 0.8f};
    material->metal_k = {3.0f, 2.5f, 2.0f};
    material->graph = std::make_shared<ure::scene_ir::MaterialGraph>();
    ure::scene_ir::MaterialGraphNode diffuse_color;
    diffuse_color.id = 1;
    diffuse_color.kind = ure::scene_ir::MaterialGraphNodeKind::ConstantColor;
    diffuse_color.color = {0.25f, 0.5f, 0.75f};
    ure::scene_ir::MaterialGraphNode diffuse;
    diffuse.id = 2;
    diffuse.kind = ure::scene_ir::MaterialGraphNodeKind::BsdfLambert;
    diffuse.inputs.push_back(input("base_color", diffuse_color.id));
    ure::scene_ir::MaterialGraphNode metal;
    metal.id = 3;
    metal.kind = ure::scene_ir::MaterialGraphNodeKind::BsdfMetal;
    ure::scene_ir::MaterialGraphNode factor;
    factor.id = 4;
    factor.kind = ure::scene_ir::MaterialGraphNodeKind::ConstantFloat;
    factor.value = 0.35f;
    ure::scene_ir::MaterialGraphNode mix;
    mix.id = 5;
    mix.kind = ure::scene_ir::MaterialGraphNodeKind::BsdfMix;
    mix.inputs = {input("a", diffuse.id), input("b", metal.id), input("factor", factor.id)};
    ure::scene_ir::MaterialGraphNode output;
    output.id = 6;
    output.kind = ure::scene_ir::MaterialGraphNodeKind::OutputSurface;
    output.inputs.push_back(input("surface", mix.id));
    material->graph->nodes = {diffuse_color, diffuse, metal, factor, mix, output};
    material->graph->output_node_id = output.id;

    ure::RenderConfig config;
    config.num_wavelengths = 8;
    auto compiled = ure::GpuSceneCompiler::compile(scene_with_material(material), config);
    CHECK(compiled.materials.size() == 1);
    const auto& header = compiled.materials[0].header;
    CHECK(header.type == ure::gpu::MaterialType::Composite);
    CHECK(header.bsdf_lobe_count == 2);
    CHECK(compiled.materials[0].bsdf_lobes.size() == 2);
    CHECK(compiled.materials[0].bsdf_lobes[0].type == ure::gpu::MaterialType::Lambertian);
    CHECK(compiled.materials[0].bsdf_lobes[1].type == ure::gpu::MaterialType::Metal);
    CHECK(compiled.materials[0].bsdf_lobes[0].albedo_expression_root >= 0);
    CHECK(compiled.materials[0].bsdf_lobes[1].metal_eta_expression_root >= 0);
    CHECK(compiled.materials[0].bsdf_lobes[1].extinction_expression_root >= 0);
    CHECK(header.bsdf_mix_expression_root >= 0);
    CHECK(!compiled.materials[0].expression_nodes.empty());
    return 0;
}

static int test_bsdf_mix_rejects_dielectric_medium_ambiguity() {
    auto material = std::make_shared<ure::scene_ir::MaterialNode>();
    material->graph = std::make_shared<ure::scene_ir::MaterialGraph>();
    ure::scene_ir::MaterialGraphNode dielectric;
    dielectric.id = 1;
    dielectric.kind = ure::scene_ir::MaterialGraphNodeKind::BsdfDielectric;
    ure::scene_ir::MaterialGraphNode diffuse;
    diffuse.id = 2;
    diffuse.kind = ure::scene_ir::MaterialGraphNodeKind::BsdfLambert;
    ure::scene_ir::MaterialGraphNode factor;
    factor.id = 3;
    factor.kind = ure::scene_ir::MaterialGraphNodeKind::ConstantFloat;
    factor.value = 0.5f;
    ure::scene_ir::MaterialGraphNode mix;
    mix.id = 4;
    mix.kind = ure::scene_ir::MaterialGraphNodeKind::BsdfMix;
    mix.inputs = {input("a", dielectric.id), input("b", diffuse.id), input("factor", factor.id)};
    ure::scene_ir::MaterialGraphNode output;
    output.id = 5;
    output.kind = ure::scene_ir::MaterialGraphNodeKind::OutputSurface;
    output.inputs.push_back(input("surface", mix.id));
    material->graph->nodes = {dielectric, diffuse, factor, mix, output};
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

static int test_bsdf_layer_compiles_dielectric_coating_over_diffuse_substrate() {
    auto material = std::make_shared<ure::scene_ir::MaterialNode>();
    material->graph = std::make_shared<ure::scene_ir::MaterialGraph>();
    ure::scene_ir::MaterialGraphNode ior;
    ior.id = 1;
    ior.kind = ure::scene_ir::MaterialGraphNodeKind::ConstantFloat;
    ior.value = 1.5f;
    ure::scene_ir::MaterialGraphNode coating;
    coating.id = 2;
    coating.kind = ure::scene_ir::MaterialGraphNodeKind::BsdfDielectric;
    coating.inputs.push_back(input("ior", ior.id));
    ure::scene_ir::MaterialGraphNode color;
    color.id = 3;
    color.kind = ure::scene_ir::MaterialGraphNodeKind::ConstantColor;
    color.color = {0.2f, 0.4f, 0.6f};
    ure::scene_ir::MaterialGraphNode substrate;
    substrate.id = 4;
    substrate.kind = ure::scene_ir::MaterialGraphNodeKind::BsdfLambert;
    substrate.inputs.push_back(input("base_color", color.id));
    ure::scene_ir::MaterialGraphNode thickness;
    thickness.id = 5;
    thickness.kind = ure::scene_ir::MaterialGraphNodeKind::ConstantFloat;
    thickness.value = 0.01f;
    ure::scene_ir::MaterialGraphNode absorption;
    absorption.id = 6;
    absorption.kind = ure::scene_ir::MaterialGraphNodeKind::ConstantColor;
    absorption.color = {0.1f, 0.2f, 0.3f};
    ure::scene_ir::MaterialGraphNode layer;
    layer.id = 7;
    layer.kind = ure::scene_ir::MaterialGraphNodeKind::BsdfLayer;
    layer.inputs = {
        input("coating", coating.id),
        input("substrate", substrate.id),
        input("thickness", thickness.id),
        input("absorption", absorption.id)};
    ure::scene_ir::MaterialGraphNode output;
    output.id = 8;
    output.kind = ure::scene_ir::MaterialGraphNodeKind::OutputSurface;
    output.inputs.push_back(input("surface", layer.id));
    material->graph->nodes = {ior, coating, color, substrate, thickness, absorption, layer, output};
    material->graph->output_node_id = output.id;

    ure::RenderConfig config;
    config.num_wavelengths = 8;
    auto compiled = ure::GpuSceneCompiler::compile(scene_with_material(material), config);
    CHECK(compiled.materials.size() == 1);
    const auto& data = compiled.materials[0];
    CHECK(data.header.type == ure::gpu::MaterialType::Layered);
    CHECK(data.header.bsdf_lobe_count == 2);
    CHECK(data.bsdf_lobes.size() == 2);
    CHECK(data.bsdf_lobes[0].type == ure::gpu::MaterialType::Dielectric);
    CHECK(data.bsdf_lobes[0].ior_expression_root >= 0);
    CHECK(data.bsdf_lobes[1].type == ure::gpu::MaterialType::Lambertian);
    CHECK(data.bsdf_lobes[1].albedo_expression_root >= 0);
    CHECK(data.header.layer_thickness_expression_root >= 0);
    CHECK(data.header.layer_absorption_expression_root >= 0);
    return 0;
}

static int test_bsdf_layer_rejects_non_dielectric_coating() {
    auto material = std::make_shared<ure::scene_ir::MaterialNode>();
    material->graph = std::make_shared<ure::scene_ir::MaterialGraph>();
    ure::scene_ir::MaterialGraphNode coating;
    coating.id = 1;
    coating.kind = ure::scene_ir::MaterialGraphNodeKind::BsdfMetal;
    ure::scene_ir::MaterialGraphNode substrate;
    substrate.id = 2;
    substrate.kind = ure::scene_ir::MaterialGraphNodeKind::BsdfLambert;
    ure::scene_ir::MaterialGraphNode layer;
    layer.id = 3;
    layer.kind = ure::scene_ir::MaterialGraphNodeKind::BsdfLayer;
    layer.inputs = {input("coating", coating.id), input("substrate", substrate.id)};
    ure::scene_ir::MaterialGraphNode output;
    output.id = 4;
    output.kind = ure::scene_ir::MaterialGraphNodeKind::OutputSurface;
    output.inputs.push_back(input("surface", layer.id));
    material->graph->nodes = {coating, substrate, layer, output};
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

static int test_material_presets_compile_to_material_graphs() {
    const char* names[] = {
        "gold",
        "copper",
        "aluminum",
        "clear_glass",
        "diamond_glass",
        "woven_fabric",
        "automotive_paint",
        "skin"
    };
    ure::RenderConfig config;
    config.num_wavelengths = 8;
    for (const char* name : names) {
        auto material = ure::scene_ir::make_material_preset(name);
        CHECK(material != nullptr);
        CHECK(material->graph != nullptr);
        auto compiled = ure::GpuSceneCompiler::compile(scene_with_material(material), config);
        CHECK(compiled.materials.size() == 1);
        CHECK(!compiled.materials[0].expression_nodes.empty());
    }
    return 0;
}

static int test_skin_preset_uses_participating_dielectric_medium() {
    auto material = ure::scene_ir::make_material_preset("skin");
    CHECK(material->model == ure::scene_ir::MaterialModel::Dielectric);
    CHECK(material->medium_density > 0.0f);
    CHECK(material->medium_scattering.x > material->medium_absorption.x);
    ure::RenderConfig config;
    config.num_wavelengths = 8;
    auto compiled = ure::GpuSceneCompiler::compile(scene_with_material(material), config);
    CHECK(compiled.materials.size() == 1);
    CHECK(compiled.materials[0].header.type == ure::gpu::MaterialType::Dielectric);
    CHECK(compiled.materials[0].header.medium_density > 0.0f);
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
    failed += run("test_graph_overrides_scene_ir_texture_fields", test_graph_overrides_scene_ir_texture_fields);
    failed += run("test_graph_texture2d_compiles_to_texture_slot", test_graph_texture2d_compiles_to_texture_slot);
    failed += run("test_graph_roughness_texture_compiles_to_texture_slot", test_graph_roughness_texture_compiles_to_texture_slot);
    failed += run("test_graph_add_and_mix_constants_compile", test_graph_add_and_mix_constants_compile);
    failed += run("test_graph_builder_assigns_node_ids", test_graph_builder_assigns_node_ids);
    failed += run("test_graph_duplicate_node_ids_rejected", test_graph_duplicate_node_ids_rejected);
    failed += run("test_graph_cycles_rejected", test_graph_cycles_rejected);
    failed += run("test_texture_add_and_mix_compile_to_expression_graph", test_texture_add_and_mix_compile_to_expression_graph);
    failed += run("test_optical_parameter_textures_compile_to_typed_expression_roots", test_optical_parameter_textures_compile_to_typed_expression_roots);
    failed += run("test_procedural_nodes_compile_to_device_expression_graph", test_procedural_nodes_compile_to_device_expression_graph);
    failed += run("test_bsdf_mix_compiles_explicit_lobes_and_weight", test_bsdf_mix_compiles_explicit_lobes_and_weight);
    failed += run("test_bsdf_mix_rejects_dielectric_medium_ambiguity", test_bsdf_mix_rejects_dielectric_medium_ambiguity);
    failed += run("test_bsdf_layer_compiles_dielectric_coating_over_diffuse_substrate", test_bsdf_layer_compiles_dielectric_coating_over_diffuse_substrate);
    failed += run("test_bsdf_layer_rejects_non_dielectric_coating", test_bsdf_layer_rejects_non_dielectric_coating);
    failed += run("test_material_presets_compile_to_material_graphs", test_material_presets_compile_to_material_graphs);
    failed += run("test_skin_preset_uses_participating_dielectric_medium", test_skin_preset_uses_participating_dielectric_medium);

    fprintf(stderr, "  passed: %d, failed: %d\n", g_passed, failed);
    g_failed += failed;
    if (g_failed > 0) {
        fprintf(stderr, "  OVERALL: FAIL\n");
    } else {
        fprintf(stderr, "  OVERALL: PASS\n");
    }
    return g_failed;
}
