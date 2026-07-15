#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

#include <ure/native_procedural_graph.hpp>
#include <ure/gpu_scene_compiler.hpp>

namespace {

int passed = 0;
int failed = 0;

#define CHECK(condition) do { if (condition) { ++passed; } else { ++failed; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (false)

void test_public_graph_model() {
    ure::native_scene::ProceduralGraph graph;
    graph.id = "procedural/test";
    graph.seed_high = UINT64_C(0x0123456789abcdef);
    graph.seed_low = UINT64_C(0xfedcba9876543210);
    graph.parameters.push_back(
        ure::native_scene::GraphParameter::integer("parameter/count", 8, 1, 1024));
    graph.nodes.push_back(
        ure::native_scene::ProceduralGraphNode::source_mesh("node/source_mesh", "mesh/00000000"));
    graph.root = {"node/source_mesh", "out"};

    CHECK(graph.parameters.front().kind == ure::native_scene::ParameterValueKind::Integer);
    CHECK(graph.nodes.front().output_type() == ure::native_scene::ProceduralPortType::MeshReference);
    CHECK(graph.root.node_id == "node/source_mesh");
}

ure::native_scene::NativeSceneArchive make_source_archive() {
    ure::scene_ir::SceneIR scene;
    auto material = std::make_shared<ure::scene_ir::MaterialNode>();
    material->name = "base";
    scene.add_material(material);
    auto mesh = std::make_shared<ure::Mesh>();
    mesh->vertices = {
        {{0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
        {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
        {{0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}}
    };
    mesh->indices = {0, 1, 2};
    scene.register_mesh("surface", mesh);
    ure::native_scene::SceneDocument document;
    document.id = "scene/q4";
    document.schema_version = {1, 0};
    return ure::native_scene::make_native_scene_archive(std::move(document), scene);
}

ure::native_scene::ProceduralGraph make_valid_graph() {
    using namespace ure::native_scene;
    ProceduralGraph graph;
    graph.id = "procedural/valid";
    graph.seed_high = 11;
    graph.seed_low = 29;
    graph.parameters.push_back(GraphParameter::integer("parameter/count", 3, 1, 8));
    ProceduralGraphNode rig;
    rig.id = "node/rig";
    rig.payload = LightRigNode{};
    ProceduralGraphNode compose;
    compose.id = "node/compose";
    compose.payload = ComposeFragmentsNode{};
    compose.inputs.push_back({"fragments", {"node/rig", "out"}});
    graph.nodes = {rig, compose};
    graph.root = {"node/compose", "out"};
    return graph;
}

ure::native_scene::ProceduralGraph make_full_graph() {
    using namespace ure::native_scene;
    ProceduralGraph graph;
    graph.id = "procedural/full";
    graph.seed_high = 17;
    graph.seed_low = 41;
    graph.nodes.push_back(ProceduralGraphNode::source_mesh("node/mesh", "mesh/00000000"));
    graph.nodes.push_back(ProceduralGraphNode::source_material("node/material", "material/00000000"));

    ProceduralGraphNode scatter;
    scatter.id = "node/scatter";
    ScatterSurfaceNode scatter_data;
    scatter_data.count = ParameterBinding::integer(3);
    scatter_data.scale_min = ParameterBinding::vec3({0.5f, 0.5f, 0.5f});
    scatter_data.scale_max = ParameterBinding::vec3({1.5f, 1.5f, 1.5f});
    scatter_data.yaw_max = ParameterBinding::scalar(6.283185307179586);
    scatter.payload = scatter_data;
    scatter.inputs.push_back({"mesh", {"node/mesh", "out"}});
    graph.nodes.push_back(scatter);

    ProceduralGraphNode instantiate;
    instantiate.id = "node/instantiate";
    instantiate.payload = InstantiateNode{};
    instantiate.inputs = {
        {"transforms", {"node/scatter", "out"}},
        {"mesh", {"node/mesh", "out"}},
        {"material", {"node/material", "out"}}
    };
    graph.nodes.push_back(instantiate);

    ProceduralGraphNode spectrum;
    spectrum.id = "node/spectrum";
    SpectrumGeneratorNode spectrum_data;
    spectrum_data.sample_count = ParameterBinding::integer(8);
    spectrum.payload = spectrum_data;
    graph.nodes.push_back(spectrum);

    ProceduralGraphNode rig;
    rig.id = "node/rig";
    LightRigNode rig_data;
    rig_data.count_x = ParameterBinding::integer(3);
    rig.payload = rig_data;
    rig.inputs.push_back({"spectrum", {"node/spectrum", "out"}});
    graph.nodes.push_back(rig);

    ProceduralGraphNode compose;
    compose.id = "node/compose";
    compose.payload = ComposeFragmentsNode{};
    compose.inputs = {
        {"fragments", {"node/instantiate", "out"}},
        {"fragments", {"node/rig", "out"}}
    };
    graph.nodes.push_back(compose);
    graph.root = {"node/compose", "out"};
    return graph;
}

void test_validation_and_identity() {
    const auto source = make_source_archive();
    auto graph = make_valid_graph();
    CHECK(ure::native_scene::validate_procedural_graph(graph, source).ok());
    const std::string source_hash = ure::native_scene::procedural_source_hash(graph, source);
    const std::string cache_key = ure::native_scene::procedural_cache_key(graph, source);
    CHECK(source_hash.size() == 64);
    CHECK(cache_key.size() == 64);

    std::reverse(graph.nodes.begin(), graph.nodes.end());
    CHECK(ure::native_scene::procedural_source_hash(graph, source) == source_hash);
    CHECK(ure::native_scene::procedural_cache_key(graph, source) == cache_key);

    graph.seed_low += 1;
    CHECK(ure::native_scene::procedural_cache_key(graph, source) != cache_key);

    auto invalid = make_valid_graph();
    invalid.nodes.push_back(invalid.nodes.front());
    const auto report = ure::native_scene::validate_procedural_graph(invalid, source);
    CHECK(!report.ok());
    CHECK(!report.diagnostics.empty());
    CHECK(report.diagnostics.front().code.starts_with("URE-Q4-"));
}

void test_deterministic_build() {
    const auto source = make_source_archive();
    auto archive = source;
    archive.procedural_graph = std::make_shared<const ure::native_scene::ProceduralGraph>(make_full_graph());
    const auto first = ure::native_scene::build_procedural_scene(archive);
    const auto second = ure::native_scene::build_procedural_scene(archive);
    CHECK(first.ok());
    CHECK(second.ok());
    if (!first.value || !second.value) return;
    CHECK(first.value->scene.instances.size() == 3);
    CHECK(first.value->scene.quad_lights.size() == 3);
    CHECK(first.value->generated_resources.size() == 1);
    CHECK(first.value->source_hash == second.value->source_hash);
    CHECK(first.value->cache_key == second.value->cache_key);
    CHECK(first.value->output_hash == second.value->output_hash);
    CHECK(first.value->generated_resources.front().payload == second.value->generated_resources.front().payload);
    CHECK(source.scene.instances.empty());
    CHECK(source.scene.quad_lights.empty());
}

void test_binary_roundtrip() {
    auto archive = make_source_archive();
    archive.procedural_graph = std::make_shared<const ure::native_scene::ProceduralGraph>(make_full_graph());
    const auto first_bytes = ure::native_scene::write_scene_ir_binary(archive);
    const auto second_bytes = ure::native_scene::write_scene_ir_binary(archive);
    CHECK(first_bytes == second_bytes);
    auto reordered_archive = archive;
    auto reordered_graph = *archive.procedural_graph;
    std::reverse(reordered_graph.nodes.begin(), reordered_graph.nodes.end());
    reordered_archive.procedural_graph = std::make_shared<const ure::native_scene::ProceduralGraph>(std::move(reordered_graph));
    CHECK(ure::native_scene::write_scene_ir_binary(reordered_archive) == first_bytes);
    const auto loaded = ure::native_scene::read_scene_ir_binary(first_bytes, {});
    CHECK(loaded.ok());
    CHECK(loaded.value && loaded.value->procedural_graph);
    if (!loaded.value || !loaded.value->procedural_graph) return;
    CHECK(ure::native_scene::procedural_source_hash(*loaded.value->procedural_graph, *loaded.value) ==
          ure::native_scene::procedural_source_hash(*archive.procedural_graph, archive));
    const auto built = ure::native_scene::build_procedural_scene(*loaded.value);
    CHECK(built.ok());
    CHECK(built.value && built.value->scene.instances.size() == 3);
}

void test_text_roundtrip() {
    auto archive = make_source_archive();
    archive.procedural_graph = std::make_shared<const ure::native_scene::ProceduralGraph>(make_full_graph());
    const auto first = ure::native_scene::write_scene_ir_text(archive);
    const auto second = ure::native_scene::write_scene_ir_text(archive);
    CHECK(first.manifest == second.manifest);
    CHECK(first.manifest.find("\"procedural_graph\"") != std::string::npos);
    const auto loaded = ure::native_scene::read_scene_ir_text(first, {});
    CHECK(loaded.ok());
    CHECK(loaded.value && loaded.value->procedural_graph);
    if (!loaded.value || !loaded.value->procedural_graph) return;
    CHECK(ure::native_scene::procedural_source_hash(*loaded.value->procedural_graph, *loaded.value) ==
          ure::native_scene::procedural_source_hash(*archive.procedural_graph, archive));
    const auto built = ure::native_scene::build_procedural_scene(*loaded.value);
    CHECK(built.ok());
    CHECK(built.value && built.value->scene.quad_lights.size() == 3);
}

void test_node_domains_and_layouts() {
    auto archive = make_source_archive();
    auto graph = make_full_graph();
    archive.procedural_graph = std::make_shared<const ure::native_scene::ProceduralGraph>(graph);
    const auto built = ure::native_scene::build_procedural_scene(archive);
    CHECK(built.ok());
    if (!built.value) return;
    for (const auto& instance : built.value->scene.instances) {
        CHECK(instance.position.y == 0.0f);
        CHECK(instance.position.x >= 0.0f && instance.position.z >= 0.0f);
        CHECK(instance.position.x + instance.position.z <= 1.00001f);
        CHECK(instance.scale.x >= 0.5f && instance.scale.x <= 1.5f);
    }
    std::string spd(built.value->generated_resources.front().payload.begin(),
                    built.value->generated_resources.front().payload.end());
    std::istringstream input(spd);
    double wavelength = 0.0;
    double value = 0.0;
    double previous = 0.0;
    int samples = 0;
    while (input >> wavelength >> value) {
        CHECK(wavelength > previous);
        CHECK(value >= 0.0 && value <= 1.0);
        previous = wavelength;
        ++samples;
    }
    CHECK(samples == 8);

    for (auto& node : graph.nodes) {
        if (auto* rig = std::get_if<ure::native_scene::LightRigNode>(&node.payload)) {
            rig->layout = ure::native_scene::LightRigLayout::Grid;
            rig->count_x = ure::native_scene::ParameterBinding::integer(2);
            rig->count_y = ure::native_scene::ParameterBinding::integer(2);
        }
        if (auto* spectrum = std::get_if<ure::native_scene::SpectrumGeneratorNode>(&node.payload)) {
            spectrum->mode = ure::native_scene::SpectrumGeneratorMode::GaussianLines;
            spectrum->lines = {{500.0, 1.0, 12.0}, {620.0, 0.5, 18.0}};
        }
    }
    archive.procedural_graph = std::make_shared<const ure::native_scene::ProceduralGraph>(graph);
    const auto grid = ure::native_scene::build_procedural_scene(archive);
    CHECK(grid.ok());
    CHECK(grid.value && grid.value->scene.quad_lights.size() == 4);

    for (auto& node : graph.nodes) if (auto* rig = std::get_if<ure::native_scene::LightRigNode>(&node.payload)) {
        rig->layout = ure::native_scene::LightRigLayout::ThreePoint;
    }
    archive.procedural_graph = std::make_shared<const ure::native_scene::ProceduralGraph>(graph);
    const auto three_point = ure::native_scene::build_procedural_scene(archive);
    CHECK(three_point.ok());
    CHECK(three_point.value && three_point.value->scene.quad_lights.size() == 3);

    for (auto& node : graph.nodes) if (auto* scatter = std::get_if<ure::native_scene::ScatterSurfaceNode>(&node.payload)) {
        scatter->count = ure::native_scene::ParameterBinding::integer(0);
    }
    archive.procedural_graph = std::make_shared<const ure::native_scene::ProceduralGraph>(graph);
    CHECK(!ure::native_scene::build_procedural_scene(archive).ok());
}

void test_cache_fingerprint_and_graph_semantics() {
    auto archive = make_source_archive();
    const std::string base_hash = ure::native_scene::scene_ir_semantic_hash(archive);
    archive.procedural_graph = std::make_shared<const ure::native_scene::ProceduralGraph>(make_full_graph());
    CHECK(ure::native_scene::scene_ir_semantic_hash(archive) != base_hash);
    ure::native_scene::ProceduralBuildOptions first;
    ure::native_scene::ProceduralBuildOptions second;
    second.deterministic_math_profile = "ure.ieee754.v2";
    CHECK(ure::native_scene::procedural_cache_key(*archive.procedural_graph, archive, first) !=
          ure::native_scene::procedural_cache_key(*archive.procedural_graph, archive, second));
}

void test_parameter_overrides_and_fail_loud() {
    auto archive = make_source_archive();
    auto graph = make_full_graph();
    graph.parameters.push_back(
        ure::native_scene::GraphParameter::integer("parameter/scatter_count", 2, 1, 4));
    for (auto& node : graph.nodes) if (auto* scatter = std::get_if<ure::native_scene::ScatterSurfaceNode>(&node.payload)) {
        scatter->count.parameter_id = "parameter/scatter_count";
    }
    archive.procedural_graph = std::make_shared<const ure::native_scene::ProceduralGraph>(graph);
    const auto default_build = ure::native_scene::build_procedural_scene(archive);
    CHECK(default_build.ok());
    CHECK(default_build.value && default_build.value->scene.instances.size() == 2);

    ure::native_scene::ProceduralBuildOptions options;
    options.parameter_overrides.emplace("parameter/scatter_count",
                                        ure::native_scene::ParameterValue::from_integer(4));
    const auto overridden = ure::native_scene::build_procedural_scene(archive, options);
    CHECK(overridden.ok());
    CHECK(overridden.value && overridden.value->scene.instances.size() == 4);
    CHECK(default_build.value && overridden.value && default_build.value->cache_key != overridden.value->cache_key);

    options.parameter_overrides["parameter/scatter_count"] =
        ure::native_scene::ParameterValue::from_integer(5);
    CHECK(!ure::native_scene::build_procedural_scene(archive, options).ok());

    auto invalid_enum = graph;
    for (auto& node : invalid_enum.nodes) if (auto* rig = std::get_if<ure::native_scene::LightRigNode>(&node.payload)) {
        rig->layout = static_cast<ure::native_scene::LightRigLayout>(99);
    }
    CHECK(!ure::native_scene::validate_procedural_graph(invalid_enum, archive).ok());

    auto cyclic = graph;
    for (auto& node : cyclic.nodes) if (std::holds_alternative<ure::native_scene::ComposeFragmentsNode>(node.payload)) {
        node.inputs.push_back({"fragments", {node.id, "out"}});
    }
    CHECK(!ure::native_scene::validate_procedural_graph(cyclic, archive).ok());
}

void write_fixture(const std::filesystem::path& root) {
    std::filesystem::create_directories(root);
    auto archive = make_source_archive();
    archive.procedural_graph = std::make_shared<const ure::native_scene::ProceduralGraph>(make_full_graph());
    ure::native_scene::save_native_scene(root / "procedural_scene.ure", archive);
    ure::native_scene::save_native_scene(root / "procedural_scene.urescene", archive);
    const auto built = ure::native_scene::build_procedural_scene(archive);
    if (!built.value) throw std::runtime_error("Fixture procedural build failed");
    for (const auto& resource : built.value->generated_resources) {
        const std::filesystem::path destination = root / resource.descriptor.uri;
        std::filesystem::create_directories(destination.parent_path());
        std::ofstream output(destination, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(resource.payload.data()),
                     static_cast<std::streamsize>(resource.payload.size()));
        if (!output) throw std::runtime_error("Fixture generated resource write failed");
    }
    std::ofstream hashes(root / "expected_hashes.txt", std::ios::binary | std::ios::trunc);
    hashes << built.value->source_hash << '\n' << built.value->cache_key << '\n' << built.value->output_hash << '\n';
    if (!hashes) throw std::runtime_error("Fixture hash write failed");
}

void test_retained_fixture() {
    const std::filesystem::path root = URE_TEST_ASSET_DIR;
    const auto binary = ure::native_scene::load_native_scene(root / "procedural_scene.urescene", {});
    const auto text = ure::native_scene::load_native_scene(root / "procedural_scene.ure", {});
    CHECK(binary.ok());
    CHECK(text.ok());
    if (!binary.value || !text.value) return;
    const auto binary_build = ure::native_scene::build_procedural_scene(*binary.value);
    const auto text_build = ure::native_scene::build_procedural_scene(*text.value);
    CHECK(binary_build.ok());
    CHECK(text_build.ok());
    if (!binary_build.value || !text_build.value) return;
    std::ifstream input(root / "expected_hashes.txt", std::ios::binary);
    std::string source_hash;
    std::string cache_key;
    std::string output_hash;
    input >> source_hash >> cache_key >> output_hash;
    CHECK(binary_build.value->source_hash == source_hash);
    CHECK(binary_build.value->cache_key == cache_key);
    CHECK(binary_build.value->output_hash == output_hash);
    CHECK(text_build.value->source_hash == source_hash);
    CHECK(text_build.value->output_hash == output_hash);
    const auto previous = std::filesystem::current_path();
    std::filesystem::current_path(root);
    try {
        const auto compiled = ure::GpuSceneCompiler::compile(binary_build.value->scene);
        CHECK(compiled.instances.size() == 3);
        CHECK(compiled.materials.size() >= 2);
    } catch (...) {
        std::filesystem::current_path(previous);
        throw;
    }
    std::filesystem::current_path(previous);
}

}

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--write-fixture") {
        write_fixture(URE_TEST_ASSET_DIR);
        return 0;
    }
    test_public_graph_model();
    test_validation_and_identity();
    test_deterministic_build();
    test_binary_roundtrip();
    test_text_roundtrip();
    test_node_domains_and_layouts();
    test_cache_fingerprint_and_graph_semantics();
    test_parameter_overrides_and_fail_loud();
    test_retained_fixture();
    std::printf("native procedural graph: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
