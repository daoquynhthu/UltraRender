#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <atomic>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <ure/native_scene_hash.hpp>
#include <ure/native_script_build.hpp>
#include <ure/native_scene_text.hpp>
#include <ure/native_scene_tooling.hpp>
#include <ure/native_scene_validation.hpp>
#include <ure/native_resource_resolution.hpp>
#include <ure/native_procedural_graph.hpp>

namespace ure::native_scene {
namespace {

constexpr std::string_view kEmbeddedScenePrefix = "scene/";
constexpr std::string_view kEmbeddedResourcePrefix = "resource/";
constexpr std::string_view kPackageProvenanceId =
    "provenance/package-resource-map";
constexpr std::string_view kPackageProvenanceSchema =
    "ure.package-provenance/1.0";

struct PackageResourceBinding {
    std::string scene_id;
    std::string logical_id;
    std::string source_uri;
    std::string content_hash;
    ResourceKind kind{ResourceKind::Extension};
    std::vector<std::string> dependencies;
};

std::vector<std::uint8_t> read_file(const std::filesystem::path& path, std::uint64_t limit) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > limit || size > std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error("Native asset is unavailable or exceeds the read budget");
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    std::ifstream input(path, std::ios::binary);
    if (!input || (!bytes.empty() && !input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))) {
        throw std::runtime_error("Native asset read failed");
    }
    return bytes;
}

void atomic_write(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    static std::atomic<std::uint64_t> sequence = 0;
    const auto parent = path.has_parent_path() ? path.parent_path() : std::filesystem::current_path();
    std::filesystem::create_directories(parent);
#if defined(_WIN32)
    const auto temporary = parent / (path.filename().wstring() + L".tmp." +
        std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(sequence.fetch_add(1)));
#else
    const auto temporary = parent / (path.filename().wstring() + L".tmp." +
        std::to_wstring(getpid()) + L"." + std::to_wstring(sequence.fetch_add(1)));
#endif
    try {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("Native asset temporary file open failed");
        if (!bytes.empty()) output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        output.flush();
        if (!output) throw std::runtime_error("Native asset write failed");
        output.close();
#if defined(_WIN32)
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            throw std::runtime_error("Native asset atomic replacement failed");
        }
#else
        std::error_code error;
        std::filesystem::rename(temporary, path, error);
        if (error) {
            throw std::runtime_error("Native asset atomic replacement failed: " + error.message());
        }
#endif
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}

std::uint32_t resource_chunk_type(ResourceKind kind) {
    switch (kind) {
    case ResourceKind::Geometry:
        return static_cast<std::uint32_t>(ChunkKind::Geometry);
    case ResourceKind::MaterialGraph:
        return static_cast<std::uint32_t>(ChunkKind::MaterialGraph);
    case ResourceKind::Texture:
        return static_cast<std::uint32_t>(ChunkKind::Texture);
    case ResourceKind::SpectralTable:
        return static_cast<std::uint32_t>(ChunkKind::SpectralTable);
    case ResourceKind::MiePhase:
        return static_cast<std::uint32_t>(ChunkKind::MiePhase);
    case ResourceKind::VolumeField:
        return static_cast<std::uint32_t>(ChunkKind::VolumeField);
    case ResourceKind::Animation:
        return static_cast<std::uint32_t>(ChunkKind::Animation);
    case ResourceKind::Physics:
        return static_cast<std::uint32_t>(ChunkKind::Physics);
    case ResourceKind::Acoustic:
        return static_cast<std::uint32_t>(ChunkKind::Acoustic);
    case ResourceKind::Video:
        return static_cast<std::uint32_t>(ChunkKind::Video);
    case ResourceKind::Validation:
        return static_cast<std::uint32_t>(ChunkKind::Validation);
    case ResourceKind::Provenance:
        return static_cast<std::uint32_t>(ChunkKind::Provenance);
    case ResourceKind::Cache:
        return static_cast<std::uint32_t>(ChunkKind::CacheReference);
    default:
        return static_cast<std::uint32_t>(ChunkKind::Extension);
    }
}

const ContainerChunk* embedded_resource(const NativeContainer& container,
                                        std::string_view hash) {
    const std::string id = std::string(kEmbeddedResourcePrefix) +
                           std::string(hash);
    const auto found =
        std::ranges::find(container.chunks, id, &ContainerChunk::id);
    return found == container.chunks.end() ? nullptr : &*found;
}

