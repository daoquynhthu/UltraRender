#include "ure/distributed_file_io.hpp"
#include "ure/distributed_wave_io.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace ure::gpu {

namespace {

constexpr std::array<char, 8> kRangeMagic = {'U', 'R', 'D', 'R', 'A', 'N', 'G', 'E'};
constexpr std::array<char, 8> kFrameMagic = {'U', 'R', 'D', 'F', 'R', 'A', 'M', 'E'};
constexpr std::array<char, 8> kComplexFrameMagic = {
    'U', 'R', 'D', 'C', 'F', 'L', 'D', '1'};
constexpr std::array<char, 8> kMutualFrameMagic = {
    'U', 'R', 'D', 'M', 'U', 'T', 'L', '1'};
constexpr int kVersion = 6;
constexpr int kExecutionMetadataVersion = 5;
constexpr int kLegacyVersion = 4;
constexpr std::uint64_t kMaximumProvenanceRecords = 65536;
constexpr std::uint32_t kMaximumIdentityLength = 4096;
constexpr std::uint64_t kMaximumIdentityBytes = 16ull << 20;
constexpr std::uint32_t kWaveFrameVersion = 1;
constexpr std::uint32_t kByteOrderMarker =
    0x01020304U;
constexpr std::uint64_t kMaximumWaveFileBytes =
    128ull << 20;

int pixel_value_count(int width, int height) {
    if (width <= 0 || height <= 0) {
        throw std::invalid_argument("distributed file dimensions must be positive");
    }
    constexpr int kChannels = 3;
    if (width > std::numeric_limits<int>::max() / height ||
        width * height > std::numeric_limits<int>::max() / kChannels) {
        throw std::overflow_error("distributed file dimensions overflow");
    }
    return width * height * kChannels;
}

template <typename T>
void write_value(std::ofstream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
    if (!out) {
        throw std::runtime_error("failed to write distributed file");
    }
}

template <typename T>
T read_value(std::ifstream& in) {
    T value{};
    in.read(reinterpret_cast<char*>(&value), sizeof(T));
    if (!in) {
        throw std::runtime_error("failed to read distributed file");
    }
    return value;
}

template <size_t N>
void write_magic(std::ofstream& out, const std::array<char, N>& magic) {
    out.write(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (!out) {
        throw std::runtime_error("failed to write distributed file magic");
    }
}

template <size_t N>
void read_magic(std::ifstream& in, const std::array<char, N>& expected) {
    std::array<char, N> actual{};
    in.read(actual.data(), static_cast<std::streamsize>(actual.size()));
    if (!in || actual != expected) {
        throw std::runtime_error("invalid distributed file magic");
    }
}

std::ofstream open_output(const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("failed to open distributed output file");
    }
    return out;
}

std::ifstream open_input(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("failed to open distributed input file");
    }
    return in;
}

void append_content_digest(
    const std::filesystem::path& path) {
    const auto file_bytes =
        std::filesystem::file_size(path);
    if (file_bytes >
        kMaximumWaveFileBytes -
            sizeof(runtime::IdentityDigest)) {
        throw std::length_error(
            "distributed wave file exceeds its bound");
    }
    std::vector<std::byte> bytes(
        static_cast<std::size_t>(file_bytes));
    auto input = open_input(path);
    input.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    if (!input) {
        throw std::runtime_error(
            "failed to hash distributed wave file");
    }
    const auto digest =
        runtime::identity_digest(bytes);
    std::ofstream output(
        path,
        std::ios::binary | std::ios::app);
    if (!output) {
        throw std::runtime_error(
            "failed to append distributed wave digest");
    }
    output.write(
        reinterpret_cast<const char*>(
            digest.data()),
        static_cast<std::streamsize>(
            digest.size()));
    if (!output) {
        throw std::runtime_error(
            "failed to write distributed wave digest");
    }
}

