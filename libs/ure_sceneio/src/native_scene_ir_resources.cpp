#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/verifier.h>

#include <ure/native_scene_hash.hpp>

#include "native_scene_ir_internal.hpp"
#include "ure_mesh_v1_generated.h"
#include "ure_mie_v1_generated.h"

namespace ure::native_scene::detail {
namespace {

namespace schema = ure::native::schema;

template <typename T>
LoadResult<T> failure(std::string code, std::string path, std::string message) {
    LoadResult<T> result;
    result.diagnostics.push_back({std::move(code), DiagnosticSeverity::Error, std::move(path),
                                  std::move(message), {}});
    return result;
}

bool finite_vector(const std::vector<float>& values) {
    return std::ranges::all_of(values, [](float value) { return std::isfinite(value); });
}

std::vector<std::uint8_t> encode_mesh(const Mesh& mesh) {
    schema::MeshPayloadT native;
    native.positions.reserve(mesh.vertices.size() * 3);
    native.normals.reserve(mesh.vertices.size() * 3);
    native.uvs.reserve(mesh.vertices.size() * 2);
    native.tangents.reserve(mesh.vertices.size() * 3);
    for (const Vertex& vertex : mesh.vertices) {
        native.positions.insert(native.positions.end(), {vertex.position.x, vertex.position.y, vertex.position.z});
        native.normals.insert(native.normals.end(), {vertex.normal.x, vertex.normal.y, vertex.normal.z});
        native.uvs.insert(native.uvs.end(), {vertex.uv.x, vertex.uv.y});
        native.tangents.insert(native.tangents.end(), {vertex.tangent.x, vertex.tangent.y, vertex.tangent.z});
    }
    native.indices.reserve(mesh.indices.size());
    for (int index : mesh.indices) native.indices.push_back(static_cast<std::int32_t>(index));
    flatbuffers::FlatBufferBuilder builder;
    schema::FinishMeshPayloadBuffer(builder, schema::MeshPayload::Pack(builder, &native));
    return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

std::vector<std::uint8_t> encode_mie(const scene_ir::MiePhaseResource& resource) {
    schema::MiePayloadT native;
    native.wavelengths_nm = resource.wavelengths_nm;
    native.cos_theta = resource.cos_theta;
    native.phase = resource.phase;
    native.cdf = resource.cdf;
    native.scattering_cross_section_m2 = resource.scattering_cross_section_m2;
    native.extinction_cross_section_m2 = resource.extinction_cross_section_m2;
    native.absorption_cross_section_m2 = resource.absorption_cross_section_m2;
    native.asymmetry = resource.asymmetry;
    native.polarization_model = schema::MiePolarizationModel::ScalarDepolarizing;
    native.provenance = resource.provenance;
    native.source_hash = resource.source_hash;
    native.content_hash = resource.content_hash;
    flatbuffers::FlatBufferBuilder builder;
    schema::FinishMiePayloadBuffer(builder, schema::MiePayload::Pack(builder, &native));
    return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

NamedResourcePayload resource_payload(ResourceKind kind,
                                      std::string_view prefix,
                                      std::string_view extension,
                                      std::vector<std::uint8_t> bytes) {
    NamedResourcePayload result;
    result.payload = std::move(bytes);
    result.descriptor.content_hash = sha256_hex(result.payload);
    result.id = std::string(prefix) + "/" + result.descriptor.content_hash;
    result.descriptor.id = result.id;
    result.descriptor.kind = kind;
    result.descriptor.schema_version = {1, 0};
    result.descriptor.uri = "resources/" + result.id + std::string(extension);
    result.descriptor.byte_length = result.payload.size();
    result.descriptor.resident_bytes = result.payload.size();
    return result;
}

bool valid_mie(const scene_ir::MiePhaseResource& value) {
    const std::size_t wavelengths = value.wavelengths_nm.size();
    const std::size_t angles = value.cos_theta.size();
    if (wavelengths < 2 || angles < 2 || wavelengths > std::numeric_limits<std::size_t>::max() / angles) return false;
    const std::size_t table = wavelengths * angles;
    if (value.phase.size() != table || value.cdf.size() != table ||
        value.scattering_cross_section_m2.size() != wavelengths ||
        value.extinction_cross_section_m2.size() != wavelengths ||
        value.absorption_cross_section_m2.size() != wavelengths || value.asymmetry.size() != wavelengths) return false;
    if (!finite_vector(value.wavelengths_nm) || !finite_vector(value.cos_theta) ||
        !finite_vector(value.phase) || !finite_vector(value.cdf) ||
        !finite_vector(value.scattering_cross_section_m2) || !finite_vector(value.extinction_cross_section_m2) ||
        !finite_vector(value.absorption_cross_section_m2) || !finite_vector(value.asymmetry)) return false;
    return value.polarization_model == scene_ir::MiePolarizationModel::ScalarDepolarizing;
}

}

EncodedResources encode_resources(const NativeSceneArchive& archive) {
    EncodedResources result;
    std::unordered_map<std::string, std::size_t> by_id;
    auto insert = [&](NamedResourcePayload payload) -> ResourceReferenceValue {
        const ResourceReferenceValue reference{payload.id, payload.descriptor.content_hash};
        if (!by_id.contains(payload.id)) {
            by_id.emplace(payload.id, result.payloads.size());
            result.payloads.push_back(std::move(payload));
        }
        return reference;
    };
    for (const auto& record : archive.scene.meshes) {
        if (record && record->mesh && !result.meshes.contains(record->mesh.get())) {
            result.meshes.emplace(record->mesh.get(), insert(resource_payload(
                ResourceKind::Geometry, "mesh", ".urmesh", encode_mesh(*record->mesh))));
        }
    }
    auto add_mie = [&](const std::shared_ptr<const scene_ir::MiePhaseResource>& resource) {
        if (resource && !result.mie.contains(resource.get())) {
            result.mie.emplace(resource.get(), insert(resource_payload(
                ResourceKind::MiePhase, "mie", ".urmie", encode_mie(*resource))));
        }
    };
    add_mie(archive.scene.medium_mie_resource);
    for (const auto& material : archive.scene.materials) {
        if (material) add_mie(material->medium_mie_resource);
    }
    std::ranges::sort(result.payloads, {}, &NamedResourcePayload::id);
    return result;
}

LoadResult<std::shared_ptr<Mesh>> decode_mesh_payload(std::span<const std::uint8_t> bytes,
                                                      const ValidationLimits& limits) {
    if (bytes.size() > limits.max_resident_resource_bytes) {
        return failure<std::shared_ptr<Mesh>>("URE-Q3-BUDGET-001", "mesh", "Mesh payload exceeds resource budget");
    }
    const auto max_tables = static_cast<flatbuffers::uoffset_t>(std::min<std::uint64_t>(
        limits.max_object_count, std::numeric_limits<flatbuffers::uoffset_t>::max()));
    flatbuffers::Verifier verifier(bytes.data(), bytes.size(), limits.max_nesting_depth, max_tables);
    if (!schema::VerifyMeshPayloadBuffer(verifier)) {
        return failure<std::shared_ptr<Mesh>>("URE-Q3-MESH-001", "mesh", "Invalid URMS payload");
    }
    std::unique_ptr<schema::MeshPayloadT> native(schema::GetMeshPayload(bytes.data())->UnPack());
    if (native->positions.size() % 3 != 0 || native->normals.size() != native->positions.size() ||
        native->tangents.size() != native->positions.size() ||
        native->uvs.size() / 2 != native->positions.size() / 3 || native->uvs.size() % 2 != 0 ||
        native->indices.size() % 3 != 0 || !finite_vector(native->positions) || !finite_vector(native->normals) ||
        !finite_vector(native->tangents) || !finite_vector(native->uvs)) {
        return failure<std::shared_ptr<Mesh>>("URE-Q3-MESH-002", "mesh", "Invalid mesh structure or scalar");
    }
    auto mesh = std::make_shared<Mesh>();
    const std::size_t count = native->positions.size() / 3;
    mesh->vertices.resize(count);
    for (std::size_t index = 0; index < count; ++index) {
        mesh->vertices[index].position = {native->positions[index * 3], native->positions[index * 3 + 1], native->positions[index * 3 + 2]};
        mesh->vertices[index].normal = {native->normals[index * 3], native->normals[index * 3 + 1], native->normals[index * 3 + 2]};
        mesh->vertices[index].uv = {native->uvs[index * 2], native->uvs[index * 2 + 1]};
        mesh->vertices[index].tangent = {native->tangents[index * 3], native->tangents[index * 3 + 1], native->tangents[index * 3 + 2]};
    }
    mesh->indices.reserve(native->indices.size());
    for (std::int32_t index : native->indices) {
        if (index < 0 || static_cast<std::size_t>(index) >= count) {
            return failure<std::shared_ptr<Mesh>>("URE-Q3-MESH-003", "mesh.indices", "Mesh index is out of range");
        }
        mesh->indices.push_back(index);
    }
    LoadResult<std::shared_ptr<Mesh>> result;
    result.value = std::move(mesh);
    return result;
}

LoadResult<std::shared_ptr<const scene_ir::MiePhaseResource>> decode_mie_payload(
    std::span<const std::uint8_t> bytes,
    const ValidationLimits& limits) {
    if (bytes.size() > limits.max_resident_resource_bytes) {
        return failure<std::shared_ptr<const scene_ir::MiePhaseResource>>(
            "URE-Q3-BUDGET-001", "mie", "Mie payload exceeds resource budget");
    }
    const auto max_tables = static_cast<flatbuffers::uoffset_t>(std::min<std::uint64_t>(
        limits.max_object_count, std::numeric_limits<flatbuffers::uoffset_t>::max()));
    flatbuffers::Verifier verifier(bytes.data(), bytes.size(), limits.max_nesting_depth, max_tables);
    if (!schema::VerifyMiePayloadBuffer(verifier)) {
        return failure<std::shared_ptr<const scene_ir::MiePhaseResource>>(
            "URE-Q3-MIE-001", "mie", "Invalid URMI payload");
    }
    std::unique_ptr<schema::MiePayloadT> native(schema::GetMiePayload(bytes.data())->UnPack());
    auto resource = std::make_shared<scene_ir::MiePhaseResource>();
    resource->wavelengths_nm = std::move(native->wavelengths_nm);
    resource->cos_theta = std::move(native->cos_theta);
    resource->phase = std::move(native->phase);
    resource->cdf = std::move(native->cdf);
    resource->scattering_cross_section_m2 = std::move(native->scattering_cross_section_m2);
    resource->extinction_cross_section_m2 = std::move(native->extinction_cross_section_m2);
    resource->absorption_cross_section_m2 = std::move(native->absorption_cross_section_m2);
    resource->asymmetry = std::move(native->asymmetry);
    resource->provenance = std::move(native->provenance);
    resource->source_hash = std::move(native->source_hash);
    resource->content_hash = std::move(native->content_hash);
    if (native->polarization_model != schema::MiePolarizationModel::ScalarDepolarizing || !valid_mie(*resource)) {
        return failure<std::shared_ptr<const scene_ir::MiePhaseResource>>(
            "URE-Q3-MIE-002", "mie", "Invalid Mie table structure or scalar");
    }
    LoadResult<std::shared_ptr<const scene_ir::MiePhaseResource>> result;
    result.value = std::move(resource);
    return result;
}

}