NativeSceneArchive realize_procedural_archive(
    const NativeSceneArchive& source,
    std::vector<NamedResourcePayload>& generated) {
    if (!source.procedural_graph)
        return source;
    const auto built = build_procedural_scene(source);
    if (!built.ok() || !built.value)
        throw std::invalid_argument(
            built.diagnostics.empty() ? "Procedural scene build failed"
                                      : built.diagnostics.front().message);
    NativeSceneArchive output = source;
    output.scene = built.value->scene;
    output.source_ids = built.value->source_ids;
    output.procedural_graph.reset();
    generated = built.value->generated_resources;
    for (auto& resource : generated) {
        resource.source_uri = resource.descriptor.uri;
        if (std::ranges::find(output.document.resources,
                              resource.descriptor.id,
                              &ResourceDescriptor::id) ==
            output.document.resources.end())
            output.document.resources.push_back(resource.descriptor);
        output.packaged_resources.push_back(resource);
    }
    assign_deterministic_object_uuids(output);
    return output;
}

void append_package_resource(
    PackageManifest& manifest,
    std::map<std::string, ContainerChunk>& chunks,
    std::vector<PackageResourceBinding>& bindings,
    std::map<std::pair<std::string, std::string>, std::string>& source_hashes,
    std::uint64_t& total_bytes,
    std::string_view scene_id,
    NativeResolvedResource resource,
    const ValidationLimits& limits) {
    if (resource.source_uri.empty())
        throw std::invalid_argument("Package resource source URI is empty");
    const std::string hash = resource.descriptor.content_hash;
    const auto source_key =
        std::pair(std::string(scene_id), resource.source_uri);
    if (const auto found = source_hashes.find(source_key);
        found != source_hashes.end()) {
        if (found->second != hash)
            throw std::invalid_argument(
                "Package source URI resolves to conflicting content");
    } else {
        source_hashes.emplace(source_key, hash);
    }
    if (resource.payload.size() > limits.max_total_stored_bytes - total_bytes)
        throw std::invalid_argument("Package resource stored-byte budget exceeded");
    if (!chunks.contains(hash)) {
        total_bytes += resource.payload.size();
        chunks.emplace(
            hash,
            ContainerChunk{
                std::string(kEmbeddedResourcePrefix) + hash,
                resource_chunk_type(resource.descriptor.kind),
                resource.descriptor.schema_version,
                RequirementLevel::Required,
                static_cast<std::uint32_t>(CompressionCodec::None),
                8,
                resource.descriptor.kind == ResourceKind::Extension
                    ? "ure.preview.resource/1.0"
                    : std::string{},
                {},
                std::move(resource.payload)});
    }
    const std::string package_id = "content/" + hash;
    if (std::ranges::find(manifest.resources, package_id,
                          &ResourceDescriptor::id) == manifest.resources.end()) {
        ResourceDescriptor packaged = resource.descriptor;
        packaged.id = package_id;
        packaged.content_hash = hash;
        packaged.uri = "ure+sha256://" + hash;
        packaged.dependencies.clear();
        packaged.byte_length = chunks.at(hash).payload.size();
        if (packaged.resident_bytes == 0)
            packaged.resident_bytes = packaged.byte_length;
        manifest.resources.push_back(std::move(packaged));
    }
    bindings.push_back(
        {std::string(scene_id), resource.logical_id,
         std::move(resource.source_uri), hash, resource.descriptor.kind,
         std::move(resource.descriptor.dependencies)});
}

std::vector<std::uint8_t> package_provenance_payload(
    std::span<const SceneReference> scenes,
    std::span<const PackageResourceBinding> bindings) {
    nlohmann::ordered_json root;
    root["schema"] = kPackageProvenanceSchema;
    root["packager"] = "ure.scene-tool/0.1";
    auto scene_values = nlohmann::ordered_json::array();
    auto sorted_scenes = std::vector<SceneReference>(scenes.begin(), scenes.end());
    std::ranges::sort(sorted_scenes, {}, &SceneReference::id);
    for (const auto& scene : sorted_scenes)
        scene_values.push_back(
            {{"id", scene.id}, {"semantic_hash", scene.content_hash}});
    root["scenes"] = std::move(scene_values);
    auto values = nlohmann::ordered_json::array();
    auto sorted = std::vector<PackageResourceBinding>(bindings.begin(),
                                                       bindings.end());
    std::ranges::sort(sorted, [](const auto& left, const auto& right) {
        return std::tie(left.scene_id, left.logical_id, left.source_uri,
                        left.content_hash) <
               std::tie(right.scene_id, right.logical_id, right.source_uri,
                        right.content_hash);
    });
    for (const auto& binding : sorted) {
        values.push_back({{"content_hash", binding.content_hash},
                          {"kind", static_cast<std::uint32_t>(binding.kind)},
                          {"logical_id", binding.logical_id},
                          {"scene_id", binding.scene_id},
                          {"source_uri", binding.source_uri}});
    }
    root["resources"] = std::move(values);
    const std::string text = root.dump(2) + "\n";
    return {text.begin(), text.end()};
}

