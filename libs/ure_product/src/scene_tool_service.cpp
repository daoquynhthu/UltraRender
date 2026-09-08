#include <algorithm>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

#include <nlohmann/json.hpp>

#include <ure/native_scene_hash.hpp>
#include <ure/native_adapter.hpp>
#include <ure/native_scene_uuid.hpp>
#include <ure/native_scene_validation.hpp>
#include <ure/native_scene_tooling.hpp>
#include <ure/material_presets.hpp>
#include <ure/product/scene_tool_service.hpp>

namespace ure::product {
namespace {

std::string operation_name(SceneToolOperation operation) {
    switch (operation) {
    case SceneToolOperation::Validate: return "validate";
    case SceneToolOperation::Inspect: return "inspect";
    case SceneToolOperation::Build: return "build";
    case SceneToolOperation::Migrate: return "migrate";
    case SceneToolOperation::Pack: return "pack";
    case SceneToolOperation::Unpack: return "unpack";
    case SceneToolOperation::Realize: return "realize";
    case SceneToolOperation::ImportMaterialX: return "material-import";
    case SceneToolOperation::ExportMaterialX: return "material-export";
    case SceneToolOperation::ApplyMaterialPreset: return "material-preset";
    }
    return "unknown";
}

std::string disposition_name(ProductFeatureDisposition disposition) {
    switch (disposition) {
    case ProductFeatureDisposition::Executed: return "Executed";
    case ProductFeatureDisposition::Rejected: return "Rejected";
    case ProductFeatureDisposition::PreservedForTooling:
        return "PreservedForTooling";
    }
    return "Rejected";
}

std::string update_class_name(ProductMaterialUpdateClass value) {
    switch (value) {
    case ProductMaterialUpdateClass::HotUpdate: return "HotUpdate";
    case ProductMaterialUpdateClass::PartialRebuild: return "PartialRebuild";
    case ProductMaterialUpdateClass::FullSnapshotReplacement:
        return "FullSnapshotReplacement";
    case ProductMaterialUpdateClass::Rejected: return "Rejected";
    }
    return "Rejected";
}

std::string identity_hex(const Identity& identity) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result(identity.size() * 2, '0');
    for (std::size_t index = 0; index < identity.size(); ++index) {
        result[index * 2] = digits[identity[index] >> 4U];
        result[index * 2 + 1] = digits[identity[index] & 15U];
    }
    return result;
}

std::string adapter_loss_identity(std::string_view report) {
    constexpr std::string_view domain =
        "UltraRender.AdapterLossReport.v1";
    std::vector<std::uint8_t> bytes;
    bytes.reserve(domain.size() + 1 + report.size());
    bytes.insert(bytes.end(), domain.begin(), domain.end());
    bytes.push_back(0);
    bytes.insert(bytes.end(), report.begin(), report.end());
    return native_scene::sha256_hex(bytes);
}

ProductSceneDiagnostic diagnostic(ProductSceneDiagnosticDomain domain,
                                  std::uint32_t detail,
                                  std::string code,
                                  std::string field_path,
                                  std::string message,
                                  std::string recovery) {
    return {domain, detail, std::move(code), std::move(field_path),
            std::move(message), std::move(recovery),
            native_scene::RequirementLevel::Required,
            ProductFeatureDisposition::Rejected};
}

native_scene::LoadResult<native_scene::NativeSceneArchive> load_scene(
    const SceneToolRequest& request,
    const std::filesystem::path& input) {
    if (input.extension() == ".urepkg")
        return native_scene::load_native_package_scene(
            input, request.package_scene_id, request.validation);
    if (!request.package_scene_id.empty()) {
        native_scene::LoadResult<native_scene::NativeSceneArchive> result;
        result.diagnostics.push_back(
            {"URE-PRV2-TOOL-SCENE-001",
             native_scene::DiagnosticSeverity::Error,
             "/package_scene_id",
             "A package scene ID is valid only for a package input", {}});
        return result;
    }
    return native_scene::load_native_asset(input, request.validation);
}

void append_load_failure(
    SceneToolResult& result,
    const native_scene::LoadResult<native_scene::NativeSceneArchive>& loaded) {
    if (loaded.diagnostics.empty()) {
        result.diagnostics.push_back(diagnostic(
            ProductSceneDiagnosticDomain::Parse, 620,
            "URE-PRV2-TOOL-PARSE-001", "/input",
            "Scene input could not be loaded",
            "fix the input path, format, or package selection"));
        return;
    }
    std::vector<std::tuple<std::string, std::string, std::string>> seen;
    for (const auto& item : loaded.diagnostics) {
        if (item.severity != native_scene::DiagnosticSeverity::Error)
            continue;
        const auto key = std::tuple(item.code, item.path, item.message);
        if (std::ranges::find(seen, key) != seen.end())
            continue;
        seen.push_back(key);
        auto domain = ProductSceneDiagnosticDomain::Parse;
        std::uint32_t detail = 620;
        std::string recovery =
            "fix the input schema, content, or package selection";
        if (item.code == "URE-Q-FEATURE-001") {
            domain = ProductSceneDiagnosticDomain::Feature;
            detail = 601;
            recovery = "remove the required feature or use a runtime that owns it";
        } else if (item.code.find("HASH") != std::string::npos) {
            domain = ProductSceneDiagnosticDomain::Resource;
            detail = 609;
            recovery = "restore the resource matching its content identity";
        } else if (item.code.find("DEP") != std::string::npos) {
            domain = ProductSceneDiagnosticDomain::Resource;
            detail = 610;
            recovery = "remove the resource dependency cycle or add its dependency";
        } else if (item.code.find("PATH") != std::string::npos) {
            domain = ProductSceneDiagnosticDomain::Resource;
            detail = 612;
            recovery = "use a safe package-relative or content-addressed resource";
        } else if (item.code == "URE-Q9-PACKAGE-009") {
            domain = ProductSceneDiagnosticDomain::Resource;
            detail = 618;
            recovery = "select one scene ID from the package inventory";
        } else if (item.code == "URE-Q-BUDGET-003") {
            domain = ProductSceneDiagnosticDomain::Resource;
            detail = 613;
            recovery = "repack the input without excessive decompression expansion";
        } else if (item.code.find("BUDGET") != std::string::npos) {
            domain = ProductSceneDiagnosticDomain::Resource;
            detail = 614;
            recovery = "reduce the input or increase its declared resource budget";
        }
        result.diagnostics.push_back(diagnostic(
            domain, detail, item.code,
            item.path.starts_with('/') ? item.path : "/input",
            item.message, std::move(recovery)));
    }
}

void inspect_result(SceneToolResult& result,
                    const std::filesystem::path& path,
                    const native_scene::ValidationLimits& limits) {
    const auto inspection = native_scene::inspect_native_asset(path, limits);
    result.semantic_identity = inspection.semantic_hash;
    result.stored_bytes = inspection.stored_bytes;
    result.resident_bytes = inspection.resident_bytes;
    result.scene_count = inspection.scene_count;
    result.resource_count = inspection.resource_count;
    result.cache_count = inspection.cache_count;
    result.dependency_count = inspection.dependency_count;
    for (const auto& item : inspection.diagnostics) {
        if (item.severity == native_scene::DiagnosticSeverity::Error) {
            result.diagnostics.push_back(diagnostic(
                ProductSceneDiagnosticDomain::Package, 621, item.code,
                item.path.starts_with('/') ? item.path : "/input",
                item.message,
                "repair or rebuild the scene or package"));
        }
    }
}

std::shared_ptr<const ProductSnapshot> realize_result(
    SceneToolResult& result, native_scene::NativeSceneArchive archive,
    const SceneToolRequest& request) {
    ProductRealizationLimits limits;
    limits.validation = request.validation;
    limits.temporary_bytes = request.temporary_bytes;
    auto realized = realize_product_scene(std::move(archive), limits);
    result.diagnostics.insert(result.diagnostics.end(),
                              realized.diagnostics.begin(),
                              realized.diagnostics.end());
    if (!realized.snapshot)
        return {};
    result.snapshot_identity = realized.snapshot->identity();
    result.semantic_identity = realized.snapshot->semantic_identity();
    result.resource_budget = realized.snapshot->resource_budget();
    result.dispositions = realized.snapshot->feature_dispositions();
    result.resource_count = realized.snapshot->resources().size();
    result.material_program_set_identity =
        realized.snapshot->material_programs().identity;
    result.material_program_count =
        realized.snapshot->material_programs().programs.size();
    return realized.snapshot;
}

std::string read_text(const std::filesystem::path& path,
                      std::uint64_t limit) {
    const auto size = std::filesystem::file_size(path);
    if (size > limit)
        throw std::length_error("Material source exceeds the content budget");
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::invalid_argument("Material source could not be opened");
    std::string value(static_cast<std::size_t>(size), '\0');
    if (!value.empty())
        input.read(value.data(), static_cast<std::streamsize>(value.size()));
    if (!input)
        throw std::invalid_argument("Material source could not be read");
    return value;
}

void write_text(const std::filesystem::path& path, std::string_view value) {
    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        throw std::runtime_error("Material output could not be created");
    output.write(value.data(), static_cast<std::streamsize>(value.size()));
    output.close();
    if (!output)
        throw std::runtime_error("Material output publication failed");
}

struct SelectedMaterial {
    std::shared_ptr<scene_ir::MaterialNode> material;
    std::string uuid;
};

SelectedMaterial select_material(
    native_scene::NativeSceneArchive& archive,
    std::string_view selector) {
    if (selector.empty())
        throw std::invalid_argument("Material operation requires a selector");
    std::vector<SelectedMaterial> matches;
    for (std::size_t index = 0; index < archive.scene.materials.size();
         ++index) {
        const auto& material = archive.scene.materials[index];
        const bool source_match = index < archive.source_ids.materials.size() &&
                                  archive.source_ids.materials[index] == selector;
        const bool uuid_match = index < archive.object_uuids.materials.size() &&
                                native_scene::format_uuid(
                                    archive.object_uuids.materials[index]) ==
                                    selector;
        if (material && (source_match || uuid_match ||
                         material->name == selector))
            matches.push_back(
                {material, native_scene::format_uuid(
                               archive.object_uuids.materials[index])});
    }
    if (matches.size() == 1)
        return std::move(matches.front());
    throw std::invalid_argument("Material selector did not resolve uniquely");
}

void retain_materialx_provenance(
    native_scene::NativeSceneArchive& archive,
    std::string_view source) {
    const auto payload = std::vector<std::uint8_t>(source.begin(),
                                                   source.end());
    const auto hash = native_scene::sha256_hex(payload);
    const auto found = std::ranges::find(archive.document.resources, hash,
        [](const native_scene::ResourceDescriptor& resource) {
            return resource.content_hash;
        });
    if (found != archive.document.resources.end())
        return;
    native_scene::ResourceDescriptor descriptor;
    descriptor.id = "provenance/materialx/" + hash.substr(0, 16);
    descriptor.content_hash = hash;
    descriptor.kind = native_scene::ResourceKind::Provenance;
    descriptor.schema_version = {1, 0};
    descriptor.uri = "provenance/materialx/" + hash + ".mtlx";
    descriptor.byte_length = payload.size();
    descriptor.resident_bytes = payload.size();
    archive.document.resources.push_back(descriptor);
    archive.packaged_resources.push_back(
        {descriptor.id, descriptor, payload, descriptor.uri});
}

void package_materialx_resources(
    native_scene::NativeSceneArchive& archive,
    scene_ir::MaterialGraph& graph,
    const std::filesystem::path& source_path,
    std::uint64_t byte_limit) {
    const auto root = std::filesystem::absolute(source_path).parent_path().lexically_normal();
    for (auto& node : graph.nodes) {
        if (!node.texture || !node.texture->image ||
            node.texture->image->uri.empty())
            continue;
        const std::filesystem::path uri(node.texture->image->uri);
        if (uri.is_absolute())
            throw std::invalid_argument(
                "MaterialX texture URI must be relative to its source document");
        const auto resolved = (root / uri).lexically_normal();
        const auto relative = resolved.lexically_relative(root);
        if (relative.empty() || relative.is_absolute() ||
            *relative.begin() == "..")
            throw std::invalid_argument(
                "MaterialX texture URI escapes its source document root");
        const auto size = std::filesystem::file_size(resolved);
        if (size > byte_limit)
            throw std::length_error(
                "MaterialX texture exceeds the content budget");
        std::ifstream input(resolved, std::ios::binary);
        std::vector<std::uint8_t> payload(static_cast<std::size_t>(size));
        if (!input || (!payload.empty() &&
                       !input.read(reinterpret_cast<char*>(payload.data()),
                                   static_cast<std::streamsize>(payload.size()))))
            throw std::invalid_argument(
                "MaterialX texture resource could not be read");
        native_scene::ResourceDescriptor descriptor;
        descriptor.content_hash = native_scene::sha256_hex(payload);
        descriptor.id = "materialx/texture/" + descriptor.content_hash.substr(0, 16);
        descriptor.kind = native_scene::ResourceKind::Texture;
        descriptor.schema_version = {1, 0};
        descriptor.uri = node.texture->image->uri;
        descriptor.byte_length = payload.size();
        descriptor.resident_bytes = payload.size();
        if (std::ranges::find(archive.document.resources,
                             descriptor.content_hash,
                             &native_scene::ResourceDescriptor::content_hash) ==
            archive.document.resources.end())
            archive.document.resources.push_back(descriptor);
        if (std::ranges::find(archive.packaged_resources,
                             descriptor.content_hash,
                             [](const native_scene::NamedResourcePayload& item) {
                                 return item.descriptor.content_hash;
                             }) == archive.packaged_resources.end())
            archive.packaged_resources.push_back(
                {descriptor.id, descriptor, std::move(payload), descriptor.uri});

        const auto image_id = "materialx/image/" +
                              descriptor.content_hash.substr(0, 16) + "/" +
                              std::to_string(static_cast<unsigned>(
                                  node.texture->image->color_space));
        auto image_position = std::ranges::find(archive.source_ids.images,
                                                image_id);
        std::shared_ptr<scene_ir::ImageResource> image;
        if (image_position == archive.source_ids.images.end()) {
            image = node.texture->image;
            archive.scene.images.push_back(image);
            archive.source_ids.images.push_back(image_id);
        } else {
            const auto index = static_cast<std::size_t>(
                std::distance(archive.source_ids.images.begin(),
                              image_position));
            image = archive.scene.images[index];
        }
        node.texture->image = image;

        const auto texture_id = "materialx/texture/" +
                                descriptor.content_hash.substr(0, 16) + "/" +
                                std::to_string(node.texture->uv_set) + "/" +
                                std::to_string(static_cast<unsigned>(
                                    image->color_space));
        auto texture_position = std::ranges::find(
            archive.source_ids.textures, texture_id);
        if (texture_position == archive.source_ids.textures.end()) {
            archive.scene.textures.push_back(node.texture);
            archive.source_ids.textures.push_back(texture_id);
        } else {
            const auto index = static_cast<std::size_t>(
                std::distance(archive.source_ids.textures.begin(),
                              texture_position));
            node.texture = archive.scene.textures[index];
        }
    }
    native_scene::assign_deterministic_object_uuids(archive);
}

void add_adapter_feature(native_scene::NativeSceneArchive& archive,
                         std::string_view name,
                         std::string canonical_parameters = "{}") {
    const auto existing = std::ranges::find_if(
        archive.document.features,
        [name, &canonical_parameters](const auto& feature) {
            return feature.name == name &&
                   feature.canonical_parameters == canonical_parameters;
        });
    if (existing != archive.document.features.end())
        return;
    archive.document.features.push_back(
        {std::string(name), {1, 0},
         native_scene::RequirementLevel::Optional, "ure", {},
         std::move(canonical_parameters)});
}

void freeze_authoring_resources(
    native_scene::NativeSceneArchive& archive,
    const native_scene::ValidationLimits& limits) {
    const auto resolved = native_scene::resolve_native_scene_resources(
        archive, limits);
    if (!resolved.ok())
        throw std::invalid_argument(
            resolved.diagnostics.empty()
                ? "Authoring resources could not be resolved"
                : resolved.diagnostics.front().message);
    archive.packaged_resources.clear();
    archive.packaged_resources.reserve(resolved.resources.size());
    for (const auto& resource : resolved.resources) {
        archive.packaged_resources.push_back(
            {resource.logical_id, resource.descriptor, resource.payload,
             resource.source_uri});
    }
}

void execute_material_operation(SceneToolResult& result,
                                const SceneToolRequest& request) {
    auto loaded = load_scene(request, request.inputs.front());
    if (!loaded.ok() || !loaded.value) {
        append_load_failure(result, loaded);
        return;
    }
    auto archive = std::move(*loaded.value);
    const auto selected = select_material(archive,
                                          request.material_selector);
    const auto& material = selected.material;
    const auto before_snapshot = realize_result(result, archive, request);
    if (!result.ok())
        return;
    result.has_material_update = true;
    const auto targeted_parameters = nlohmann::ordered_json{
        {"material_uuid", selected.uuid}}.dump();
    if (request.operation == SceneToolOperation::ImportMaterialX) {
        const auto source = read_text(request.inputs[1],
                                      request.validation.max_total_stored_bytes);
        const auto imported = native_scene::import_materialx_native(source);
        result.adapter_loss_report = native_scene::write_adapter_loss_report(
            imported.loss_report);
        if (!imported.ok())
            throw std::invalid_argument(
                imported.diagnostics.empty()
                    ? "MaterialX import is unsupported or lossy"
                    : imported.diagnostics.front().message);
        material->graph = std::make_shared<scene_ir::MaterialGraph>(
            imported.graph);
        package_materialx_resources(
            archive, *material->graph, request.inputs[1],
            request.validation.max_total_stored_bytes);
        retain_materialx_provenance(archive, source);
        add_adapter_feature(archive, "ure.adapter.materialx",
                            targeted_parameters);
        freeze_authoring_resources(archive, request.validation);
        native_scene::save_native_scene(request.output, archive);
    } else if (request.operation == SceneToolOperation::ExportMaterialX) {
        if (!material->graph || material->graph->empty())
            throw std::invalid_argument(
                "MaterialX export requires a canonical MaterialGraph");
        native_scene::AdapterLossReport loss_report;
        const auto output = native_scene::export_materialx_native(
            *material->graph, loss_report, material->name);
        result.adapter_loss_report =
            native_scene::write_adapter_loss_report(loss_report);
        if (!loss_report.exportable())
            throw std::invalid_argument(
                "MaterialGraph cannot be exported without unsupported loss");
        write_text(request.output, output);
    } else {
        const auto presets = scene_ir::material_preset_names();
        if (std::ranges::find(presets, request.preset_name) == presets.end())
            throw std::invalid_argument("Unknown material preset");
        const auto preset = scene_ir::make_material_preset(request.preset_name);
        const auto retained_name = material->name;
        *material = *preset;
        material->name = retained_name;
        add_adapter_feature(archive, "ure.adapter.material-preset",
                            targeted_parameters);
        freeze_authoring_resources(archive, request.validation);
        native_scene::save_native_scene(request.output, archive);
    }
    if (request.operation != SceneToolOperation::ExportMaterialX) {
        auto result_archive = load_scene(request, request.output);
        if (!result_archive.ok() || !result_archive.value) {
            append_load_failure(result, result_archive);
            return;
        }
        result.snapshot_identity = {};
        result.material_program_set_identity = {};
        result.material_program_count = 0;
        const auto after_snapshot = realize_result(
            result, std::move(*result_archive.value), request);
        if (result.ok() && before_snapshot && after_snapshot) {
            const auto update = classify_product_material_update(
                before_snapshot->material_programs(),
                after_snapshot->material_programs());
            result.material_update_class = update.update_class;
            result.changed_material_uuids =
                update.changed_material_uuids;
            inspect_result(result, request.output, request.validation);
        }
    } else {
        result.material_update_class = ProductMaterialUpdateClass::HotUpdate;
    }
}

void validate_inputs(const SceneToolRequest& request) {
    if (request.inputs.empty())
        throw std::invalid_argument("Scene-tool operation requires input");
    if (request.allow_script_execution)
        throw std::invalid_argument(
            "Product runtime does not execute ambient scripts");
    const bool writes = request.operation == SceneToolOperation::Build ||
                        request.operation == SceneToolOperation::Migrate ||
                        request.operation == SceneToolOperation::Pack ||
                        request.operation == SceneToolOperation::Unpack ||
                        request.operation == SceneToolOperation::ImportMaterialX ||
                        request.operation == SceneToolOperation::ExportMaterialX ||
                        request.operation == SceneToolOperation::ApplyMaterialPreset;
    if (writes && request.output.empty())
        throw std::invalid_argument(
            "Scene-tool publication operation requires output");
    const auto expected_inputs =
        request.operation == SceneToolOperation::ImportMaterialX ? 2U : 1U;
    if (request.operation != SceneToolOperation::Pack &&
        request.inputs.size() != expected_inputs)
        throw std::invalid_argument(
            "Scene-tool operation received an invalid input count");
    const bool package_republication =
        (request.operation == SceneToolOperation::Build ||
         request.operation == SceneToolOperation::Migrate) &&
        request.inputs.front().extension() == ".urepkg";
    const bool package_repack =
        request.operation == SceneToolOperation::Pack &&
        std::ranges::any_of(request.inputs, [](const auto& input) {
            return input.extension() == ".urepkg";
        });
    if (package_republication || package_repack)
        throw std::invalid_argument(
            "Package inputs must be unpacked before build, migrate, or repack");
}

}

