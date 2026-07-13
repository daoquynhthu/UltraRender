#pragma once

#include <cstdint>
#include <span>
#include <string>

#include <ure/native_scene.hpp>

namespace ure::native_scene {

std::string sha256_hex(std::span<const std::uint8_t> bytes);
std::string semantic_hash(const SceneDocument& document);
std::string semantic_hash(const PackageManifest& manifest);

}
