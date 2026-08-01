#pragma once

#include "ure/semantic_types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ure::research {

enum class ArtifactChunkKind : std::uint32_t {
    Metadata = 1,
    RawObservable = 2,
    SampleCount = 3,
    FirstMoment = 4,
    SecondMoment = 5,
    CrossMoment = 6,
    Covariance = 7,
    Confidence = 8,
    OodDiagnostics = 9
};

enum class ArtifactCodec : std::uint32_t {
    None,
    RunLength
};

struct ArtifactLimits {
    std::uint64_t max_container_bytes = 1ull << 30;
    std::uint64_t max_stored_bytes = 1ull << 30;
    std::uint64_t max_uncompressed_bytes = 2ull << 30;
    std::uint64_t max_chunk_uncompressed_bytes = 1ull << 29;
    std::uint32_t max_chunks = 4096;
    std::uint32_t max_expansion_ratio = 1024;
};

struct ArtifactChunk {
    ArtifactChunkKind kind = ArtifactChunkKind::Metadata;
    std::uint32_t schema_version = 1;
    ArtifactCodec codec = ArtifactCodec::None;
    semantic::IdentityDigest semantic_identity = {};
    std::uint64_t element_count = 0;
    std::uint32_t component_count = 1;
    std::vector<std::uint8_t> payload;
};

struct MeasurementArtifact {
    std::uint32_t schema_version = 1;
    semantic::IdentityDigest measurement_schema_identity = {};
    semantic::IdentityDigest source_identity = {};
    std::vector<ArtifactChunk> chunks;
};

struct ArtifactChunkDescriptor {
    ArtifactChunkKind kind = ArtifactChunkKind::Metadata;
    std::uint32_t schema_version = 0;
    ArtifactCodec codec = ArtifactCodec::None;
    std::uint64_t offset = 0;
    std::uint64_t stored_size = 0;
    std::uint64_t uncompressed_size = 0;
    std::uint64_t element_count = 0;
    std::uint32_t component_count = 0;
    semantic::IdentityDigest semantic_identity = {};
    semantic::IdentityDigest content_digest = {};
};

struct ArtifactIndex {
    std::uint32_t container_version = 0;
    std::uint32_t schema_version = 0;
    semantic::IdentityDigest measurement_schema_identity = {};
    semantic::IdentityDigest source_identity = {};
    semantic::IdentityDigest artifact_identity = {};
    std::uint64_t total_uncompressed_bytes = 0;
    std::vector<ArtifactChunkDescriptor> chunks;
};

std::vector<std::uint8_t> write_measurement_artifact(
    const MeasurementArtifact& artifact,
    const ArtifactLimits& limits = {});

std::uint64_t measurement_artifact_index_size(
    std::span<const std::uint8_t> header,
    const ArtifactLimits& limits = {});

ArtifactIndex inspect_measurement_artifact_index(
    std::span<const std::uint8_t> index_bytes,
    std::uint64_t total_container_bytes,
    const ArtifactLimits& limits = {});

ArtifactIndex inspect_measurement_artifact(
    std::span<const std::uint8_t> bytes,
    const ArtifactLimits& limits = {});

std::vector<std::uint8_t> read_artifact_chunk(
    std::span<const std::uint8_t> bytes,
    const ArtifactIndex& index,
    std::size_t chunk_index,
    const ArtifactLimits& limits = {});

std::vector<std::uint8_t> read_artifact_chunk_payload(
    std::span<const std::uint8_t> stored_payload,
    const ArtifactChunkDescriptor& descriptor,
    const ArtifactLimits& limits = {});

MeasurementArtifact read_measurement_artifact(
    std::span<const std::uint8_t> bytes,
    const ArtifactLimits& limits = {});

bool has_sufficient_statistics(const ArtifactIndex& index);

}
