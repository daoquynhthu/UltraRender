#include <algorithm>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>

#include <flatbuffers/flatbuffers.h>

#include <ure/native_scene_metadata.hpp>
#include <ure/native_scene_validation.hpp>

#include "ure_native_v1_generated.h"

namespace ure::native_scene {
namespace {

namespace fb = ure::native::fb;

fb::Version fb_version(Version version) {
    return {version.major, version.minor};
}

Version native_version(const fb::Version* version) {
    return version ? Version{version->major(), version->minor()} : Version{0, 0};
}

fb::RequirementLevel fb_requirement(RequirementLevel level) {
    return static_cast<fb::RequirementLevel>(static_cast<std::uint8_t>(level));
}

RequirementLevel native_requirement(fb::RequirementLevel level) {
    return static_cast<RequirementLevel>(static_cast<std::uint8_t>(level));
}

std::string string_value(const flatbuffers::String* value) {
    return value ? value->str() : std::string{};
}

std::vector<std::string> string_vector(const flatbuffers::Vector<flatbuffers::Offset<flatbuffers::String>>* values) {
    std::vector<std::string> result;
    if (!values) return result;
    result.reserve(values->size());
    for (const auto* value : *values) result.push_back(string_value(value));
    return result;
}

flatbuffers::Offset<fb::Resource> encode_resource(flatbuffers::FlatBufferBuilder& builder,
                                                 const ResourceDescriptor& resource) {
    std::vector<flatbuffers::Offset<flatbuffers::String>> dependencies;
    dependencies.reserve(resource.dependencies.size());
    for (const auto& dependency : resource.dependencies) dependencies.push_back(builder.CreateString(dependency));
    const auto dependency_vector = builder.CreateVector(dependencies);
    const fb::Version version = fb_version(resource.schema_version);
    return fb::CreateResource(builder, resource.byte_length, builder.CreateString(resource.content_hash),
                              dependency_vector, builder.CreateString(resource.id),
                              static_cast<std::uint32_t>(resource.kind), resource.resident_bytes,
                              &version, builder.CreateString(resource.uri));
}

ResourceDescriptor decode_resource(const fb::Resource& resource) {
    ResourceDescriptor result;
    result.byte_length = resource.byte_length();
    result.content_hash = string_value(resource.content_hash());
    result.dependencies = string_vector(resource.dependencies());
    result.id = string_value(resource.id());
    result.kind = static_cast<ResourceKind>(resource.kind());
    result.resident_bytes = resource.resident_bytes();
    result.schema_version = native_version(resource.schema_version());
    result.uri = string_value(resource.uri());
    return result;
}

template <typename T>
LoadResult<T> metadata_error(std::string message) {
    LoadResult<T> result;
    result.diagnostics.push_back({"URE-Q-METADATA-001", DiagnosticSeverity::Error, "/metadata", std::move(message), {}});
    return result;
}

template <typename T>
void append_validation(LoadResult<T>& result, ValidationReport validation) {
    result.diagnostics.insert(result.diagnostics.end(),
                              std::make_move_iterator(validation.diagnostics.begin()),
                              std::make_move_iterator(validation.diagnostics.end()));
}

bool verify_metadata(std::span<const std::uint8_t> bytes, const ValidationLimits& limits) {
    if (bytes.size() < 8 || !fb::MetadataEnvelopeBufferHasIdentifier(bytes.data())) return false;
    const auto max_tables = static_cast<flatbuffers::uoffset_t>(std::min<std::uint64_t>(
        limits.max_directory_entries,
        std::numeric_limits<flatbuffers::uoffset_t>::max()));
    flatbuffers::Verifier verifier(bytes.data(), bytes.size(), 64, max_tables);
    return fb::VerifyMetadataEnvelopeBuffer(verifier);
}

}

bool metadata_buffer_has_identifier(std::span<const std::uint8_t> bytes) {
    return bytes.size() >= 8 && fb::MetadataEnvelopeBufferHasIdentifier(bytes.data());
}

std::vector<std::uint8_t> encode_scene_metadata(const SceneDocument& document) {
    flatbuffers::FlatBufferBuilder builder;
    auto sorted_features = document.features;
    auto sorted_extensions = document.extensions;
    auto sorted_resources = document.resources;
    std::ranges::sort(sorted_features, {}, &FeatureDeclaration::name);
    std::ranges::sort(sorted_extensions, {}, &ExtensionRecord::name);
    std::ranges::sort(sorted_resources, {}, &ResourceDescriptor::id);
    const auto conventions = fb::CreateConventionsDirect(
        builder, document.conventions.angle_unit.c_str(), document.conventions.camera_forward.c_str(),
        document.conventions.color_encoding.c_str(), document.conventions.handedness.c_str(),
        document.conventions.length_unit.c_str(), document.conventions.mass_unit.c_str(),
        document.conventions.time_unit.c_str(), document.conventions.up_axis.c_str(),
        document.conventions.wavelength_unit.c_str());

    std::vector<flatbuffers::Offset<fb::Feature>> features;
    features.reserve(sorted_features.size());
    for (const auto& feature : sorted_features) {
        std::vector<flatbuffers::Offset<flatbuffers::String>> dependencies;
        dependencies.reserve(feature.dependencies.size());
        for (const auto& dependency : feature.dependencies) dependencies.push_back(builder.CreateString(dependency));
        const auto dependency_vector = builder.CreateVector(dependencies);
        const fb::Version version = fb_version(feature.minimum_version);
        features.push_back(fb::CreateFeature(builder, dependency_vector, &version,
                                             builder.CreateString(feature.name),
                                             builder.CreateString(feature.canonical_parameters),
                                             builder.CreateString(feature.provider),
                                             fb_requirement(feature.requirement)));
    }

    std::vector<flatbuffers::Offset<fb::Extension>> extensions;
    extensions.reserve(sorted_extensions.size());
    for (const auto& extension : sorted_extensions) {
        const fb::Version version = fb_version(extension.version);
        extensions.push_back(fb::CreateExtension(builder, builder.CreateString(extension.name),
                                                 builder.CreateVector(extension.opaque_payload),
                                                 builder.CreateString(extension.payload_type),
                                                 fb_requirement(extension.requirement), &version));
    }

    std::vector<flatbuffers::Offset<fb::Resource>> resources;
    resources.reserve(sorted_resources.size());
    for (const auto& resource : sorted_resources) resources.push_back(encode_resource(builder, resource));

    std::vector<flatbuffers::Offset<fb::Migration>> migrations;
    migrations.reserve(document.migrations.size());
    for (const auto& migration : document.migrations) {
        const fb::Version source_version = fb_version(migration.source_version);
        const fb::Version target_version = fb_version(migration.target_version);
        const fb::Version tool_version = fb_version(migration.tool_version);
        migrations.push_back(fb::CreateMigration(builder, builder.CreateString(migration.input_hash), migration.lossy,
                                                 builder.CreateString(migration.output_hash), &source_version,
                                                 &target_version, builder.CreateString(migration.tool_id), &tool_version));
    }

    const fb::Version schema_version = fb_version(document.schema_version);
    const auto scene = fb::CreateSceneMetadata(builder, conventions, builder.CreateVector(extensions),
                                               builder.CreateVector(features), builder.CreateString(document.id),
                                               builder.CreateVector(migrations), builder.CreateVector(resources),
                                               &schema_version);
    const auto envelope = fb::CreateMetadataEnvelope(builder, fb::MetadataKind::Scene, 0, scene);
    fb::FinishMetadataEnvelopeBuffer(builder, envelope);
    return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

LoadResult<SceneDocument> decode_scene_metadata(std::span<const std::uint8_t> bytes,
                                                const CapabilityRegistry& registry,
                                                const ValidationLimits& limits) {
    if (!verify_metadata(bytes, limits)) return metadata_error<SceneDocument>("FlatBuffers verification failed");
    const fb::MetadataEnvelope* envelope = fb::GetMetadataEnvelope(bytes.data());
    if (envelope->kind() != fb::MetadataKind::Scene || !envelope->scene() || envelope->package()) {
        return metadata_error<SceneDocument>("Metadata envelope is not a scene");
    }
    const fb::SceneMetadata& source = *envelope->scene();
    SceneDocument document;
    document.id = string_value(source.id());
    document.schema_version = native_version(source.schema_version());
    if (source.conventions()) {
        const auto& value = *source.conventions();
        document.conventions.angle_unit = string_value(value.angle_unit());
        document.conventions.camera_forward = string_value(value.camera_forward());
        document.conventions.color_encoding = string_value(value.color_encoding());
        document.conventions.handedness = string_value(value.handedness());
        document.conventions.length_unit = string_value(value.length_unit());
        document.conventions.mass_unit = string_value(value.mass_unit());
        document.conventions.time_unit = string_value(value.time_unit());
        document.conventions.up_axis = string_value(value.up_axis());
        document.conventions.wavelength_unit = string_value(value.wavelength_unit());
    }
    if (source.features()) {
        document.features.reserve(source.features()->size());
        for (const auto* value : *source.features()) {
            document.features.push_back({string_value(value->name()), native_version(value->minimum_version()),
                                         native_requirement(value->requirement()), string_value(value->provider()),
                                         string_vector(value->dependencies()), string_value(value->parameters())});
        }
    }
    if (source.extensions()) {
        document.extensions.reserve(source.extensions()->size());
        for (const auto* value : *source.extensions()) {
            ExtensionRecord extension;
            extension.name = string_value(value->name());
            extension.version = native_version(value->version());
            extension.requirement = native_requirement(value->requirement());
            extension.payload_type = string_value(value->payload_type());
            if (value->opaque_payload()) extension.opaque_payload.assign(value->opaque_payload()->begin(), value->opaque_payload()->end());
            document.extensions.push_back(std::move(extension));
        }
    }
    if (source.resources()) {
        document.resources.reserve(source.resources()->size());
        for (const auto* value : *source.resources()) document.resources.push_back(decode_resource(*value));
    }
    if (source.migrations()) {
        document.migrations.reserve(source.migrations()->size());
        for (const auto* value : *source.migrations()) {
            document.migrations.push_back({native_version(value->source_version()), native_version(value->target_version()),
                                           string_value(value->tool_id()), native_version(value->tool_version()),
                                           string_value(value->input_hash()), string_value(value->output_hash()),
                                           value->lossy()});
        }
    }
    LoadResult<SceneDocument> result;
    result.value = std::move(document);
    append_validation(result, validate_scene_document(*result.value, registry, limits));
    return result;
}

std::vector<std::uint8_t> encode_package_metadata(const PackageManifest& manifest) {
    flatbuffers::FlatBufferBuilder builder;
    auto sorted_resources = manifest.resources;
    auto sorted_caches = manifest.caches;
    auto sorted_scenes = manifest.scenes;
    auto sorted_dependencies = manifest.dependencies;
    std::ranges::sort(sorted_resources, {}, &ResourceDescriptor::id);
    std::ranges::sort(sorted_caches, {}, &ResourceDescriptor::id);
    std::ranges::sort(sorted_scenes, {}, &SceneReference::id);
    std::ranges::sort(sorted_dependencies, {}, &PackageDependency::package_id);
    std::vector<flatbuffers::Offset<fb::Resource>> resources;
    for (const auto& resource : sorted_resources) resources.push_back(encode_resource(builder, resource));
    std::vector<flatbuffers::Offset<fb::Resource>> caches;
    for (const auto& cache : sorted_caches) caches.push_back(encode_resource(builder, cache));
    std::vector<flatbuffers::Offset<fb::SceneReference>> scenes;
    for (const auto& scene : sorted_scenes) {
        scenes.push_back(fb::CreateSceneReferenceDirect(builder, scene.content_hash.c_str(), scene.id.c_str(), scene.uri.c_str()));
    }
    std::vector<flatbuffers::Offset<fb::PackageDependency>> dependencies;
    for (const auto& dependency : sorted_dependencies) {
        dependencies.push_back(fb::CreatePackageDependencyDirect(builder, dependency.manifest_hash.c_str(), dependency.package_id.c_str()));
    }
    const fb::Version format_version = fb_version(manifest.format_version);
    const auto package = fb::CreatePackageMetadata(builder, builder.CreateVector(caches), builder.CreateVector(dependencies),
                                                   &format_version, builder.CreateString(manifest.id),
                                                   builder.CreateVector(resources), builder.CreateVector(scenes));
    const auto envelope = fb::CreateMetadataEnvelope(builder, fb::MetadataKind::Package, package, 0);
    fb::FinishMetadataEnvelopeBuffer(builder, envelope);
    return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

LoadResult<PackageManifest> decode_package_metadata(std::span<const std::uint8_t> bytes,
                                                    const CapabilityRegistry& registry,
                                                    const ValidationLimits& limits) {
    if (!verify_metadata(bytes, limits)) return metadata_error<PackageManifest>("FlatBuffers verification failed");
    const fb::MetadataEnvelope* envelope = fb::GetMetadataEnvelope(bytes.data());
    if (envelope->kind() != fb::MetadataKind::Package || !envelope->package() || envelope->scene()) {
        return metadata_error<PackageManifest>("Metadata envelope is not a package");
    }
    const fb::PackageMetadata& source = *envelope->package();
    PackageManifest manifest;
    manifest.id = string_value(source.id());
    manifest.format_version = native_version(source.format_version());
    if (source.scenes()) {
        for (const auto* value : *source.scenes()) {
            manifest.scenes.push_back({string_value(value->id()), string_value(value->content_hash()), string_value(value->uri())});
        }
    }
    if (source.resources()) for (const auto* value : *source.resources()) manifest.resources.push_back(decode_resource(*value));
    if (source.caches()) for (const auto* value : *source.caches()) manifest.caches.push_back(decode_resource(*value));
    if (source.dependencies()) {
        for (const auto* value : *source.dependencies()) {
            manifest.dependencies.push_back({string_value(value->package_id()), string_value(value->manifest_hash())});
        }
    }
    LoadResult<PackageManifest> result;
    result.value = std::move(manifest);
    append_validation(result, validate_package_manifest(*result.value, registry, limits));
    return result;
}

}
