#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include <pxr/base/gf/vec3f.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/array.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/sphere.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdShade/shader.h>

#include <ure/native_adapter.hpp>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

ure::native_scene::NativeSceneArchive fixture() {
    ure::scene_ir::SceneIR scene;
    auto material =
        std::make_shared<ure::scene_ir::MaterialNode>();
    material->name = "usd_material";
    material->base_color = {0.25f, 0.5f, 0.75f};
    scene.materials.push_back(material);

    auto mesh =
        std::make_shared<ure::scene_ir::MeshResource>();
    mesh->name = "usd_mesh";
    mesh->mesh = std::make_shared<ure::Mesh>();
    mesh->mesh->vertices = {
        {{-1.0f, -1.0f, 0.0f},
         {0.0f, 0.0f, 1.0f},
         {0.0f, 0.0f}},
        {{1.0f, -1.0f, 0.0f},
         {0.0f, 0.0f, 1.0f},
         {1.0f, 0.0f}},
        {{0.0f, 1.0f, 0.0f},
         {0.0f, 0.0f, 1.0f},
         {0.5f, 1.0f}}};
    mesh->mesh->indices = {0, 1, 2};
    scene.meshes.push_back(mesh);

    ure::scene_ir::InstanceNode instance;
    instance.name = "usd_instance";
    instance.mesh = mesh;
    instance.material = material;
    instance.position = {1.0f, 2.0f, 3.0f};
    scene.instances.push_back(instance);
    instance.name = "usd_instance_2";
    instance.position = {-1.0f, 0.0f, -2.0f};
    scene.instances.push_back(instance);

    ure::scene_ir::SphereNode sphere;
    sphere.name = "usd_sphere";
    sphere.center = {0.0f, 1.0f, -4.0f};
    sphere.radius = 0.75f;
    sphere.material = material;
    scene.spheres.push_back(sphere);

    ure::native_scene::SceneDocument document;
    document.id = "u6_openusd_fixture";
    document.schema_version =
        ure::native_scene::kSceneSchemaVersion;
    return ure::native_scene::make_native_scene_archive(
        std::move(document),
        scene);
}

}

int main() {
    const auto result =
        ure::native_scene::export_usda_native(
            fixture(),
            ure::native_scene::UsdExportPolicy::Strict);
    check(result.ok(),
          "SDK-free USDA export failed");
    const auto layer = pxr::SdfLayer::CreateAnonymous(
        "ure_u6.usda");
    check(layer && layer->ImportFromString(result.usda),
          "OpenUSD rejected the generated USDA syntax");
    if (!layer) {
        return EXIT_FAILURE;
    }
    const auto stage = pxr::UsdStage::Open(layer);
    check(stage &&
              stage->GetDefaultPrim().GetPath() ==
                  pxr::SdfPath("/URE"),
          "OpenUSD stage/default prim is invalid");
    if (stage) {
        const pxr::UsdGeomMesh mesh(
            stage->GetPrimAtPath(
                pxr::SdfPath(
                    "/URE/World/i_000000/Geometry")));
        pxr::VtVec3fArray points;
        check(mesh &&
                  mesh.GetPointsAttr().Get(&points) &&
                  points.size() == 3 &&
                  mesh.ComputeVisibility() ==
                      pxr::TfToken("inherited"),
              "OpenUSD did not recover exported mesh geometry");
        const auto first_instance =
            stage->GetPrimAtPath(
                pxr::SdfPath(
                    "/URE/World/i_000000"));
        const auto second_instance =
            stage->GetPrimAtPath(
                pxr::SdfPath(
                    "/URE/World/i_000001"));
        check(first_instance.IsInstance() &&
                  second_instance.IsInstance() &&
                  first_instance.GetPrototype() ==
                      second_instance.GetPrototype(),
              "OpenUSD did not preserve shared mesh instancing");
        pxr::GfVec3f translation;
        check(stage->GetPrimAtPath(
                  pxr::SdfPath(
                      "/URE/World/i_000000"))
                      .GetAttribute(
                          pxr::TfToken(
                              "xformOp:translate"))
                      .Get(&translation) &&
                  translation ==
                      pxr::GfVec3f(
                          1.0f,
                          2.0f,
                          3.0f),
              "OpenUSD did not recover the native instance transform");
        const pxr::UsdShadeMaterial material(
            stage->GetPrimAtPath(
                pxr::SdfPath(
                    "/URE/Materials/m_000000")));
        check(material && material.GetSurfaceOutput(),
              "OpenUSD did not recover the material surface output");
        check(pxr::UsdShadeMaterialBindingAPI(mesh)
                      .ComputeBoundMaterial()
                      .GetPath() == material.GetPath(),
              "OpenUSD did not resolve the instance material binding");
        const pxr::UsdShadeShader shader(
            stage->GetPrimAtPath(
                pxr::SdfPath(
                    "/URE/Materials/m_000000/PreviewSurface")));
        pxr::GfVec3f color;
        check(shader &&
                  shader.GetInput(
                      pxr::TfToken("diffuseColor"))
                      .Get(&color) &&
                  color ==
                      pxr::GfVec3f(
                          0.25f,
                          0.5f,
                          0.75f),
              "OpenUSD did not recover Preview Surface parameters");
        const pxr::UsdGeomCamera camera(
            stage->GetPrimAtPath(
                pxr::SdfPath("/URE/Camera")));
        check(static_cast<bool>(camera),
              "OpenUSD did not recover the exported camera");
        const auto sphere = stage->GetPrimAtPath(
            pxr::SdfPath(
                "/URE/World/s_000000"));
        double radius = 0.0;
        check(sphere.IsA<pxr::UsdGeomSphere>() &&
                  sphere.GetAttribute(
                      pxr::TfToken("radius"))
                      .Get(&radius) &&
                  radius == 0.75,
              "OpenUSD did not recover the analytic sphere");
    }

    std::cout << "Phase U.6 USDA/OpenUSD checks: "
              << (failures ? "FAILED" : "PASSED")
              << '\n';
    return failures == 0
        ? EXIT_SUCCESS
        : EXIT_FAILURE;
}
