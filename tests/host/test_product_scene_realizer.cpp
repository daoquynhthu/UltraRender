#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>

#include <ure/native_scene_tooling.hpp>
#include <ure/native_script_build.hpp>
#include <ure/product/product_scene.hpp>
#include <ure/product/product_service.hpp>
#include <ure/scene_io.hpp>

namespace {

int failures{};

void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

bool has_disposition(
    const ure::product::ProductSnapshot& snapshot,
    std::string_view id,
    ure::product::ProductFeatureDisposition disposition) {
    return std::ranges::any_of(
        snapshot.feature_dispositions(),
        [id, disposition](const auto& record) {
            return record.id == id && record.disposition == disposition;
        });
}

bool has_detail(const ure::product::ProductRealizationResult& result,
                std::uint32_t detail) {
    return std::ranges::any_of(result.diagnostics, [detail](const auto& item) {
        return item.detail == detail;
    });
}

}

int main() {
    const auto procedural = ure::native_scene::load_native_asset(
        std::filesystem::path(URE_PRV2_PROCEDURAL_SCENE));
    check(procedural.ok() && procedural.value.has_value(),
          "procedural source fixture did not load");
    if (!procedural.ok() || !procedural.value)
        return 1;
    const auto first = ure::product::realize_product_scene(*procedural.value);
    const auto second = ure::product::realize_product_scene(*procedural.value);
    check(first.ok() && second.ok(),
          "procedural ProductSnapshot realization failed");
    if (first.snapshot && second.snapshot) {
        check(first.snapshot->identity() == second.snapshot->identity(),
              "snapshot identity depends on materialization location");
        check(first.snapshot->render_scene().instances.size() >
                  procedural.value->scene.instances.size(),
              "procedural instances were not realized");
        check(first.snapshot->render_scene().quad_lights.size() >
                  procedural.value->scene.quad_lights.size(),
              "procedural lights were not realized");
        auto mutable_copy = first.snapshot->render_scene();
        const auto retained_copy = first.snapshot->render_scene();
        mutable_copy.instances.front().position.x += 100.0f;
        mutable_copy.instances.front().material->base_color.x = 0.0f;
        check(first.snapshot->render_scene().instances.front().position.x ==
                      retained_copy.instances.front().position.x &&
                  first.snapshot->render_scene()
                          .instances.front()
                          .material->base_color.x ==
                      retained_copy.instances.front().material->base_color.x,
              "ProductSnapshot exposed mutable retained scene state");
        check(has_disposition(
                  *first.snapshot, "ure.scene.procedural",
                  ure::product::ProductFeatureDisposition::Executed),
              "procedural feature was not classified as executed");
        const auto generated = std::ranges::find_if(
            first.snapshot->resources(), [](const auto& resource) {
                return resource.source_uri.find("resources/generated/") == 0;
            });
        check(generated != first.snapshot->resources().end(),
              "generated spectral resource was not retained");
        if (generated != first.snapshot->resources().end()) {
            check(std::filesystem::is_regular_file(
                      first.snapshot->materialization_root() /
                      generated->source_uri),
                  "generated spectral resource was not materialized");
        }
    }

    try {
        const auto adapted_path = std::filesystem::path(URE_PRV2_GLTF_SCENE);
        ure::native_scene::SceneDocument adapted_document;
        adapted_document.id = "scene/adapted-gltf";
        adapted_document.schema_version = {1, 0};
        adapted_document.features.push_back(
            {"ure.adapter.gltf", {1, 0},
             ure::native_scene::RequirementLevel::Optional, "ure", {}, "{}"});
        auto adapted_archive = ure::native_scene::make_native_scene_archive(
            std::move(adapted_document),
            ure::scene_io::load_gltf(adapted_path.string()));
        adapted_archive.execution_root = adapted_path.parent_path();
        const auto adapted_realized =
            ure::product::realize_product_scene(std::move(adapted_archive));
        check(adapted_realized.ok() && adapted_realized.snapshot &&
                  !adapted_realized.snapshot->render_scene().meshes.empty() &&
                  has_disposition(
                      *adapted_realized.snapshot, "ure.adapter.gltf",
                      ure::product::ProductFeatureDisposition::PreservedForTooling),
              "adapted glTF archive did not enter the unique ProductSnapshot realizer");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: adapted archive threw: %s\n", error.what());
        ++failures;
    }

    const auto root = std::filesystem::temp_directory_path() /
                      "ultrarender_prv2_realizer";
    std::filesystem::remove_all(root);
    const auto author = root / "author";
    std::filesystem::create_directories(root);
    const auto source =
        std::filesystem::path(URE_PRV2_RESOURCE_SCENE);
    std::filesystem::copy(source.parent_path(), author,
                          std::filesystem::copy_options::recursive);
    const auto package = root / "self-contained.urepkg";
    ure::native_scene::pack_native_scenes(package,
                                          {author / source.filename()});
    std::filesystem::remove_all(author);
    const auto packaged = ure::native_scene::load_native_package_scene(
        package, "scene/full");
    check(packaged.ok() && packaged.value.has_value(),
          "self-contained package did not load after source deletion");
    if (packaged.ok() && packaged.value) {
        const auto realized =
            ure::product::realize_product_scene(*packaged.value);
        check(realized.ok() && realized.snapshot,
              "self-contained package did not realize");
        if (realized.snapshot) {
            check(realized.snapshot->resources().size() >= 5,
                  "package resource inventory was not consumed");
            check(has_disposition(
                      *realized.snapshot, "ure.scene.package",
                      ure::product::ProductFeatureDisposition::Executed),
                  "package feature was not classified as executed");
        }
    }

    const auto resource_scene = ure::native_scene::load_native_asset(source);
    check(resource_scene.ok() && resource_scene.value.has_value(),
          "resource source fixture did not load");
    auto missing = *resource_scene.value;
    missing.execution_root.clear();
    const auto rejected = ure::product::realize_product_scene(std::move(missing));
    check(!rejected.ok() &&
              std::ranges::any_of(rejected.diagnostics, [](const auto& item) {
                  return item.domain ==
                         ure::product::ProductSceneDiagnosticDomain::Resource;
              }),
          "missing resource did not produce a resource-domain rejection");

    auto scripted = *procedural.value;
    scripted.document.features.push_back(
        {std::string(ure::native_scene::kScriptBuildFeature), {1, 0},
         ure::native_scene::RequirementLevel::Required, "ure", {}, "{}"});
    const auto script_rejected =
        ure::product::realize_product_scene(std::move(scripted));
    check(!script_rejected.ok() &&
              std::ranges::any_of(
                  script_rejected.diagnostics, [](const auto& item) {
                      return item.code == "URE-PRV2-SCRIPT-001";
                  }),
          "required script build did not receive its explicit runtime rejection");

    ure::native_scene::SceneDocument empty_document;
    empty_document.id = "scene/empty-resource-catalog";
    empty_document.schema_version = {1, 0};
    empty_document.features.push_back(
        {std::string(ure::native_scene::kResourceCatalogFeature), {1, 0},
         ure::native_scene::RequirementLevel::Required, "ure", {}, "{}"});
    ure::scene_ir::SceneIR empty_scene;
    auto empty_catalog = ure::native_scene::make_native_scene_archive(
        std::move(empty_document), empty_scene);
    ure::native_scene::NativeResourceCatalog catalog;
    catalog.id = "resources/empty";
    catalog.schema_version = {1, 0};
    empty_catalog.resource_catalog =
        std::make_shared<const ure::native_scene::NativeResourceCatalog>(
            std::move(catalog));
    const auto catalog_realized =
        ure::product::realize_product_scene(std::move(empty_catalog));
    check(catalog_realized.ok() && catalog_realized.snapshot &&
              has_disposition(
                  *catalog_realized.snapshot,
                  ure::native_scene::kResourceCatalogFeature,
                  ure::product::ProductFeatureDisposition::Executed),
          "declared empty resource catalog was left without a disposition");

    auto solver_scene = *procedural.value;
    solver_scene.document.features.push_back(
        {ure::native_scene::kSolverContractFeature, {1, 0},
         ure::native_scene::RequirementLevel::Required, "ure", {}, "{}"});
    ure::native_scene::NativeSolverContract solver;
    solver.id = "solver/product-wavefront";
    solver.schema_version = {1, 0};
    solver.spectral_domain_bins = 32;
    solver.spectral_packet_lanes = 8;
    solver.spectral_resident_mb = 1;
    solver.max_trace_depth = 8;
    solver_scene.solver_contract =
        std::make_shared<const ure::native_scene::NativeSolverContract>(solver);
    const auto solver_realized =
        ure::product::realize_product_scene(solver_scene);
    check(solver_realized.ok() && solver_realized.snapshot &&
              solver_realized.snapshot->solver_config() &&
              has_disposition(
                  *solver_realized.snapshot,
                  ure::native_scene::kSolverContractFeature,
                  ure::product::ProductFeatureDisposition::Executed),
          "supported solver contract was not compiled into the snapshot");
    try {
        ure::product::ProductObjective solver_objective;
        solver_objective.requested_samples = 1;
        auto solver_job = ure::product::ProductJob::create(
            solver_scene, {}, solver_objective);
        solver_job->begin();
        solver_job->render_sample();
        const auto solver_frame = solver_job->publish_frame();
        check(solver_job->operation().state ==
                      ure::product::ProductOperationState::Succeeded &&
                  solver_frame.accepted_samples == 1 &&
                  !solver_frame.rgb.empty(),
              "supported solver constraints were dropped by ProductJob execution");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: solver ProductJob threw: %s\n", error.what());
        ++failures;
    }
    solver.spectral_packet_lanes = 4;
    solver_scene.solver_contract =
        std::make_shared<const ure::native_scene::NativeSolverContract>(solver);
    const auto lane_rejected =
        ure::product::realize_product_scene(solver_scene);
    check(!lane_rejected.ok() && has_detail(lane_rejected, 604),
          "renderer-inapplicable solver packet width was not rejected during realization");
    solver.spectral_packet_lanes = 8;
    solver.integrator = ure::native_scene::NativeIntegratorMode::BDPT;
    solver_scene.solver_contract =
        std::make_shared<const ure::native_scene::NativeSolverContract>(solver);
    const auto solver_rejected =
        ure::product::realize_product_scene(std::move(solver_scene));
    check(!solver_rejected.ok() && has_detail(solver_rejected, 604),
          "unsupported required solver did not reject before execution");

    auto simulation_scene = *procedural.value;
    simulation_scene.document.features.push_back(
        {ure::native_scene::kSimulationFeature, {1, 0},
         ure::native_scene::RequirementLevel::Required, "ure", {}, "{}"});
    ure::native_scene::NativeSimulationContract simulation;
    simulation.id = "simulation/bounded-time";
    simulation.schema_version = {1, 0};
    simulation.time = {0, 3, 1, 1, 0};
    simulation_scene.simulation_contract = std::make_shared<
        const ure::native_scene::NativeSimulationContract>(simulation);
    const auto simulation_realized =
        ure::product::realize_product_scene(simulation_scene);
    check(simulation_realized.ok() && simulation_realized.snapshot &&
              simulation_realized.snapshot->simulation_plan() &&
              has_disposition(
                  *simulation_realized.snapshot,
                  ure::native_scene::kSimulationFeature,
                  ure::product::ProductFeatureDisposition::Executed),
          "bounded simulation time plan was not compiled");
    simulation.domains.push_back(
        {"dynamic", ure::native_scene::SimulationDomain::RigidBody,
         ure::native_scene::RequirementLevel::Required,
         "ure.physics.current", {1, 0}, {}, {}});
    simulation_scene.simulation_contract = std::make_shared<
        const ure::native_scene::NativeSimulationContract>(simulation);
    const auto simulation_rejected =
        ure::product::realize_product_scene(std::move(simulation_scene));
    check(!simulation_rejected.ok() && has_detail(simulation_rejected, 605),
          "required dynamic simulation was accepted without execution");

    auto adapter_scene = *procedural.value;
    adapter_scene.document.features.push_back(
        {"ure.adapter.loss-report", {1, 0},
         ure::native_scene::RequirementLevel::Optional, "ure", {}, "{}"});
    const auto adapter_realized =
        ure::product::realize_product_scene(std::move(adapter_scene));
    check(adapter_realized.ok() && adapter_realized.snapshot &&
              has_disposition(
                  *adapter_realized.snapshot, "ure.adapter.loss-report",
                  ure::product::ProductFeatureDisposition::PreservedForTooling),
          "optional adapter metadata was not preserved for tooling");

    const auto procedural_package = root / "procedural.urepkg";
    ure::native_scene::pack_native_scenes(
        procedural_package,
        {std::filesystem::path(URE_PRV2_PROCEDURAL_SCENE)});
    const auto procedural_packaged =
        ure::native_scene::load_native_package_scene(
            procedural_package, "scene/q4");
    check(procedural_packaged.ok() && procedural_packaged.value,
          "procedural package was not available for cache disposition");
    if (procedural_packaged.ok() && procedural_packaged.value) {
        const auto cache_realized = ure::product::realize_product_scene(
            *procedural_packaged.value);
        check(cache_realized.ok() && cache_realized.snapshot &&
                  has_disposition(
                      *cache_realized.snapshot,
                      "ure.scene.compiled-cache",
                      ure::product::ProductFeatureDisposition::PreservedForTooling),
              "rebuildable package cache had no explicit disposition");
    }

    std::filesystem::remove_all(root);
    if (failures == 0)
        std::printf("product scene realizer tests passed\n");
    return failures == 0 ? 0 : 1;
}