void verify_content_digest(
    const std::filesystem::path& path) {
    const auto file_bytes =
        std::filesystem::file_size(path);
    if (file_bytes <
            sizeof(runtime::IdentityDigest) ||
        file_bytes > kMaximumWaveFileBytes) {
        throw std::runtime_error(
            "distributed wave file size is invalid");
    }
    const std::size_t payload_size =
        static_cast<std::size_t>(
            file_bytes -
            sizeof(runtime::IdentityDigest));
    std::vector<std::byte> bytes(payload_size);
    runtime::IdentityDigest stored{};
    auto input = open_input(path);
    input.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    input.read(
        reinterpret_cast<char*>(stored.data()),
        static_cast<std::streamsize>(
            stored.size()));
    if (!input ||
        runtime::identity_digest(bytes) != stored) {
        throw std::runtime_error(
            "distributed wave file content digest mismatch");
    }
}

void read_digest_footer(std::ifstream& in) {
    static_cast<void>(
        read_value<runtime::IdentityDigest>(in));
    if (in.peek() !=
        std::char_traits<char>::eof()) {
        throw std::runtime_error(
            "distributed wave file has trailing data");
    }
}

void write_string(
    std::ofstream& out,
    const std::string& value) {
    if (value.size() > kMaximumIdentityLength) {
        throw std::invalid_argument(
            "distributed execution identity is too long");
    }
    write_value(
        out, static_cast<std::uint32_t>(value.size()));
    out.write(
        value.data(),
        static_cast<std::streamsize>(value.size()));
    if (!out) {
        throw std::runtime_error(
            "failed to write distributed execution identity");
    }
}

std::string read_string(
    std::ifstream& in,
    std::uint64_t& total_bytes) {
    const auto size = read_value<std::uint32_t>(in);
    if (size > kMaximumIdentityLength ||
        total_bytes >
            kMaximumIdentityBytes - size) {
        throw std::runtime_error(
            "distributed execution identity budget exceeded");
    }
    total_bytes += size;
    std::string value(size, '\0');
    in.read(
        value.data(),
        static_cast<std::streamsize>(size));
    if (!in) {
        throw std::runtime_error(
            "truncated distributed execution identity");
    }
    return value;
}

void write_execution_identity(
    std::ofstream& out,
    const runtime::BackendExecutionIdentity& identity) {
    write_value(
        out,
        static_cast<std::uint32_t>(identity.backend));
    write_value(out, identity.vendor_id);
    write_value(out, identity.device_id);
    write_string(out, identity.adapter_id);
    write_string(out, identity.driver_identity);
    write_string(out, identity.compiler_identity);
    write_value(out, identity.executable_identity);
}

runtime::BackendExecutionIdentity read_execution_identity(
    std::ifstream& in,
    std::uint64_t& identity_bytes) {
    runtime::BackendExecutionIdentity identity;
    identity.backend = static_cast<BackendKind>(
        read_value<std::uint32_t>(in));
    identity.vendor_id =
        read_value<std::uint32_t>(in);
    identity.device_id =
        read_value<std::uint32_t>(in);
    identity.adapter_id =
        read_string(in, identity_bytes);
    identity.driver_identity =
        read_string(in, identity_bytes);
    identity.compiler_identity =
        read_string(in, identity_bytes);
    identity.executable_identity =
        read_value<runtime::IdentityDigest>(in);
    return identity;
}