void canonicalize_package_manifest(PackageManifest& manifest) {
    const auto canonicalize_resources = [](auto& resources) {
        for (auto& resource : resources) {
            std::ranges::sort(resource.dependencies);
            resource.dependencies.erase(
                std::unique(resource.dependencies.begin(),
                            resource.dependencies.end()),
                resource.dependencies.end());
        }
        std::ranges::sort(resources, [](const auto& left, const auto& right) {
            return std::tie(left.id, left.content_hash) <
                   std::tie(right.id, right.content_hash);
        });
    };
    std::ranges::sort(manifest.scenes, {}, &SceneReference::id);
    canonicalize_resources(manifest.resources);
    canonicalize_resources(manifest.caches);
    std::ranges::sort(manifest.dependencies, [](const auto& left,
                                                const auto& right) {
        return std::tie(left.package_id, left.manifest_hash) <
               std::tie(right.package_id, right.manifest_hash);
    });
    manifest.id.clear();
    manifest.id = "package-" + semantic_hash(manifest).substr(0, 24);
}

std::vector<PackageResourceBinding> read_package_provenance(
    const NativeContainer& container,
    const PackageManifest& manifest) {
    const auto descriptor = std::ranges::find(
        manifest.resources, kPackageProvenanceId, &ResourceDescriptor::id);
    if (descriptor == manifest.resources.end())
        throw std::invalid_argument("Package provenance resource is missing");
    const auto* chunk = embedded_resource(container, descriptor->content_hash);
    if (!chunk || sha256_hex(chunk->payload) != descriptor->content_hash)
        throw std::invalid_argument("Package provenance payload is invalid");
    const auto root = nlohmann::json::parse(chunk->payload);
    if (root.at("schema").get<std::string>() != kPackageProvenanceSchema)
        throw std::invalid_argument("Package provenance schema is unsupported");
    std::vector<PackageResourceBinding> bindings;
    for (const auto& value : root.at("resources")) {
        PackageResourceBinding binding;
        binding.scene_id = value.at("scene_id");
        binding.logical_id = value.at("logical_id");
        binding.source_uri = value.at("source_uri");
        binding.content_hash = value.at("content_hash");
        binding.kind = static_cast<ResourceKind>(value.at("kind").get<std::uint32_t>());
        bindings.push_back(std::move(binding));
    }
    return bindings;
}

LoadResult<NativeSceneArchive> failure(std::string code, const std::filesystem::path& path, std::string message) {
    LoadResult<NativeSceneArchive> result;
    result.diagnostics.push_back({std::move(code), DiagnosticSeverity::Error, path.string(), std::move(message), {}});
    return result;
}

const ContainerChunk* embedded_scene(const NativeContainer& container, std::string_view id) {
    const std::string chunk_id = std::string(kEmbeddedScenePrefix) + std::string(id);
    const auto found = std::ranges::find(container.chunks, chunk_id, &ContainerChunk::id);
    return found == container.chunks.end() ? nullptr : &*found;
}

