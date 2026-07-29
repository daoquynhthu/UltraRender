#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/unitTestDelegate.h>
#include <pxr/imaging/pxOsd/tokens.h>

#include "mesh_rprim.hpp"
#include "render_delegate.hpp"
#include "render_param.hpp"

namespace {

int failures = 0;

class TestSceneDelegate final
    : public pxr::HdUnitTestDelegate {
public:
    TestSceneDelegate(
        pxr::HdRenderIndex* index,
        const pxr::SdfPath& delegate_id)
        : HdUnitTestDelegate(
              index,
              delegate_id) {
    }

    pxr::VtValue Get(
        const pxr::SdfPath& id,
        const pxr::TfToken& key) override {
        if (id == overridden_id_ &&
            key == pxr::HdTokens->points &&
            !overridden_points_.empty()) {
            return pxr::VtValue(
                overridden_points_);
        }
        return HdUnitTestDelegate::Get(id, key);
    }

    void OverridePoints(
        const pxr::SdfPath& id,
        pxr::VtVec3fArray points) {
        overridden_id_ = id;
        overridden_points_ = std::move(points);
    }

private:
    pxr::SdfPath overridden_id_;
    pxr::VtVec3fArray overridden_points_;
};

void check(
    bool condition,
    const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool almost_equal(float first, float second) {
    return std::abs(first - second) <= 1.0e-5f;
}

void sync_mesh(
    pxr::HdUREMesh& mesh,
    TestSceneDelegate& scene,
    pxr::HdURE& renderer,
    pxr::HdDirtyBits bits) {
    mesh.Sync(
        &scene,
        renderer.GetRenderParam(),
        &bits,
        pxr::TfToken());
    check(bits == pxr::HdChangeTracker::Clean,
          "Hydra mesh dirty bits were not consumed");
}

}

int main() {
    pxr::HdURE renderer;
    std::unique_ptr<pxr::HdRenderIndex> index(
        pxr::HdRenderIndex::New(
            &renderer,
            {}));
    check(index != nullptr,
          "Hydra render index creation failed");
    if (!index) {
        return EXIT_FAILURE;
    }

    TestSceneDelegate scene(
        index.get(),
        pxr::SdfPath("/unit"));
    const pxr::SdfPath id("/unit/quad");
    pxr::GfMatrix4f transform(1.0f);
    transform.SetTranslate(
        pxr::GfVec3f(2.0f, 3.0f, 4.0f));
    const pxr::VtVec3fArray points{
        {-1.0f, -1.0f, 0.0f},
        {1.0f, -1.0f, 0.0f},
        {1.0f, 1.0f, 0.0f},
        {-1.0f, 1.0f, 0.0f}};
    const pxr::VtIntArray counts{4};
    const pxr::VtIntArray vertices{0, 1, 2, 3};
    scene.AddMesh(
        id,
        transform,
        points,
        counts,
        vertices,
        false,
        pxr::SdfPath(),
        pxr::PxOsdOpenSubdivTokens->none,
        pxr::HdTokens->rightHanded,
        true);
    scene.AddPrimvar(
        id,
        pxr::HdTokens->normals,
        pxr::VtValue(pxr::VtVec3fArray{
            {0.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, 1.0f}}),
        pxr::HdInterpolationVertex,
        pxr::HdPrimvarRoleTokens->normal);
    scene.AddPrimvar(
        id,
        pxr::TfToken("st"),
        pxr::VtValue(pxr::VtVec2fArray{
            {0.0f, 0.0f},
            {1.0f, 1.0f}}),
        pxr::HdInterpolationFaceVarying,
        pxr::HdPrimvarRoleTokens->textureCoordinate,
        pxr::VtIntArray{0, 1, 0, 1});
    scene.BindMaterial(
        id,
        pxr::SdfPath("/unit/material"));

    auto* mesh = dynamic_cast<pxr::HdUREMesh*>(
        const_cast<pxr::HdRprim*>(
            index->GetRprim(id)));
    check(mesh != nullptr,
          "HdURE did not create a mesh RPrim");
    if (!mesh) {
        return EXIT_FAILURE;
    }
    sync_mesh(
        *mesh,
        scene,
        renderer,
        mesh->GetInitialDirtyBitsMask());

    auto* state = dynamic_cast<pxr::HdURERenderParam*>(
        renderer.GetRenderParam());
    check(state != nullptr,
          "HdURE render state has the wrong type");
    if (!state) {
        return EXIT_FAILURE;
    }
    const auto record =
        state->FindMesh(id.GetString());
    check(record && record->geometry &&
              record->geometry->mesh &&
              record->geometry->mesh->vertices.size() == 6 &&
              record->geometry->mesh->indices.size() == 6,
          "Hydra quad was not triangulated into SceneIR geometry");
    if (record && record->geometry &&
        record->geometry->mesh) {
        const auto& first =
            record->geometry->mesh->vertices.front();
        check(almost_equal(first.normal.z, 1.0f) &&
                  almost_equal(first.uv.x, 0.0f) &&
                  almost_equal(first.uv.y, 0.0f),
              "Hydra normal or face-varying UV was lost");
        check(almost_equal(
                  static_cast<float>(
                      record->transform[12]),
                  2.0f) &&
                  record->material_path ==
                      "/unit/material" &&
                  record->visible &&
                  record->double_sided,
              "Hydra instance metadata was lost");
    }

    pxr::GfMatrix4f moved(1.0f);
    moved.SetTranslate(
        pxr::GfVec3f(5.0f, 6.0f, 7.0f));
    scene.UpdateTransform(id, moved);
    sync_mesh(
        *mesh,
        scene,
        renderer,
        pxr::HdChangeTracker::DirtyTransform);
    const auto transformed =
        state->FindMesh(id.GetString());
    check(transformed && record &&
              transformed->revision >
                  record->revision &&
              transformed->geometry ==
                  record->geometry &&
              almost_equal(
                  static_cast<float>(
                      transformed->transform[12]),
                  5.0f),
          "Hydra transform update rebuilt or lost geometry");

    auto updated = points;
    updated[0][2] = 0.5f;
    scene.OverridePoints(id, updated);
    sync_mesh(
        *mesh,
        scene,
        renderer,
        pxr::HdChangeTracker::DirtyPoints |
            pxr::HdChangeTracker::DirtyPrimvar);
    const auto changed =
        state->FindMesh(id.GetString());
    check(changed && transformed &&
              changed->geometry &&
              changed->geometry->mesh &&
              changed->revision >
                  transformed->revision &&
              changed->geometry !=
                  transformed->geometry &&
              almost_equal(
                  changed->geometry->mesh->
                      vertices.front().position.z,
                  0.5f),
          "Hydra point update did not refresh native geometry");

    const pxr::SdfPath rejected_id(
        "/unit/subdivision");
    scene.AddMesh(rejected_id);
    auto* rejected = dynamic_cast<pxr::HdUREMesh*>(
        const_cast<pxr::HdRprim*>(
            index->GetRprim(rejected_id)));
    check(rejected != nullptr,
          "subdivision rejection fixture is missing");
    if (rejected) {
        sync_mesh(
            *rejected,
            scene,
            renderer,
            rejected->GetInitialDirtyBitsMask());
    }
    check(
        !state->FindMesh(
             rejected_id.GetString()) &&
            state->RejectedMeshCount() == 1 &&
            state->LastError().find(
                "subdivision") !=
                std::string::npos,
        "unsupported subdivision did not fail loud");

    scene.UpdateTransform(id, transform);
    sync_mesh(
        *mesh,
        scene,
        renderer,
        pxr::HdChangeTracker::DirtyTransform);
    check(
        state->RejectedMeshCount() == 1 &&
            state->LastError().find(
                "subdivision") !=
                std::string::npos,
        "a valid mesh update erased another mesh rejection");

    mesh->Finalize(renderer.GetRenderParam());
    check(!state->FindMesh(id.GetString()),
          "Hydra mesh finalization retained native geometry");

    index->Clear();
    std::cout << "Phase U.3 Hydra mesh checks: "
              << (failures ? "FAILED" : "PASSED")
              << '\n';
    return failures == 0
        ? EXIT_SUCCESS
        : EXIT_FAILURE;
}
