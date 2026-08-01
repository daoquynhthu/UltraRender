#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/camera.h>
#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/renderBuffer.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/renderPass.h>
#include <pxr/imaging/hd/renderPassState.h>
#include <pxr/imaging/hd/repr.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/unitTestDelegate.h>
#include <pxr/imaging/pxOsd/tokens.h>

#include "mesh_rprim.hpp"
#include "render_buffer.hpp"
#include "render_delegate.hpp"
#include "render_param.hpp"
#include "scene_snapshot.hpp"

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

const pxr::VtValue* find_value(
    const pxr::VtDictionary& values,
    const char* key) {
    const auto found = values.find(key);
    return found == values.end()
        ? nullptr
        : &found->second;
}

}

int main() {
    pxr::HdRenderSettingsMap settings;
    settings[pxr::TfToken("ure:backend")] =
        pxr::VtValue(pxr::TfToken("cuda"));
    settings[pxr::TfToken("ure:samplesPerPass")] =
        pxr::VtValue(1);
    settings[pxr::TfToken("ure:maxSpp")] =
        pxr::VtValue(2);
    pxr::HdURE renderer(settings);
    std::unique_ptr<pxr::HdRenderIndex> index(
        pxr::HdRenderIndex::New(&renderer, {}));
    check(index != nullptr,
          "Hydra render index creation failed");
    if (!index) {
        return EXIT_FAILURE;
    }

    pxr::HdUnitTestDelegate scene(
        index.get(),
        pxr::SdfPath("/unit"));
    const pxr::SdfPath mesh_id("/unit/quad");
    pxr::GfMatrix4f mesh_transform(1.0f);
    mesh_transform.SetScale(
        pxr::GfVec3f(-1.0f, 1.0f, 1.0f));
    scene.AddMesh(
        mesh_id,
        mesh_transform,
        pxr::VtVec3fArray{
            {-2.0f, -2.0f, -3.0f},
            {2.0f, -2.0f, -3.0f},
            {2.0f, 2.0f, -3.0f},
            {-2.0f, 2.0f, -3.0f}},
        pxr::VtIntArray{4},
        pxr::VtIntArray{0, 1, 2, 3},
        false,
        pxr::SdfPath(),
        pxr::PxOsdOpenSubdivTokens->none,
        pxr::HdTokens->rightHanded,
        true);
    scene.AddPrimvar(
        mesh_id,
        pxr::HdTokens->normals,
        pxr::VtValue(pxr::VtVec3fArray{
            {0.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, 1.0f}}),
        pxr::HdInterpolationVertex,
        pxr::HdPrimvarRoleTokens->normal);
    scene.AddPrimvar(
        mesh_id,
        pxr::TfToken("st"),
        pxr::VtValue(pxr::VtVec2fArray{
            {0.0f, 0.0f},
            {1.0f, 0.0f},
            {1.0f, 1.0f},
            {0.0f, 1.0f}}),
        pxr::HdInterpolationVertex,
        pxr::HdPrimvarRoleTokens->textureCoordinate);
    auto* mesh = dynamic_cast<pxr::HdUREMesh*>(
        const_cast<pxr::HdRprim*>(
            index->GetRprim(mesh_id)));
    check(mesh != nullptr,
          "Hydra mesh RPrim was not created");
    if (!mesh) {
        return EXIT_FAILURE;
    }
    pxr::HdDirtyBits mesh_bits =
        mesh->GetInitialDirtyBitsMask();
    mesh->Sync(
        &scene,
        renderer.GetRenderParam(),
        &mesh_bits,
        pxr::TfToken());
    pxr::HdRprimCollection collection(
        pxr::TfToken("geometry"),
        pxr::HdReprSelector(
            pxr::HdReprTokens->hull),
        pxr::SdfPath("/unit"));
    auto* retained =
        dynamic_cast<pxr::HdURERenderParam*>(
            renderer.GetRenderParam());
    check(retained != nullptr,
          "Hydra retained render state has the wrong type");
    if (!retained) {
        return EXIT_FAILURE;
    }
    const auto source_record =
        retained->FindMesh(mesh_id.GetString());
    const auto native_snapshot =
        pxr::BuildSceneSnapshot(
            retained->SnapshotScene(),
            collection,
            ure::Camera{},
            4,
            4);
    check(native_snapshot.scene.meshes.size() == 1 &&
              native_snapshot.scene.meshes.front() &&
              native_snapshot.scene.meshes.front()->mesh,
          "Hydra retained scene did not produce native geometry");
    if (native_snapshot.scene.meshes.empty() ||
        !native_snapshot.scene.meshes.front() ||
        !native_snapshot.scene.meshes.front()->mesh) {
        return EXIT_FAILURE;
    }
    const auto& baked =
        native_snapshot.scene.meshes.front()->mesh;
    check(source_record && source_record->geometry &&
              source_record->geometry->mesh && baked &&
              !baked->vertices.empty() &&
              !source_record->geometry->mesh->vertices.empty() &&
              baked->vertices.size() ==
                  source_record->geometry->mesh->vertices.size() &&
              baked->indices.size() >= 3 &&
              source_record->geometry->mesh->indices.size() >= 3 &&
              std::abs(
                  baked->vertices.front().position.x +
                  source_record->geometry->mesh->vertices
                      .front().position.x) < 1.0e-6f &&
              baked->indices[1] ==
                  source_record->geometry->mesh->indices[2] &&
              baked->indices[2] ==
                  source_record->geometry->mesh->indices[1],
          "Hydra affine bake did not preserve mirrored geometry orientation");

    const pxr::SdfPath camera_id("/unit/camera");
    scene.AddCamera(camera_id);
    scene.UpdateTransform(
        camera_id,
        pxr::GfMatrix4f(1.0f));
    scene.UpdateCamera(
        camera_id,
        pxr::HdCameraTokens->horizontalAperture,
        pxr::VtValue(20.0f));
    scene.UpdateCamera(
        camera_id,
        pxr::HdCameraTokens->verticalAperture,
        pxr::VtValue(20.0f));
    scene.UpdateCamera(
        camera_id,
        pxr::HdCameraTokens->focalLength,
        pxr::VtValue(50.0f));
    auto* camera = dynamic_cast<pxr::HdCamera*>(
        index->GetSprim(
            pxr::HdPrimTypeTokens->camera,
            camera_id));
    check(camera != nullptr,
          "Hydra camera SPrim was not created");
    if (!camera) {
        return EXIT_FAILURE;
    }
    pxr::HdDirtyBits camera_bits =
        camera->GetInitialDirtyBitsMask();
    camera->Sync(
        &scene,
        renderer.GetRenderParam(),
        &camera_bits);

    const pxr::SdfPath buffer_id("/unit/color");
    scene.AddRenderBuffer(
        buffer_id,
        pxr::HdRenderBufferDescriptor(
            pxr::GfVec3i(4, 4, 1),
            pxr::HdFormatFloat32Vec4,
            false));
    auto* buffer = dynamic_cast<pxr::HdURERenderBuffer*>(
        index->GetBprim(
            pxr::HdPrimTypeTokens->renderBuffer,
            buffer_id));
    check(buffer != nullptr,
          "Hydra render-buffer BPrim was not created");
    if (!buffer) {
        return EXIT_FAILURE;
    }
    pxr::HdDirtyBits buffer_bits =
        buffer->GetInitialDirtyBitsMask();
    buffer->Sync(
        &scene,
        renderer.GetRenderParam(),
        &buffer_bits);

    auto pass = renderer.CreateRenderPass(
        index.get(),
        collection);
    check(pass != nullptr,
          "Hydra progressive render pass was not created");
    if (!pass) {
        return EXIT_FAILURE;
    }
    auto state = renderer.CreateRenderPassState();
    state->SetCamera(camera);
    pxr::HdRenderPassAovBinding binding;
    binding.aovName = pxr::HdAovTokens->color;
    binding.renderBufferId = buffer_id;
    binding.renderBuffer = buffer;
    state->SetAovBindings({binding});

    pass->Execute(
        state,
        {pxr::HdRenderTagTokens->geometry});
    const auto first_stats = renderer.GetRenderStats();
    const auto* first_spp =
        find_value(first_stats, "renderSpp");
    check(first_spp && first_spp->Get<int>() == 1 &&
              !pass->IsConverged() &&
              !buffer->IsConverged(),
          "first Hydra execute did not publish progressive state");
    pass->Execute(
        state,
        {pxr::HdRenderTagTokens->geometry});
    check(pass->IsConverged() &&
              buffer->IsConverged(),
          "Hydra render did not converge at the configured SPP");
    auto* pixels = static_cast<float*>(buffer->Map());
    bool finite = pixels != nullptr;
    float energy = 0.0f;
    if (pixels) {
        for (std::size_t index_value = 0;
             index_value < 64;
             ++index_value) {
            finite = finite &&
                std::isfinite(pixels[index_value]);
            energy += std::abs(pixels[index_value]);
        }
    }
    buffer->Unmap();
    check(finite && energy > 0.0f,
          "Hydra session bridge produced an invalid framebuffer");

    pxr::GfMatrix4f moved(1.0f);
    moved.SetTranslate(
        pxr::GfVec3f(0.25f, 0.0f, 0.0f));
    scene.UpdateTransform(camera_id, moved);
    camera_bits = pxr::HdCamera::DirtyTransform;
    camera->Sync(
        &scene,
        renderer.GetRenderParam(),
        &camera_bits);
    pass->Execute(
        state,
        {pxr::HdRenderTagTokens->geometry});
    const auto moved_stats = renderer.GetRenderStats();
    const auto* moved_spp =
        find_value(moved_stats, "renderSpp");
    const auto* render_error =
        find_value(moved_stats, "renderError");
    check(moved_spp && moved_spp->Get<int>() == 1 &&
              render_error &&
              render_error->Get<std::string>().empty() &&
              !pass->IsConverged(),
          "Hydra camera mutation did not reset progressive accumulation");

    binding.aovName = pxr::HdAovTokens->depth;
    state->SetAovBindings({binding});
    pass->Execute(
        state,
        {pxr::HdRenderTagTokens->geometry});
    const auto invalid_stats = renderer.GetRenderStats();
    const auto* invalid_spp =
        find_value(invalid_stats, "renderSpp");
    const auto* invalid_error =
        find_value(invalid_stats, "renderError");
    check(invalid_spp && invalid_spp->Get<int>() == 0 &&
              invalid_error &&
              invalid_error->Get<std::string>().find(
                  "channel count") != std::string::npos,
          "incompatible Hydra AOV format did not fail loud");

    index->Clear();
    std::cout << "Phase U.5 Hydra progressive render checks: "
              << (failures ? "FAILED" : "PASSED")
              << '\n';
    return failures == 0
        ? EXIT_SUCCESS
        : EXIT_FAILURE;
}
