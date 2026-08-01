#include "ure/reconstruction/checkpoint.hpp"

#include "ure/runtime/multi_backend.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>

namespace ure::reconstruction {
namespace {

constexpr std::uint32_t kMetadataMagic = 0x4d425255;

semantic::IdentityDigest digest_payload(
    std::span<const std::uint8_t> bytes) {
    return runtime::identity_digest(std::as_bytes(bytes));
}

class Writer {
public:
    void u8(std::uint8_t value) { bytes.push_back(value); }
    void u32(std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) {
            u8(static_cast<std::uint8_t>(value >> shift));
        }
    }
    void u64(std::uint64_t value) {
        for (int shift = 0; shift < 64; shift += 8) {
            u8(static_cast<std::uint8_t>(value >> shift));
        }
    }
    void i64(std::int64_t value) {
        u64(std::bit_cast<std::uint64_t>(value));
    }
    void digest(const semantic::IdentityDigest& value) {
        bytes.insert(bytes.end(), value.begin(), value.end());
    }
    void unit(const semantic::UnitDescriptor& value) {
        u8(static_cast<std::uint8_t>(value.dimension.length));
        u8(static_cast<std::uint8_t>(value.dimension.mass));
        u8(static_cast<std::uint8_t>(value.dimension.time));
        u8(static_cast<std::uint8_t>(value.dimension.electric_current));
        u8(static_cast<std::uint8_t>(value.dimension.temperature));
        u8(static_cast<std::uint8_t>(value.dimension.amount));
        u8(static_cast<std::uint8_t>(value.dimension.luminous_intensity));
        u64(std::bit_cast<std::uint64_t>(value.scale_to_si));
        u64(std::bit_cast<std::uint64_t>(value.offset_to_si));
        u8(value.affine ? 1 : 0);
    }
    std::vector<std::uint8_t> bytes;
};

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> input) : bytes(input) {}
    std::uint8_t u8() {
        require(1);
        return bytes[cursor++];
    }
    std::uint32_t u32() {
        std::uint32_t value = 0;
        for (int shift = 0; shift < 32; shift += 8) {
            value |= static_cast<std::uint32_t>(u8()) << shift;
        }
        return value;
    }
    std::uint64_t u64() {
        std::uint64_t value = 0;
        for (int shift = 0; shift < 64; shift += 8) {
            value |= static_cast<std::uint64_t>(u8()) << shift;
        }
        return value;
    }
    std::int64_t i64() {
        return std::bit_cast<std::int64_t>(u64());
    }
    semantic::IdentityDigest digest() {
        semantic::IdentityDigest value{};
        for (auto& byte : value) byte = u8();
        return value;
    }
    semantic::UnitDescriptor unit() {
        semantic::UnitDescriptor value;
        value.dimension.length = static_cast<std::int8_t>(u8());
        value.dimension.mass = static_cast<std::int8_t>(u8());
        value.dimension.time = static_cast<std::int8_t>(u8());
        value.dimension.electric_current = static_cast<std::int8_t>(u8());
        value.dimension.temperature = static_cast<std::int8_t>(u8());
        value.dimension.amount = static_cast<std::int8_t>(u8());
        value.dimension.luminous_intensity = static_cast<std::int8_t>(u8());
        value.scale_to_si = std::bit_cast<double>(u64());
        value.offset_to_si = std::bit_cast<double>(u64());
        value.affine = u8() != 0;
        return value;
    }
    bool done() const { return cursor == bytes.size(); }

private:
    void require(std::size_t count) {
        if (count > bytes.size() - cursor) {
            throw std::invalid_argument("Truncated measurement metadata");
        }
    }
    std::span<const std::uint8_t> bytes;
    std::size_t cursor = 0;
};

void write_observable(Writer& writer,
                      const transport::ObservableDescriptor& value) {
    writer.u32(value.version);
    writer.u8(static_cast<std::uint8_t>(value.kind));
    writer.u8(static_cast<std::uint8_t>(value.value_domain));
    writer.u8(static_cast<std::uint8_t>(value.coherence));
    writer.u32(value.component_count);
    writer.u8(value.time_resolved ? 1 : 0);
    writer.unit(value.unit);
    writer.digest(value.phase_reference_identity);
    writer.digest(value.sensor_response_identity);
}

transport::ObservableDescriptor read_observable(Reader& reader) {
    transport::ObservableDescriptor value;
    value.version = reader.u32();
    value.kind = static_cast<transport::ObservableKind>(reader.u8());
    value.value_domain = static_cast<transport::ValueDomain>(reader.u8());
    value.coherence = static_cast<transport::CoherenceClass>(reader.u8());
    value.component_count = reader.u32();
    value.time_resolved = reader.u8() != 0;
    value.unit = reader.unit();
    value.phase_reference_identity = reader.digest();
    value.sensor_response_identity = reader.digest();
    return value;
}

