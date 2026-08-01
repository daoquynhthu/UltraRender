#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#endif

#include <ure/native_adapter.hpp>
#include <ure/materialx_io.hpp>
#include <ure/native_scene_tooling.hpp>

namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

int run_cli_export(
    const std::filesystem::path& input,
    const std::filesystem::path& output) {
#if defined(_WIN32)
    const std::filesystem::path executable(
        URE_TEST_CLI);
    return static_cast<int>(_wspawnl(
        _P_WAIT,
        executable.c_str(),
        L"ure_cli",
        L"export",
        input.c_str(),
        L"--output",
        output.c_str(),
        nullptr));
#else
    const std::string command =
        "\"" + std::string(URE_TEST_CLI) +
        "\" export \"" + input.string() +
        "\" --output \"" + output.string() +
        "\"";
    return std::system(command.c_str());
#endif
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
    const auto advanced_usd = assess_native_export(
        advanced,
        AdapterFormat::Usd);
    check(!advanced_usd.exportable() &&
              !advanced_usd.losses.empty(),
          "unsupported USD semantics did not fail loud");

    ure::scene_ir::SceneIR usd_scene;
    auto usd_material =
        std::make_shared<ure::scene_ir::MaterialNode>();
    usd_material->name = "mat_fixture";
    usd_material->base_color = {0.2f, 0.4f, 0.6f};
    usd_scene.materials.push_back(usd_material);
    auto usd_mesh =
        std::make_shared<ure::scene_ir::MeshResource>();
    usd_mesh->name = "mesh_fixture";
    usd_mesh->mesh = std::make_shared<ure::Mesh>();
    usd_mesh->mesh->vertices = {
        {{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 1.0f}}};
    usd_mesh->mesh->indices = {0, 1, 2};
    usd_scene.meshes.push_back(usd_mesh);
    ure::scene_ir::InstanceNode usd_instance;
    usd_instance.name = "instance_fixture";
    usd_instance.mesh = usd_mesh;
    usd_instance.material = usd_material;
    usd_instance.position = {1.0f, 2.0f, 3.0f};
    usd_scene.instances.push_back(usd_instance);
    SceneDocument usd_document;
    usd_document.id = "u6_fixture";
    usd_document.schema_version = kSceneSchemaVersion;
    const auto usd_archive = make_native_scene_archive(
        std::move(usd_document),
        usd_scene);
    const auto strict_usd = export_usda_native(
        usd_archive,
        UsdExportPolicy::Strict);
    check(strict_usd.ok() &&
              strict_usd.loss_report.lossless() &&
              strict_usd.usda.find("#usda 1.0") == 0 &&
              strict_usd.usda.find("UsdPreviewSurface") !=
                  std::string::npos &&
              strict_usd.mappings.size() == 4,
          "lossless USDA export is incomplete");
    const auto repeated_usd = export_usda_native(
        usd_archive,
        UsdExportPolicy::Strict);
    check(repeated_usd.usda == strict_usd.usda,
          "USDA export is not deterministic");
    const auto usd_root =
        std::filesystem::temp_directory_path() /
        "ure_u6_export";
    std::filesystem::remove_all(usd_root);
    const auto strict_path = usd_root / "strict.usda";
    save_usda_native(
        strict_path,
        usd_archive,
        UsdExportPolicy::Strict);
    const auto native_path =
        usd_root / "source.urescene";
    const auto tooling_path =
        usd_root / "tooling.usda";
    save_native_scene(native_path, usd_archive);
    export_native_scene_usda(
        native_path,
        tooling_path,
        UsdExportPolicy::Strict);
    const auto cli_path = usd_root / "cli.usda";
    const int cli_result = run_cli_export(
        native_path,
        cli_path);
    check(std::filesystem::file_size(strict_path) ==
              strict_usd.usda.size() &&
              std::filesystem::file_size(tooling_path) ==
                  strict_usd.usda.size() &&
              cli_result == 0 &&
              std::filesystem::file_size(cli_path) ==
                  strict_usd.usda.size(),
          "atomic USDA save produced the wrong payload");

    const auto single_package =
        usd_root / "single.urepkg";
    pack_native_scenes(single_package, {native_path});
    const auto single_package_usd =
        usd_root / "single_package.usda";
    export_native_scene_usda(
        single_package,
        single_package_usd,
        UsdExportPolicy::Strict);
    auto second_archive = usd_archive;
    second_archive.document.id = "u6_fixture_second";
    const auto second_native_path =
        usd_root / "second.urescene";
    save_native_scene(second_native_path, second_archive);
    const auto multi_package =
        usd_root / "multi.urepkg";
    pack_native_scenes(
        multi_package,
        {native_path, second_native_path});
    bool ambiguous_package_rejected = false;
    try {
        export_native_scene_usda(
            multi_package,
            usd_root / "ambiguous.usda",
            UsdExportPolicy::Strict);
    } catch (const std::invalid_argument&) {
        ambiguous_package_rejected = true;
    }
    const auto selected_package_usd =
        usd_root / "selected_package.usda";
    export_native_scene_usda(
        multi_package,
        selected_package_usd,
        UsdExportPolicy::Strict,
        {},
        "u6_fixture_second");
    check(std::filesystem::file_size(single_package_usd) ==
              strict_usd.usda.size() &&
              ambiguous_package_rejected &&
              std::filesystem::exists(selected_package_usd),
          "native package USDA scene selection is incomplete");

    const auto strict_imported = export_usda_native(
        imported.archive,
        UsdExportPolicy::Strict);
    const auto lossy_imported = export_usda_native(
        imported.archive,
        UsdExportPolicy::AllowDocumentedLoss);
    check(!strict_imported.ok() &&
              lossy_imported.ok() &&
              !lossy_imported.loss_report.lossless(),
          "USDA strict/loss-report policy boundary is wrong");
    bool missing_report_rejected = false;
    try {
        save_usda_native(
            usd_root / "lossy.usda",
            imported.archive,
            UsdExportPolicy::AllowDocumentedLoss);
    } catch (const std::invalid_argument&) {
        missing_report_rejected = true;
    }
    const auto lossy_path = usd_root / "documented.usda";
    const auto loss_path = usd_root / "documented.loss.json";
    save_usda_native(
        lossy_path,
        imported.archive,
        UsdExportPolicy::AllowDocumentedLoss,
        loss_path);
    check(missing_report_rejected &&
              std::filesystem::exists(lossy_path) &&
              std::filesystem::exists(loss_path),
          "lossy USDA save did not enforce a durable report");
    std::filesystem::remove_all(usd_root);
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
