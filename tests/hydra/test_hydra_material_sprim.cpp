#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <ranges>
#include <string>
#include <utility>

#include <pxr/base/gf/vec3f.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/material.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/unitTestDelegate.h>
#include <pxr/usd/sdf/assetPath.h>

#include "material_sprim.hpp"
#include "render_delegate.hpp"
#include "render_param.hpp"

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

pxr::HdMaterialConnection2 connection(
    const pxr::SdfPath& path,
    const char* output = "out") {
    return {
        path,
        pxr::TfToken(output)};
}

pxr::HdMaterialNetwork2 native_network(
    const pxr::GfVec3f& color) {
    pxr::HdMaterialNetwork2 network;
    const pxr::SdfPath color_path(
        "/unit/material/color");
    const pxr::SdfPath roughness_path(
        "/unit/material/roughness");
    const pxr::SdfPath bsdf_path(
        "/unit/material/bsdf");
    const pxr::SdfPath output_path(
        "/unit/material/output");

    pxr::HdMaterialNode2 color_node;
    color_node.nodeTypeId =
        pxr::TfToken("URE_constant_color");
    color_node.parameters[
        pxr::TfToken("value")] =
        pxr::VtValue(color);
    network.nodes.emplace(
        color_path,
        std::move(color_node));

    pxr::HdMaterialNode2 roughness_node;
    roughness_node.nodeTypeId =
        pxr::TfToken("ure:constant_float");
    roughness_node.parameters[
        pxr::TfToken("value")] =
        pxr::VtValue(0.25f);
    network.nodes.emplace(
        roughness_path,
        std::move(roughness_node));

    pxr::HdMaterialNode2 bsdf_node;
    bsdf_node.nodeTypeId =
        pxr::TfToken("URE_bsdf_lambert");
    bsdf_node.inputConnections[
        pxr::TfToken("base_color")] = {
            connection(color_path)};
    bsdf_node.inputConnections[
        pxr::TfToken("roughness")] = {
            connection(roughness_path)};
    network.nodes.emplace(
        bsdf_path,
        std::move(bsdf_node));

    pxr::HdMaterialNode2 output_node;
    output_node.nodeTypeId =
        pxr::TfToken("ure:output_surface");
    output_node.inputConnections[
        pxr::TfToken("surface")] = {
            connection(bsdf_path)};
    network.nodes.emplace(
        output_path,
        std::move(output_node));
    network.terminals[
        pxr::HdMaterialTerminalTokens->surface] =
        connection(output_path);
    return network;
}

pxr::HdMaterialNetwork2 preview_network() {
    pxr::HdMaterialNetwork2 network;
    const pxr::SdfPath path(
        "/unit/preview/surface");
    const pxr::SdfPath texture_path(
        "/unit/preview/texture");
    const pxr::SdfPath reader_path(
        "/unit/preview/st_reader");

    pxr::HdMaterialNode2 reader;
    reader.nodeTypeId =
        pxr::TfToken("UsdPrimvarReader_float2");
    reader.parameters[
        pxr::TfToken("varname")] =
        pxr::VtValue(pxr::TfToken("st1"));
    reader.parameters[
        pxr::TfToken("fallback")] =
        pxr::VtValue(
            pxr::GfVec3f(
                0.0f,
                0.0f,
                0.0f));
    network.nodes.emplace(
        reader_path,
        std::move(reader));

    pxr::HdMaterialNode2 texture;
    texture.nodeTypeId =
        pxr::TfToken("UsdUVTexture");
    texture.parameters[
        pxr::TfToken("file")] =
        pxr::VtValue(
            pxr::SdfAssetPath(
                "textures/albedo.exr"));
    texture.parameters[
        pxr::TfToken("sourceColorSpace")] =
        pxr::VtValue(pxr::TfToken("raw"));
    texture.inputConnections[
        pxr::TfToken("st")] = {
            connection(reader_path,
                       "result")};
    network.nodes.emplace(
        texture_path,
        std::move(texture));

    pxr::HdMaterialNode2 preview;
    preview.nodeTypeId =
        pxr::TfToken("UsdPreviewSurface");
    preview.parameters[
        pxr::TfToken("diffuseColor")] =
        pxr::VtValue(
            pxr::GfVec3f(
                0.2f,
                0.4f,
                0.6f));
    preview.parameters[
        pxr::TfToken("roughness")] =
        pxr::VtValue(0.35f);
    preview.parameters[
        pxr::TfToken("vendor:coat")] =
        pxr::VtValue(1.0f);
    preview.inputConnections[
        pxr::TfToken("diffuseColor")] = {
            connection(texture_path, "rgb")};
    network.nodes.emplace(
        path,
        std::move(preview));
    network.terminals[
        pxr::HdMaterialTerminalTokens->surface] =
        connection(path, "surface");
    return network;
}