LoadResult<NativeSceneArchive> load_package_scene_bytes(
    std::span<const std::uint8_t> bytes,
    const std::filesystem::path& source,
    std::string_view scene_id,
    const ValidationLimits& limits,
    bool require_unambiguous) {
    try {
        const auto registry = native_tool_capabilities();
        const auto container = read_container(
            bytes,
            registry,
            limits);
        const auto manifest = read_package_binary(
            bytes,
            registry,
            limits);
        if (!container.ok() || !container.value ||
            !manifest.ok() || !manifest.value) {
            LoadResult<NativeSceneArchive> result;
            result.diagnostics = container.diagnostics;
            result.diagnostics.insert(
                result.diagnostics.end(),
                manifest.diagnostics.begin(),
                manifest.diagnostics.end());
            return result;
        }
        if (manifest.value->scenes.empty()) {
            return failure(
                "URE-Q9-PACKAGE-001",
                source,
                "Package contains no scene");
        }
        if (scene_id.empty() && require_unambiguous &&
            manifest.value->scenes.size() != 1) {
            return failure(
                "URE-Q9-PACKAGE-009",
                source,
                "Multi-scene package operation requires an explicit scene ID");
        }
        auto selected = manifest.value->scenes.begin();
        if (!scene_id.empty()) {
            selected = std::ranges::find(
                manifest.value->scenes,
                scene_id,
                &SceneReference::id);
            if (selected == manifest.value->scenes.end()) {
                return failure(
                    "URE-Q9-PACKAGE-006",
                    source,
                    "Package scene ID was not found: " +
                        std::string(scene_id));
            }
        }
        const auto* chunk = embedded_scene(
            *container.value,
            selected->id);
        if (!chunk) {
            return failure(
                "URE-Q9-PACKAGE-002",
                source,
                "Package scene payload is missing");
        }
        if (selected->uri !=
            std::string("ure+sha256://") +
                sha256_hex(chunk->payload)) {
            return failure(
                "URE-Q9-PACKAGE-005",
                source,
                "Package scene payload content hash mismatch");
        }
        auto scene = read_scene_ir_binary(
            chunk->payload,
            registry,
            limits);
        if (scene.value &&
            scene_ir_semantic_hash(*scene.value) !=
                selected->content_hash) {
            return failure(
                "URE-Q9-PACKAGE-003",
                source,
                "Package scene semantic hash mismatch");
        }
        if (scene.value && !manifest.value->resources.empty()) {
            const auto bindings = read_package_provenance(
                *container.value, *manifest.value);
            for (const auto& descriptor : manifest.value->resources) {
                const auto* resource = embedded_resource(
                    *container.value, descriptor.content_hash);
                if (!resource || sha256_hex(resource->payload) !=
                                     descriptor.content_hash ||
                    resource->payload.size() != descriptor.byte_length) {
                    return failure(
                        "URE-PRV2-PACKAGE-RESOURCE-001", source,
                        "Package resource payload is missing or corrupt");
                }
            }
            for (const auto& binding : bindings) {
                if (binding.scene_id != selected->id)
                    continue;
                const auto descriptor = std::ranges::find_if(
                    manifest.value->resources,
                    [&binding](const ResourceDescriptor& candidate) {
                        return candidate.content_hash == binding.content_hash;
                    });
                const auto* resource = embedded_resource(
                    *container.value, binding.content_hash);
                if (descriptor == manifest.value->resources.end() ||
                    !resource) {
                    return failure(
                        "URE-PRV2-PACKAGE-RESOURCE-002", source,
                        "Package resource binding is unresolved");
                }
                auto attached = *descriptor;
                attached.id = binding.logical_id;
                attached.kind = binding.kind;
                scene.value->packaged_resources.push_back(
                    {binding.logical_id, std::move(attached),
                     resource->payload, binding.source_uri});
            }
            scene.value->package_manifest =
                std::make_shared<const PackageManifest>(*manifest.value);
            scene.value->package_semantic_hash =
                semantic_hash(*manifest.value);
        }
        return scene;
    } catch (const std::exception& error) {
        return failure(
            "URE-Q9-PACKAGE-004",
            source,
            error.what());
    }
}

LoadResult<NativeSceneArchive> load_package_scene(
    const std::filesystem::path& path,
    std::string_view scene_id,
    const ValidationLimits& limits,
    bool require_unambiguous) {
    try {
        std::error_code size_error;
        const auto size = std::filesystem::file_size(path, size_error);
        if (size_error)
            return failure("URE-Q9-PACKAGE-004", path,
                           "Native package is unavailable");
        if (size > limits.max_total_stored_bytes)
            return failure("URE-Q-BUDGET-001", path,
                           "Native package exceeds the stored-byte budget");
        const auto bytes = read_file(path, limits.max_total_stored_bytes);
        return load_package_scene_bytes(bytes, path, scene_id, limits,
                                        require_unambiguous);
    } catch (const std::exception& error) {
        return failure("URE-Q9-PACKAGE-004", path, error.what());
    }
}

}

bool NativeInspection::ok() const {
    return std::ranges::none_of(diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Error;
    });
}

