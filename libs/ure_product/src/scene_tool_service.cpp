#include <algorithm>
#include <filesystem>
#include <ranges>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

#include <nlohmann/json.hpp>

#include <ure/native_scene_hash.hpp>
#include <ure/native_scene_tooling.hpp>
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

std::string identity_hex(const Identity& identity) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result(identity.size() * 2, '0');
    for (std::size_t index = 0; index < identity.size(); ++index) {
        result[index * 2] = digits[identity[index] >> 4U];
        result[index * 2 + 1] = digits[identity[index] & 15U];
    }
    return result;
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

void realize_result(SceneToolResult& result,
                    native_scene::NativeSceneArchive archive,
                    const SceneToolRequest& request) {
    ProductRealizationLimits limits;
    limits.validation = request.validation;
    limits.temporary_bytes = request.temporary_bytes;
    auto realized = realize_product_scene(std::move(archive), limits);
    result.diagnostics.insert(result.diagnostics.end(),
                              realized.diagnostics.begin(),
                              realized.diagnostics.end());
    if (!realized.snapshot)
        return;
    result.snapshot_identity = realized.snapshot->identity();
    result.semantic_identity = realized.snapshot->semantic_identity();
    result.resource_budget = realized.snapshot->resource_budget();
    result.dispositions = realized.snapshot->feature_dispositions();
    result.resource_count = realized.snapshot->resources().size();
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
                        request.operation == SceneToolOperation::Unpack;
    if (writes && request.output.empty())
        throw std::invalid_argument(
            "Scene-tool publication operation requires output");
    if (request.operation != SceneToolOperation::Pack &&
        request.inputs.size() != 1)
        throw std::invalid_argument(
            "Scene-tool operation accepts exactly one input");
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
    root["schema"] = "ure.preview.scene-tool-report/0.1";
    root["operation"] = operation_name(operation);
    root["ok"] = ok();
    root["snapshot_identity"] = identity_hex(snapshot_identity);
    root["semantic_identity"] = semantic_identity;
    root["inventory"] = {
        {"scenes", scene_count}, {"resources", resource_count},
        {"caches", cache_count}, {"dependencies", dependency_count}};
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
            native_scene::build_native_scene(
                request.inputs.front(), request.output, request.validation);
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
    } catch (const std::invalid_argument& error) {
        result.diagnostics.push_back(diagnostic(
            ProductSceneDiagnosticDomain::Feature, 622,
            "URE-PRV2-TOOL-REQUEST-001", "/request", error.what(),
            "fix the operation input, output, selection, or policy"));
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