void write_execution_metadata(
    std::ofstream& out,
    const runtime::MergeExecutionMetadata& metadata) {
    write_value(
        out,
        metadata.compatibility.schedule_version);
    write_value(
        out,
        metadata.compatibility.required_features);
    write_value(
        out,
        static_cast<std::uint8_t>(
            metadata.compatibility.precision));
    write_value(
        out,
        static_cast<std::uint8_t>(
            metadata.compatibility.coherence));
    write_value(
        out,
        metadata.compatibility.semantic_identity);
    write_value(
        out,
        metadata.compatibility.resource_schema_version);
    if (metadata.shards.size() >
        kMaximumProvenanceRecords) {
        throw std::invalid_argument(
            "distributed provenance record budget exceeded");
    }
    write_value(
        out,
        static_cast<std::uint64_t>(
            metadata.shards.size()));
    for (const auto& shard : metadata.shards) {
        write_value(out, shard.sample_start);
        write_value(out, shard.sample_count);
        write_value(
            out, shard.spectral_domain_start);
        write_value(
            out, shard.spectral_domain_count);
        write_value(out, shard.frame_index);
        write_execution_identity(out, shard.worker);
        write_value(
            out, shard.resource_cache.schema_version);
        write_value(
            out,
            static_cast<std::uint32_t>(
                shard.resource_cache.backend));
        write_value(
            out, shard.resource_cache.digest);
    }
}

runtime::MergeExecutionMetadata read_execution_metadata(
    std::ifstream& in) {
    runtime::MergeExecutionMetadata metadata;
    metadata.compatibility.schedule_version =
        read_value<std::uint32_t>(in);
    metadata.compatibility.required_features =
        read_value<BackendFeatureSet>(in);
    metadata.compatibility.precision =
        static_cast<runtime::NumericPrecision>(
            read_value<std::uint8_t>(in));
    metadata.compatibility.coherence =
        static_cast<runtime::CoherenceMode>(
            read_value<std::uint8_t>(in));
    metadata.compatibility.semantic_identity =
        read_value<runtime::IdentityDigest>(in);
    metadata.compatibility.resource_schema_version =
        read_value<std::uint32_t>(in);
    const auto count =
        read_value<std::uint64_t>(in);
    if (count > kMaximumProvenanceRecords) {
        throw std::runtime_error(
            "distributed provenance record budget exceeded");
    }
    metadata.shards.reserve(
        static_cast<std::size_t>(count));
    std::uint64_t identity_bytes = 0;
    for (std::uint64_t index = 0;
         index < count;
         ++index) {
        runtime::SampleShardProvenance shard;
        shard.sample_start =
            read_value<std::uint64_t>(in);
        shard.sample_count =
            read_value<std::uint64_t>(in);
        shard.spectral_domain_start =
            read_value<std::uint64_t>(in);
        shard.spectral_domain_count =
            read_value<std::uint64_t>(in);
        shard.frame_index =
            read_value<std::uint32_t>(in);
        shard.worker =
            read_execution_identity(
                in, identity_bytes);
        shard.resource_cache.schema_version =
            read_value<std::uint32_t>(in);
        shard.resource_cache.backend =
            static_cast<BackendKind>(
                read_value<std::uint32_t>(in));
        shard.resource_cache.digest =
            read_value<runtime::IdentityDigest>(in);
        metadata.shards.push_back(std::move(shard));
    }
    return metadata;
}

void write_frame_semantics(
    std::ofstream& out,
    const DistributedFrameSemantics& semantics) {
    write_value(out, semantics.schema_version);
    write_value(
        out,
        static_cast<std::uint8_t>(semantics.kind));
    write_value(
        out,
        semantics.phase_reference_identity);
    write_value(
        out,
        semantics.field_layout_identity);
    write_value(out, semantics.source_id);
    write_value(out, semantics.group_id);
    write_value(out, semantics.realization_id);
    if (semantics.realization_ranges.size() >
        kMaxDistributedRealizationRanges) {
        throw std::invalid_argument(
            "distributed realization range budget exceeded");
    }
    write_value(
        out,
        static_cast<std::uint64_t>(
            semantics.realization_ranges.size()));
    for (const auto& range :
         semantics.realization_ranges) {
        write_value(out, range.start);
        write_value(out, range.count);
    }
}