CapabilityRegistry native_tool_capabilities() {
    CapabilityRegistry registry;
    registry.features.emplace("ure.scene.procedural", Version{1, 0});
    registry.features.emplace(std::string(kScriptBuildFeature), Version{1, 0});
    registry.features.emplace("ure.scene.resource", Version{1, 0});
    registry.features.emplace("ure.render.solver", Version{1, 0});
    registry.features.emplace("ure.scene.simulation", Version{1, 0});
    for (std::uint32_t kind = static_cast<std::uint32_t>(ChunkKind::Metadata);
         kind <= static_cast<std::uint32_t>(ChunkKind::SimulationContract); ++kind) {
        if (kind != 16) registry.chunk_kinds.insert(kind);
    }
    registry.chunk_kinds.insert(static_cast<std::uint32_t>(ChunkKind::Extension));
    return registry;
}

NativeSceneArchive migrate_native_scene_archive(
    const NativeSceneArchive& archive) {
    if (archive.document.schema_version.major ==
        kSceneSchemaVersionV2.major) {
        return archive;
    }
    if (archive.document.schema_version.major !=
        kSceneSchemaVersion.major) {
        throw std::invalid_argument("Native scene schema has no registered migration");
    }
    NativeSceneArchive migrated = archive;
    const std::string input_hash = scene_ir_semantic_hash(archive);
    migrated.document.schema_version = kSceneSchemaVersionV2;
    assign_deterministic_object_uuids(migrated);
    migrated.canonical_camera = canonical_camera_from_scene(migrated.scene.camera);
    const std::string output_hash = scene_ir_semantic_hash(migrated);
    migrated.document.migrations.push_back({
        archive.document.schema_version,
        kSceneSchemaVersionV2,
        "ure.scene.migrate.uuid-v2",
        {1, 0},
        input_hash,
        output_hash,
        false});
    const ValidationReport validation = validate_scene_ir_archive(migrated);
    if (!validation.ok()) {
        throw std::invalid_argument(validation.diagnostics.front().message);
    }
    return migrated;
}

LoadResult<NativeSceneArchive> load_native_asset(const std::filesystem::path& path,
                                                 const ValidationLimits& limits) {
    auto loaded = path.extension() != ".urepkg"
        ? load_native_scene(
            path,
            native_tool_capabilities(),
            limits)
        : load_package_scene(path, {}, limits, false);
    if (loaded.value) {
        loaded.value->execution_root =
            std::filesystem::absolute(path).lexically_normal().parent_path();
    }
    return loaded;
}

LoadResult<NativeSceneArchive> load_native_package_scene(
    const std::filesystem::path& path,
    std::string_view scene_id,
    const ValidationLimits& limits) {
    if (path.extension() != ".urepkg") {
        return failure(
            "URE-Q9-PACKAGE-007",
            path,
            "Explicit package scene selection requires a .urepkg input");
    }
    auto loaded = load_package_scene(
        path,
        scene_id,
        limits,
        true);
    if (loaded.value) {
        loaded.value->execution_root =
            std::filesystem::absolute(path).lexically_normal().parent_path();
    }
    return loaded;
}

LoadResult<NativeSceneArchive> load_native_package_scene(
    std::span<const std::uint8_t> bytes,
    std::string_view scene_id,
    const ValidationLimits& limits) {
    return load_package_scene_bytes(bytes, "<memory-package>", scene_id,
                                    limits, true);
}

