#include "ure/research/artifact.hpp"

#include "ure/runtime/multi_backend.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

namespace ure::research {
namespace {

constexpr std::array<std::uint8_t, 8> kMagic{
    'U', 'R', 'E', 'M', '\r', '\n', 0x1a, '\n'};
constexpr std::uint32_t kContainerVersion = 1;
constexpr std::size_t kHeaderSize = 160;
constexpr std::size_t kDirectoryEntrySize = 128;

bool checked_add(std::uint64_t left,
                 std::uint64_t right,
                 std::uint64_t& result) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        return false;
    }
    result = left + right;
    return true;
}

bool checked_multiply(std::uint64_t left,
                      std::uint64_t right,
                      std::uint64_t& result) {
    if (left != 0 &&
        right > std::numeric_limits<std::uint64_t>::max() / left) {
        return false;
    }
    result = left * right;
    return true;
}

void write_u32(std::vector<std::uint8_t>& bytes,
               std::size_t offset,
               std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        bytes[offset++] = static_cast<std::uint8_t>(value >> shift);
    }
}

void write_u64(std::vector<std::uint8_t>& bytes,
               std::size_t offset,
               std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        bytes[offset++] = static_cast<std::uint8_t>(value >> shift);
    }
}

std::uint32_t read_u32(std::span<const std::uint8_t> bytes,
                       std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 4) {
        throw std::invalid_argument("Truncated measurement artifact");
    }
    std::uint32_t value = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        value |= static_cast<std::uint32_t>(bytes[offset++]) << shift;
    }
    return value;
}

std::uint64_t read_u64(std::span<const std::uint8_t> bytes,
                       std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 8) {
        throw std::invalid_argument("Truncated measurement artifact");
    }
    std::uint64_t value = 0;
    for (int shift = 0; shift < 64; shift += 8) {
        value |= static_cast<std::uint64_t>(bytes[offset++]) << shift;
    }
    return value;
}

semantic::IdentityDigest read_digest(
    std::span<const std::uint8_t> bytes,
    std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 32) {
        throw std::invalid_argument("Truncated measurement artifact digest");
    }
    semantic::IdentityDigest result{};
    std::ranges::copy(bytes.subspan(offset, result.size()), result.begin());
    return result;
}

void write_digest(std::vector<std::uint8_t>& bytes,
                  std::size_t offset,
                  const semantic::IdentityDigest& digest) {
    std::ranges::copy(digest, bytes.begin() + offset);
}

semantic::IdentityDigest digest_bytes(
    std::span<const std::uint8_t> bytes) {
    return runtime::identity_digest(std::as_bytes(bytes));
}

std::vector<std::uint8_t> run_length_encode(
    std::span<const std::uint8_t> input) {
    std::vector<std::uint8_t> result;
    if (input.empty()) return result;
    result.reserve(input.size());
    std::size_t cursor = 0;
    while (cursor < input.size()) {
        const auto value = input[cursor];
        std::uint8_t count = 1;
        while (cursor + count < input.size() && count < 255 &&
               input[cursor + count] == value) {
            ++count;
        }
        result.push_back(count);
        result.push_back(value);
        cursor += count;
    }
    return result;
}

std::vector<std::uint8_t> run_length_decode(
    std::span<const std::uint8_t> input,
    std::uint64_t expected_size,
    const ArtifactLimits& limits) {
    if (input.size() % 2 != 0 ||
        expected_size > limits.max_chunk_uncompressed_bytes ||
        expected_size > std::numeric_limits<std::size_t>::max()) {
        throw std::invalid_argument("Invalid compressed artifact chunk");
    }
    std::vector<std::uint8_t> result;
    result.reserve(static_cast<std::size_t>(expected_size));
    for (std::size_t index = 0; index < input.size(); index += 2) {
        const auto count = input[index];
        if (count == 0 || count > expected_size - result.size()) {
            throw std::invalid_argument("Invalid run-length artifact chunk");
        }
        result.insert(result.end(), count, input[index + 1]);
    }
    if (result.size() != expected_size) {
        throw std::invalid_argument("Artifact chunk size mismatch");
    }
    return result;
}

bool expansion_ratio_exceeded(std::uint64_t uncompressed,
                              std::uint64_t stored,
                              std::uint32_t maximum_ratio) {
    if (stored == 0) return uncompressed != 0;
    const auto quotient = uncompressed / stored;
    return quotient > maximum_ratio ||
           (quotient == maximum_ratio && uncompressed % stored != 0);
}