DistributedFrameSemantics read_frame_semantics(
    std::ifstream& in) {
    DistributedFrameSemantics semantics;
    semantics.schema_version =
        read_value<std::uint32_t>(in);
    semantics.kind =
        static_cast<DistributedFrameKind>(
            read_value<std::uint8_t>(in));
    semantics.phase_reference_identity =
        read_value<runtime::IdentityDigest>(in);
    semantics.field_layout_identity =
        read_value<runtime::IdentityDigest>(in);
    semantics.source_id =
        read_value<std::uint64_t>(in);
    semantics.group_id =
        read_value<std::uint64_t>(in);
    semantics.realization_id =
        read_value<std::uint64_t>(in);
    const std::uint64_t range_count =
        read_value<std::uint64_t>(in);
    if (range_count >
        kMaxDistributedRealizationRanges) {
        throw std::runtime_error(
            "distributed realization range budget exceeded");
    }
    semantics.realization_ranges.reserve(
        static_cast<std::size_t>(range_count));
    for (std::uint64_t index = 0;
         index < range_count;
         ++index) {
        semantics.realization_ranges.push_back({
            read_value<std::uint64_t>(in),
            read_value<std::uint64_t>(in)});
    }
    return semantics;
}

void write_shard_metadata(std::ofstream& out, const DistributedShardMetadata& metadata) {
    if (!validate_shard_metadata(metadata)) {
        throw std::invalid_argument("invalid distributed shard metadata");
    }
    write_value(out, metadata.spectral.shard_id);
    write_value(out, metadata.spectral.shard_count);
    write_value(out, metadata.spectral.domain_bins);
    write_value(out, metadata.spectral.domain_start);
    write_value(out, metadata.spectral.domain_count);
    write_value(out, metadata.spectral.lambda_min);
    write_value(out, metadata.spectral.lambda_max);
    write_value(out, metadata.spectral.wavelength_pdf_integral);
    write_value(out, metadata.frame.frame_index);
    write_value(out, metadata.frame.frame_count);
    write_value(out, metadata.resources.content_hash);
    write_value(out, metadata.resources.descriptor_count);
    write_value(out, metadata.resources.logical_bytes);
    write_value(out, metadata.resources.minimum_resident_bytes);
    write_execution_metadata(
        out, metadata.execution);
    write_frame_semantics(
        out, metadata.frame_semantics);
}

DistributedShardMetadata read_shard_metadata(
    std::ifstream& in,
    int version) {
    DistributedShardMetadata metadata{};
    metadata.spectral.shard_id = read_value<int>(in);
    metadata.spectral.shard_count = read_value<int>(in);
    metadata.spectral.domain_bins = read_value<std::uint64_t>(in);
    metadata.spectral.domain_start = read_value<std::uint64_t>(in);
    metadata.spectral.domain_count = read_value<std::uint64_t>(in);
    metadata.spectral.lambda_min = read_value<float>(in);
    metadata.spectral.lambda_max = read_value<float>(in);
    metadata.spectral.wavelength_pdf_integral = read_value<float>(in);
    metadata.frame.frame_index = read_value<int>(in);
    metadata.frame.frame_count = read_value<int>(in);
    metadata.resources.content_hash =
        read_value<std::array<std::uint8_t, 32>>(in);
    metadata.resources.descriptor_count = read_value<std::uint64_t>(in);
    metadata.resources.logical_bytes = read_value<std::uint64_t>(in);
    metadata.resources.minimum_resident_bytes =
        read_value<std::uint64_t>(in);
    if (version >= kExecutionMetadataVersion) {
        metadata.execution =
            read_execution_metadata(in);
    }
    if (version >= kVersion) {
        metadata.frame_semantics =
            read_frame_semantics(in);
    }
    if (!validate_shard_metadata(metadata)) {
        throw std::runtime_error("invalid distributed shard metadata payload");
    }
    return metadata;
}

