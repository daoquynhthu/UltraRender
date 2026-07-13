#pragma once

#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <ure/native_scene_ir.hpp>

namespace ure::native_scene::detail {

struct ResourceReferenceValue {
    std::string id;
    std::string content_hash;
};

struct EncodedResources {
    std::vector<NamedResourcePayload> payloads;
    std::unordered_map<const Mesh*, ResourceReferenceValue> meshes;
    std::unordered_map<const scene_ir::MiePhaseResource*, ResourceReferenceValue> mie;
};

EncodedResources encode_resources(const NativeSceneArchive& archive);
LoadResult<std::shared_ptr<Mesh>> decode_mesh_payload(std::span<const std::uint8_t> bytes,
                                                      const ValidationLimits& limits);
LoadResult<std::shared_ptr<const scene_ir::MiePhaseResource>> decode_mie_payload(
    std::span<const std::uint8_t> bytes,
    const ValidationLimits& limits);

std::vector<std::uint8_t> encode_scene_graph(const NativeSceneArchive& archive,
                                             const EncodedResources& resources);
LoadResult<NativeSceneArchive> decode_scene_graph(
    const SceneDocument& document,
    std::span<const std::uint8_t> bytes,
    const std::unordered_map<std::string, std::shared_ptr<Mesh>>& meshes,
    const std::unordered_map<std::string, std::shared_ptr<const scene_ir::MiePhaseResource>>& mie,
    const std::unordered_map<std::string, std::string>& resource_hashes,
    const ValidationLimits& limits);

}
