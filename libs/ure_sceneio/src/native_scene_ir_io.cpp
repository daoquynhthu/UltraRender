#define NOMINMAX

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <limits>
#include <exception>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <windows.h>

#include <nlohmann/json.hpp>

#include <ure/native_scene_hash.hpp>
#include <ure/native_scene_metadata.hpp>
#include <ure/native_scene_text.hpp>
#include <ure/native_scene_validation.hpp>

#include "native_scene_ir_internal.hpp"

namespace ure::native_scene {
namespace {

template <typename T>
LoadResult<T> io_failure(std::string code, std::string path, std::string message) {
    LoadResult<T> result;
    result.diagnostics.push_back({std::move(code), DiagnosticSeverity::Error, std::move(path),
                                  std::move(message), {}});
    return result;
}

void append_diagnostics(std::vector<ValidationDiagnostic>& destination,
                        const std::vector<ValidationDiagnostic>& source) {
    destination.insert(destination.end(), source.begin(), source.end());
}

std::vector<std::uint8_t> read_file(const std::filesystem::path& path, std::uint64_t maximum_size) {
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error || size > maximum_size || size > std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error("Native scene file exceeds read budget or is unavailable");
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    std::ifstream input(path, std::ios::binary);
    if (!input || (!bytes.empty() && !input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))) {
        throw std::runtime_error("Native scene file read failed");
    }
    return bytes;
}

void atomic_write(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    static std::atomic<std::uint64_t> sequence = 0;
    std::filesystem::create_directories(path.parent_path());
    const std::filesystem::path temporary = path.parent_path() /
        (path.filename().wstring() + L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." +
         std::to_wstring(sequence.fetch_add(1, std::memory_order_relaxed)));
    try {
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) throw std::runtime_error("Native scene temporary file open failed");
            if (!bytes.empty()) output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            output.flush();
            if (!output) throw std::runtime_error("Native scene temporary file write failed");
        }
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            throw std::runtime_error("Native scene atomic replacement failed");
        }
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}

}