void write_estimator_metadata(
    std::ofstream& out,
    const IntegratorEstimatorMetadata& metadata) {
    if (!validate_integrator_estimator_metadata(metadata)) {
        throw std::invalid_argument("invalid integrator estimator metadata");
    }
    write_value(out, static_cast<std::uint32_t>(metadata.mode));
    write_value(out, static_cast<std::uint32_t>(metadata.policy));
    write_value(out, static_cast<std::uint8_t>(metadata.biased));
    write_value(out, static_cast<std::uint8_t>(metadata.temporal_reuse));
    write_value(out, static_cast<std::uint8_t>(metadata.spatial_reuse));
    write_value(out, metadata.sample_space_version);
    write_value(out, metadata.scene_epoch);
}

IntegratorEstimatorMetadata read_estimator_metadata(std::ifstream& in) {
    IntegratorEstimatorMetadata metadata;
    metadata.mode = static_cast<IntegratorMode>(read_value<std::uint32_t>(in));
    metadata.policy = static_cast<IntegratorEstimatorPolicy>(read_value<std::uint32_t>(in));
    metadata.biased = read_value<std::uint8_t>(in) != 0;
    metadata.temporal_reuse = read_value<std::uint8_t>(in) != 0;
    metadata.spatial_reuse = read_value<std::uint8_t>(in) != 0;
    metadata.sample_space_version = read_value<std::uint32_t>(in);
    metadata.scene_epoch = read_value<std::uint32_t>(in);
    if (!validate_integrator_estimator_metadata(metadata)) {
        throw std::runtime_error("invalid integrator estimator metadata payload");
    }
    return metadata;
}

} // namespace

DistributedFrameBuffer DistributedFrameBufferStorage::view() {
    return {width, height, total_samples, data.data(), shard, estimator};
}

void write_sample_range_file(const std::filesystem::path& path,
                             const DistributedSampleRange& range) {
    if (!validate_sample_range(range)) {
        throw std::invalid_argument("invalid distributed sample range");
    }
    auto out = open_output(path);
    write_magic(out, kRangeMagic);
    write_value(out, kVersion);
    write_value(out, range.node_id);
    write_value(out, range.node_count);
    write_value(out, range.sample_start);
    write_value(out, range.sample_count);
    write_value(out, range.total_samples);
    write_value(out, range.width);
    write_value(out, range.height);
    write_shard_metadata(out, range.shard);
    write_estimator_metadata(out, range.estimator);
}

DistributedSampleRange read_sample_range_file(const std::filesystem::path& path) {
    auto in = open_input(path);
    read_magic(in, kRangeMagic);
    const int version = read_value<int>(in);
    if (version != kVersion &&
        version != kExecutionMetadataVersion &&
        version != kLegacyVersion) {
        throw std::runtime_error("unsupported distributed range file version");
    }
    DistributedSampleRange range{};
    range.node_id = read_value<int>(in);
    range.node_count = read_value<int>(in);
    range.sample_start = read_value<int>(in);
    range.sample_count = read_value<int>(in);
    range.total_samples = read_value<int>(in);
    range.width = read_value<int>(in);
    range.height = read_value<int>(in);
    range.shard = read_shard_metadata(in, version);
    range.estimator = read_estimator_metadata(in);
    if (!validate_sample_range(range)) {
        throw std::runtime_error("invalid distributed range file payload");
    }
    return range;
}