bool SceneToolResult::ok() const noexcept {
    return diagnostics.empty();
}

std::string SceneToolResult::report_json() const {
    nlohmann::ordered_json root;
    root["schema"] = "ure.preview.scene-tool-report/0.2";
    root["operation"] = operation_name(operation);
    root["ok"] = ok();
    root["snapshot_identity"] = identity_hex(snapshot_identity);
    root["material_program_set_identity"] =
        identity_hex(material_program_set_identity);
    root["semantic_identity"] = semantic_identity;
    root["inventory"] = {
        {"scenes", scene_count}, {"resources", resource_count},
        {"caches", cache_count}, {"dependencies", dependency_count},
        {"material_programs", material_program_count}};
    if (!adapter_loss_report.empty()) {
        root["adapter_loss_report"] =
            nlohmann::ordered_json::parse(adapter_loss_report);
        root["adapter_loss_report_identity"] =
            adapter_loss_identity(adapter_loss_report);
    }
    if (has_material_update) {
        root["material_update"] = {
            {"classification", update_class_name(material_update_class)},
            {"changed_material_uuids", changed_material_uuids}};
    }
    root["budget"] = {
        {"stored_bytes", resource_budget.stored_bytes},
        {"decompressed_bytes", resource_budget.decompressed_bytes},
        {"resident_bytes", resource_budget.resident_bytes},
        {"streamed_bytes", resource_budget.streamed_bytes},
        {"temporary_bytes", resource_budget.temporary_bytes},
        {"output_bytes", resource_budget.output_bytes}};
    auto disposition_values = nlohmann::ordered_json::array();
    auto ordered = dispositions;
    std::ranges::sort(ordered, {}, &ProductFeatureRecord::id);
    for (const auto& item : ordered) {
        disposition_values.push_back(
            {{"id", item.id},
             {"requirement", static_cast<std::uint32_t>(item.requirement)},
             {"disposition", disposition_name(item.disposition)},
             {"detail", item.detail}});
    }
    root["feature_dispositions"] = std::move(disposition_values);
    auto diagnostic_values = nlohmann::ordered_json::array();
    for (const auto& item : diagnostics) {
        diagnostic_values.push_back(
            {{"domain", static_cast<std::uint32_t>(item.domain)},
             {"detail", item.detail}, {"code", item.code},
             {"field_path", item.field_path}, {"message", item.message},
             {"recovery", item.recovery},
             {"requirement", static_cast<std::uint32_t>(item.requirement)},
             {"disposition", disposition_name(item.disposition)}});
    }
    root["diagnostics"] = std::move(diagnostic_values);
    return root.dump(2) + "\n";
}