void write_identities(Writer& writer,
                      const semantic::ProvenanceIdentitySet& value) {
    writer.digest(value.world_definition);
    writer.digest(value.world_state);
    writer.digest(value.time_sample);
    writer.digest(value.observation_snapshot);
    writer.digest(value.technique_graph);
    writer.digest(value.measurement_schema);
    writer.digest(value.parameter_set);
    writer.digest(value.solver_semantics);
    writer.digest(value.evidence);
}

semantic::ProvenanceIdentitySet read_identities(Reader& reader) {
    semantic::ProvenanceIdentitySet value;
    value.world_definition = reader.digest();
    value.world_state = reader.digest();
    value.time_sample = reader.digest();
    value.observation_snapshot = reader.digest();
    value.technique_graph = reader.digest();
    value.measurement_schema = reader.digest();
    value.parameter_set = reader.digest();
    value.solver_semantics = reader.digest();
    value.evidence = reader.digest();
    return value;
}

std::vector<std::uint8_t> encode_metadata(
    const MeasurementBundle& bundle) {
    Writer writer;
    writer.u32(kMetadataMagic);
    writer.u32(kMeasurementCheckpointVersion);
    writer.u32(bundle.schema.version);
    writer.u32(bundle.schema.width);
    writer.u32(bundle.schema.height);
    writer.digest(bundle.schema.schema_identity);
    writer.u32(static_cast<std::uint32_t>(bundle.schema.planes.size()));
    for (const auto& plane : bundle.schema.planes) {
        writer.u32(plane.version);
        writer.u8(static_cast<std::uint8_t>(plane.kind));
        writer.u8(static_cast<std::uint8_t>(plane.scalar_type));
        writer.u8(static_cast<std::uint8_t>(plane.merge_rule));
        writer.u8(static_cast<std::uint8_t>(plane.retention));
        writer.digest(plane.semantic_identity);
        write_observable(writer, plane.observable);
        writer.unit(plane.unit);
        writer.u64(plane.element_count);
        writer.u32(plane.component_count);
        writer.u32(plane.validity_plane);
        writer.u8(static_cast<std::uint8_t>(plane.derivation.kind));
        writer.u32(plane.derivation.count_plane);
        writer.u32(plane.derivation.first_plane);
        writer.u32(plane.derivation.second_plane);
        writer.u32(plane.derivation.cross_plane);
        writer.u8(plane.required ? 1 : 0);
    }
    write_identities(writer, bundle.provenance.identities);
    writer.u64(bundle.provenance.exposure.basis.ticks_per_second);
    writer.i64(bundle.provenance.exposure.basis.synchronization_epoch);
    writer.digest(bundle.provenance.exposure.basis.clock_identity);
    writer.i64(bundle.provenance.exposure.start_tick);
    writer.i64(bundle.provenance.exposure.end_tick);
    writer.digest(bundle.provenance.sample_namespace_identity);
    writer.digest(bundle.provenance.producer_identity);
    writer.u32(static_cast<std::uint32_t>(
        bundle.provenance.sample_ranges.size()));
    for (const auto& range : bundle.provenance.sample_ranges) {
        writer.u64(range.start);
        writer.u64(range.count);
    }
    return std::move(writer.bytes);
}

