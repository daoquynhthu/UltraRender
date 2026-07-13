#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <ure/native_scene.hpp>

namespace ure::native_scene {

struct ContainerChunk {
    std::string id;
    std::uint32_t type = 0;
    Version schema_version;
    RequirementLevel requirement = RequirementLevel::Required;
    std::uint32_t codec = static_cast<std::uint32_t>(CompressionCodec::None);
    std::uint64_t alignment = 1;
    std::string extension_owner;
    std::vector<std::string> dependencies;
    std::vector<std::uint8_t> payload;
    std::uint64_t uncompressed_size = 0;
};

struct NativeContainer {
    ContainerKind kind = ContainerKind::Scene;
    Version container_version;
    std::array<std::uint8_t, 16> document_uuid{};
    std::string semantic_hash;
    std::uint32_t flags = 0;
    std::vector<ContainerChunk> chunks;
};

std::vector<std::uint8_t> write_container(const NativeContainer& container);
LoadResult<NativeContainer> read_container(std::span<const std::uint8_t> bytes,
                                           const CapabilityRegistry& registry,
                                           const ValidationLimits& limits = {});

std::vector<std::uint8_t> write_scene_binary(const SceneDocument& document,
                                             std::vector<ContainerChunk> extra_chunks = {});
LoadResult<SceneDocument> read_scene_binary(std::span<const std::uint8_t> bytes,
                                           const CapabilityRegistry& registry,
                                           const ValidationLimits& limits = {});

std::vector<std::uint8_t> write_package_binary(const PackageManifest& manifest,
                                               std::vector<ContainerChunk> extra_chunks = {});
LoadResult<PackageManifest> read_package_binary(std::span<const std::uint8_t> bytes,
                                               const CapabilityRegistry& registry,
                                               const ValidationLimits& limits = {});

}
