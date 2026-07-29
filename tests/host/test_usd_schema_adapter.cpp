#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string_view>

#include <ure/native_scene_hash.hpp>
#include <ure/usd_schema_adapter.hpp>

namespace {

int failures = 0;

void check(
    bool condition,
    const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool near(
    float first,
    float second,
    float tolerance = 1.0e-5f) {
    return std::abs(first - second) <= tolerance;
}

std::string digest(std::string_view value) {
    return ure::native_scene::sha256_hex(
        std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(
                value.data()),
            value.size()));
}

ure::usd::UsdStageSnapshot fixture() {
    using namespace ure;
    using namespace ure::usd;
    UsdStageSnapshot stage;
    stage.source_identifier = "studio/shot.usdc";
    stage.metres_per_unit = 0.01;
    stage.up_axis = UsdUpAxis::Z;
    stage.camera_path = "/World/Camera";
    stage.required_schemas = {
        std::string(kUsdPhysicsApiSchema),
        std::string(kUsdSpectralMaterialApiSchema)};
    stage.optional_schemas = {
        "vendor.optional/1.0"};

    UsdMaterialPrim surface;
    surface.path = "/World/Looks/Surface";
    surface.display_name = "surface";
    surface.base_color = {0.2f, 0.4f, 0.6f};
    surface.roughness = 0.3f;
    UsdSpectralResourceBinding spectrum;
    spectrum.id = "spectrum/surface";
    spectrum.content_hash =
        digest("surface-spectrum");
    spectrum.uri =
        "content://sha256/" +
        spectrum.content_hash;
    spectrum.semantic =
        native_scene::SpectralSemantic::Reflectance;
    spectrum.representation =
        native_scene::SpectralRepresentation::Tiled;
    spectrum.domain_bins = 1'000'000;
    spectrum.packet_lanes = 16;
    spectrum.tile_bins = 4096;
    spectrum.value_min = 0.0;
    spectrum.value_max = 1.0;
    spectrum.payload_bytes = 8192;
    spectrum.resident_bytes = 4096;
    surface.spectral_resources.push_back(spectrum);
    UsdSpectralResourceBinding basis;
    basis.id = "spectrum/surface-basis";
    basis.content_hash =
        digest("surface-basis");
    basis.uri =
        "content://sha256/" +
        basis.content_hash;
    basis.semantic =
        native_scene::SpectralSemantic::Ior;
    basis.representation =
        native_scene::SpectralRepresentation::Basis;
    basis.domain_bins = 4096;
    basis.packet_lanes = 8;
    basis.basis_count = 6;
    basis.value_min = 1.0;
    basis.value_max = 2.0;
    basis.payload_bytes = 2048;
    basis.resident_bytes = 2048;
    surface.spectral_resources.push_back(basis);

    UsdMaterialPrim glass;
    glass.path = "/World/Looks/Glass";
    glass.model =
        scene_ir::MaterialModel::Dielectric;
    glass.roughness = 0.05f;
    glass.ior = 1.52f;
    stage.materials = {surface, glass};

    UsdMeshPrim mesh;
    mesh.path = "/World/Geometry/Triangle";
    mesh.points = {
        {0.0f, 0.0f, 0.0f},
        {100.0f, 0.0f, 0.0f},
        {0.0f, 100.0f, 0.0f}};
    mesh.texcoords = {
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {0.0f, 1.0f}};
    mesh.face_vertex_counts = {3};
    mesh.face_vertex_indices = {0, 1, 2};
    mesh.transform.translation =
        {100.0f, 200.0f, 300.0f};
    mesh.transform.scale = {1.0f, 2.0f, 3.0f};
    mesh.material_path = surface.path;
    mesh.rigid_body.enabled = true;
    mesh.rigid_body.mass = 2.0f;
    stage.meshes.push_back(mesh);

    UsdSpherePrim sphere;
    sphere.path = "/World/Geometry/Sphere";
    sphere.center = {0.0f, 0.0f, 200.0f};
    sphere.radius = 50.0f;
    sphere.material_path = glass.path;
    stage.spheres.push_back(sphere);

    UsdCameraPrim camera;
    camera.path = stage.camera_path;
    camera.position = {0.0f, -500.0f, 200.0f};
    camera.look_at = {0.0f, 0.0f, 100.0f};
    camera.up = {0.0f, 0.0f, 1.0f};
    camera.vertical_fov_degrees = 50.0f;
    camera.aspect_ratio = 1.5f;
    camera.aperture = 2.0f;
    camera.focus_distance = 500.0f;
    stage.cameras.push_back(camera);
    return stage;
}

void test_mapping_and_native_roundtrip() {
    using namespace ure;
    const auto imported =
        usd::import_usd_schema_stage(fixture());
    check(imported.ok(),
          "valid USD schema snapshot was rejected");
    check(imported.native.loss_report.exportable() &&
              !imported.native.loss_report.lossless(),
          "optional USD schema loss was not reported");
    const auto& archive = imported.native.archive;
    check(archive.scene.materials.size() == 2 &&
              archive.scene.meshes.size() == 1 &&
              archive.scene.instances.size() == 1 &&
              archive.scene.spheres.size() == 1,
          "USD prim registry mapping is incomplete");
    check(imported.mappings.size() == 5 &&
              std::ranges::is_sorted(
                  imported.mappings,
                  {},
                  &usd::UsdPrimMapping::usd_path),
          "USD prim mapping is incomplete or unstable");

    const auto& vertex =
        archive.scene.meshes[0]->mesh->vertices[2];
    check(near(vertex.position.x, 0.0f) &&
              near(vertex.position.y, 0.0f) &&
              near(vertex.position.z, -1.0f),
          "Z-up centimetre mesh conversion is wrong");
    const auto& instance = archive.scene.instances[0];
    check(near(instance.position.x, 1.0f) &&
              near(instance.position.y, 3.0f) &&
              near(instance.position.z, -2.0f) &&
              near(instance.scale.x, 1.0f) &&
              near(instance.scale.y, 3.0f) &&
              near(instance.scale.z, 2.0f),
          "USD transform conversion is wrong");
    check(instance.rigid_body.enabled &&
              near(instance.rigid_body.mass, 2.0f),
          "USD physics API binding was lost");
    check(near(archive.scene.spheres[0].center.y, 2.0f) &&
              near(archive.scene.spheres[0].radius, 0.5f),
          "USD analytic sphere units are wrong");
    check(near(archive.scene.camera.position.y, 2.0f) &&
              near(archive.scene.camera.position.z, 5.0f) &&
              near(archive.scene.camera.aperture, 0.02f) &&
              near(archive.scene.camera.focus_dist, 5.0f),
          "USD camera units or axis conversion is wrong");

    const auto surface =
        archive.scene.find_material("surface");
    check(surface && surface->graph &&
              !surface->graph->empty(),
          "USD material did not map to MaterialGraph");
    check(!surface->spectral_extension,
          "USD spectral domain collapsed to legacy bands");
    check(archive.resource_catalog &&
              archive.resource_catalog->resources.size() == 3,
          "USD spectral resource catalog is incomplete");
    if (archive.resource_catalog) {
        const auto found = std::ranges::find(
            archive.resource_catalog->resources,
            std::string("spectrum/surface"),
            &native_scene::NativeResourceEntry::id);
        check(found !=
                  archive.resource_catalog->resources.end() &&
                  found->spectral &&
                  found->spectral->domain.domain_bins ==
                      1'000'000 &&
                  found->spectral->domain.
                          packet_lanes_hint ==
                      16 &&
                  found->spectral->tile_bins == 4096,
              "USD spectral domain/resource mapping is weak");
    }

    native_scene::CapabilityRegistry registry;
    registry.features.emplace(
        std::string(
            usd::kUsdSchemaAdapterIdentity),
        usd::kUsdSchemaAdapterVersion);
    registry.features.emplace(
        std::string(
            native_scene::kResourceCatalogFeature),
        native_scene::Version{1, 0});
    const auto binary =
        native_scene::write_scene_ir_binary(archive);
    const auto loaded =
        native_scene::read_scene_ir_binary(
            binary,
            registry);
    check(loaded.ok() &&
              native_scene::scene_ir_semantic_hash(
                  *loaded.value) ==
                  native_scene::
                      scene_ir_semantic_hash(archive),
          "USD mapped native archive did not roundtrip");
}

void test_deterministic_order() {
    auto first = fixture();
    auto second = first;
    std::ranges::reverse(second.materials);
    std::ranges::reverse(
        second.materials[1].spectral_resources);
    const auto a =
        ure::usd::import_usd_schema_stage(first);
    const auto b =
        ure::usd::import_usd_schema_stage(second);
    check(a.ok() && b.ok() &&
              ure::native_scene::scene_ir_semantic_hash(
                  a.native.archive) ==
                  ure::native_scene::
                      scene_ir_semantic_hash(
                          b.native.archive),
          "USD authored order changed native semantics");
}

void test_fail_loud_boundaries() {
    auto value = fixture();
    value.meshes[0].face_vertex_counts = {4};
    value.meshes[0].face_vertex_indices =
        {0, 1, 2, 0};
    check(!ure::usd::import_usd_schema_stage(value).ok(),
          "non-triangulated USD mesh was accepted");

    value = fixture();
    value.meshes[0].material_path =
        "/World/Looks/Missing";
    check(!ure::usd::import_usd_schema_stage(value).ok(),
          "missing USD material binding was accepted");

    value = fixture();
    value.meshes[0].transform.
        affine_trs_compatible = false;
    check(!ure::usd::import_usd_schema_stage(value).ok(),
          "unsupported USD xform stack was accepted");

    value = fixture();
    value.required_schemas.push_back(
        "vendor.required/1.0");
    check(!ure::usd::import_usd_schema_stage(value).ok(),
          "unsupported required USD schema was accepted");

    value = fixture();
    value.authored_time_sample_count = 2;
    check(!ure::usd::import_usd_schema_stage(value).ok(),
          "animated USD stage silently entered static SceneIR");

    value = fixture();
    value.materials[0].spectral_resources[0].
        domain_bins = 0;
    check(!ure::usd::import_usd_schema_stage(value).ok(),
          "weak USD spectral bands mapping was accepted");

    value = fixture();
    value.spheres[0].rigid_body.enabled = true;
    check(!ure::usd::import_usd_schema_stage(value).ok(),
          "unsupported analytic-sphere physics was dropped");

    value = fixture();
    value.meshes[0].path =
        value.materials[0].path;
    check(!ure::usd::import_usd_schema_stage(value).ok(),
          "duplicate USD prim path was accepted");
}

}

int main() {
    test_mapping_and_native_roundtrip();
    test_deterministic_order();
    test_fail_loud_boundaries();
    std::cout << "Phase U.1 USD schema adapter checks: "
              << (failures ? "FAILED" : "PASSED")
              << '\n';
    return failures == 0
        ? EXIT_SUCCESS
        : EXIT_FAILURE;
}