SceneToolResult execute_scene_tool(const SceneToolRequest& request) {
    SceneToolResult result;
    result.operation = request.operation;
    try {
        validate_inputs(request);
        if (request.operation == SceneToolOperation::ImportMaterialX ||
            request.operation == SceneToolOperation::ExportMaterialX ||
            request.operation == SceneToolOperation::ApplyMaterialPreset) {
            execute_material_operation(result, request);
            return result;
        }
        if (request.operation == SceneToolOperation::Inspect) {
            inspect_result(result, request.inputs.front(), request.validation);
            return result;
        }
        if (request.operation == SceneToolOperation::Pack) {
            for (const auto& input : request.inputs) {
                auto loaded = load_scene(request, input);
                if (!loaded.ok() || !loaded.value) {
                    append_load_failure(result, loaded);
                    return result;
                }
                realize_result(result, std::move(*loaded.value), request);
                if (!result.ok())
                    return result;
            }
            native_scene::pack_native_scenes(
                request.output, request.inputs, request.validation);
            inspect_result(result, request.output, request.validation);
            return result;
        }
        if (request.operation == SceneToolOperation::Unpack) {
            native_scene::unpack_native_package(
                request.inputs.front(), request.output, request.validation);
            inspect_result(result, request.inputs.front(), request.validation);
            return result;
        }
        if (request.operation == SceneToolOperation::Build) {
            const auto extension = request.inputs.front().extension().string();
            if (extension == ".gltf" || extension == ".glb") {
                const auto imported = native_scene::import_gltf_native(
                    request.inputs.front(), request.validation);
                result.adapter_loss_report =
                    native_scene::write_adapter_loss_report(
                        imported.loss_report);
                if (!imported.ok())
                    throw std::invalid_argument(
                        imported.diagnostics.empty()
                            ? "glTF import failed"
                            : imported.diagnostics.front().message);
                auto archive = imported.archive;
                add_adapter_feature(archive, "ure.adapter.gltf");
                freeze_authoring_resources(archive, request.validation);
                native_scene::save_native_scene(request.output, archive);
            } else {
                native_scene::build_native_scene(
                    request.inputs.front(), request.output,
                    request.validation);
            }
        } else if (request.operation == SceneToolOperation::Migrate) {
            native_scene::migrate_native_scene(
                request.inputs.front(), request.output, request.validation);
        }
        const auto input =
            request.operation == SceneToolOperation::Build ||
                    request.operation == SceneToolOperation::Migrate
                ? request.output
                : request.inputs.front();
        auto loaded = load_scene(request, input);
        if (!loaded.ok() || !loaded.value) {
            append_load_failure(result, loaded);
            return result;
        }
        realize_result(result, std::move(*loaded.value), request);
        if (result.ok())
            inspect_result(result, input, request.validation);
    } catch (const ProductMaterialError& error) {
        if (request.operation == SceneToolOperation::ImportMaterialX ||
            request.operation == SceneToolOperation::ExportMaterialX ||
            request.operation == SceneToolOperation::ApplyMaterialPreset) {
            result.has_material_update = true;
            result.material_update_class =
                ProductMaterialUpdateClass::Rejected;
        }
        result.diagnostics.push_back(diagnostic(
            ProductSceneDiagnosticDomain::Material, error.detail(),
            error.code(), error.field_path(), error.what(),
            error.recovery()));
    } catch (const std::invalid_argument& error) {
        const bool material_operation =
            request.operation == SceneToolOperation::ImportMaterialX ||
            request.operation == SceneToolOperation::ExportMaterialX ||
            request.operation == SceneToolOperation::ApplyMaterialPreset;
        if (material_operation) {
            result.has_material_update = true;
            result.material_update_class =
                ProductMaterialUpdateClass::Rejected;
        }
        result.diagnostics.push_back(diagnostic(
            material_operation ? ProductSceneDiagnosticDomain::Adapter
                               : ProductSceneDiagnosticDomain::Feature,
            material_operation ? 706U : 622U,
            material_operation ? "URE-PRV3-MATERIAL-ADAPTER-001"
                               : "URE-PRV2-TOOL-REQUEST-001",
            material_operation ? "/material" : "/request", error.what(),
            material_operation
                ? "fix the material selector, adapter source, or preset"
                : "fix the operation input, output, selection, or policy"));
    } catch (const std::exception&) {
        result.diagnostics.push_back(diagnostic(
            ProductSceneDiagnosticDomain::Publication, 623,
            "URE-PRV2-TOOL-PUBLICATION-001", "/output",
            "Scene-tool publication failed",
            "check the output budget and destination permissions"));
    }
    return result;
}

}