semantic::IdentityDigest artifact_identity(
    std::uint32_t schema_version,
    const semantic::IdentityDigest& measurement_schema,
    const semantic::IdentityDigest& source,
    std::span<const ArtifactChunkDescriptor> chunks) {
    std::vector<std::uint8_t> canonical;
    canonical.reserve(80 + chunks.size() * 96);
    auto append_u32 = [&canonical](std::uint32_t value) {
        const auto offset = canonical.size();
        canonical.resize(offset + 4);
        write_u32(canonical, offset, value);
    };
    auto append_u64 = [&canonical](std::uint64_t value) {
        const auto offset = canonical.size();
        canonical.resize(offset + 8);
        write_u64(canonical, offset, value);
    };
    auto append_digest = [&canonical](const semantic::IdentityDigest& value) {
        canonical.insert(canonical.end(), value.begin(), value.end());
    };
    append_u32(kContainerVersion);
    append_u32(schema_version);
    append_digest(measurement_schema);
    append_digest(source);
    for (const auto& chunk : chunks) {
        append_u32(static_cast<std::uint32_t>(chunk.kind));
        append_u32(chunk.schema_version);
        append_u32(static_cast<std::uint32_t>(chunk.codec));
        append_u64(chunk.stored_size);
        append_u64(chunk.uncompressed_size);
        append_u64(chunk.element_count);
        append_u32(chunk.component_count);
        append_digest(chunk.semantic_identity);
        append_digest(chunk.content_digest);
    }
    return digest_bytes(canonical);
}

void validate_limits(const ArtifactLimits& limits) {
    if (limits.max_chunks == 0 || limits.max_container_bytes < kHeaderSize ||
        limits.max_stored_bytes == 0 ||
        limits.max_uncompressed_bytes == 0 ||
        limits.max_chunk_uncompressed_bytes == 0 ||
        limits.max_expansion_ratio == 0) {
        throw std::invalid_argument("Invalid measurement artifact limits");
    }
}

}