void write_framebuffer_file(const std::filesystem::path& path,
                            const DistributedFrameBuffer& framebuffer) {
    if (!framebuffer.data) {
        throw std::invalid_argument("distributed framebuffer data must not be null");
    }
    if (framebuffer.total_samples < 0) {
        throw std::invalid_argument("distributed framebuffer sample count must be non-negative");
    }
    if (!validate_framebuffer_sample_provenance(
            framebuffer.shard,
            framebuffer.total_samples)) {
        throw std::invalid_argument(
            "distributed framebuffer sample provenance is invalid");
    }
    if (!runtime::is_legacy_merge_metadata(
            framebuffer.shard.execution) &&
        framebuffer.shard.execution
                .compatibility.coherence ==
            runtime::CoherenceMode::CoherentField) {
        throw std::invalid_argument(
            "RGB distributed framebuffer cannot store coherent fields");
    }
    const int count = pixel_value_count(framebuffer.width, framebuffer.height);
    auto out = open_output(path);
    write_magic(out, kFrameMagic);
    write_value(out, kVersion);
    write_value(out, framebuffer.width);
    write_value(out, framebuffer.height);
    write_value(out, framebuffer.total_samples);
    write_shard_metadata(out, framebuffer.shard);
    write_estimator_metadata(out, framebuffer.estimator);
    out.write(reinterpret_cast<const char*>(framebuffer.data),
              static_cast<std::streamsize>(count * sizeof(float)));
    if (!out) {
        throw std::runtime_error("failed to write distributed framebuffer payload");
    }
}

DistributedFrameBufferStorage read_framebuffer_file(const std::filesystem::path& path) {
    auto in = open_input(path);
    read_magic(in, kFrameMagic);
    const int version = read_value<int>(in);
    if (version != kVersion &&
        version != kExecutionMetadataVersion &&
        version != kLegacyVersion) {
        throw std::runtime_error("unsupported distributed framebuffer file version");
    }
    DistributedFrameBufferStorage storage;
    storage.width = read_value<int>(in);
    storage.height = read_value<int>(in);
    storage.total_samples = read_value<int>(in);
    storage.shard = read_shard_metadata(in, version);
    storage.estimator = read_estimator_metadata(in);
    if (storage.total_samples < 0) {
        throw std::runtime_error("invalid distributed framebuffer sample count");
    }
    if (!validate_framebuffer_sample_provenance(
            storage.shard,
            storage.total_samples)) {
        throw std::runtime_error(
            "invalid distributed framebuffer sample provenance");
    }
    if (storage.shard.frame_semantics.kind !=
        DistributedFrameKind::Radiance) {
        throw std::runtime_error(
            "RGB distributed framebuffer contains a non-radiance frame");
    }
    const int count = pixel_value_count(storage.width, storage.height);
    storage.data.resize(static_cast<size_t>(count));
    in.read(reinterpret_cast<char*>(storage.data.data()),
            static_cast<std::streamsize>(count * sizeof(float)));
    if (!in) {
        throw std::runtime_error("truncated distributed framebuffer payload");
    }
    return storage;
}

void merge_framebuffer_files(const std::filesystem::path& accum_path,
                             const std::vector<std::filesystem::path>& incoming_paths,
                             const std::filesystem::path& output_path) {
    DistributedFrameBufferStorage accum = read_framebuffer_file(accum_path);
    DistributedFrameBuffer accum_view = accum.view();
    for (const auto& path : incoming_paths) {
        DistributedFrameBufferStorage incoming = read_framebuffer_file(path);
        DistributedFrameBuffer incoming_view = incoming.view();
        merge_partial_framebuffer(accum_view, incoming_view);
    }
    write_framebuffer_file(output_path, accum_view);
}

void write_complex_field_frame_file(
    const std::filesystem::path& path,
    const DistributedComplexFrameStorage& frame) {
    if (!frame.is_valid()) {
        throw std::invalid_argument(
            "invalid distributed complex-field frame");
    }
    {
        auto out = open_output(path);
        write_magic(out, kComplexFrameMagic);
        write_value(out, kWaveFrameVersion);
        write_value(out, kByteOrderMarker);
        write_value(out, frame.width);
        write_value(out, frame.height);
        write_value(out, frame.total_samples);
        write_shard_metadata(out, frame.shard);
        write_value(
            out,
            static_cast<std::uint32_t>(
                frame.wavelengths_m.size()));
        for (const double wavelength :
             frame.wavelengths_m) {
            write_value(out, wavelength);
        }
        write_value(out, frame.realization_weight);
        write_value(
            out,
            static_cast<std::uint64_t>(
                frame.amplitude_sums.size()));
        for (const auto amplitude :
             frame.amplitude_sums) {
            write_value(out, amplitude.real);
            write_value(out, amplitude.imag);
        }
        for (const double weight :
             frame.estimator_weights) {
            write_value(out, weight);
        }
    }
    append_content_digest(path);
}

