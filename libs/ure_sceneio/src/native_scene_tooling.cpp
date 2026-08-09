#define NOMINMAX

#include <atomic>
#include <filesystem>
#include <fstream>
#include <limits>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <windows.h>

#include <ure/native_scene_hash.hpp>
#include <ure/native_scene_tooling.hpp>

namespace ure::native_scene {
namespace {

constexpr std::string_view kEmbeddedScenePrefix = "scene/";

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
    const auto temporary = parent / (path.filename().wstring() + L".tmp." +
        std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(sequence.fetch_add(1)));
    try {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("Native asset temporary file open failed");
        if (!bytes.empty()) output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        output.flush();
        if (!output) throw std::runtime_error("Native asset write failed");
        output.close();
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            throw std::runtime_error("Native asset atomic replacement failed");
        }
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
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

LoadResult<NativeSceneArchive> load_package_scene(
    const std::filesystem::path& path,
    std::string_view scene_id,
    const ValidationLimits& limits,
    bool require_unambiguous) {
    try {
        const auto bytes = read_file(
            path,
            limits.max_total_stored_bytes);
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
                path,
                "Package contains no scene");
        }
        if (scene_id.empty() && require_unambiguous &&
            manifest.value->scenes.size() != 1) {
            return failure(
                "URE-Q9-PACKAGE-009",
                path,
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
                    path,
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
                path,
                "Package scene payload is missing");
        }
        if (selected->uri !=
            std::string("ure+sha256://") +
                sha256_hex(chunk->payload)) {
            return failure(
                "URE-Q9-PACKAGE-005",
                path,
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
                path,
                "Package scene semantic hash mismatch");
        }
        return scene;
    } catch (const std::exception& error) {
        return failure(
            "URE-Q9-PACKAGE-004",
            path,
            error.what());
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
    registry.features.emplace("ure.scene.script-build", Version{1, 0});
    registry.features.emplace("ure.scene.resource", Version{1, 0});
    registry.features.emplace("ure.render.solver", Version{1, 0});
    registry.features.emplace("ure.scene.simulation", Version{1, 0});
    for (std::uint32_t kind = static_cast<std::uint32_t>(ChunkKind::Metadata);
         kind <= static_cast<std::uint32_t>(ChunkKind::SimulationContract); ++kind) {
        if (kind != 16) registry.chunk_kinds.insert(kind);
    }
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
    if (path.extension() != ".urepkg") {
        return load_native_scene(
            path,
            native_tool_capabilities(),
            limits);
    }
    return load_package_scene(
        path,
        {},
        limits,
        false);
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
    return load_package_scene(
        path,
        scene_id,
        limits,
        true);
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
    save_native_scene(output, *loaded.value);
}

void pack_native_scenes(const std::filesystem::path& output,
                        const std::vector<std::filesystem::path>& inputs,
                        const ValidationLimits& limits) {
    if (inputs.empty()) throw std::invalid_argument("Package requires at least one scene");
    PackageManifest manifest;
    manifest.id = output.stem().string();
    manifest.format_version = kPackageFormatVersion;
    std::vector<ContainerChunk> chunks;
    for (const auto& input : inputs) {
        const auto loaded = load_native_asset(input, limits);
        if (!loaded.ok()) throw std::invalid_argument(loaded.diagnostics.empty() ? "Package scene load failed" : loaded.diagnostics.front().message);
        const auto payload = write_scene_ir_binary(*loaded.value);
        const std::string id = loaded.value->document.id;
        if (id.empty() || std::ranges::any_of(manifest.scenes, [&](const auto& scene) { return scene.id == id; })) {
            throw std::invalid_argument("Package scene IDs must be non-empty and unique");
        }
        const std::string hash = scene_ir_semantic_hash(*loaded.value);
        manifest.scenes.push_back({id, hash, "ure+sha256://" + sha256_hex(payload)});
        chunks.push_back({std::string(kEmbeddedScenePrefix) + id,
                          static_cast<std::uint32_t>(ChunkKind::SceneGraph), loaded.value->document.schema_version, RequirementLevel::Required,
                          static_cast<std::uint32_t>(CompressionCodec::None), 8, {}, {}, payload});
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