std::vector<std::uint8_t> write_scene_ir_binary(const NativeSceneArchive& archive) {
    const ValidationReport validation = validate_scene_ir_archive(archive);
    if (!validation.ok()) throw std::invalid_argument(validation.diagnostics.front().message);
    if (archive.procedural_graph) {
        const ValidationReport graph_validation = validate_procedural_graph(*archive.procedural_graph, archive);
        if (!graph_validation.ok()) throw std::invalid_argument(graph_validation.diagnostics.front().message);
    }
    if (archive.resource_catalog) {
        if (std::ranges::none_of(archive.document.features, [](const FeatureDeclaration& feature) { return feature.name == kResourceCatalogFeature && feature.requirement == RequirementLevel::Required; })) {
            throw std::invalid_argument("Resource catalog requires ure.scene.resource feature declaration");
        }
        const ValidationReport catalog_validation = validate_resource_catalog(*archive.resource_catalog);
        if (!catalog_validation.ok()) throw std::invalid_argument(catalog_validation.diagnostics.front().message);
    }
    if (archive.solver_contract && std::ranges::none_of(archive.document.features, [](const FeatureDeclaration& feature) { return feature.name == kSolverContractFeature && feature.requirement == RequirementLevel::Required; })) throw std::invalid_argument("Solver contract requires ure.render.solver feature declaration");
    detail::EncodedResources resources = detail::encode_resources(archive);
    SceneDocument document = archive.document;
    for (const auto& resource : resources.payloads) {
        const auto found = std::ranges::find(document.resources, resource.id, &ResourceDescriptor::id);
        if (found == document.resources.end()) document.resources.push_back(resource.descriptor);
    }
    NativeContainer container;
    container.kind = ContainerKind::Scene;
    container.container_version = kSceneContainerVersion;
    container.semantic_hash = semantic_hash(document);
    container.chunks.push_back({"metadata", static_cast<std::uint32_t>(ChunkKind::Metadata), {1, 0},
                                RequirementLevel::Required, static_cast<std::uint32_t>(CompressionCodec::None),
                                8, {}, {}, encode_scene_metadata(document)});
    std::vector<std::string> resource_dependencies;
    for (const auto& resource : resources.payloads) resource_dependencies.push_back(resource.id);
    container.chunks.push_back({"scene_graph", static_cast<std::uint32_t>(ChunkKind::SceneGraph), {1, 0},
                                RequirementLevel::Required, static_cast<std::uint32_t>(CompressionCodec::None),
                                8, {}, std::move(resource_dependencies), detail::encode_scene_graph(archive, resources)});
    if (archive.procedural_graph) {
        auto procedural_payload = detail::encode_procedural_graph(*archive.procedural_graph);
        container.chunks.push_back({"procedural_graph", static_cast<std::uint32_t>(ChunkKind::ProceduralGraph), {1, 0},
                                    RequirementLevel::Required, static_cast<std::uint32_t>(CompressionCodec::None),
                                    8, {}, {"scene_graph"}, std::move(procedural_payload)});
    }
    if (archive.resource_catalog) {
        container.chunks.push_back({"resource_catalog", static_cast<std::uint32_t>(ChunkKind::ResourceCatalog), {1, 0},
                                    RequirementLevel::Required, static_cast<std::uint32_t>(CompressionCodec::None),
                                    8, {}, {"scene_graph"}, write_resource_catalog_binary(*archive.resource_catalog)});
    }
    if (archive.solver_contract) container.chunks.push_back({"solver_contract", static_cast<std::uint32_t>(ChunkKind::SolverContract), {1, 0}, RequirementLevel::Required, static_cast<std::uint32_t>(CompressionCodec::None), 8, {}, {"scene_graph"}, write_solver_contract_binary(*archive.solver_contract)});
    for (auto& resource : resources.payloads) {
        const ChunkKind kind = resource.descriptor.kind == ResourceKind::Geometry
            ? ChunkKind::Geometry : ChunkKind::MiePhase;
        container.chunks.push_back({resource.id, static_cast<std::uint32_t>(kind), {1, 0},
                                    RequirementLevel::Required, static_cast<std::uint32_t>(CompressionCodec::None),
                                    8, {}, {}, std::move(resource.payload)});
    }
    for (const auto& chunk : archive.preserved_optional_chunks) {
        if (chunk.requirement == RequirementLevel::Required) {
            throw std::invalid_argument("Preserved unknown chunks must be optional or advisory");
        }
        container.chunks.push_back(chunk);
    }
    auto bytes = write_container(container);
    return bytes;
}