NativeInspection inspect_native_asset(const std::filesystem::path& path,
                                      const ValidationLimits& limits) {
    NativeInspection inspection;
    try {
        inspection.stored_bytes = std::filesystem::file_size(path);
        if (path.extension() == ".urepkg") {
            const auto bytes = read_file(path, limits.max_total_stored_bytes);
            const auto registry = native_tool_capabilities();
            const auto container = read_container(bytes, registry, limits);
            const auto manifest = read_package_binary(bytes, registry, limits);
            inspection.kind = ContainerKind::Package;
            inspection.diagnostics = container.diagnostics;
            inspection.diagnostics.insert(inspection.diagnostics.end(), manifest.diagnostics.begin(), manifest.diagnostics.end());
            if (manifest.value) {
                inspection.id = manifest.value->id;
                inspection.version = manifest.value->format_version;
                inspection.semantic_hash = semantic_hash(*manifest.value);
                inspection.scene_count = manifest.value->scenes.size();
                inspection.resource_count = manifest.value->resources.size();
                inspection.cache_count = manifest.value->caches.size();
                inspection.dependency_count =
                    manifest.value->dependencies.size();
                for (const auto& resource : manifest.value->resources) inspection.resident_bytes += resource.resident_bytes;
            }
            for (const auto& reference : manifest.value ? manifest.value->scenes : std::vector<SceneReference>{}) {
                const auto* chunk = container.value ? embedded_scene(*container.value, reference.id) : nullptr;
                if (!chunk) {
                    inspection.diagnostics.push_back({"URE-Q9-PACKAGE-002", DiagnosticSeverity::Error,
                        "scenes/" + reference.id, "Package scene payload is missing", {}});
                } else if (reference.uri != std::string("ure+sha256://") + sha256_hex(chunk->payload)) {
                    inspection.diagnostics.push_back({"URE-Q9-PACKAGE-005", DiagnosticSeverity::Error,
                        "scenes/" + reference.id, "Package scene payload content hash mismatch", {}});
                }
            }
            if (manifest.value && container.value) {
                for (const auto& resource : manifest.value->resources) {
                    const auto* chunk = embedded_resource(
                        *container.value, resource.content_hash);
                    if (!chunk || sha256_hex(chunk->payload) !=
                                      resource.content_hash ||
                        chunk->payload.size() != resource.byte_length) {
                        inspection.diagnostics.push_back(
                            {"URE-PRV2-PACKAGE-RESOURCE-001",
                             DiagnosticSeverity::Error,
                             "resources/" + resource.id,
                             "Package resource payload is missing or corrupt",
                             {}});
                    }
                }
                for (const auto& cache : manifest.value->caches) {
                    const auto* chunk = embedded_resource(
                        *container.value, cache.content_hash);
                    if (chunk && (sha256_hex(chunk->payload) !=
                                      cache.content_hash ||
                                  chunk->payload.size() !=
                                      cache.byte_length)) {
                        inspection.diagnostics.push_back(
                            {"URE-PRV2-PACKAGE-CACHE-001",
                             DiagnosticSeverity::Warning,
                             "caches/" + cache.id,
                             "Optional package cache is corrupt and will be rebuilt",
                             "delete or rebuild the optional cache"});
                    }
                }
            }
            return inspection;
        }
        const auto loaded = load_native_asset(path, limits);
        inspection.diagnostics = loaded.diagnostics;
        if (loaded.value) {
            inspection.id = loaded.value->document.id;
            inspection.version = loaded.value->document.schema_version;
            inspection.semantic_hash = scene_ir_semantic_hash(*loaded.value);
            inspection.scene_count = 1;
            inspection.resource_count = loaded.value->document.resources.size();
            for (const auto& resource : loaded.value->document.resources) inspection.resident_bytes += resource.resident_bytes;
        }
    } catch (const std::exception& error) {
        inspection.diagnostics.push_back({"URE-Q9-INSPECT-001", DiagnosticSeverity::Error, path.string(), error.what(), {}});
    }
    return inspection;
}

void build_native_scene(const std::filesystem::path& input,
                        const std::filesystem::path& output,
                        const ValidationLimits& limits) {
    const auto loaded = load_native_asset(input, limits);
    if (!loaded.ok()) throw std::invalid_argument(loaded.diagnostics.empty() ? "Native scene build failed" : loaded.diagnostics.front().message);
    std::vector<NamedResourcePayload> generated;
    auto realized = realize_procedural_archive(*loaded.value, generated);
    for (const auto& resource : generated) {
        const auto validation = validate_exploded_resource_path(
            output.parent_path(), resource.descriptor.uri);
        if (!validation.ok())
            throw std::invalid_argument(
                validation.diagnostics.front().message);
        atomic_write(output.parent_path() / resource.descriptor.uri,
                     resource.payload);
    }
    save_native_scene(output, realized);
}

