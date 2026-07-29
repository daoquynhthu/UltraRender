#include <algorithm>
#include <filesystem>
#include <ranges>
#include <string>

#include <nlohmann/json.hpp>

#include <ure/native_adapter.hpp>
#include <ure/materialx_io.hpp>
#include <ure/native_scene_validation.hpp>
#include <ure/scene_frontend.hpp>

namespace ure::native_scene {
namespace {

std::string format_name(AdapterFormat format) {
    switch (format) {
    case AdapterFormat::Gltf: return "gltf";
    case AdapterFormat::Usd: return "usd";
    case AdapterFormat::MaterialX: return "materialx";
    }
    return "unknown";
}

std::string severity_name(AdapterLossSeverity severity) {
    switch (severity) {
    case AdapterLossSeverity::Information: return "information";
    case AdapterLossSeverity::Warning: return "warning";
    case AdapterLossSeverity::Error: return "error";
    }
    return "error";
}

void add_loss(AdapterLossReport& report, std::string code, std::string path,
              std::string feature, std::string message, std::string remediation,
              AdapterLossSeverity severity = AdapterLossSeverity::Error) {
    report.losses.push_back({std::move(code), severity, std::move(path), std::move(feature),
                             std::move(message), std::move(remediation)});
}

}

bool AdapterLossReport::lossless() const { return losses.empty(); }

bool AdapterLossReport::exportable() const {
    return std::ranges::none_of(losses, [](const auto& loss) { return loss.severity == AdapterLossSeverity::Error; });
}

bool NativeAdapterResult::ok() const {
    return std::ranges::none_of(diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Error;
    });
}

bool MaterialXAdapterResult::ok() const {
    return std::ranges::none_of(diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Error;
    });
}

NativeAdapterResult import_gltf_native(const std::filesystem::path& path,
                                       const ValidationLimits& limits) {
    NativeAdapterResult result;
    result.loss_report.format = AdapterFormat::Gltf;
    try {
        auto scene = SceneFrontend::parse_file_to_ir(path.string());
        SceneDocument document;
        document.id = path.stem().string();
        document.schema_version = kSceneSchemaVersion;
        result.archive = make_native_scene_archive(std::move(document), scene);
        const auto validation = validate_scene_ir_archive(result.archive, limits);
        result.diagnostics = validation.diagnostics;
        if (scene.physics.enabled) {
            add_loss(result.loss_report, "URE-Q10-GLTF-001", "/physics", "ure.scene.simulation",
                     "glTF physics metadata was imported as adapter provenance and is not an authoritative native simulation contract",
                     "Author the simulation contract in URE native schema", AdapterLossSeverity::Warning);
        }
    } catch (const std::exception& error) {
        result.diagnostics.push_back({"URE-Q10-IMPORT-001", DiagnosticSeverity::Error, path.string(), error.what(), {}});
    }
    return result;
}

AdapterLossReport assess_native_export(const NativeSceneArchive& archive, AdapterFormat format) {
    AdapterLossReport report;
    report.format = format;
    if (format == AdapterFormat::Usd) {
        add_loss(report, "URE-Q10-USD-001", "/", "ure.adapter.usd",
                 "USD export is reserved for Phase U.6 and cannot currently serialize native scenes",
                 "Retain .ure/.urescene as the authoritative source or use the U.1 import schema adapter");
        return report;
    }
    if (archive.procedural_graph) add_loss(report, "URE-Q10-LOSS-001", "/procedural_graph", "ure.scene.procedural", "Target format cannot preserve the deterministic procedural graph", "Bake generated geometry or retain URE native source");
    if (archive.solver_contract) add_loss(report, "URE-Q10-LOSS-002", "/solver_contract", "ure.render.solver", "Target format cannot preserve the native solver request", "Store the solver contract in URE native source");
    if (archive.simulation_contract) add_loss(report, "URE-Q10-LOSS-003", "/simulation_contract", "ure.scene.simulation", "Target format cannot preserve the native simulation and coupling contract", "Store simulation domains in URE native source");
    if (archive.resource_catalog) add_loss(report, "URE-Q10-LOSS-004", "/resource_catalog", "ure.scene.resource", "Target format cannot preserve the complete typed spectral/resource catalog", "Export only selected exchange resources and retain URE ownership");
    if (format == AdapterFormat::Gltf) {
        if (!archive.scene.spheres.empty()) add_loss(report, "URE-Q10-GLTF-002", "/scene/spheres", "ure.geometry.analytic", "glTF has no authoritative analytic sphere primitive", "Tessellate explicitly or reject export");
        if (archive.scene.physics.enabled) add_loss(report, "URE-Q10-GLTF-003", "/scene/physics", "ure.scene.simulation", "glTF cannot preserve native physics semantics", "Retain URE simulation contract");
        if (std::ranges::any_of(archive.scene.materials, [](const auto& material) { return material && (material->graph || material->medium_mie_resource); })) {
            add_loss(report, "URE-Q10-GLTF-004", "/scene/materials", "ure.material.graph", "glTF can express only the compatible PBR subset of native material and medium graphs", "Bake or export a declared approximation with this loss report");
        }
    }
    return report;
}

MaterialXAdapterResult import_materialx_native(std::string_view xml) {
    MaterialXAdapterResult result;
    try {
        result.graph = io::import_materialx_graph(xml);
        result.graph.validate();
    } catch (const std::exception& error) {
        result.diagnostics.push_back({"URE-Q10-MTLX-001", DiagnosticSeverity::Error,
            "/material", error.what(), "Use the supported URE MaterialX node subset"});
    }
    return result;
}

std::string export_materialx_native(const scene_ir::MaterialGraph& graph,
                                    AdapterLossReport& loss_report,
                                    std::string_view material_name) {
    loss_report = AdapterLossReport{AdapterFormat::MaterialX};
    graph.validate();
    return io::export_materialx_graph(graph, material_name);
}

std::string write_adapter_loss_report(const AdapterLossReport& report) {
    nlohmann::ordered_json root;
    root["schema"] = "ure.adapter.loss/1.0";
    root["target"] = format_name(report.format);
    root["lossless"] = report.lossless();
    root["exportable"] = report.exportable();
    root["losses"] = nlohmann::ordered_json::array();
    for (const auto& loss : report.losses) {
        root["losses"].push_back({{"code", loss.code}, {"severity", severity_name(loss.severity)},
            {"native_path", loss.native_path}, {"feature", loss.feature}, {"message", loss.message},
            {"remediation", loss.remediation}});
    }
    return root.dump(2) + "\n";
}

}