pxr::HdMaterialNetwork2 invalid_network() {
    pxr::HdMaterialNetwork2 network;
    const pxr::SdfPath path(
        "/unit/invalid/node");
    pxr::HdMaterialNode2 node;
    node.nodeTypeId =
        pxr::TfToken("VendorUnknownSurface");
    network.nodes.emplace(path, std::move(node));
    network.terminals[
        pxr::HdMaterialTerminalTokens->surface] =
        connection(path);
    return network;
}

pxr::HdMaterialNetwork2
invalid_connection_network() {
    auto network = native_network(
        pxr::GfVec3f(
            0.1f,
            0.2f,
            0.3f));
    network.nodes.at(
        pxr::SdfPath(
            "/unit/material/output"))
        .inputConnections[
            pxr::TfToken("ior")] = {
                connection(
                    pxr::SdfPath(
                        "/unit/material/color"))};
    return network;
}

pxr::HdMaterialNetworkMap legacy_network() {
    pxr::HdMaterialNetworkMap result;
    pxr::HdMaterialNetwork network;
    pxr::HdMaterialNode node;
    node.path = pxr::SdfPath(
        "/unit/legacy/surface");
    node.identifier =
        pxr::TfToken("UsdPreviewSurface");
    node.parameters[
        pxr::TfToken("diffuseColor")] =
        pxr::VtValue(
            pxr::GfVec3f(
                0.3f,
                0.2f,
                0.1f));
    network.nodes.push_back(node);
    result.map.emplace(
        pxr::HdMaterialTerminalTokens->surface,
        std::move(network));
    result.terminals.push_back(node.path);
    return result;
}

