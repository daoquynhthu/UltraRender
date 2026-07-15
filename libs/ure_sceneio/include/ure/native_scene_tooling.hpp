#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <ure/native_scene_ir.hpp>

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

}