void pack_native_scenes(const std::filesystem::path& output,
                        const std::vector<std::filesystem::path>& inputs,
                        const ValidationLimits& limits) {
    if (inputs.empty()) throw std::invalid_argument("Package requires at least one scene");
    PackageManifest manifest;
    manifest.format_version = kPackageFormatVersion;
    std::vector<ContainerChunk> scene_chunks;
    std::map<std::string, ContainerChunk> resource_chunks;
    std::vector<PackageResourceBinding> bindings;
    std::map<std::pair<std::string, std::string>, std::string> source_hashes;
    std::uint64_t total_bytes{};
    for (const auto& input : inputs) {
        const auto loaded = load_native_asset(input, limits);
        if (!loaded.ok()) throw std::invalid_argument(loaded.diagnostics.empty() ? "Package scene load failed" : loaded.diagnostics.front().message);
        const auto payload = write_scene_ir_binary(*loaded.value);
        if (payload.size() > limits.max_total_stored_bytes - total_bytes)
            throw std::invalid_argument("Package scene stored-byte budget exceeded");
        total_bytes += payload.size();
        const std::string id = loaded.value->document.id;
        if (id.empty() || std::ranges::any_of(manifest.scenes, [&](const auto& scene) { return scene.id == id; })) {
            throw std::invalid_argument("Package scene IDs must be non-empty and unique");
        }
        const std::string hash = scene_ir_semantic_hash(*loaded.value);
        manifest.scenes.push_back({id, hash, "ure+sha256://" + sha256_hex(payload)});
        scene_chunks.push_back({std::string(kEmbeddedScenePrefix) + id,
                                static_cast<std::uint32_t>(ChunkKind::SceneGraph), loaded.value->document.schema_version, RequirementLevel::Required,
                                static_cast<std::uint32_t>(CompressionCodec::None), 8, {}, {}, payload});
        if (loaded.value->procedural_graph) {
            std::vector<NamedResourcePayload> generated;
            const auto realized = realize_procedural_archive(
                *loaded.value, generated);
            const auto cache_payload = write_scene_ir_binary(realized);
            const std::string cache_hash = sha256_hex(cache_payload);
            if (cache_payload.size() >
                limits.max_total_stored_bytes - total_bytes)
                throw std::invalid_argument(
                    "Package cache stored-byte budget exceeded");
            total_bytes += cache_payload.size();
            manifest.caches.push_back(
                {"cache/realized/" + id, cache_hash, ResourceKind::Cache,
                 {1, 0}, "ure+sha256://" + cache_hash, {},
                 cache_payload.size(), cache_payload.size()});
            resource_chunks.emplace(
                cache_hash,
                ContainerChunk{
                    std::string(kEmbeddedResourcePrefix) + cache_hash,
                    static_cast<std::uint32_t>(ChunkKind::CacheReference),
                    {1, 0}, RequirementLevel::Optional,
                    static_cast<std::uint32_t>(CompressionCodec::None), 8,
                    {}, {std::string(kEmbeddedScenePrefix) + id},
                    cache_payload});
        }
        auto resolution = resolve_native_scene_resources(*loaded.value, limits);
        if (!resolution.ok())
            throw std::invalid_argument(
                resolution.diagnostics.front().message);
        for (auto& resource : resolution.resources) {
            append_package_resource(
                manifest, resource_chunks, bindings, source_hashes,
                total_bytes, id, std::move(resource), limits);
        }
    }
    std::map<std::pair<std::string, std::string>, std::string> logical_content;
    for (const auto& binding : bindings)
        logical_content.emplace(
            std::pair(binding.scene_id, binding.logical_id),
            binding.content_hash);
    for (const auto& binding : bindings) {
        const auto descriptor = std::ranges::find(
            manifest.resources, "content/" + binding.content_hash,
            &ResourceDescriptor::id);
        if (descriptor == manifest.resources.end())
            throw std::logic_error("Package resource descriptor is missing");
        for (const auto& dependency : binding.dependencies) {
            const auto found = logical_content.find(
                {binding.scene_id, dependency});
            if (found == logical_content.end())
                throw std::invalid_argument(
                    "Package resource dependency is unresolved");
            descriptor->dependencies.push_back("content/" + found->second);
        }
        std::ranges::sort(descriptor->dependencies);
        descriptor->dependencies.erase(
            std::unique(descriptor->dependencies.begin(),
                        descriptor->dependencies.end()),
            descriptor->dependencies.end());
    }
    auto provenance = package_provenance_payload(manifest.scenes, bindings);
    const std::string provenance_hash = sha256_hex(provenance);
    if (provenance.size() > limits.max_total_stored_bytes - total_bytes)
        throw std::invalid_argument("Package provenance stored-byte budget exceeded");
    total_bytes += provenance.size();
    manifest.resources.push_back(
        {std::string(kPackageProvenanceId), provenance_hash,
         ResourceKind::Provenance, {1, 0},
         "ure+sha256://" + provenance_hash, {}, provenance.size(),
         provenance.size()});
    resource_chunks.emplace(
        provenance_hash,
        ContainerChunk{
            std::string(kEmbeddedResourcePrefix) + provenance_hash,
            static_cast<std::uint32_t>(ChunkKind::Provenance), {1, 0},
            RequirementLevel::Required,
            static_cast<std::uint32_t>(CompressionCodec::None), 8, {}, {},
            std::move(provenance)});
    canonicalize_package_manifest(manifest);
    std::vector<ContainerChunk> chunks = std::move(scene_chunks);
    for (auto& [hash, chunk] : resource_chunks) {
        static_cast<void>(hash);
        chunks.push_back(std::move(chunk));
    }
    atomic_write(output, write_package_binary(manifest, std::move(chunks)));
}