void sync_material(
    pxr::HdUREMaterial& material,
    pxr::HdUnitTestDelegate& scene,
    pxr::HdURE& renderer,
    pxr::HdDirtyBits bits) {
    material.Sync(
        &scene,
        renderer.GetRenderParam(),
        &bits);
    check(
        bits == pxr::HdChangeTracker::Clean,
        "Hydra material dirty bits were not consumed");
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

    pxr::HdUnitTestDelegate scene(
        index.get(),
        pxr::SdfPath("/unit"));
    const pxr::SdfPath material_id(
        "/unit/material");
    scene.AddMaterialResource(
        material_id,
        pxr::VtValue(native_network(
            pxr::GfVec3f(
                0.1f,
                0.2f,
                0.3f))));

    auto* material =
        dynamic_cast<pxr::HdUREMaterial*>(
            index->GetSprim(
                pxr::HdPrimTypeTokens->material,
                material_id));
    check(material != nullptr,
          "HdURE did not create a material SPrim");
    if (!material) {
        return EXIT_FAILURE;
    }
    sync_material(
        *material,
        scene,
        renderer,
        material->
            GetInitialDirtyBitsMask());

    auto* state =
        dynamic_cast<pxr::HdURERenderParam*>(
            renderer.GetRenderParam());
    check(state != nullptr,
          "HdURE render state has the wrong type");
    if (!state) {
        return EXIT_FAILURE;
    }
    const auto record =
        state->FindMaterial(
            material_id.GetString());
    check(
        record &&
            record->material &&
            record->material->graph &&
            !record->material->graph->empty() &&
            record->loss_report.empty(),
        "URE adapter material graph was not retained losslessly");
    if (record &&
        record->material &&
        record->material->graph) {
        const auto color = std::find_if(
            record->material->graph->
                nodes.begin(),
            record->material->graph->
                nodes.end(),
            [](const auto& node) {
                return node.kind ==
                    ure::scene_ir::
                        MaterialGraphNodeKind::
                            ConstantColor;
            });
        check(
            color !=
                record->material->graph->
                    nodes.end() &&
                color->color.x == 0.1f,
            "URE adapter color parameter was lost");
    }

    scene.UpdateMaterialResource(
        material_id,
        pxr::VtValue(native_network(
            pxr::GfVec3f(
                0.7f,
                0.8f,
                0.9f))));
    sync_material(
        *material,
        scene,
        renderer,
        pxr::HdMaterial::DirtyResource);
    const auto updated =
        state->FindMaterial(
            material_id.GetString());
    check(
        updated && record &&
            updated->revision >
                record->revision,
        "Hydra material update did not advance native revision");
    if (updated &&
        updated->material &&
        updated->material->graph) {
        const auto color = std::find_if(
            updated->material->graph->
                nodes.begin(),
            updated->material->graph->
                nodes.end(),
            [](const auto& node) {
                return node.kind ==
                    ure::scene_ir::
                        MaterialGraphNodeKind::
                            ConstantColor;
            });
        check(
            color !=
                updated->material->graph->
                    nodes.end() &&
                color->color.x == 0.7f,
            "Hydra material update retained stale graph data");
    }

    const pxr::SdfPath preview_id(
        "/unit/preview");
    scene.AddMaterialResource(
        preview_id,
        pxr::VtValue(preview_network()));
    auto* preview =
        dynamic_cast<pxr::HdUREMaterial*>(
            index->GetSprim(
                pxr::HdPrimTypeTokens->material,
                preview_id));
    check(preview != nullptr,
          "UsdPreviewSurface fixture is missing");
    if (preview) {
        sync_material(
            *preview,
            scene,
            renderer,
            preview->
                GetInitialDirtyBitsMask());
    }
    const auto preview_record =
        state->FindMaterial(
            preview_id.GetString());
    const auto preview_loss =
        state->FindMaterialLossReport(
            preview_id.GetString());
    check(
        preview_record &&
            preview_loss.size() >= 2 &&
            std::ranges::any_of(
                preview_loss,
                [](const auto& loss) {
                    return loss.code ==
                        "URE-U4-LOSS-PREVIEW-BSDF";
                }) &&
            std::ranges::any_of(
                preview_loss,
                [](const auto& loss) {
                    return loss.code ==
                               "URE-U4-LOSS-UNUSED-PARAMETER" &&
                           loss.path ==
                               "/unit/preview/st_reader";
                }),
        "UsdPreviewSurface or primvar-reader capability loss was not reported");
    if (preview_record &&
        preview_record->material &&
        preview_record->material->graph) {
        const auto texture = std::find_if(
            preview_record->material->graph->
                nodes.begin(),
            preview_record->material->graph->
                nodes.end(),
            [](const auto& node) {
                return node.kind ==
                    ure::scene_ir::
                        MaterialGraphNodeKind::
                            Texture2D;
            });
        check(
            texture !=
                preview_record->material->graph->
                    nodes.end() &&
                texture->texture &&
                texture->texture->image &&
                texture->texture->uv_set == 1 &&
                texture->texture->image->
                    color_space ==
                    ure::scene_ir::
                        ImageColorSpace::Linear,
            "UsdUVTexture resource or primvar set was lost");
    }

    const pxr::SdfPath invalid_id(
        "/unit/invalid");
    scene.AddMaterialResource(
        invalid_id,
        pxr::VtValue(invalid_network()));
    auto* invalid =
        dynamic_cast<pxr::HdUREMaterial*>(
            index->GetSprim(
                pxr::HdPrimTypeTokens->material,
                invalid_id));
    check(invalid != nullptr,
          "invalid material fixture is missing");
    if (invalid) {
        sync_material(
            *invalid,
            scene,
            renderer,
            invalid->
                GetInitialDirtyBitsMask());
    }
    const auto invalid_loss =
        state->FindMaterialLossReport(
            invalid_id.GetString());
    check(
        !state->FindMaterial(
             invalid_id.GetString()) &&
            state->
                RejectedMaterialCount() == 1 &&
            std::ranges::any_of(
                invalid_loss,
                [](const auto& loss) {
                    return loss.severity ==
                        pxr::
                            HdUREMaterialLossSeverity::
                                Error &&
                        loss.code ==
                            "URE-U4-ERROR-UNSUPPORTED-NODE";
                }),
        "unsupported USD material node did not fail loud");

    const pxr::SdfPath invalid_connection_id(
        "/unit/invalid_connection");
    scene.AddMaterialResource(
        invalid_connection_id,
        pxr::VtValue(
            invalid_connection_network()));
    auto* invalid_connection =
        dynamic_cast<pxr::HdUREMaterial*>(
            index->GetSprim(
                pxr::HdPrimTypeTokens->material,
                invalid_connection_id));
    check(
        invalid_connection != nullptr,
        "invalid connected-input fixture is missing");
    if (invalid_connection) {
        sync_material(
            *invalid_connection,
            scene,
            renderer,
            invalid_connection->
                GetInitialDirtyBitsMask());
    }
    const auto invalid_connection_loss =
        state->FindMaterialLossReport(
            invalid_connection_id.
                GetString());
    check(
        !state->FindMaterial(
             invalid_connection_id.
                 GetString()) &&
            state->
                RejectedMaterialCount() == 2 &&
            std::ranges::any_of(
                invalid_connection_loss,
                [](const auto& loss) {
                    return loss.code ==
                        "URE-U4-ERROR-UNUSED-CONNECTION";
                }),
        "unknown connected material semantic did not fail closed");

    const pxr::SdfPath legacy_id(
        "/unit/legacy");
    scene.AddMaterialResource(
        legacy_id,
        pxr::VtValue(legacy_network()));
    auto* legacy =
        dynamic_cast<pxr::HdUREMaterial*>(
            index->GetSprim(
                pxr::HdPrimTypeTokens->material,
                legacy_id));
    check(legacy != nullptr,
          "legacy material-network fixture is missing");
    if (legacy) {
        sync_material(
            *legacy,
            scene,
            renderer,
            legacy->
                GetInitialDirtyBitsMask());
    }
    check(
        state->FindMaterial(
            legacy_id.GetString())
            .has_value(),
        "legacy HdMaterialNetworkMap was not normalized");

    material->Finalize(
        renderer.GetRenderParam());
    check(
        !state->FindMaterial(
             material_id.GetString()),
        "Hydra material finalization retained native state");

    index->Clear();
    std::cout << "Phase U.4 Hydra material checks: "
              << (failures ? "FAILED" : "PASSED")
              << '\n';
    return failures == 0
        ? EXIT_SUCCESS
        : EXIT_FAILURE;
}
