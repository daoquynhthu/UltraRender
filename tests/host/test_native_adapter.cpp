#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#include <ure/native_adapter.hpp>
#include <ure/materialx_io.hpp>

namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
}

int main() {
    using namespace ure::native_scene;
    const auto imported = import_gltf_native(std::filesystem::path(URE_TEST_SCENE_DIR) / "cornell_box.gltf");
    check(imported.ok(), "glTF did not enter the validated native boundary");
    check(!imported.archive.document.id.empty() && !imported.archive.scene.materials.empty(), "glTF native archive is incomplete");
    auto advanced = imported.archive;
    advanced.scene.spheres.emplace_back();
    advanced.solver_contract = std::make_shared<const NativeSolverContract>();
    advanced.simulation_contract = std::make_shared<const NativeSimulationContract>();
    advanced.resource_catalog = std::make_shared<const NativeResourceCatalog>();
    const auto gltf = assess_native_export(advanced, AdapterFormat::Gltf);
    check(!gltf.lossless() && !gltf.exportable(), "advanced glTF export did not fail loud");
    check(gltf.losses.size() >= 4, "advanced feature loss inventory is incomplete");
    const std::string serialized = write_adapter_loss_report(gltf);
    check(serialized.find("ure.adapter.loss/1.0") != std::string::npos, "loss report schema identity is missing");
    check(serialized.find("ure.render.solver") != std::string::npos, "solver loss is missing");
    const auto usd = assess_native_export(imported.archive, AdapterFormat::Usd);
    check(!usd.exportable() && !usd.losses.empty(), "unimplemented USD export did not fail loud");
    const bool has_graph = !imported.archive.scene.materials.empty() && imported.archive.scene.materials.front() && imported.archive.scene.materials.front()->graph;
    check(has_graph, "glTF adapter did not create a MaterialGraph");
    if (has_graph) {
        AdapterLossReport materialx_loss;
        const std::string materialx = export_materialx_native(*imported.archive.scene.materials.front()->graph, materialx_loss, "adapter_fixture");
        const auto materialx_import = import_materialx_native(materialx);
        check(materialx_loss.lossless() && materialx_import.ok(), "MaterialX expressible subset did not roundtrip");
    }
    const auto invalid_materialx = import_materialx_native("<materialx version=\"1.38\"><unsupported/></materialx>");
    check(!invalid_materialx.ok(), "unsupported MaterialX subset did not fail loud");
    std::cout << "Phase Q.10 native adapter checks: " << (failures ? "FAILED" : "PASSED") << '\n';
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