std::vector<std::uint8_t> write_measurement_artifact(
    const MeasurementArtifact& artifact,
    const ArtifactLimits& limits) {
    validate_limits(limits);
    if (artifact.schema_version == 0 || artifact.chunks.empty() ||
        artifact.chunks.size() > limits.max_chunks ||
        semantic::identity_empty(artifact.measurement_schema_identity) ||
        semantic::identity_empty(artifact.source_identity)) {
        throw std::invalid_argument("Invalid measurement artifact");
    }

    std::vector<std::vector<std::uint8_t>> stored_chunks;
    std::vector<ArtifactChunkDescriptor> descriptors;
    stored_chunks.reserve(artifact.chunks.size());
    descriptors.reserve(artifact.chunks.size());
    std::uint64_t total_stored = 0;
    std::uint64_t total_uncompressed = 0;
    for (const auto& chunk : artifact.chunks) {
        if (chunk.schema_version == 0 || chunk.component_count == 0 ||
            semantic::identity_empty(chunk.semantic_identity) ||
            chunk.payload.size() > limits.max_chunk_uncompressed_bytes) {
            throw std::invalid_argument("Invalid measurement artifact chunk");
        }
        auto stored = chunk.codec == ArtifactCodec::RunLength
            ? run_length_encode(chunk.payload)
            : chunk.payload;
        if (chunk.codec != ArtifactCodec::None &&
            chunk.codec != ArtifactCodec::RunLength) {
            throw std::invalid_argument("Unsupported artifact codec");
        }
        if (!checked_add(total_stored, stored.size(), total_stored) ||
            !checked_add(total_uncompressed,
                         chunk.payload.size(),
                         total_uncompressed) ||
            total_stored > limits.max_stored_bytes ||
            total_uncompressed > limits.max_uncompressed_bytes) {
            throw std::length_error("Measurement artifact budget exceeded");
        }
        ArtifactChunkDescriptor descriptor;
        descriptor.kind = chunk.kind;
        descriptor.schema_version = chunk.schema_version;
        descriptor.codec = chunk.codec;
        descriptor.stored_size = stored.size();
        descriptor.uncompressed_size = chunk.payload.size();
        descriptor.element_count = chunk.element_count;
        descriptor.component_count = chunk.component_count;
        descriptor.semantic_identity = chunk.semantic_identity;
        descriptor.content_digest = digest_bytes(chunk.payload);
        descriptors.push_back(descriptor);
        stored_chunks.push_back(std::move(stored));
    }

    std::uint64_t directory_bytes = 0;
    std::uint64_t payload_offset = 0;
    std::uint64_t total_size = 0;
    if (!checked_multiply(descriptors.size(),
                          kDirectoryEntrySize,
                          directory_bytes) ||
        !checked_add(kHeaderSize, directory_bytes, payload_offset) ||
        !checked_add(payload_offset, total_stored, total_size) ||
        total_size > limits.max_container_bytes ||
        total_size > std::numeric_limits<std::size_t>::max()) {
        throw std::length_error("Measurement artifact container too large");
    }

    std::vector<std::uint8_t> result(static_cast<std::size_t>(total_size));
    std::ranges::copy(kMagic, result.begin());
    write_u32(result, 8, kContainerVersion);
    write_u32(result, 12, artifact.schema_version);
    write_u32(result, 16, static_cast<std::uint32_t>(descriptors.size()));
    write_u32(result, 20, 0);
    write_u64(result, 24, kHeaderSize);
    write_u64(result, 32, directory_bytes);
    write_u64(result, 40, payload_offset);
    write_u64(result, 48, total_uncompressed);
    write_digest(result, 56, artifact.measurement_schema_identity);
    write_digest(result, 88, artifact.source_identity);

    std::uint64_t cursor = payload_offset;
    for (std::size_t index = 0; index < descriptors.size(); ++index) {
        auto& descriptor = descriptors[index];
        descriptor.offset = cursor;
        const auto directory = kHeaderSize + index * kDirectoryEntrySize;
        write_u32(result, directory,
                  static_cast<std::uint32_t>(descriptor.kind));
        write_u32(result, directory + 4, descriptor.schema_version);
        write_u32(result, directory + 8,
                  static_cast<std::uint32_t>(descriptor.codec));
        write_u32(result, directory + 12, 0);
        write_u64(result, directory + 16, descriptor.offset);
        write_u64(result, directory + 24, descriptor.stored_size);
        write_u64(result, directory + 32, descriptor.uncompressed_size);
        write_u64(result, directory + 40, descriptor.element_count);
        write_u32(result, directory + 48, descriptor.component_count);
        write_u32(result, directory + 52, 0);
        write_digest(result, directory + 56, descriptor.semantic_identity);
        write_digest(result, directory + 88, descriptor.content_digest);
        std::ranges::copy(stored_chunks[index], result.begin() + cursor);
        cursor += descriptor.stored_size;
    }
    write_digest(result, 120,
                 artifact_identity(artifact.schema_version,
                                   artifact.measurement_schema_identity,
                                   artifact.source_identity,
                                   descriptors));
    return result;
}

ArtifactIndex inspect_measurement_artifact(
    std::span<const std::uint8_t> bytes,
    const ArtifactLimits& limits) {
    const auto index_size = measurement_artifact_index_size(bytes, limits);
    return inspect_measurement_artifact_index(
        bytes.first(static_cast<std::size_t>(index_size)),
        bytes.size(),
        limits);
}

std::uint64_t measurement_artifact_index_size(
    std::span<const std::uint8_t> header,
    const ArtifactLimits& limits) {
    validate_limits(limits);
    if (header.size() < kHeaderSize ||
        !std::ranges::equal(kMagic, header.first(kMagic.size()))) {
        throw std::invalid_argument("Invalid measurement artifact header");
    }
    const auto chunk_count = read_u32(header, 16);
    const auto directory_offset = read_u64(header, 24);
    const auto directory_size = read_u64(header, 32);
    const auto payload_offset = read_u64(header, 40);
    std::uint64_t expected_directory_size = 0;
    std::uint64_t expected_payload_offset = 0;
    if (chunk_count == 0 || chunk_count > limits.max_chunks ||
        !checked_multiply(chunk_count,
                          kDirectoryEntrySize,
                          expected_directory_size) ||
        directory_offset != kHeaderSize ||
        directory_size != expected_directory_size ||
        !checked_add(directory_offset,
                     directory_size,
                     expected_payload_offset) ||
        payload_offset != expected_payload_offset ||
        payload_offset > limits.max_container_bytes) {
        throw std::invalid_argument("Invalid measurement artifact index size");
    }
    return payload_offset;
}