MeasurementBundle decode_metadata(
    std::span<const std::uint8_t> bytes) {
    Reader reader(bytes);
    if (reader.u32() != kMetadataMagic ||
        reader.u32() != kMeasurementCheckpointVersion) {
        throw std::invalid_argument("Invalid measurement metadata version");
    }
    MeasurementBundle result;
    result.schema.version = reader.u32();
    result.schema.width = reader.u32();
    result.schema.height = reader.u32();
    result.schema.schema_identity = reader.digest();
    const auto plane_count = reader.u32();
    if (plane_count == 0 || plane_count > 65536) {
        throw std::length_error("Invalid measurement plane count");
    }
    result.schema.planes.reserve(plane_count);
    for (std::uint32_t index = 0; index < plane_count; ++index) {
        MeasurementPlaneDescriptor plane;
        plane.version = reader.u32();
        plane.kind = static_cast<MeasurementPlaneKind>(reader.u8());
        plane.scalar_type = static_cast<MeasurementScalarType>(reader.u8());
        plane.merge_rule = static_cast<MeasurementMergeRule>(reader.u8());
        plane.retention = static_cast<MeasurementRetention>(reader.u8());
        plane.semantic_identity = reader.digest();
        plane.observable = read_observable(reader);
        plane.unit = reader.unit();
        plane.element_count = reader.u64();
        plane.component_count = reader.u32();
        plane.validity_plane = reader.u32();
        plane.derivation.kind = static_cast<MeasurementDerivationKind>(
            reader.u8());
        plane.derivation.count_plane = reader.u32();
        plane.derivation.first_plane = reader.u32();
        plane.derivation.second_plane = reader.u32();
        plane.derivation.cross_plane = reader.u32();
        plane.required = reader.u8() != 0;
        result.schema.planes.push_back(plane);
    }
    result.provenance.identities = read_identities(reader);
    result.provenance.exposure.basis.ticks_per_second = reader.u64();
    result.provenance.exposure.basis.synchronization_epoch = reader.i64();
    result.provenance.exposure.basis.clock_identity = reader.digest();
    result.provenance.exposure.start_tick = reader.i64();
    result.provenance.exposure.end_tick = reader.i64();
    result.provenance.sample_namespace_identity = reader.digest();
    result.provenance.producer_identity = reader.digest();
    const auto range_count = reader.u32();
    if (range_count == 0 || range_count > 1048576) {
        throw std::length_error("Invalid measurement range count");
    }
    result.provenance.sample_ranges.reserve(range_count);
    for (std::uint32_t index = 0; index < range_count; ++index) {
        result.provenance.sample_ranges.push_back(
            {reader.u64(), reader.u64()});
    }
    if (!reader.done() ||
        !validate_measurement_schema(result.schema).ok()) {
        throw std::invalid_argument("Invalid measurement metadata");
    }
    return result;
}

research::ArtifactChunkKind chunk_kind(MeasurementPlaneKind kind) {
    switch (kind) {
    case MeasurementPlaneKind::SampleCount:
        return research::ArtifactChunkKind::SampleCount;
    case MeasurementPlaneKind::FirstMoment:
        return research::ArtifactChunkKind::FirstMoment;
    case MeasurementPlaneKind::SecondMoment:
        return research::ArtifactChunkKind::SecondMoment;
    case MeasurementPlaneKind::CrossMoment:
        return research::ArtifactChunkKind::CrossMoment;
    case MeasurementPlaneKind::Covariance:
        return research::ArtifactChunkKind::Covariance;
    default:
        return research::ArtifactChunkKind::RawObservable;
    }
}

bool chunk_matches_plane(
    research::ArtifactChunkKind kind,
    std::uint32_t schema_version,
    std::uint64_t element_count,
    std::uint32_t component_count,
    const semantic::IdentityDigest& semantic_identity,
    const MeasurementPlaneDescriptor& plane) {
    return kind == chunk_kind(plane.kind) &&
           schema_version == plane.version &&
           component_count == plane.component_count &&
           element_count <= plane.element_count &&
           (plane.merge_rule == MeasurementMergeRule::Append ||
            element_count == plane.element_count) &&
           semantic_identity == plane.semantic_identity;
}

}

std::vector<std::uint8_t> write_measurement_checkpoint(
    const MeasurementBundle& bundle,
    const research::ArtifactLimits& limits) {
    if (!validate_measurement_bundle(bundle).ok()) {
        throw std::invalid_argument("Invalid measurement bundle");
    }
    research::MeasurementArtifact artifact;
    artifact.schema_version = kMeasurementCheckpointVersion;
    artifact.measurement_schema_identity = bundle.schema.schema_identity;
    auto metadata = encode_metadata(bundle);
    artifact.source_identity = digest_payload(metadata);
    artifact.chunks.push_back({
        research::ArtifactChunkKind::Metadata, 1,
        research::ArtifactCodec::None,
        runtime::identity_digest("ure.measurement-bundle.metadata.v1"),
        metadata.size(), 1, std::move(metadata)});
    for (std::size_t index = 0; index < bundle.planes.size(); ++index) {
        const auto& descriptor = bundle.schema.planes[index];
        const auto& plane = bundle.planes[index];
        artifact.chunks.push_back({
            chunk_kind(descriptor.kind), descriptor.version,
            descriptor.kind == MeasurementPlaneKind::ValidityMask
                ? research::ArtifactCodec::RunLength
                : research::ArtifactCodec::None,
            descriptor.semantic_identity,
            descriptor.merge_rule == MeasurementMergeRule::Append
                ? plane.payload.size() /
                      (measurement_scalar_size(descriptor.scalar_type) *
                       descriptor.component_count)
                : descriptor.element_count,
            descriptor.component_count,
            plane.payload});
    }
    return research::write_measurement_artifact(artifact, limits);
}

