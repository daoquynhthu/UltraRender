#include "ure/materialx_io.hpp"

#include "ure/detail/cuda_scene_compiler.hpp"
#include "ure/render_config.hpp"

#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>

static int g_failed = 0;
static int g_passed = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        ++g_failed; \
        return 1; \
    } \
    ++g_passed; \
} while (0)

static ure::scene_ir::MaterialGraphInput input(const char* name, ure::scene_ir::MaterialGraphNodeId id) {
    ure::scene_ir::MaterialGraphInput in;
    in.name = name;
    in.node_id = id;
    return in;
}

static ure::scene_ir::SceneIR scene_with_graph(ure::scene_ir::MaterialGraph graph) {
    ure::scene_ir::SceneIR scene;
    auto material = std::make_shared<ure::scene_ir::MaterialNode>();
    material->name = "materialx_import";
    material->graph = std::make_shared<ure::scene_ir::MaterialGraph>(std::move(graph));
    scene.materials.push_back(material);
    ure::scene_ir::SphereNode sphere;
    sphere.material = material;
    scene.spheres.push_back(sphere);
    return scene;
}

static int test_ure_materialx_roundtrips_layer_graph() {
    ure::scene_ir::MaterialGraph graph;
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
    graph.nodes = {ior, coating, color, substrate, thickness, absorption, layer, output};
    graph.output_node_id = output.id;

    std::string xml = ure::io::export_materialx_graph(graph, "LayeredMaterial");
    CHECK(xml.find("URE_bsdf_layer") != std::string::npos);
    ure::scene_ir::MaterialGraph imported = ure::io::import_materialx_graph(xml);
    CHECK(imported.nodes.size() == graph.nodes.size());
    CHECK(imported.output_node_id == output.id);
    const auto& imported_layer = imported.require_node(layer.id, "layer");
    CHECK(imported_layer.kind == ure::scene_ir::MaterialGraphNodeKind::BsdfLayer);
    CHECK(imported_layer.inputs.size() == 4);
    CHECK(imported.require_node(absorption.id, "absorption").color.z == 0.3f);
    ure::RenderConfig config;
    config.num_wavelengths = 8;
    auto compiled = ure::GpuSceneCompiler::compile(scene_with_graph(imported), config);
    CHECK(compiled.materials[0].header.type == ure::gpu::MaterialType::Layered);
    return 0;
}

static int test_standard_surface_imports_as_lambert_graph() {
    const char* xml =
        "<materialx version=\"1.39\">"
        "<standard_surface name=\"shader\">"
        "<input name=\"base_color\" type=\"color3\" value=\"0.25,0.5,0.75\" />"
        "<input name=\"roughness\" type=\"float\" value=\"0.4\" />"
        "</standard_surface>"
        "<surfacematerial name=\"mat\">"
        "<input name=\"surface\" nodename=\"shader\" />"
        "</surfacematerial>"
        "</materialx>";
    ure::scene_ir::MaterialGraph graph = ure::io::import_materialx_graph(xml);
    CHECK(!graph.empty());
    const auto& output = graph.require_node(graph.output_node_id, "output");
    CHECK(output.kind == ure::scene_ir::MaterialGraphNodeKind::OutputSurface);
    const auto& shader = graph.require_node(output.inputs[0].node_id, "shader");
    CHECK(shader.kind == ure::scene_ir::MaterialGraphNodeKind::BsdfLambert);
    CHECK(shader.inputs.size() == 2);
    ure::RenderConfig config;
    config.num_wavelengths = 8;
    auto compiled = ure::GpuSceneCompiler::compile(scene_with_graph(graph), config);
    CHECK(compiled.materials[0].header.type == ure::gpu::MaterialType::Lambertian);
    return 0;
}

static int test_materialx_unknown_node_fails_loud() {
    bool rejected = false;
    try {
        (void)ure::io::import_materialx_graph(
            "<materialx><unsupported_shader name=\"bad\" /></materialx>");
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    CHECK(rejected);
    return 0;
}

int main() {
    std::fprintf(stderr, "[MaterialX IO Test]\n");
    auto run = [](const char* name, int (*fn)()) -> int {
        std::fprintf(stderr, "  test: %s ... ", name);
        int r = 1;
        try {
            r = fn();
        } catch (const std::exception& e) {
            std::fprintf(stderr, "EXCEPTION: %s\n", e.what());
            return 1;
        }
        std::fprintf(stderr, "%s\n", r == 0 ? "PASS" : "FAIL");
        return r != 0;
    };

    int failed = 0;
    failed += run("test_ure_materialx_roundtrips_layer_graph", test_ure_materialx_roundtrips_layer_graph);
    failed += run("test_standard_surface_imports_as_lambert_graph", test_standard_surface_imports_as_lambert_graph);
    failed += run("test_materialx_unknown_node_fails_loud", test_materialx_unknown_node_fails_loud);
    std::fprintf(stderr, "  passed: %d, failed: %d\n", g_passed, failed);
    g_failed += failed;
    if (g_failed > 0) {
        std::fprintf(stderr, "  OVERALL: FAIL\n");
        return 1;
    }
    std::fprintf(stderr, "  OVERALL: PASS\n");
    return 0;
}