DistributedComplexFrameStorage
read_complex_field_frame_file(
    const std::filesystem::path& path) {
    verify_content_digest(path);
    auto in = open_input(path);
    read_magic(in, kComplexFrameMagic);
    if (read_value<std::uint32_t>(in) !=
        kWaveFrameVersion) {
        throw std::runtime_error(
            "unsupported distributed complex-field file version");
    }
    if (read_value<std::uint32_t>(in) !=
        kByteOrderMarker) {
        throw std::runtime_error(
            "distributed complex-field byte order is unsupported");
    }
    DistributedComplexFrameStorage frame;
    frame.width = read_value<int>(in);
    frame.height = read_value<int>(in);
    frame.total_samples = read_value<int>(in);
    frame.shard = read_shard_metadata(in, kVersion);
    const std::size_t wavelength_count =
        read_value<std::uint32_t>(in);
    if (wavelength_count == 0 ||
        wavelength_count >
            kMaxDistributedComplexFrameElements) {
        throw std::runtime_error(
            "distributed complex-field wavelength count is invalid");
    }
    frame.wavelengths_m.resize(wavelength_count);
    for (double& wavelength :
         frame.wavelengths_m) {
        wavelength = read_value<double>(in);
    }
    frame.realization_weight =
        read_value<double>(in);
    const std::uint64_t encoded_element_count =
        read_value<std::uint64_t>(in);
    if (encoded_element_count == 0 ||
        encoded_element_count >
            std::numeric_limits<std::size_t>::
                max() ||
        encoded_element_count >
            kMaxDistributedComplexFrameElements) {
        throw std::runtime_error(
            "distributed complex-field element count is invalid");
    }
    const std::size_t element_count =
        static_cast<std::size_t>(
            encoded_element_count);
    if (frame.width <= 0 ||
        frame.height <= 0 ||
        static_cast<std::uint64_t>(frame.width) >
            std::numeric_limits<std::uint64_t>::
                max() /
                static_cast<std::uint64_t>(
                    frame.height) ||
        static_cast<std::uint64_t>(frame.width) *
                static_cast<std::uint64_t>(
                    frame.height) >
            std::numeric_limits<std::uint64_t>::
                max() /
                wavelength_count ||
        static_cast<std::uint64_t>(frame.width) *
                static_cast<std::uint64_t>(
                    frame.height) *
                wavelength_count !=
            element_count) {
        throw std::runtime_error(
            "distributed complex-field element count is invalid");
    }
    frame.amplitude_sums.resize(element_count);
    for (auto& amplitude :
         frame.amplitude_sums) {
        amplitude.real = read_value<double>(in);
        amplitude.imag = read_value<double>(in);
    }
    frame.estimator_weights.resize(element_count);
    for (double& weight :
         frame.estimator_weights) {
        weight = read_value<double>(in);
    }
    read_digest_footer(in);
    if (!frame.is_valid()) {
        throw std::runtime_error(
            "invalid distributed complex-field payload");
    }
    return frame;
}

void merge_complex_field_frame_files(
    const std::filesystem::path& accum_path,
    const std::vector<std::filesystem::path>&
        incoming_paths,
    const std::filesystem::path& output_path) {
    auto accum =
        read_complex_field_frame_file(accum_path);
    for (const auto& path : incoming_paths) {
        const auto incoming =
            read_complex_field_frame_file(path);
        merge_complex_field_frame(
            accum,
            incoming);
    }
    write_complex_field_frame_file(
        output_path,
        accum);
}

