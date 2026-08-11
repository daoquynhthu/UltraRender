#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <ure/native_scene_ir.hpp>

namespace ure::native_scene {

enum class AdapterFormat : std::uint8_t { Gltf, Usd, MaterialX };
enum class AdapterLossSeverity : std::uint8_t { Information, Warning, Error };
enum class UsdExportPolicy : std::uint8_t {
    Strict,
    AllowDocumentedLoss
};

struct AdapterLoss {
    std::string code;
    AdapterLossSeverity severity = AdapterLossSeverity::Error;
    std::string native_path;
    std::string feature;
    std::string message;
    std::string remediation;
};

struct AdapterLossReport {
    AdapterFormat format = AdapterFormat::Gltf;
    std::vector<AdapterLoss> losses;

    bool lossless() const;
    bool exportable() const;
};

struct NativeAdapterResult {
    NativeSceneArchive archive;
    AdapterLossReport loss_report;
    std::vector<ValidationDiagnostic> diagnostics;

    bool ok() const;
};

struct MaterialXAdapterResult {
    scene_ir::MaterialGraph graph;
    AdapterLossReport loss_report{AdapterFormat::MaterialX, {}};
    std::vector<ValidationDiagnostic> diagnostics;

    bool ok() const;
};

struct UsdExportMapping {
    std::string native_path;
    std::string usd_path;
};

struct UsdExportResult {
    std::string usda;
    AdapterLossReport loss_report{AdapterFormat::Usd, {}};
    std::vector<ValidationDiagnostic> diagnostics;
    std::vector<UsdExportMapping> mappings;
    UsdExportPolicy policy = UsdExportPolicy::Strict;

    bool ok() const;
};

NativeAdapterResult import_gltf_native(const std::filesystem::path& path,
                                       const ValidationLimits& limits = {});
AdapterLossReport assess_native_export(const NativeSceneArchive& archive, AdapterFormat format);
MaterialXAdapterResult import_materialx_native(std::string_view xml);
std::string export_materialx_native(const scene_ir::MaterialGraph& graph,
                                    AdapterLossReport& loss_report,
                                    std::string_view material_name = "UREMaterial");
UsdExportResult export_usda_native(
    const NativeSceneArchive& archive,
    UsdExportPolicy policy = UsdExportPolicy::Strict,
    const ValidationLimits& limits = {});
void save_usda_native(
    const std::filesystem::path& path,
    const NativeSceneArchive& archive,
    UsdExportPolicy policy = UsdExportPolicy::Strict,
    const std::filesystem::path& loss_report_path = {},
    const ValidationLimits& limits = {});
std::string write_adapter_loss_report(const AdapterLossReport& report);

}
