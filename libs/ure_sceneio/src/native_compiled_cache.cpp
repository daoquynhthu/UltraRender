#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <ranges>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <ure/native_compiled_cache.hpp>
#include <ure/native_scene_hash.hpp>

namespace ure::native_scene {
namespace {

constexpr std::array<std::uint8_t, 8> kCacheMagic{'U', 'R', 'E', 'C', '\r', '\n', 0x1a, '\n'};
constexpr std::size_t kHeaderSize = 96;

void append_u64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}

std::uint64_t read_u64(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 8) throw std::runtime_error("Compiled cache header is truncated");
    std::uint64_t value = 0;
    for (unsigned shift = 0; shift < 64; shift += 8) value |= static_cast<std::uint64_t>(bytes[offset + shift / 8]) << shift;
    return value;
}

bool valid_hash(std::string_view value) {
    return value.size() == 64 && std::ranges::all_of(value, [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

void add_error(std::vector<ValidationDiagnostic>& diagnostics, std::string code,
               std::string path, std::string message) {
    diagnostics.push_back({std::move(code), DiagnosticSeverity::Error, std::move(path), std::move(message), {}});
}

nlohmann::ordered_json encode(const CompiledCacheManifest& cache) {
    nlohmann::ordered_json root;
    root["format_version"] = {cache.format_version.major, cache.format_version.minor};
    root["schema_version"] = {cache.schema_version.major, cache.schema_version.minor};
    root["source_hash"] = cache.source_hash;
    root["compiler_hash"] = cache.compiler_hash;
    root["scene_ir_hash"] = cache.scene_ir_hash;
    root["gpu_upload_plan"] = nlohmann::ordered_json::array();
    for (const auto& entry : cache.gpu_upload_plan) root["gpu_upload_plan"].push_back({entry.resource_id, entry.offset, entry.byte_length, entry.alignment});
    root["spectral_cache"] = nlohmann::ordered_json::array();
    for (const auto& artifact : cache.spectral_cache) root["spectral_cache"].push_back({artifact.id, static_cast<std::uint32_t>(artifact.kind), artifact.content_hash, artifact.byte_length});
    root["resource_cache"] = nlohmann::ordered_json::array();
    for (const auto& artifact : cache.resource_cache) root["resource_cache"].push_back({artifact.id, static_cast<std::uint32_t>(artifact.kind), artifact.content_hash, artifact.byte_length});
    root["acceleration"] = {cache.acceleration.provider, cache.acceleration.layout_hash, cache.acceleration.blas_count, cache.acceleration.tlas_count};
    root["validation_metrics"] = nlohmann::ordered_json::array();
    for (const auto& metric : cache.validation_metrics) root["validation_metrics"].push_back({metric.name, metric.value, metric.tolerance, metric.passed});
    return root;
}

CompiledCacheManifest decode(const nlohmann::json& root) {
    CompiledCacheManifest cache;
    cache.format_version = {root.at("format_version").at(0).get<std::uint32_t>(), root.at("format_version").at(1).get<std::uint32_t>()};
    cache.schema_version = {root.at("schema_version").at(0).get<std::uint32_t>(), root.at("schema_version").at(1).get<std::uint32_t>()};
    cache.source_hash = root.at("source_hash").get<std::string>();
    cache.compiler_hash = root.at("compiler_hash").get<std::string>();
    cache.scene_ir_hash = root.at("scene_ir_hash").get<std::string>();
    for (const auto& value : root.at("gpu_upload_plan")) cache.gpu_upload_plan.push_back({value.at(0).get<std::string>(), value.at(1).get<std::uint64_t>(), value.at(2).get<std::uint64_t>(), value.at(3).get<std::uint64_t>()});
    for (const auto& value : root.at("spectral_cache")) cache.spectral_cache.push_back({value.at(0).get<std::string>(), static_cast<ResourceKind>(value.at(1).get<std::uint32_t>()), value.at(2).get<std::string>(), value.at(3).get<std::uint64_t>()});
    for (const auto& value : root.at("resource_cache")) cache.resource_cache.push_back({value.at(0).get<std::string>(), static_cast<ResourceKind>(value.at(1).get<std::uint32_t>()), value.at(2).get<std::string>(), value.at(3).get<std::uint64_t>()});
    const auto& acceleration = root.at("acceleration");
    cache.acceleration = {acceleration.at(0).get<std::string>(), acceleration.at(1).get<std::string>(), acceleration.at(2).get<std::uint64_t>(), acceleration.at(3).get<std::uint64_t>()};
    for (const auto& value : root.at("validation_metrics")) cache.validation_metrics.push_back({value.at(0).get<std::string>(), value.at(1).get<double>(), value.at(2).get<double>(), value.at(3).get<bool>()});
    return cache;
}

ValidationReport validate_manifest(const CompiledCacheManifest& cache, const ValidationLimits& limits) {
    ValidationReport report;
    if (cache.format_version.major != 1 || cache.schema_version.major == 0) add_error(report.diagnostics, "URE-Q11-VERSION-001", "/version", "Compiled cache version is unsupported");
    if (!valid_hash(cache.source_hash) || !valid_hash(cache.compiler_hash) || !valid_hash(cache.scene_ir_hash)) add_error(report.diagnostics, "URE-Q11-HASH-001", "/identity", "Compiled cache identity hashes are invalid");
    std::uint64_t total = 0;
    std::set<std::string> ids;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> upload_ranges;
    for (std::size_t i = 0; i < cache.gpu_upload_plan.size(); ++i) {
        const auto& entry = cache.gpu_upload_plan[i];
        if (entry.resource_id.empty() || !ids.insert(entry.resource_id).second || entry.alignment == 0 || !std::has_single_bit(entry.alignment) || entry.offset % entry.alignment != 0 || entry.byte_length > std::numeric_limits<std::uint64_t>::max() - entry.offset || entry.byte_length > std::numeric_limits<std::uint64_t>::max() - total) {
            add_error(report.diagnostics, "URE-Q11-UPLOAD-001", "/gpu_upload_plan/" + std::to_string(i), "GPU upload plan entry is invalid");
        } else {
            total += entry.byte_length;
            upload_ranges.emplace_back(entry.offset, entry.offset + entry.byte_length);
        }
    }
    std::ranges::sort(upload_ranges);
    for (std::size_t i = 1; i < upload_ranges.size(); ++i) {
        if (upload_ranges[i].first < upload_ranges[i - 1].second) add_error(report.diagnostics, "URE-Q11-UPLOAD-002", "/gpu_upload_plan", "GPU upload plan ranges overlap");
    }
    const auto validate_artifacts = [&](const auto& artifacts, std::string_view path) {
        for (std::size_t i = 0; i < artifacts.size(); ++i) {
            const auto& artifact = artifacts[i];
            if (artifact.id.empty() || !valid_hash(artifact.content_hash) || artifact.byte_length > std::numeric_limits<std::uint64_t>::max() - total) {
                add_error(report.diagnostics, "URE-Q11-ARTIFACT-001", std::string(path) + "/" + std::to_string(i), "Cache artifact is invalid");
            } else total += artifact.byte_length;
        }
    };
    validate_artifacts(cache.spectral_cache, "/spectral_cache");
    validate_artifacts(cache.resource_cache, "/resource_cache");
    if (total > limits.max_total_uncompressed_bytes) add_error(report.diagnostics, "URE-Q11-BUDGET-001", "/", "Compiled cache exceeds the uncompressed budget");
    if (!cache.acceleration.layout_hash.empty() && !valid_hash(cache.acceleration.layout_hash)) add_error(report.diagnostics, "URE-Q11-HASH-001", "/acceleration/layout_hash", "Acceleration layout hash is invalid");
    for (std::size_t i = 0; i < cache.validation_metrics.size(); ++i) {
        const auto& metric = cache.validation_metrics[i];
        if (metric.name.empty() || !std::isfinite(metric.value) || !std::isfinite(metric.tolerance) || metric.tolerance < 0.0) add_error(report.diagnostics, "URE-Q11-METRIC-001", "/validation_metrics/" + std::to_string(i), "Cached validation metric is invalid");
    }
    return report;
}

}

std::vector<std::uint8_t> write_compiled_cache(const CompiledCacheManifest& cache) {
    const auto validation = validate_manifest(cache, {});
    if (!validation.ok()) throw std::invalid_argument(validation.diagnostics.front().message);
    const std::string payload_text = encode(cache).dump();
    const std::vector<std::uint8_t> payload(payload_text.begin(), payload_text.end());
    const std::string hash = sha256_hex(payload);
    std::vector<std::uint8_t> bytes(kHeaderSize, 0);
    std::ranges::copy(kCacheMagic, bytes.begin());
    bytes[8] = 1;
    bytes[12] = 1;
    std::vector<std::uint8_t> length;
    append_u64(length, payload.size());
    std::ranges::copy(length, bytes.begin() + 16);
    std::memcpy(bytes.data() + 24, hash.data(), hash.size());
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}

LoadResult<CompiledCacheManifest> read_compiled_cache(std::span<const std::uint8_t> bytes,
                                                     const ValidationLimits& limits) {
    LoadResult<CompiledCacheManifest> result;
    try {
        if (bytes.size() < kHeaderSize || !std::ranges::equal(bytes.first(8), kCacheMagic)) throw std::runtime_error("Compiled cache magic is invalid");
        if (bytes[8] != 1 || bytes[9] != 0 || bytes[10] != 0 || bytes[11] != 0 ||
            bytes[12] != 1 || bytes[13] != 0 || bytes[14] != 0 || bytes[15] != 0) {
            throw std::runtime_error("Compiled cache container or schema version is unsupported");
        }
        const std::uint64_t payload_size = read_u64(bytes, 16);
        if (payload_size > limits.max_total_stored_bytes || payload_size != bytes.size() - kHeaderSize) throw std::runtime_error("Compiled cache payload size is invalid");
        const auto payload = bytes.subspan(kHeaderSize);
        const std::string stored_hash(reinterpret_cast<const char*>(bytes.data() + 24), 64);
        if (sha256_hex(payload) != stored_hash) throw std::runtime_error("Compiled cache payload hash mismatch");
        auto cache = decode(nlohmann::json::parse(payload));
        const auto validation = validate_manifest(cache, limits);
        result.diagnostics = validation.diagnostics;
        if (validation.ok()) result.value = std::move(cache);
    } catch (const std::exception& error) {
        add_error(result.diagnostics, "URE-Q11-CACHE-001", "/", error.what());
    }
    return result;
}

CacheValidationResult validate_compiled_cache(const CompiledCacheManifest& cache,
                                              std::string_view expected_source_hash,
                                              std::string_view expected_compiler_hash,
                                              CacheMismatchPolicy policy) {
    CacheValidationResult result;
    const bool mismatch = cache.source_hash != expected_source_hash || cache.compiler_hash != expected_compiler_hash;
    if (!mismatch) { result.usable = true; return result; }
    result.rebuild_required = policy == CacheMismatchPolicy::Rebuild;
    result.diagnostics.push_back({"URE-Q11-IDENTITY-001",
        policy == CacheMismatchPolicy::Reject ? DiagnosticSeverity::Error : DiagnosticSeverity::Warning,
        "/identity", "Compiled cache source or compiler hash mismatch",
        policy == CacheMismatchPolicy::Rebuild ? "Delete and rebuild the cache from authoritative source" : "Provide a matching cache"});
    return result;
}

std::vector<FarmShardAssignment> schedule_farm_shards(const PackageManifest& package,
                                                      const std::vector<FarmWorkerInventory>& workers) {
    if (workers.empty() && !package.scenes.empty()) throw std::invalid_argument("Farm scheduling requires at least one worker");
    std::vector<FarmShardAssignment> assignments;
    for (const auto& scene : package.scenes) {
        const FarmWorkerInventory* best = nullptr;
        std::uint64_t best_local = 0;
        std::uint64_t required = 0;
        for (const auto& resource : package.resources) {
            if (resource.byte_length > std::numeric_limits<std::uint64_t>::max() - required) throw std::overflow_error("Farm resource size overflow");
            required += resource.byte_length;
        }
        for (const auto& worker : workers) {
            if (worker.capacity_bytes < required) continue;
            std::uint64_t local = 0;
            for (const auto& resource : package.resources) {
                if (std::ranges::find(worker.local_content_hashes, resource.content_hash) != worker.local_content_hashes.end()) local += resource.byte_length;
            }
            if (!best || local > best_local || (local == best_local && worker.id < best->id)) { best = &worker; best_local = local; }
        }
        if (!best) throw std::runtime_error("No farm worker satisfies package resource budget");
        FarmShardAssignment assignment;
        assignment.scene_id = scene.id;
        assignment.worker_id = best->id;
        assignment.local_bytes = best_local;
        assignment.transfer_bytes = required - best_local;
        for (const auto& resource : package.resources) assignment.resource_ids.push_back(resource.id);
        assignments.push_back(std::move(assignment));
    }
    return assignments;
}

}
