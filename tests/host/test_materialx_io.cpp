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

static int test_diffractive_materialx_roundtrip_and_gate() {
    const ure::scene_ir::MaterialGraphNodeKind kinds[] = {
        ure::scene_ir::MaterialGraphNodeKind::BsdfGrating,
        ure::scene_ir::MaterialGraphNodeKind::BsdfPhaseMask,
        ure::scene_ir::MaterialGraphNodeKind::BsdfZonePlate,
        ure::scene_ir::MaterialGraphNodeKind::BsdfDoe,
        ure::scene_ir::MaterialGraphNodeKind::BsdfScatteringTable};
    for (int kind_index = 0;
         kind_index < 5;
         ++kind_index) {
        ure::scene_ir::MaterialGraph graph;
        ure::scene_ir::MaterialGraphNode diffraction;
        diffraction.id = 1;
        diffraction.kind = kinds[kind_index];
        diffraction.diffraction.kind =
            static_cast<
                ure::scene_ir::DiffractiveOperatorKind>(
                kind_index);
        diffraction.diffraction.side =
            ure::scene_ir::DiffractiveScatterSide::
                Transmission;
        diffraction.diffraction.period_m = 1.25e-6;
        diffraction.diffraction.orientation_rad = 0.3;
        diffraction.diffraction.duty_cycle = 0.4;
        diffraction.diffraction.phase_depth_rad = 1.2;
        diffraction.diffraction.max_order = 2;
        if (kind_index == 4) {
            diffraction.diffraction.table_id =
                "rcwa/<>&";
            ure::scene_ir::DiffractiveScatteringEntry entry;
            entry.wavelength_nm = 532.0f;
            entry.incident_cosine = 0.8f;
            entry.order = 0;
            entry.side =
                ure::scene_ir::DiffractiveScatterSide::
                    Reflection;
            entry.jones_ss = {0.5f, 0.1f};
            entry.jones_sp = {0.1f, -0.2f};
            entry.jones_ps = {-0.1f, 0.2f};
            entry.jones_pp = {0.4f, -0.1f};
            diffraction.diffraction.table.push_back(entry);
        }
        ure::scene_ir::MaterialGraphNode output;
        output.id = 2;
        output.kind =
            ure::scene_ir::MaterialGraphNodeKind::
                OutputSurface;
        output.inputs.push_back(
            input("surface", diffraction.id));
        graph.nodes = {diffraction, output};
        graph.output_node_id = output.id;

        const std::string xml =
            ure::io::export_materialx_graph(
                graph,
                "DiffractiveMaterial");
        const auto imported =
            ure::io::import_materialx_graph(xml);
        const auto& imported_diffraction =
            imported.require_node(1, "diffraction");
        CHECK(imported_diffraction.kind ==
              kinds[kind_index]);
        CHECK(imported_diffraction.diffraction.kind ==
              diffraction.diffraction.kind);
        CHECK(imported_diffraction.diffraction.period_m ==
              diffraction.diffraction.period_m);
        if (kind_index == 4) {
            CHECK(imported_diffraction.diffraction.table_id ==
                  "rcwa/<>&");
            CHECK(imported_diffraction.diffraction.table.size() ==
                  1);
            CHECK(imported_diffraction.diffraction.table[0]
                      .jones_sp.imag == -0.2f);
        }

        ure::RenderConfig disabled;
        bool rejected = false;
        try {
            (void)ure::GpuSceneCompiler::compile(
                scene_with_graph(imported),
                disabled);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        CHECK(rejected);

        ure::RenderConfig enabled;
        enabled.wave_optics.diffractive_materials_enabled =
            true;
        const auto compiled =
            ure::GpuSceneCompiler::compile(
                scene_with_graph(imported),
                enabled);
        CHECK(compiled.materials.size() == 1);
        CHECK(compiled.materials[0].header.type ==
              ure::gpu::MaterialType::Diffractive);
        CHECK(compiled.materials[0].diffraction_operator.kind ==
              static_cast<
                  ure::gpu::GpuDiffractiveOperatorKind>(
                  kind_index));
    }
    return 0;
}

static int test_fluorescence_materialx_roundtrip_and_gate() {
    ure::scene_ir::MaterialGraph graph;
    ure::scene_ir::MaterialGraphNode fluorescence;
    fluorescence.id = 1;
    fluorescence.kind =
        ure::scene_ir::MaterialGraphNodeKind::
            BsdfFluorescence;
    fluorescence.fluorescence.resource_id =
        "fluorescence/<>&";
    fluorescence.fluorescence
        .excitation_wavelengths_nm = {
            400.0f,
            500.0f};
    fluorescence.fluorescence
        .emission_wavelengths_nm = {
            600.0f,
            700.0f};
    fluorescence.fluorescence
        .excitation_efficiency = {
            0.8f,
            0.6f};
    fluorescence.fluorescence.quantum_yield = {
        0.5f,
        0.4f};
    fluorescence.fluorescence
        .emission_pdf_per_nm = {
            0.01f,
            0.01f,
            0.01f,
            0.01f};
    fluorescence.fluorescence.lifetime_seconds =
        0.002;
    ure::scene_ir::MaterialGraphNode output;
    output.id = 2;
    output.kind =
        ure::scene_ir::MaterialGraphNodeKind::
            OutputSurface;
    output.inputs.push_back(
        input("surface", fluorescence.id));
    graph.nodes = {fluorescence, output};
    graph.output_node_id = output.id;

    const auto imported =
        ure::io::import_materialx_graph(
            ure::io::export_materialx_graph(
                graph,
                "FluorescenceMaterial"));
    const auto& node =
        imported.require_node(1, "fluorescence");
    CHECK(node.kind ==
          ure::scene_ir::MaterialGraphNodeKind::
              BsdfFluorescence);
    CHECK(node.fluorescence.resource_id ==
          "fluorescence/<>&");
    CHECK(node.fluorescence
              .emission_pdf_per_nm ==
          fluorescence.fluorescence
              .emission_pdf_per_nm);
    CHECK(node.fluorescence.lifetime_seconds ==
          0.002);

    bool rejected = false;
    try {
        (void)ure::GpuSceneCompiler::compile(
            scene_with_graph(imported),
            ure::RenderConfig{});
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    CHECK(rejected);
    ure::RenderConfig enabled;
    enabled.wave_optics.fluorescence_enabled =
        true;
    const auto compiled =
        ure::GpuSceneCompiler::compile(
            scene_with_graph(imported),
            enabled);
    CHECK(compiled.materials.size() == 1);
    CHECK(compiled.materials[0].header.type ==
          ure::gpu::MaterialType::Fluorescent);
    CHECK(compiled.materials[0]
              .fluorescence.resource_id ==
          "fluorescence/<>&");
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
    failed += run("test_diffractive_materialx_roundtrip_and_gate", test_diffractive_materialx_roundtrip_and_gate);
    failed += run("test_fluorescence_materialx_roundtrip_and_gate", test_fluorescence_materialx_roundtrip_and_gate);
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