void unpack_native_package(const std::filesystem::path& input,
                           const std::filesystem::path& output_directory,
                           const ValidationLimits& limits) {
    const auto bytes = read_file(input, limits.max_total_stored_bytes);
    const auto registry = native_tool_capabilities();
    const auto container = read_container(bytes, registry, limits);
    const auto manifest = read_package_binary(bytes, registry, limits);
    if (!container.ok() || !container.value || !manifest.ok() || !manifest.value) throw std::invalid_argument("Native package validation failed");
    std::filesystem::create_directories(output_directory);
    for (const auto& reference : manifest.value->scenes) {
        const auto* chunk = embedded_scene(*container.value, reference.id);
        if (!chunk) throw std::invalid_argument("Package scene payload is missing");
        const auto scene = read_scene_ir_binary(chunk->payload, registry, limits);
        if (!scene.ok() || scene_ir_semantic_hash(*scene.value) != reference.content_hash) throw std::invalid_argument("Package scene validation failed");
        atomic_write(output_directory / (reference.id + ".urescene"), chunk->payload);
    }
    const auto manifest_text = write_package_text(*manifest.value);
    atomic_write(
        output_directory / "package.ure",
        std::span(reinterpret_cast<const std::uint8_t*>(manifest_text.data()),
                  manifest_text.size()));
    for (const auto& descriptor : manifest.value->resources) {
        const auto* chunk = embedded_resource(*container.value,
                                              descriptor.content_hash);
        if (!chunk)
            throw std::invalid_argument("Package resource payload is missing");
        atomic_write(output_directory / "resources" / "content" /
                         descriptor.content_hash,
                     chunk->payload);
    }
    for (const auto& descriptor : manifest.value->caches) {
        const auto* chunk = embedded_resource(*container.value,
                                              descriptor.content_hash);
        if (chunk) {
            atomic_write(output_directory / "caches" / "content" /
                             descriptor.content_hash,
                         chunk->payload);
        }
    }
    if (!manifest.value->resources.empty()) {
        const auto bindings = read_package_provenance(*container.value,
                                                      *manifest.value);
        for (const auto& binding : bindings) {
            const auto* chunk = embedded_resource(*container.value,
                                                  binding.content_hash);
            if (!chunk)
                throw std::invalid_argument(
                    "Package resource binding is unresolved");
            const auto validation = validate_exploded_resource_path(
                output_directory, binding.source_uri);
            if (!validation.ok())
                throw std::invalid_argument(
                    validation.diagnostics.front().message);
            atomic_write(output_directory / binding.source_uri,
                         chunk->payload);
        }
    }
}

void migrate_native_scene(const std::filesystem::path& input,
                          const std::filesystem::path& output,
                          const ValidationLimits& limits) {
    const auto loaded = load_native_asset(input, limits);
    if (!loaded.ok()) {
        throw std::invalid_argument(
            loaded.diagnostics.empty() ? "Native scene migration failed" :
                                         loaded.diagnostics.front().message);
    }
    save_native_scene(output, migrate_native_scene_archive(*loaded.value));
}

void export_native_scene_usda(
    const std::filesystem::path& input,
    const std::filesystem::path& output,
    UsdExportPolicy policy,
    const std::filesystem::path& loss_report_path,
    std::string_view scene_id,
    const ValidationLimits& limits) {
    LoadResult<NativeSceneArchive> loaded;
    if (input.extension() == ".urepkg") {
        loaded = load_native_package_scene(
            input,
            scene_id,
            limits);
    } else {
        if (!scene_id.empty()) {
            throw std::invalid_argument(
                "A package scene ID is valid only for .urepkg input");
        }
        loaded = load_native_asset(input, limits);
    }
    if (!loaded.ok() || !loaded.value) {
        throw std::invalid_argument(
            loaded.diagnostics.empty()
            ? "Native scene export load failed"
            : loaded.diagnostics.front().message);
    }
    save_usda_native(
        output,
        *loaded.value,
        policy,
        loss_report_path,
        limits);
}

}
