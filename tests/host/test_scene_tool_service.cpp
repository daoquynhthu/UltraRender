#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include <ure/native_scene_tooling.hpp>
#include <ure/product/scene_tool_service.hpp>

namespace {

int failures{};

void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

bool nonzero(const ure::product::Identity& identity) {
    return std::ranges::any_of(
        identity, [](std::uint8_t value) { return value != 0; });
}

bool has_report_identity(const std::string& report) {
    const std::string marker = "\"adapter_loss_report_identity\": \"";
    const auto begin = report.find(marker);
    if (begin == std::string::npos)
        return false;
    const auto identity_begin = begin + marker.size();
    const auto identity_end = report.find('"', identity_begin);
    return identity_end == identity_begin + 64 &&
           std::ranges::all_of(
               report.begin() + static_cast<std::ptrdiff_t>(identity_begin),
               report.begin() + static_cast<std::ptrdiff_t>(identity_end),
               [](const char value) {
                   return (value >= '0' && value <= '9') ||
                          (value >= 'a' && value <= 'f');
               });
}

}

int main() {
    using ure::product::SceneToolOperation;
    using ure::product::SceneToolRequest;
    const auto root = std::filesystem::temp_directory_path() /
                      "ultrarender_prv2_scene_tool";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    SceneToolRequest realize;
    realize.operation = SceneToolOperation::Realize;
    realize.inputs = {std::filesystem::path(URE_SCENE_TOOL_PROCEDURAL)};
    const auto realized = ure::product::execute_scene_tool(realize);
    const auto realized_report = realized.report_json();
    check(realized.ok() && nonzero(realized.snapshot_identity),
          "scene-tool procedural realization failed");
    check(realized_report.find("ure.scene.procedural") != std::string::npos &&
              realized_report.find("Executed") != std::string::npos,
          "scene-tool report omitted procedural disposition");
    check(realized_report.find(realize.inputs.front().parent_path().string()) ==
              std::string::npos,
          "scene-tool report leaked an author path");

    const auto author = root / "author";
    const auto source = std::filesystem::path(URE_SCENE_TOOL_RESOURCE);
    std::filesystem::copy(source.parent_path(), author,
                          std::filesystem::copy_options::recursive);
    const auto package = root / "self-contained.urepkg";
    SceneToolRequest pack;
    pack.operation = SceneToolOperation::Pack;
    pack.inputs = {author / source.filename()};
    pack.output = package;
    const auto packed = ure::product::execute_scene_tool(pack);
    check(packed.ok() && packed.resource_count >= 5,
          "scene-tool package operation did not retain resources");
    std::filesystem::remove_all(author);

    SceneToolRequest validate;
    validate.operation = SceneToolOperation::Validate;
    validate.inputs = {package};
    validate.package_scene_id = "scene/full";
    const auto validated = ure::product::execute_scene_tool(validate);
    check(validated.ok() && nonzero(validated.snapshot_identity) &&
              validated.resource_count >= 5,
          "scene-tool self-contained package validation failed");

    validate.package_scene_id = "missing";
    const auto missing = ure::product::execute_scene_tool(validate);
    check(!missing.ok() &&
              missing.report_json().find(root.string()) == std::string::npos,
          "scene-tool missing-scene diagnostic was absent or unredacted");

    SceneToolRequest procedural_pack;
    procedural_pack.operation = SceneToolOperation::Pack;
    procedural_pack.inputs = {
        std::filesystem::path(URE_SCENE_TOOL_PROCEDURAL)};
    procedural_pack.output = root / "procedural.urepkg";
    const auto procedural =
        ure::product::execute_scene_tool(procedural_pack);
    check(procedural.ok() && procedural.cache_count == 1,
          "scene-tool procedural package omitted its rebuildable cache");

    const auto loaded_material_scene = ure::native_scene::load_native_asset(
        std::filesystem::path(URE_SCENE_TOOL_RESOURCE));
    check(loaded_material_scene.ok() && loaded_material_scene.value &&
              !loaded_material_scene.value->source_ids.materials.empty(),
          "material authoring fixture did not load");
    if (loaded_material_scene.ok() && loaded_material_scene.value &&
        !loaded_material_scene.value->source_ids.materials.empty()) {
        const auto selector =
            loaded_material_scene.value->source_ids.materials.front();
        SceneToolRequest preset;
        preset.operation = SceneToolOperation::ApplyMaterialPreset;
        preset.inputs = {std::filesystem::path(URE_SCENE_TOOL_RESOURCE)};
        preset.output = root / "preset.urescene";
        preset.material_selector = selector;
        preset.preset_name = "clear_glass";
        const auto preset_result = ure::product::execute_scene_tool(preset);
        check(preset_result.ok() &&
                  nonzero(preset_result.material_program_set_identity) &&
                  preset_result.material_program_count != 0,
              "preset did not produce a canonical material program set");

        SceneToolRequest export_material;
        export_material.operation = SceneToolOperation::ExportMaterialX;
        export_material.inputs = {preset.output};
        export_material.output = root / "preset.mtlx";
        export_material.material_selector = selector;
        const auto exported =
            ure::product::execute_scene_tool(export_material);
        check(exported.ok() &&
                  std::filesystem::is_regular_file(export_material.output) &&
                  !exported.adapter_loss_report.empty() &&
                  nonzero(exported.material_program_set_identity) &&
                  has_report_identity(exported.report_json()),
              "canonical material did not export through MaterialX");

        SceneToolRequest import_material;
        import_material.operation = SceneToolOperation::ImportMaterialX;
        import_material.inputs = {
            std::filesystem::path(URE_SCENE_TOOL_RESOURCE),
            export_material.output};
        import_material.output = root / "materialx.urescene";
        import_material.material_selector = selector;
        const auto imported =
            ure::product::execute_scene_tool(import_material);
        check(imported.ok() && imported.resource_count != 0 &&
                  imported.report_json().find("ure.adapter.materialx") !=
                      std::string::npos,
              "MaterialX source did not converge through canonical authoring");

        const auto materialx_root = root / "materialx-source";
        std::filesystem::create_directories(materialx_root);
        const auto materialx_texture = materialx_root / "relative.ppm";
        {
            std::ofstream output(materialx_texture, std::ios::binary);
            output << "P3\n1 1\n255\n32 64 128\n";
        }
        const auto relative_materialx = materialx_root / "relative.mtlx";
        {
            std::ofstream output(relative_materialx);
            output <<
                "<materialx xmlns:URE=\"https://ultrarender/MaterialGraph\">"
                "<URE_texture2d name=\"n1\" file=\"relative.ppm\" URE:color_space=\"linear\" />"
                "<URE_bsdf_lambert name=\"n2\"><input name=\"base_color\" nodename=\"n1\" />"
                "</URE_bsdf_lambert><surfacematerial name=\"n3\">"
                "<input name=\"surface\" nodename=\"n2\" />"
                "</surfacematerial></materialx>";
        }
        import_material.inputs[1] = relative_materialx;
        import_material.output = root / "relative-materialx.urescene";
        const auto relative_import =
            ure::product::execute_scene_tool(import_material);
        std::filesystem::remove_all(materialx_root);
        SceneToolRequest relative_realize;
        relative_realize.operation = SceneToolOperation::Realize;
        relative_realize.inputs = {import_material.output};
        const auto relative_realized =
            ure::product::execute_scene_tool(relative_realize);
        check(relative_import.ok() && relative_realized.ok() &&
                  relative_realized.resource_count != 0,
              "MaterialX relative texture was not packaged from its source root");

        const auto color_space_root = root / "materialx-color-space";
        std::filesystem::create_directories(color_space_root);
        const auto shared_texture = color_space_root / "shared.ppm";
        {
            std::ofstream output(shared_texture, std::ios::binary);
            output << "P3\n1 1\n255\n32 64 128\n";
        }
        const auto color_space_materialx = color_space_root / "dual.mtlx";
        {
            std::ofstream output(color_space_materialx);
            output <<
                "<materialx xmlns:URE=\"https://ultrarender/MaterialGraph\">"
                "<URE_texture2d name=\"n1\" file=\"shared.ppm\" URE:color_space=\"linear\" />"
                "<URE_texture2d name=\"n2\" file=\"shared.ppm\" URE:color_space=\"srgb\" />"
                "<URE_bsdf_lambert name=\"n3\"><input name=\"base_color\" nodename=\"n1\" />"
                "</URE_bsdf_lambert><URE_bsdf_lambert name=\"n4\">"
                "<input name=\"base_color\" nodename=\"n2\" /></URE_bsdf_lambert>"
                "<URE_constant_float name=\"n5\" value=\"0.5\" />"
                "<URE_bsdf_mix name=\"n6\"><input name=\"a\" nodename=\"n3\" />"
                "<input name=\"b\" nodename=\"n4\" /><input name=\"factor\" nodename=\"n5\" />"
                "</URE_bsdf_mix><surfacematerial name=\"n7\">"
                "<input name=\"surface\" nodename=\"n6\" />"
                "</surfacematerial></materialx>";
        }
        import_material.inputs[1] = color_space_materialx;
        import_material.output = root / "dual-color-space.urescene";
        const auto color_space_import =
            ure::product::execute_scene_tool(import_material);
        const auto color_space_archive = ure::native_scene::load_native_scene(
            import_material.output, {});
        bool has_linear_shared_texture = false;
        bool has_srgb_shared_texture = false;
        if (color_space_archive.ok()) {
            for (const auto& image : color_space_archive.value->scene.images) {
                if (!image || image->uri != "shared.ppm")
                    continue;
                has_linear_shared_texture |=
                    image->color_space == ure::scene_ir::ImageColorSpace::Linear;
                has_srgb_shared_texture |=
                    image->color_space == ure::scene_ir::ImageColorSpace::SRGB;
            }
        }
        check(color_space_import.ok() && color_space_archive.ok() &&
                  has_linear_shared_texture && has_srgb_shared_texture,
              "MaterialX content deduplication collapsed image color-space semantics");

        const auto unsupported_materialx = root / "unsupported.mtlx";
        {
            std::ofstream output(unsupported_materialx);
            output << "<materialx><unsupported_shader name=\"bad\" /></materialx>";
        }
        import_material.inputs[1] = unsupported_materialx;
        import_material.output = root / "unsupported.urescene";
        const auto unsupported =
            ure::product::execute_scene_tool(import_material);
        check(!unsupported.ok() &&
                  !unsupported.adapter_loss_report.empty() &&
                  nonzero(unsupported.material_program_set_identity) &&
                  has_report_identity(unsupported.report_json()) &&
                  unsupported.diagnostics.front().detail == 706,
              "MaterialX rejection did not retain the shared loss identity");

        preset.preset_name = "missing-preset";
        preset.output = root / "missing.urescene";
        const auto rejected = ure::product::execute_scene_tool(preset);
        check(!rejected.ok() && !rejected.diagnostics.empty() &&
                  rejected.diagnostics.front().detail == 706,
              "unknown preset did not produce a material adapter diagnostic");

        ure::scene_ir::SceneIR duplicate_scene;
        auto duplicate_a = std::make_shared<ure::scene_ir::MaterialNode>();
        auto duplicate_b = std::make_shared<ure::scene_ir::MaterialNode>();
        duplicate_a->name = "duplicate";
        duplicate_b->name = "duplicate";
        duplicate_scene.materials = {duplicate_a, duplicate_b};
        ure::native_scene::SceneDocument duplicate_document;
        duplicate_document.id = "scene/duplicate-material-name";
        duplicate_document.schema_version = {1, 0};
        const auto duplicate_archive =
            ure::native_scene::make_native_scene_archive(
                std::move(duplicate_document), std::move(duplicate_scene));
        const auto duplicate_path = root / "duplicate.urescene";
        ure::native_scene::save_native_scene(duplicate_path,
                                             duplicate_archive);
        preset.inputs = {duplicate_path};
        preset.material_selector = "duplicate";
        preset.preset_name = "clear_glass";
        preset.output = root / "duplicate-output.urescene";
        const auto duplicate_rejected =
            ure::product::execute_scene_tool(preset);
        check(!duplicate_rejected.ok() &&
                  duplicate_rejected.material_update_class ==
                      ure::product::ProductMaterialUpdateClass::Rejected,
              "ambiguous material selector was not rejected");
    }

    SceneToolRequest gltf;
    gltf.operation = SceneToolOperation::Build;
    gltf.inputs = {std::filesystem::path(URE_SCENE_TOOL_GLTF)};
    gltf.output = root / "gltf.urescene";
    const auto gltf_result = ure::product::execute_scene_tool(gltf);
    check(gltf_result.ok() && gltf_result.material_program_count != 0 &&
              gltf_result.report_json().find("ure.adapter.gltf") !=
                  std::string::npos,
          "glTF material authoring did not converge through Scene Tool");

    std::filesystem::remove_all(root);
    if (failures == 0)
        std::printf("product scene-tool service tests passed\n");
    return failures == 0 ? 0 : 1;
}
