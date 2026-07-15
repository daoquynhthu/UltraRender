#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <ure/native_scene.hpp>

namespace ure::native_scene {

struct GpuUploadEntry {
    std::string resource_id;
    std::uint64_t offset = 0;
    std::uint64_t byte_length = 0;
    std::uint64_t alignment = 1;
};

struct CacheArtifact {
    std::string id;
    ResourceKind kind = ResourceKind::Cache;
    std::string content_hash;
    std::uint64_t byte_length = 0;
};

struct AccelerationCacheMetadata {
    std::string provider;
    std::string layout_hash;
    std::uint64_t blas_count = 0;
    std::uint64_t tlas_count = 0;
};

struct CachedValidationMetric {
    std::string name;
    double value = 0.0;
    double tolerance = 0.0;
    bool passed = false;
};

struct CompiledCacheManifest {
    Version format_version;
    Version schema_version;
    std::string source_hash;
    std::string compiler_hash;
    std::string scene_ir_hash;
    std::vector<GpuUploadEntry> gpu_upload_plan;
    std::vector<CacheArtifact> spectral_cache;
    std::vector<CacheArtifact> resource_cache;
    AccelerationCacheMetadata acceleration;
    std::vector<CachedValidationMetric> validation_metrics;
};

enum class CacheMismatchPolicy : std::uint8_t { Rebuild, Reject };

struct CacheValidationResult {
    bool usable = false;
    bool rebuild_required = false;
    std::vector<ValidationDiagnostic> diagnostics;
};

struct FarmWorkerInventory {
    std::string id;
    std::uint64_t capacity_bytes = 0;
    std::vector<std::string> local_content_hashes;
};

struct FarmShardAssignment {
    std::string scene_id;
    std::string worker_id;
    std::vector<std::string> resource_ids;
    std::uint64_t local_bytes = 0;
    std::uint64_t transfer_bytes = 0;
};

std::vector<std::uint8_t> write_compiled_cache(const CompiledCacheManifest& cache);
LoadResult<CompiledCacheManifest> read_compiled_cache(std::span<const std::uint8_t> bytes,
                                                     const ValidationLimits& limits = {});
CacheValidationResult validate_compiled_cache(const CompiledCacheManifest& cache,
                                              std::string_view expected_source_hash,
                                              std::string_view expected_compiler_hash,
                                              CacheMismatchPolicy policy);
std::vector<FarmShardAssignment> schedule_farm_shards(const PackageManifest& package,
                                                      const std::vector<FarmWorkerInventory>& workers);

}