LoadResult<NativeSceneArchive> read_scene_ir_binary(
    std::span<const std::uint8_t> bytes,
    const CapabilityRegistry& registry,
    const ValidationLimits& limits) {
    const auto loaded_container = read_container(bytes, registry, limits);
    if (!loaded_container.value) {
        LoadResult<NativeSceneArchive> result;
        result.diagnostics = loaded_container.diagnostics;
        return result;
    }
    try {
        const ContainerChunk* metadata = nullptr;
        const ContainerChunk* graph = nullptr;
        const ContainerChunk* procedural_graph = nullptr;
        const ContainerChunk* resource_catalog = nullptr;
        const ContainerChunk* solver_contract = nullptr;
        std::unordered_map<std::string, std::shared_ptr<Mesh>> meshes;
        std::unordered_map<std::string, std::shared_ptr<const scene_ir::MiePhaseResource>> mie;
        std::unordered_map<std::string, std::string> resource_hashes;
        std::vector<ContainerChunk> preserved;
        std::vector<ValidationDiagnostic> diagnostics = loaded_container.diagnostics;
        for (const auto& chunk : loaded_container.value->chunks) {
            if (chunk.type == static_cast<std::uint32_t>(ChunkKind::Metadata)) {
                if (metadata) return io_failure<NativeSceneArchive>("URE-Q3-CONTAINER-001", "metadata", "Duplicate metadata chunk");
                metadata = &chunk;
            } else if (chunk.type == static_cast<std::uint32_t>(ChunkKind::SceneGraph)) {
                if (graph) return io_failure<NativeSceneArchive>("URE-Q3-CONTAINER-002", "scene_graph", "Duplicate scene graph chunk");
                graph = &chunk;
            } else if (chunk.type == static_cast<std::uint32_t>(ChunkKind::ProceduralGraph)) {
                if (procedural_graph) return io_failure<NativeSceneArchive>("URE-Q4-CONTAINER-001", "procedural_graph", "Duplicate procedural graph chunk");
                procedural_graph = &chunk;
            } else if (chunk.type == static_cast<std::uint32_t>(ChunkKind::ResourceCatalog)) {
                if (resource_catalog) return io_failure<NativeSceneArchive>("URE-Q6-CONTAINER-001", "resource_catalog", "Duplicate resource catalog chunk");
                resource_catalog = &chunk;
            } else if (chunk.type == static_cast<std::uint32_t>(ChunkKind::SolverContract)) {
                if (solver_contract) return io_failure<NativeSceneArchive>("URE-Q7-CONTAINER-001", "solver_contract", "Duplicate solver contract chunk"); solver_contract = &chunk;
            } else if (chunk.type == static_cast<std::uint32_t>(ChunkKind::Geometry)) {
                auto decoded = detail::decode_mesh_payload(chunk.payload, limits);
                append_diagnostics(diagnostics, decoded.diagnostics);
                if (decoded.value) {
                    meshes.emplace(chunk.id, *decoded.value);
                    resource_hashes.emplace(chunk.id, sha256_hex(chunk.payload));
                }
            } else if (chunk.type == static_cast<std::uint32_t>(ChunkKind::MiePhase)) {
                auto decoded = detail::decode_mie_payload(chunk.payload, limits);
                append_diagnostics(diagnostics, decoded.diagnostics);
                if (decoded.value) {
                    mie.emplace(chunk.id, *decoded.value);
                    resource_hashes.emplace(chunk.id, sha256_hex(chunk.payload));
                }
            } else if (chunk.requirement != RequirementLevel::Required) {
                preserved.push_back(chunk);
            }
        }
        if (!metadata || !graph) {
            return io_failure<NativeSceneArchive>("URE-Q3-CONTAINER-003", "container", "Missing metadata or scene graph chunk");
        }
        if (std::ranges::any_of(diagnostics, [](const ValidationDiagnostic& diagnostic) {
                return diagnostic.severity == DiagnosticSeverity::Error;
            })) {
            LoadResult<NativeSceneArchive> result;
            result.diagnostics = std::move(diagnostics);
            return result;
        }
        const auto document = decode_scene_metadata(metadata->payload, registry, limits);
        append_diagnostics(diagnostics, document.diagnostics);
        if (!document.value) {
            LoadResult<NativeSceneArchive> result;
            result.diagnostics = std::move(diagnostics);
            return result;
        }
        auto archive = detail::decode_scene_graph(*document.value, graph->payload, meshes, mie, resource_hashes, limits);
        append_diagnostics(diagnostics, archive.diagnostics);
        if (!archive.value) {
            LoadResult<NativeSceneArchive> result;
            result.diagnostics = std::move(diagnostics);
            return result;
        }
        archive.value->preserved_optional_chunks = std::move(preserved);
        if (procedural_graph) {
            auto decoded = detail::decode_procedural_graph(procedural_graph->payload, limits);
            append_diagnostics(diagnostics, decoded.diagnostics);
            if (!decoded.value) {
                LoadResult<NativeSceneArchive> result;
                result.diagnostics = std::move(diagnostics);
                return result;
            }
            archive.value->procedural_graph = std::move(*decoded.value);
            const ValidationReport graph_validation = validate_procedural_graph(*archive.value->procedural_graph, *archive.value);
            append_diagnostics(diagnostics, graph_validation.diagnostics);
            if (!graph_validation.ok()) {
                LoadResult<NativeSceneArchive> result;
                result.diagnostics = std::move(diagnostics);
                return result;
            }
        }
        if (resource_catalog) {
            auto decoded = read_resource_catalog_binary(resource_catalog->payload, limits);
            append_diagnostics(diagnostics, decoded.diagnostics);
            if (!decoded.value) { LoadResult<NativeSceneArchive> result; result.diagnostics = std::move(diagnostics); return result; }
            archive.value->resource_catalog = std::make_shared<const NativeResourceCatalog>(std::move(*decoded.value));
            if (std::ranges::none_of(archive.value->document.features, [](const FeatureDeclaration& feature) { return feature.name == kResourceCatalogFeature && feature.requirement == RequirementLevel::Required; })) {
                return io_failure<NativeSceneArchive>("URE-Q6-FEATURE-001", "resource_catalog", "Resource catalog lacks required ure.scene.resource feature declaration");
            }
        }
        if (solver_contract) { auto decoded = read_solver_contract_binary(solver_contract->payload); append_diagnostics(diagnostics, decoded.diagnostics); if (!decoded.value) { LoadResult<NativeSceneArchive> result; result.diagnostics = std::move(diagnostics); return result; } archive.value->solver_contract = std::make_shared<const NativeSolverContract>(std::move(*decoded.value)); if (std::ranges::none_of(archive.value->document.features, [](const FeatureDeclaration& feature) { return feature.name == kSolverContractFeature && feature.requirement == RequirementLevel::Required; })) return io_failure<NativeSceneArchive>("URE-Q7-FEATURE-001", "solver_contract", "Solver contract lacks required feature declaration"); }
        archive.diagnostics = std::move(diagnostics);
        return archive;
    } catch (const std::exception& error) {
        return io_failure<NativeSceneArchive>("URE-Q3-CONTAINER-004", "container", error.what());
    }
}