void write_mutual_intensity_frame_file(
    const std::filesystem::path& path,
    const DistributedMutualIntensityFrameStorage&
        frame) {
    if (!frame.is_valid()) {
        throw std::invalid_argument(
            "invalid distributed mutual-intensity frame");
    }
    {
        auto out = open_output(path);
        write_magic(out, kMutualFrameMagic);
        write_value(out, kWaveFrameVersion);
        write_value(out, kByteOrderMarker);
        write_value(out, frame.total_samples);
        write_shard_metadata(out, frame.shard);
        write_value(
            out,
            frame.total_statistical_weight);
        write_value(
            out,
            frame.weighted_density.wavelength_m);
        write_value(
            out,
            static_cast<std::uint32_t>(
                frame.weighted_density.
                    sample_points.size()));
        for (const auto point :
             frame.weighted_density.sample_points) {
            write_value(out, point.x_m);
            write_value(out, point.y_m);
        }
        write_value(
            out,
            static_cast<std::uint64_t>(
                frame.weighted_density.values.size()));
        for (const auto value :
             frame.weighted_density.values) {
            write_value(out, value.real);
            write_value(out, value.imag);
        }
    }
    append_content_digest(path);
}

DistributedMutualIntensityFrameStorage
read_mutual_intensity_frame_file(
    const std::filesystem::path& path) {
    verify_content_digest(path);
    auto in = open_input(path);
    read_magic(in, kMutualFrameMagic);
    if (read_value<std::uint32_t>(in) !=
        kWaveFrameVersion) {
        throw std::runtime_error(
            "unsupported distributed mutual-intensity file version");
    }
    if (read_value<std::uint32_t>(in) !=
        kByteOrderMarker) {
        throw std::runtime_error(
            "distributed mutual-intensity byte order is unsupported");
    }
    DistributedMutualIntensityFrameStorage frame;
    frame.total_samples = read_value<int>(in);
    frame.shard = read_shard_metadata(in, kVersion);
    frame.total_statistical_weight =
        read_value<double>(in);
    frame.weighted_density.wavelength_m =
        read_value<double>(in);
    const std::size_t sample_count =
        read_value<std::uint32_t>(in);
    if (sample_count == 0 ||
        sample_count >
            wave::kMaxPartialCoherenceSamples) {
        throw std::runtime_error(
            "distributed mutual-intensity sample count is invalid");
    }
    frame.weighted_density.sample_points.resize(
        sample_count);
    for (auto& point :
         frame.weighted_density.sample_points) {
        point.x_m = read_value<double>(in);
        point.y_m = read_value<double>(in);
    }
    const std::uint64_t encoded_value_count =
        read_value<std::uint64_t>(in);
    if (encoded_value_count >
        std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error(
            "distributed mutual-intensity matrix size is invalid");
    }
    const std::size_t value_count =
        static_cast<std::size_t>(
            encoded_value_count);
    if (sample_count >
            std::numeric_limits<std::size_t>::
                max() /
                sample_count ||
        value_count != sample_count * sample_count) {
        throw std::runtime_error(
            "distributed mutual-intensity matrix size is invalid");
    }
    frame.weighted_density.values.resize(
        value_count);
    for (auto& value :
         frame.weighted_density.values) {
        value.real = read_value<double>(in);
        value.imag = read_value<double>(in);
    }
    read_digest_footer(in);
    if (!frame.is_valid()) {
        throw std::runtime_error(
            "invalid distributed mutual-intensity payload");
    }
    return frame;
}

void merge_mutual_intensity_frame_files(
    const std::filesystem::path& accum_path,
    const std::vector<std::filesystem::path>&
        incoming_paths,
    const std::filesystem::path& output_path) {
    auto accum =
        read_mutual_intensity_frame_file(
            accum_path);
    for (const auto& path : incoming_paths) {
        const auto incoming =
            read_mutual_intensity_frame_file(
                path);
        merge_mutual_intensity_frame(
            accum,
            incoming);
    }
    write_mutual_intensity_frame_file(
        output_path,
        accum);
}

} // namespace ure::gpu
