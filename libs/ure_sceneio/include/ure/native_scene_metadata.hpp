#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <ure/native_scene.hpp>

namespace ure::native_scene {

std::vector<std::uint8_t> encode_scene_metadata(const SceneDocument& document);
LoadResult<SceneDocument> decode_scene_metadata(std::span<const std::uint8_t> bytes,
                                                const CapabilityRegistry& registry,
                                                const ValidationLimits& limits = {});

std::vector<std::uint8_t> encode_package_metadata(const PackageManifest& manifest);
LoadResult<PackageManifest> decode_package_metadata(std::span<const std::uint8_t> bytes,
                                                    const CapabilityRegistry& registry,
                                                    const ValidationLimits& limits = {});

bool metadata_buffer_has_identifier(std::span<const std::uint8_t> bytes);

}