void save_native_scene(const std::filesystem::path& path,
                       const NativeSceneArchive& archive) {
    const std::string extension = path.extension().string();
    if (extension == ".urescene") {
        const auto bytes = write_scene_ir_binary(archive);
        atomic_write(path, bytes);
        return;
    }
    if (extension != ".ure") throw std::invalid_argument("Native scene path must end in .ure or .urescene");
    const ExplodedSceneArchive exploded = write_scene_ir_text(archive);
    for (const auto& resource : exploded.resources) {
        const ValidationReport path_report = validate_exploded_resource_path(path.parent_path(), resource.descriptor.uri);
        if (!path_report.ok()) throw std::invalid_argument(path_report.diagnostics.front().message);
        atomic_write(path.parent_path() / std::filesystem::path(resource.descriptor.uri), resource.payload);
    }
    const std::vector<std::uint8_t> manifest(exploded.manifest.begin(), exploded.manifest.end());
    atomic_write(path, manifest);
}

LoadResult<NativeSceneArchive> load_native_scene(
    const std::filesystem::path& path,
    const CapabilityRegistry& registry,
    const ValidationLimits& limits) {
    try {
        const auto bytes = read_file(path, limits.max_total_stored_bytes);
        if (path.extension() == ".urescene") return read_scene_ir_binary(bytes, registry, limits);
        if (path.extension() != ".ure") return io_failure<NativeSceneArchive>(
            "URE-Q3-FILE-001", path.string(), "Native scene path must end in .ure or .urescene");
        const std::string manifest(bytes.begin(), bytes.end());
        const nlohmann::json root = nlohmann::json::parse(manifest);
        const auto document = read_scene_text(root.at("document").dump() + "\n", registry, limits);
        if (!document.value) {
            LoadResult<NativeSceneArchive> result;
            result.diagnostics = document.diagnostics;
            return result;
        }
        ExplodedSceneArchive exploded;
        exploded.manifest = manifest;
        std::uint64_t total = bytes.size();
        for (const auto& descriptor : document.value->resources) {
            if (descriptor.kind != ResourceKind::Geometry && descriptor.kind != ResourceKind::MiePhase) continue;
            const ValidationReport path_report = validate_exploded_resource_path(path.parent_path(), descriptor.uri);
            if (!path_report.ok()) {
                LoadResult<NativeSceneArchive> result;
                result.diagnostics = path_report.diagnostics;
                return result;
            }
            if (descriptor.byte_length > limits.max_total_stored_bytes - total) {
                return io_failure<NativeSceneArchive>("URE-Q3-BUDGET-001", descriptor.uri, "Exploded resources exceed read budget");
            }
            NamedResourcePayload payload;
            payload.id = descriptor.id;
            payload.descriptor = descriptor;
            payload.payload = read_file(path.parent_path() / std::filesystem::path(descriptor.uri), descriptor.byte_length);
            total += payload.payload.size();
            exploded.resources.push_back(std::move(payload));
        }
        return read_scene_ir_text(exploded, registry, limits);
    } catch (const std::exception& error) {
        return io_failure<NativeSceneArchive>("URE-Q3-FILE-002", path.string(), error.what());
    }
}

}
