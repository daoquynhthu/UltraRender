#pragma once

#include <string>
#include <string_view>

#include <ure/native_scene.hpp>

namespace ure::native_scene {

std::string write_scene_text(const SceneDocument& document);
LoadResult<SceneDocument> read_scene_text(std::string_view text,
                                         const CapabilityRegistry& registry,
                                         const ValidationLimits& limits = {});

std::string write_package_text(const PackageManifest& manifest);
LoadResult<PackageManifest> read_package_text(std::string_view text,
                                             const CapabilityRegistry& registry,
                                             const ValidationLimits& limits = {});

}