MeasurementCheckpointIndex inspect_measurement_checkpoint(
    std::span<const std::uint8_t> index_bytes,
    std::uint64_t total_container_bytes,
    const research::ArtifactLimits& limits) {
    MeasurementCheckpointIndex result;
    result.artifact = research::inspect_measurement_artifact_index(
        index_bytes, total_container_bytes, limits);
    if (result.artifact.schema_version !=
            kMeasurementCheckpointVersion ||
        result.artifact.chunks.empty()) {
        throw std::invalid_argument("Invalid measurement checkpoint");
    }
    bool found_metadata = false;
    for (std::size_t index = 0;
         index < result.artifact.chunks.size(); ++index) {
        if (result.artifact.chunks[index].kind ==
            research::ArtifactChunkKind::Metadata) {
            if (found_metadata) {
                throw std::invalid_argument(
                    "Duplicate measurement metadata");
            }
            found_metadata = true;
            result.metadata_chunk = index;
        } else {
            result.plane_chunks.push_back(index);
        }
    }
    if (!found_metadata || result.plane_chunks.empty()) {
        throw std::invalid_argument("Incomplete measurement checkpoint");
    }
    return result;
}

MeasurementPlane read_measurement_checkpoint_plane(
    std::span<const std::uint8_t> stored_payload,
    const MeasurementCheckpointIndex& index,
    std::size_t plane_ordinal,
    const MeasurementSchema& expected_schema,
    const research::ArtifactLimits& limits) {
    if (!validate_measurement_schema(expected_schema).ok() ||
        expected_schema.schema_identity !=
            index.artifact.measurement_schema_identity ||
        plane_ordinal >= expected_schema.planes.size() ||
        index.plane_chunks.size() != expected_schema.planes.size()) {
        throw std::invalid_argument("Measurement plane schema mismatch");
    }
    const auto& chunk =
        index.artifact.chunks[index.plane_chunks[plane_ordinal]];
    const auto& descriptor = expected_schema.planes[plane_ordinal];
    if (!chunk_matches_plane(
            chunk.kind, chunk.schema_version, chunk.element_count,
            chunk.component_count, chunk.semantic_identity,
            descriptor)) {
        throw std::invalid_argument("Measurement plane identity mismatch");
    }
    auto payload = research::read_artifact_chunk_payload(
        stored_payload, chunk, limits);
    if (!validate_measurement_plane_payload(
            descriptor, payload).ok()) {
        throw std::invalid_argument("Invalid measurement plane payload");
    }
    return {static_cast<std::uint32_t>(plane_ordinal),
            std::move(payload)};
}

MeasurementBundle read_measurement_checkpoint(
    std::span<const std::uint8_t> bytes,
    const research::ArtifactLimits& limits) {
    const auto artifact =
        research::read_measurement_artifact(bytes, limits);
    if (artifact.schema_version != kMeasurementCheckpointVersion ||
        artifact.chunks.empty()) {
        throw std::invalid_argument("Invalid measurement checkpoint");
    }
    const auto metadata = std::ranges::find_if(
        artifact.chunks,
        [](const research::ArtifactChunk& chunk) {
            return chunk.kind == research::ArtifactChunkKind::Metadata;
        });
    if (metadata == artifact.chunks.end()) {
        throw std::invalid_argument("Missing measurement metadata");
    }
    if (digest_payload(metadata->payload) !=
        artifact.source_identity) {
        throw std::invalid_argument(
            "Measurement metadata identity mismatch");
    }
    auto result = decode_metadata(metadata->payload);
    if (result.schema.schema_identity !=
        artifact.measurement_schema_identity) {
        throw std::invalid_argument("Measurement schema identity mismatch");
    }
    for (const auto& chunk : artifact.chunks) {
        if (&chunk == &*metadata) continue;
        const auto ordinal = result.planes.size();
        if (ordinal >= result.schema.planes.size() ||
            !chunk_matches_plane(
                chunk.kind, chunk.schema_version, chunk.element_count,
                chunk.component_count, chunk.semantic_identity,
                result.schema.planes[ordinal])) {
            throw std::invalid_argument("Measurement plane order mismatch");
        }
        result.planes.push_back({
            static_cast<std::uint32_t>(ordinal), chunk.payload});
    }
    if (!validate_measurement_bundle(result).ok()) {
        throw std::invalid_argument("Invalid checkpoint measurement bundle");
    }
    return result;
}

}
