#pragma once

#include <filesystem>
#include <string_view>

#include <ure/native_scene.hpp>

namespace ure::native_scene {

ValidationReport validate_scene_document(const SceneDocument& document,
                                         const CapabilityRegistry& registry,
                                         const ValidationLimits& limits = {});

ValidationReport validate_package_manifest(const PackageManifest& manifest,
                                           const CapabilityRegistry& registry,
                                           const ValidationLimits& limits = {});

ValidationReport validate_exploded_resource_path(const std::filesystem::path& package_root,
                                                 std::string_view resource_uri);

}