ArtifactIndex inspect_measurement_artifact_index(
    std::span<const std::uint8_t> bytes,
    std::uint64_t total_container_bytes,
    const ArtifactLimits& limits) {
    validate_limits(limits);
    const auto required_index_size =
        measurement_artifact_index_size(bytes, limits);
    if (bytes.size() != required_index_size ||
        total_container_bytes < required_index_size ||
        total_container_bytes > limits.max_container_bytes) {
        throw std::invalid_argument("Invalid measurement artifact index span");
    }
    ArtifactIndex result;
    result.container_version = read_u32(bytes, 8);
    result.schema_version = read_u32(bytes, 12);
    const auto chunk_count = read_u32(bytes, 16);
    const auto flags = read_u32(bytes, 20);
    const auto directory_offset = read_u64(bytes, 24);
    const auto directory_size = read_u64(bytes, 32);
    const auto payload_offset = read_u64(bytes, 40);
    result.total_uncompressed_bytes = read_u64(bytes, 48);
    result.measurement_schema_identity = read_digest(bytes, 56);
    result.source_identity = read_digest(bytes, 88);
    result.artifact_identity = read_digest(bytes, 120);

    std::uint64_t expected_directory_size = 0;
    std::uint64_t expected_payload_offset = 0;
    if (result.container_version != kContainerVersion ||
        result.schema_version == 0 || flags != 0 || chunk_count == 0 ||
        chunk_count > limits.max_chunks ||
        semantic::identity_empty(result.measurement_schema_identity) ||
        semantic::identity_empty(result.source_identity) ||
        semantic::identity_empty(result.artifact_identity) ||
        !checked_multiply(chunk_count,
                          kDirectoryEntrySize,
                          expected_directory_size) ||
        directory_offset != kHeaderSize ||
        directory_size != expected_directory_size ||
        !checked_add(directory_offset,
                     directory_size,
                     expected_payload_offset) ||
        payload_offset != expected_payload_offset ||
        payload_offset > total_container_bytes ||
        result.total_uncompressed_bytes > limits.max_uncompressed_bytes) {
        throw std::invalid_argument("Invalid measurement artifact index");
    }

    result.chunks.reserve(chunk_count);
    std::uint64_t stored_total = 0;
    std::uint64_t uncompressed_total = 0;
    std::uint64_t cursor = payload_offset;
    for (std::uint32_t index = 0; index < chunk_count; ++index) {
        const auto directory = kHeaderSize + index * kDirectoryEntrySize;
        ArtifactChunkDescriptor chunk;
        chunk.kind = static_cast<ArtifactChunkKind>(read_u32(bytes, directory));
        chunk.schema_version = read_u32(bytes, directory + 4);
        chunk.codec = static_cast<ArtifactCodec>(read_u32(bytes, directory + 8));
        const auto chunk_flags = read_u32(bytes, directory + 12);
        chunk.offset = read_u64(bytes, directory + 16);
        chunk.stored_size = read_u64(bytes, directory + 24);
        chunk.uncompressed_size = read_u64(bytes, directory + 32);
        chunk.element_count = read_u64(bytes, directory + 40);
        chunk.component_count = read_u32(bytes, directory + 48);
        const auto reserved = read_u32(bytes, directory + 52);
        chunk.semantic_identity = read_digest(bytes, directory + 56);
        chunk.content_digest = read_digest(bytes, directory + 88);
        std::uint64_t chunk_end = 0;
        if (chunk.schema_version == 0 || chunk.component_count == 0 ||
            chunk_flags != 0 || reserved != 0 ||
            semantic::identity_empty(chunk.semantic_identity) ||
            semantic::identity_empty(chunk.content_digest) ||
            (chunk.codec != ArtifactCodec::None &&
             chunk.codec != ArtifactCodec::RunLength) ||
            chunk.offset != cursor ||
            chunk.uncompressed_size > limits.max_chunk_uncompressed_bytes ||
            !checked_add(chunk.offset, chunk.stored_size, chunk_end) ||
            chunk_end > total_container_bytes ||
            !checked_add(stored_total, chunk.stored_size, stored_total) ||
            !checked_add(uncompressed_total,
                         chunk.uncompressed_size,
                         uncompressed_total) ||
            stored_total > limits.max_stored_bytes ||
            uncompressed_total > limits.max_uncompressed_bytes) {
            throw std::invalid_argument("Invalid measurement artifact chunk index");
        }
        if (chunk.codec == ArtifactCodec::RunLength &&
            expansion_ratio_exceeded(chunk.uncompressed_size,
                                     chunk.stored_size,
                                     limits.max_expansion_ratio)) {
            throw std::length_error("Artifact expansion ratio exceeded");
        }
        cursor = chunk_end;
        result.chunks.push_back(chunk);
    }
    if (cursor != total_container_bytes ||
        uncompressed_total != result.total_uncompressed_bytes ||
        result.artifact_identity !=
            artifact_identity(result.schema_version,
                              result.measurement_schema_identity,
                              result.source_identity,
                              result.chunks)) {
        throw std::invalid_argument("Measurement artifact identity mismatch");
    }
    return result;
}

