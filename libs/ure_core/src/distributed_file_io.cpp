#include "ure/distributed_file_io.hpp"

#include <array>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace ure::gpu {

namespace {

constexpr std::array<char, 8> kRangeMagic = {'U', 'R', 'D', 'R', 'A', 'N', 'G', 'E'};
constexpr std::array<char, 8> kFrameMagic = {'U', 'R', 'D', 'F', 'R', 'A', 'M', 'E'};
constexpr int kVersion = 3;

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
}

DistributedShardMetadata read_shard_metadata(std::ifstream& in) {
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
    if (version != kVersion) {
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
    range.shard = read_shard_metadata(in);
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
    if (version != kVersion) {
        throw std::runtime_error("unsupported distributed framebuffer file version");
    }
    DistributedFrameBufferStorage storage;
    storage.width = read_value<int>(in);
    storage.height = read_value<int>(in);
    storage.total_samples = read_value<int>(in);
    storage.shard = read_shard_metadata(in);
    storage.estimator = read_estimator_metadata(in);
    if (storage.total_samples < 0) {
        throw std::runtime_error("invalid distributed framebuffer sample count");
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

} // namespace ure::gpu
