#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <ure/native_adapter.hpp>

namespace ure::native_scene {

struct NativeInspection {
    ContainerKind kind = ContainerKind::Scene;
    std::string id;
    Version version;
    std::string semantic_hash;
    std::uint64_t stored_bytes = 0;
    std::uint64_t resident_bytes = 0;
    std::size_t scene_count = 0;
    std::size_t resource_count = 0;
    std::vector<ValidationDiagnostic> diagnostics;

    bool ok() const;
};

CapabilityRegistry native_tool_capabilities();
LoadResult<NativeSceneArchive> load_native_asset(const std::filesystem::path& path,
                                                 const ValidationLimits& limits = {});
LoadResult<NativeSceneArchive> load_native_package_scene(
    const std::filesystem::path& path,
    std::string_view scene_id,
    const ValidationLimits& limits = {});
NativeInspection inspect_native_asset(const std::filesystem::path& path,
                                      const ValidationLimits& limits = {});
void build_native_scene(const std::filesystem::path& input,
                        const std::filesystem::path& output,
                        const ValidationLimits& limits = {});
void pack_native_scenes(const std::filesystem::path& output,
                        const std::vector<std::filesystem::path>& inputs,
                        const ValidationLimits& limits = {});
void unpack_native_package(const std::filesystem::path& input,
                           const std::filesystem::path& output_directory,
                           const ValidationLimits& limits = {});
void migrate_native_scene(const std::filesystem::path& input,
                          const std::filesystem::path& output,
                          const ValidationLimits& limits = {});
void export_native_scene_usda(
    const std::filesystem::path& input,
    const std::filesystem::path& output,
    UsdExportPolicy policy = UsdExportPolicy::Strict,
    const std::filesystem::path& loss_report_path = {},
    std::string_view scene_id = {},
    const ValidationLimits& limits = {});

}