std::vector<std::uint8_t> read_artifact_chunk(
    std::span<const std::uint8_t> bytes,
    const ArtifactIndex& index,
    std::size_t chunk_index,
    const ArtifactLimits& limits) {
    if (chunk_index >= index.chunks.size()) {
        throw std::out_of_range("Artifact chunk index out of range");
    }
    const auto& chunk = index.chunks[chunk_index];
    if (chunk.offset > bytes.size() ||
        chunk.stored_size > bytes.size() - chunk.offset) {
        throw std::invalid_argument("Truncated artifact chunk payload");
    }
    const auto stored = bytes.subspan(
        static_cast<std::size_t>(chunk.offset),
        static_cast<std::size_t>(chunk.stored_size));
    return read_artifact_chunk_payload(stored, chunk, limits);
}

std::vector<std::uint8_t> read_artifact_chunk_payload(
    std::span<const std::uint8_t> stored_payload,
    const ArtifactChunkDescriptor& descriptor,
    const ArtifactLimits& limits) {
    validate_limits(limits);
    if (stored_payload.size() != descriptor.stored_size ||
        descriptor.uncompressed_size >
            limits.max_chunk_uncompressed_bytes ||
        (descriptor.codec != ArtifactCodec::None &&
         descriptor.codec != ArtifactCodec::RunLength)) {
        throw std::invalid_argument("Invalid artifact chunk payload span");
    }
    auto result = descriptor.codec == ArtifactCodec::RunLength
        ? run_length_decode(stored_payload,
                            descriptor.uncompressed_size,
                            limits)
        : std::vector<std::uint8_t>(stored_payload.begin(),
                                    stored_payload.end());
    if (result.size() != descriptor.uncompressed_size ||
        digest_bytes(result) != descriptor.content_digest) {
        throw std::invalid_argument("Artifact chunk digest mismatch");
    }
    return result;
}

MeasurementArtifact read_measurement_artifact(
    std::span<const std::uint8_t> bytes,
    const ArtifactLimits& limits) {
    const auto index = inspect_measurement_artifact(bytes, limits);
    MeasurementArtifact result;
    result.schema_version = index.schema_version;
    result.measurement_schema_identity = index.measurement_schema_identity;
    result.source_identity = index.source_identity;
    result.chunks.reserve(index.chunks.size());
    for (std::size_t chunk_index = 0;
         chunk_index < index.chunks.size();
         ++chunk_index) {
        const auto& descriptor = index.chunks[chunk_index];
        result.chunks.push_back({
            descriptor.kind,
            descriptor.schema_version,
            descriptor.codec,
            descriptor.semantic_identity,
            descriptor.element_count,
            descriptor.component_count,
            read_artifact_chunk(bytes, index, chunk_index, limits)});
    }
    return result;
}

bool has_sufficient_statistics(const ArtifactIndex& index) {
    const auto has = [&index](ArtifactChunkKind kind) {
        return std::ranges::any_of(
            index.chunks,
            [kind](const ArtifactChunkDescriptor& chunk) {
                return chunk.kind == kind;
            });
    };
    return has(ArtifactChunkKind::SampleCount) &&
           has(ArtifactChunkKind::FirstMoment) &&
           has(ArtifactChunkKind::SecondMoment);
}

}
