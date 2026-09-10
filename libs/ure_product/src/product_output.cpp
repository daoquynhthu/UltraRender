#include <ure/product/product_output.hpp>

#include <ImfChannelList.h>
#include <ImfFrameBuffer.h>
#include <ImfInputPart.h>
#include <ImfMultiPartInputFile.h>
#include <ImfMultiPartOutputFile.h>
#include <ImfOutputPart.h>
#include <ImfPartType.h>
#include <ImfStringAttribute.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <span>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include <ure/reconstruction/checkpoint.hpp>
#include <ure/image_loader.hpp>
#include <ure/image_saver.hpp>

namespace ure::product {
namespace {

namespace imf = OPENEXR_IMF_NAMESPACE;

constexpr std::string_view kManifestVersion = "ure.product.output.v1";
constexpr std::string_view kCheckpointMagic = "UREMEAS2";
std::atomic<std::uint64_t> kTempSequence{0};

std::string hex(const Identity& value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(value.size() * 2);
    for (const std::uint8_t byte : value) {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 15]);
    }
    return result;
}

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Unable to open product artifact");
    stream.seekg(0, std::ios::end);
    const auto size = stream.tellg();
    if (size < 0) throw std::runtime_error("Unable to size product artifact");
    stream.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        stream.read(reinterpret_cast<char*>(bytes.data()), size);
        if (!stream) throw std::runtime_error("Unable to read product artifact");
    }
    return bytes;
}

void sync_file(const std::filesystem::path& path) {
#ifdef _WIN32
    const int descriptor = _wopen(path.c_str(), _O_WRONLY | _O_BINARY);
    if (descriptor < 0) throw ProductOutputError(
        ProductOutputFailure::DiskOrPermission,
        "Unable to reopen artifact for durable publication");
    const int result = _commit(descriptor);
    _close(descriptor);
    if (result != 0) throw ProductOutputError(
        ProductOutputFailure::DiskOrPermission,
        "Unable to flush artifact for durable publication");
    const HANDLE directory = CreateFileW(
        path.parent_path().c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (directory != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(directory);
        CloseHandle(directory);
    }
#else
    const int descriptor = ::open(path.c_str(), O_RDONLY);
    if (descriptor < 0) throw ProductOutputError(
        ProductOutputFailure::DiskOrPermission,
        "Unable to reopen artifact for durable publication");
    const int result = ::fsync(descriptor);
    ::close(descriptor);
    if (result != 0) throw ProductOutputError(
        ProductOutputFailure::DiskOrPermission,
        "Unable to flush artifact for durable publication");
#endif
}

void write_bytes(const std::filesystem::path& path,
                 std::span<const std::uint8_t> bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) throw ProductOutputError(
        ProductOutputFailure::DiskOrPermission,
        "Unable to create product artifact");
    if (!bytes.empty()) stream.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    stream.flush();
    if (!stream) throw ProductOutputError(
        ProductOutputFailure::DiskOrPermission,
        "Unable to write product artifact");
    stream.close();
    if (!stream) throw ProductOutputError(
        ProductOutputFailure::DiskOrPermission,
        "Unable to close product artifact");
    sync_file(path);
}

void atomic_replace(const std::filesystem::path& temporary,
                    const std::filesystem::path& destination) {
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), destination.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw ProductOutputError(ProductOutputFailure::AtomicPublication,
                                 "Unable to atomically replace artifact");
#else
    if (::rename(temporary.c_str(), destination.c_str()) != 0)
        throw ProductOutputError(ProductOutputFailure::AtomicPublication,
                                 "Unable to atomically replace artifact");
    const int descriptor = ::open(destination.parent_path().c_str(), O_RDONLY);
    if (descriptor >= 0) {
        ::fsync(descriptor);
        ::close(descriptor);
    }
#endif
}

std::filesystem::path temporary_path(
    const std::filesystem::path& destination) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    const auto ticks = static_cast<std::uint64_t>(now.count());
    const auto sequence = kTempSequence.fetch_add(1, std::memory_order_relaxed);
    return destination.parent_path() / (destination.filename().string() +
        ".tmp." + std::to_string(ticks) + "." + std::to_string(sequence));
}

float read_scalar(const reconstruction::MeasurementPlaneDescriptor& descriptor,
                  const reconstruction::MeasurementPlane& plane,
                  std::size_t index) {
    const std::size_t size = reconstruction::measurement_scalar_size(
        descriptor.scalar_type);
    const auto begin = plane.payload.data() + index * size;
    std::uint64_t bits = 0;
    for (std::size_t byte = 0; byte < size; ++byte)
        bits |= static_cast<std::uint64_t>(begin[byte]) << (byte * 8);
    switch (descriptor.scalar_type) {
    case reconstruction::MeasurementScalarType::UInt8:
        return static_cast<float>(bits);
    case reconstruction::MeasurementScalarType::UInt32:
        return static_cast<float>(static_cast<std::uint32_t>(bits));
    case reconstruction::MeasurementScalarType::UInt64:
        return static_cast<float>(bits);
    case reconstruction::MeasurementScalarType::Float32:
        return std::bit_cast<float>(static_cast<std::uint32_t>(bits));
    case reconstruction::MeasurementScalarType::Float64:
        return static_cast<float>(std::bit_cast<double>(bits));
    case reconstruction::MeasurementScalarType::ComplexFloat32:
    case reconstruction::MeasurementScalarType::ComplexFloat64:
        throw std::invalid_argument("Complex measurement planes are not flat EXR output");
    }
    throw std::invalid_argument("Unknown measurement scalar type");
}

struct ChannelStorage {
    std::string name;
    reconstruction::MeasurementScalarType source_scalar_type{};
    std::vector<float> values;
};

std::string_view scalar_type_name(
    reconstruction::MeasurementScalarType type) {
    using Type = reconstruction::MeasurementScalarType;
    switch (type) {
    case Type::UInt8: return "uint8";
    case Type::UInt32: return "uint32";
    case Type::UInt64: return "uint64";
    case Type::Float32: return "float32";
    case Type::Float64: return "float64";
    case Type::ComplexFloat32: return "complex-float32";
    case Type::ComplexFloat64: return "complex-float64";
    }
    throw std::invalid_argument("Unknown measurement scalar type");
}

std::vector<ChannelStorage> channels_for_bundle(
    const reconstruction::MeasurementBundle& bundle,
    std::uint32_t part,
    std::uint32_t width,
    std::uint32_t height) {
    const auto pixels = static_cast<std::size_t>(width) * height;
    std::vector<ChannelStorage> result;
    for (std::size_t plane_index = 0; plane_index < bundle.planes.size();
         ++plane_index) {
        const auto& descriptor = bundle.schema.planes.at(plane_index);
        const auto& plane = bundle.planes.at(plane_index);
        const auto expected = reconstruction::measurement_plane_bytes(
            descriptor);
        if (plane.payload.size() != expected)
            throw std::invalid_argument("Invalid measurement plane payload");
        if (descriptor.element_count != 1 &&
            descriptor.element_count != pixels)
            throw std::invalid_argument("Measurement plane extent is not an image");
        for (std::uint32_t component = 0;
             component < descriptor.component_count; ++component) {
            ChannelStorage channel;
            channel.name = "p" + std::to_string(part) + "." +
                hex(descriptor.semantic_identity) + ".c" +
                std::to_string(component);
            channel.source_scalar_type = descriptor.scalar_type;
            channel.values.resize(pixels);
            for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
                const auto source = descriptor.element_count == 1 ? 0 : pixel;
                channel.values[pixel] = read_scalar(
                    descriptor, plane,
                    source * descriptor.component_count + component);
                if (!std::isfinite(channel.values[pixel]))
                    throw std::invalid_argument("Measurement plane contains non-finite values");
            }
            result.push_back(std::move(channel));
        }
    }
    return result;
}

void insert_metadata(imf::Header& header, const ProductIdentitySet& identities,
                     const ProductMeasurementSet& measurements,
                     std::uint32_t part) {
    const auto& bundle = part == 0 ? measurements.estimate :
        measurements.endpoints.at(part - 1);
    header.insert("ure_output_version", imf::StringAttribute("1"));
    header.insert("ure_part_identity", imf::StringAttribute(
        part == 0 ? "estimate" : "endpoint." + std::to_string(part - 1)));
    header.insert("ure_scene_identity", imf::StringAttribute(hex(identities.snapshot)));
    header.insert("ure_objective_identity", imf::StringAttribute(hex(identities.objective)));
    header.insert("ure_plan_identity", imf::StringAttribute(hex(identities.plan)));
    header.insert("ure_build_identity", imf::StringAttribute(hex(identities.build)));
    header.insert("ure_measurement_schema_identity", imf::StringAttribute(
        hex(bundle.schema.schema_identity)));
    header.insert("ure_producer_identity", imf::StringAttribute(
        hex(bundle.provenance.producer_identity)));
    header.insert("ure_sample_namespace_identity", imf::StringAttribute(
        hex(bundle.provenance.sample_namespace_identity)));
    header.insert("ure_portfolio_schedule_identity", imf::StringAttribute(
        hex(bundle.provenance.portfolio_schedule_identity)));
    header.insert("ure_time_sample_identity", imf::StringAttribute(
        hex(bundle.provenance.identities.time_sample)));
    header.insert("ure_auxiliary_outputs_wavefront_only", imf::StringAttribute(
        measurements.auxiliary_outputs_wavefront_only ? "true" : "false"));
    header.insert("ure_measurement_container", imf::StringAttribute(
        "ure.measurement-checkpoint.v2"));
}

void write_exr(const std::filesystem::path& path,
               const ProductMeasurementSet& measurements,
               const ProductIdentitySet& identities) {
    const auto width = measurements.estimate.schema.width;
    const auto height = measurements.estimate.schema.height;
    if (width == 0 || height == 0 || measurements.endpoints.size() >
        (std::numeric_limits<int>::max)() - 1)
        throw std::invalid_argument("Invalid product EXR dimensions");
    const auto part_count = measurements.endpoints.size() + 1;
    std::vector<imf::Header> headers;
    std::vector<std::vector<ChannelStorage>> storage;
    headers.reserve(part_count);
    storage.reserve(part_count);
    for (std::size_t part = 0; part < part_count; ++part) {
        const auto& bundle = part == 0 ? measurements.estimate :
            measurements.endpoints.at(part - 1);
        storage.push_back(channels_for_bundle(bundle,
            static_cast<std::uint32_t>(part), width, height));
        headers.emplace_back(static_cast<int>(width), static_cast<int>(height));
        auto& header = headers.back();
        header.setType(imf::SCANLINEIMAGE);
        header.setName(part == 0 ? "estimate" :
            "endpoint." + std::to_string(part - 1));
        insert_metadata(header, identities, measurements,
                        static_cast<std::uint32_t>(part));
        std::string channel_storage;
        for (const auto& channel : storage.back()) {
            if (!channel_storage.empty()) channel_storage.push_back(';');
            channel_storage += channel.name;
            channel_storage.push_back('=');
            channel_storage += scalar_type_name(channel.source_scalar_type);
            channel_storage += "->float32";
            header.channels().insert(channel.name, imf::Channel(imf::FLOAT));
        }
        header.insert("ure_channel_storage", imf::StringAttribute(
            std::move(channel_storage)));
    }
    {
        imf::MultiPartOutputFile output(path.string().c_str(), headers.data(),
                                        static_cast<int>(headers.size()));
        for (std::size_t part = 0; part < storage.size(); ++part) {
            imf::OutputPart output_part(output, static_cast<int>(part));
            imf::FrameBuffer frame_buffer;
            for (const auto& channel : storage[part])
                frame_buffer.insert(channel.name, imf::Slice(
                    imf::FLOAT,
                    reinterpret_cast<char*>(const_cast<float*>(channel.values.data())),
                    sizeof(float), static_cast<std::size_t>(width) * sizeof(float)));
            output_part.setFrameBuffer(frame_buffer);
            output_part.writePixels(static_cast<int>(height));
        }
    }
    sync_file(path);
}

std::vector<core::Vec3f> beauty_pixels(
    const ProductMeasurementSet& measurements) {
    const auto& bundle = measurements.estimate;
    const auto pixels = static_cast<std::size_t>(bundle.schema.width) *
                        bundle.schema.height;
    for (std::size_t index = 0; index < bundle.schema.planes.size(); ++index) {
        const auto& descriptor = bundle.schema.planes[index];
        if (descriptor.kind != reconstruction::MeasurementPlaneKind::Observable)
            continue;
        if (descriptor.scalar_type !=
                reconstruction::MeasurementScalarType::Float32 ||
            descriptor.component_count != 3 ||
            descriptor.element_count != pixels)
            throw std::invalid_argument(
                "Beauty measurement is not RGB float32 image data");
        std::vector<core::Vec3f> result(pixels);
        for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
            result[pixel] = {
                read_scalar(descriptor, bundle.planes[index], pixel * 3),
                read_scalar(descriptor, bundle.planes[index], pixel * 3 + 1),
                read_scalar(descriptor, bundle.planes[index], pixel * 3 + 2)};
        }
        return result;
    }
    throw std::invalid_argument("Beauty measurement plane is missing");
}

void write_derived_display(const std::filesystem::path& path,
                           const ProductMeasurementSet& measurements,
                           ProductOutputFormat format,
                           ProductToneMap tone_map) {
    const auto pixels = beauty_pixels(measurements);
    const auto width = static_cast<int>(measurements.estimate.schema.width);
    const auto height = static_cast<int>(measurements.estimate.schema.height);
    io::ToneMapType mapped = io::ToneMapType::Linear;
    if (tone_map == ProductToneMap::Reinhard)
        mapped = io::ToneMapType::Reinhard;
    else if (tone_map == ProductToneMap::Aces)
        mapped = io::ToneMapType::ACES;
    bool written = false;
    if (format == ProductOutputFormat::Hdr)
        written = io::ImageSaver::save_hdr(path.string(), width, height,
                                           pixels, 1.0F);
    else if (format == ProductOutputFormat::Ppm)
        written = io::ImageSaver::save_ppm(path.string(), width, height,
                                           pixels, mapped, 1.0F);
    else if (format == ProductOutputFormat::Bmp)
        written = io::ImageSaver::save_bmp(path.string(), width, height,
                                           pixels, mapped, 1.0F);
    else
        throw std::invalid_argument("Requested output is not a display format");
    if (!written)
        throw ProductOutputError(ProductOutputFailure::DiskOrPermission,
                                 "Unable to create derived display artifact");
    sync_file(path);
    gpu::HostTexture reopened{};
    if (!io::load_image_rgb32f(path.string(), reopened) ||
        reopened.width != width || reopened.height != height ||
        reopened.channels != 3 ||
        !std::ranges::all_of(reopened.data, [](float value) {
            return std::isfinite(value);
        }))
        throw ProductOutputError(ProductOutputFailure::Codec,
                                 "Derived display artifact did not reopen");
}

void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (int shift = 0; shift != 32; shift += 8)
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}

void append_u64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
    for (int shift = 0; shift != 64; shift += 8)
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}

std::vector<std::uint8_t> write_measurement_set(
    const ProductMeasurementSet& measurements) {
    std::vector<std::vector<std::uint8_t>> bundles;
    bundles.push_back(reconstruction::write_measurement_checkpoint(
        measurements.estimate));
    for (const auto& endpoint : measurements.endpoints)
        bundles.push_back(reconstruction::write_measurement_checkpoint(endpoint));
    std::vector<std::uint8_t> bytes(kCheckpointMagic.begin(), kCheckpointMagic.end());
    append_u32(bytes, 1);
    append_u32(bytes, static_cast<std::uint32_t>(bundles.size()));
    for (const auto& bundle : bundles) {
        append_u64(bytes, bundle.size());
        bytes.insert(bytes.end(), bundle.begin(), bundle.end());
    }
    return bytes;
}

void verify_measurement_set(
    std::span<const std::uint8_t> bytes,
    std::size_t expected_bundle_count) {
    if (bytes.size() < kCheckpointMagic.size() + 8 ||
        !std::equal(kCheckpointMagic.begin(), kCheckpointMagic.end(),
                    bytes.begin()))
        throw std::runtime_error("Measurement set header is invalid");
    std::size_t offset = kCheckpointMagic.size();
    const auto read_u32 = [&bytes, &offset]() {
        if (bytes.size() - offset < 4)
            throw std::runtime_error("Measurement set is truncated");
        std::uint32_t value{};
        for (std::size_t byte = 0; byte < 4; ++byte)
            value |= static_cast<std::uint32_t>(bytes[offset++]) << (byte * 8);
        return value;
    };
    const auto read_u64 = [&bytes, &offset]() {
        if (bytes.size() - offset < 8)
            throw std::runtime_error("Measurement set is truncated");
        std::uint64_t value{};
        for (std::size_t byte = 0; byte < 8; ++byte)
            value |= static_cast<std::uint64_t>(bytes[offset++]) << (byte * 8);
        return value;
    };
    if (read_u32() != 1 || read_u32() != expected_bundle_count)
        throw std::runtime_error("Measurement set version is unsupported");
    for (std::size_t index = 0; index < expected_bundle_count; ++index) {
        const auto size = read_u64();
        if (size > bytes.size() - offset)
            throw std::runtime_error("Measurement checkpoint is truncated");
        const auto checkpoint = bytes.subspan(offset, static_cast<std::size_t>(size));
        if (!reconstruction::validate_measurement_bundle(
                 reconstruction::read_measurement_checkpoint(checkpoint)).ok())
            throw std::runtime_error("Measurement checkpoint did not reopen");
        offset += static_cast<std::size_t>(size);
    }
    if (offset != bytes.size())
        throw std::runtime_error("Measurement set has trailing bytes");
}

std::string json_string(std::string_view value) {
    std::string result = "\"";
    for (const char character : value) {
        if (character == '\\' || character == '"') result.push_back('\\');
        result.push_back(character);
    }
    result.push_back('"');
    return result;
}

std::vector<std::uint8_t> manifest_bytes(const ProductOutputPaths& paths,
                                         const ProductIdentitySet& identities,
                                         const ProductMeasurementSet& measurements,
                                         ProductOutputFormat format,
                                         ProductToneMap tone_map) {
    const auto exr_name = paths.exr.filename().string();
    const auto checkpoint_name = paths.measurement_checkpoint.filename().string();
    const auto manifest_name = paths.manifest.filename().string();
    const auto format_name = [format]() -> std::string_view {
        switch (format) {
        case ProductOutputFormat::OpenExr: return "openexr";
        case ProductOutputFormat::Measurement: return "measurement";
        case ProductOutputFormat::Hdr: return "hdr";
        case ProductOutputFormat::Ppm: return "ppm";
        case ProductOutputFormat::Bmp: return "bmp";
        }
        throw std::invalid_argument("Unknown product output format");
    }();
    const auto tone_map_name = [tone_map]() -> std::string_view {
        switch (tone_map) {
        case ProductToneMap::Linear: return "linear";
        case ProductToneMap::Reinhard: return "reinhard";
        case ProductToneMap::Aces: return "aces";
        }
        throw std::invalid_argument("Unknown product tone map");
    }();
    const std::string view_transform =
        format == ProductOutputFormat::Hdr
            ? "ure.preview.view.linear-rgb-rgbe/1.0"
        : format == ProductOutputFormat::Ppm ||
                format == ProductOutputFormat::Bmp
            ? "ure.preview.view.linear-srgb-" + std::string(tone_map_name) +
                  "-srgb8/1.0"
            : "none";
    const std::string derived_name = paths.derived_display.empty()
        ? std::string{}
        : paths.derived_display.filename().string();
    std::string value = "{\"version\":" + json_string(kManifestVersion) +
        ",\"requested_format\":" + json_string(format_name) +
        ",\"tone_map\":" + json_string(tone_map_name) +
        ",\"view_transform_identity\":" + json_string(view_transform) +
        ",\"orientation\":\"measurement-row-order\"" +
        ",\"exposure\":1.0" +
        ",\"exr\":" + json_string(exr_name) +
        ",\"measurement_checkpoint\":" + json_string(checkpoint_name) +
        ",\"manifest\":" + json_string(manifest_name) +
        ",\"scene_identity\":" + json_string(hex(identities.snapshot)) +
        ",\"objective_identity\":" + json_string(hex(identities.objective)) +
        ",\"plan_identity\":" + json_string(hex(identities.plan)) +
        ",\"build_identity\":" + json_string(hex(identities.build)) +
        ",\"exr_content_identity\":" + json_string(hex(paths.exr_content_identity)) +
        ",\"measurement_content_identity\":" + json_string(hex(paths.measurement_content_identity)) +
        ",\"measurement_bundle_count\":" + std::to_string(measurements.endpoints.size() + 1) +
        ",\"derived_display\":" + json_string(derived_name) +
        ",\"derived_display_content_identity\":" +
            json_string(hex(paths.derived_display_content_identity)) +
        ",\"publication\":\"manifest-last\"}\n";
    return std::vector<std::uint8_t>(value.begin(), value.end());
}

}

ProductOutputError::ProductOutputError(ProductOutputFailure failure,
                                       std::string message)
    : std::runtime_error(std::move(message)), failure_(failure) {}

ProductOutputFailure ProductOutputError::failure() const noexcept {
    return failure_;
}

ProductExrInspection inspect_product_exr(const std::filesystem::path& path) {
    const auto bytes = read_file(path);
    imf::MultiPartInputFile input(path.string().c_str());
    ProductExrInspection result;
    result.part_count = static_cast<std::uint32_t>(input.parts());
    if (result.part_count == 0) throw std::runtime_error("EXR has no parts");
    for (int part = 0; part < input.parts(); ++part) {
        if (!input.partComplete(part)) throw std::runtime_error("EXR part is incomplete");
        const auto& header = input.header(part);
        constexpr std::array<std::string_view, 13> required_metadata{
            "ure_output_version", "ure_part_identity", "ure_scene_identity",
            "ure_objective_identity", "ure_plan_identity", "ure_build_identity",
            "ure_measurement_schema_identity", "ure_producer_identity",
            "ure_sample_namespace_identity", "ure_portfolio_schedule_identity",
            "ure_time_sample_identity", "ure_measurement_container",
            "ure_channel_storage"};
        for (const auto key : required_metadata) {
            const auto* attribute = header.findTypedAttribute<imf::StringAttribute>(
                std::string(key).c_str());
            if (!attribute || attribute->value().empty())
                throw std::runtime_error("EXR part metadata is incomplete");
        }
        const auto window = header.dataWindow();
        const auto width = static_cast<std::uint32_t>(window.max.x - window.min.x + 1);
        const auto height = static_cast<std::uint32_t>(window.max.y - window.min.y + 1);
        if (part == 0) {
            result.width = width;
            result.height = height;
        } else if (width != result.width || height != result.height) {
            throw std::runtime_error("EXR parts have inconsistent dimensions");
        }
        for (imf::Header::ConstIterator metadata = header.begin();
             metadata != header.end(); ++metadata) {
            if (const auto* attribute = dynamic_cast<const imf::StringAttribute*>(
                    &metadata.attribute())) {
                const std::string key = metadata.name();
                const std::string value = attribute->value();
                const bool shared_identity =
                    key == "ure_output_version" ||
                    key == "ure_scene_identity" ||
                    key == "ure_objective_identity" ||
                    key == "ure_plan_identity" ||
                    key == "ure_build_identity" ||
                    key == "ure_auxiliary_outputs_wavefront_only" ||
                    key == "ure_measurement_container";
                const auto existing = result.metadata.find(key);
                if (shared_identity && existing != result.metadata.end() &&
                    existing->second != value)
                    throw std::runtime_error(
                        "EXR parts have inconsistent product metadata");
                result.metadata.emplace(key, value);
                result.metadata.emplace(
                    "part." + std::to_string(part) + "." + key, value);
            }
        }
        std::vector<std::vector<float>> values;
        std::size_t channel_count = 0;
        for (imf::ChannelList::ConstIterator channel = header.channels().begin();
             channel != header.channels().end(); ++channel)
            ++channel_count;
        values.reserve(channel_count);
        imf::FrameBuffer frame_buffer;
        for (imf::ChannelList::ConstIterator channel = header.channels().begin();
             channel != header.channels().end(); ++channel) {
            result.channels.push_back({static_cast<std::uint32_t>(part), channel.name()});
            values.emplace_back(static_cast<std::size_t>(width) * height);
            frame_buffer.insert(channel.name(), imf::Slice(
                imf::FLOAT, reinterpret_cast<char*>(values.back().data()),
                sizeof(float), static_cast<std::size_t>(width) * sizeof(float),
                1, 1, 0.0));
        }
        imf::InputPart input_part(input, part);
        input_part.setFrameBuffer(frame_buffer);
        input_part.readPixels(window.min.y, window.max.y);
        for (const auto& channel : values)
            for (const float value : channel)
                if (!std::isfinite(value)) throw std::runtime_error("EXR contains non-finite values");
    }
    result.content_identity = content_identity(bytes);
    return result;
}

ProductOutputPaths publish_product_measurement_set(
    const ProductMeasurementSet& measurements,
    const ProductIdentitySet& identities,
    const std::filesystem::path& directory,
    std::string_view stem,
    ProductOutputFormat format,
    ProductToneMap tone_map,
    std::uint64_t byte_budget) {
    if (stem.empty() || stem.find_first_of("\\/:\"'") != std::string_view::npos)
        throw std::invalid_argument("Invalid product output stem");
    if (!reconstruction::validate_measurement_bundle(measurements.estimate).ok() ||
        std::ranges::any_of(measurements.endpoints, [](const auto& bundle) {
            return !reconstruction::validate_measurement_bundle(bundle).ok();
        }))
        throw std::invalid_argument("Invalid product measurement set");
    const bool derived = format == ProductOutputFormat::Hdr ||
                         format == ProductOutputFormat::Ppm ||
                         format == ProductOutputFormat::Bmp;
    if (!derived && format != ProductOutputFormat::OpenExr &&
        format != ProductOutputFormat::Measurement)
        throw std::invalid_argument("Unsupported product output format");
    if ((format == ProductOutputFormat::Hdr &&
         tone_map != ProductToneMap::Linear) ||
        (!derived && tone_map != ProductToneMap::Linear))
        throw std::invalid_argument(
            "Tone mapping is only executable for PPM and BMP output");
    std::error_code directory_error;
    std::filesystem::create_directories(directory, directory_error);
    if (directory_error)
        throw ProductOutputError(ProductOutputFailure::DiskOrPermission,
                                 "Unable to create product output directory");
    ProductOutputPaths result;
    result.manifest = directory / (std::string(stem) + ".manifest.json");
    const auto exr_temporary = temporary_path(
        directory / (std::string(stem) + ".exr"));
    const auto checkpoint_temporary = temporary_path(
        directory / (std::string(stem) + ".measurement.v2"));
    const auto manifest_temporary = temporary_path(result.manifest);
    std::filesystem::path derived_temporary;
    std::string derived_extension;
    if (format == ProductOutputFormat::Hdr) derived_extension = ".hdr";
    if (format == ProductOutputFormat::Ppm) derived_extension = ".ppm";
    if (format == ProductOutputFormat::Bmp) derived_extension = ".bmp";
    if (derived)
        derived_temporary = temporary_path(
            directory / (std::string(stem) + derived_extension));
    try {
        try {
            write_exr(exr_temporary, measurements, identities);
        } catch (const ProductOutputError&) {
            throw;
        } catch (const std::exception& exception) {
            throw ProductOutputError(
                ProductOutputFailure::Codec,
                "OpenEXR encoding failed: " + std::string(exception.what()));
        }
        ProductExrInspection inspection;
        try {
            inspection = inspect_product_exr(exr_temporary);
        } catch (const ProductOutputError&) {
            throw;
        } catch (const std::exception& exception) {
            throw ProductOutputError(
                ProductOutputFailure::Codec,
                "OpenEXR verification failed: " +
                    std::string(exception.what()));
        }
        result.exr_content_identity = inspection.content_identity;
        std::vector<std::uint8_t> checkpoint;
        try {
            checkpoint = write_measurement_set(measurements);
            verify_measurement_set(checkpoint,
                                   measurements.endpoints.size() + 1);
        } catch (const std::exception& exception) {
            throw ProductOutputError(
                ProductOutputFailure::Codec,
                "Measurement checkpoint encoding failed: " +
                    std::string(exception.what()));
        }
        result.measurement_content_identity = content_identity(checkpoint);
        write_bytes(checkpoint_temporary, checkpoint);
        if (derived) {
            write_derived_display(derived_temporary, measurements, format,
                                  tone_map);
            result.derived_display_content_identity = content_identity(
                read_file(derived_temporary));
            result.derived_display = directory / (std::string(stem) + "." +
                hex(result.derived_display_content_identity) +
                derived_extension);
            result.artifact_count = 4;
        }
        std::error_code size_error;
        const auto exr_size = std::filesystem::file_size(
            exr_temporary, size_error);
        if (size_error)
            throw ProductOutputError(ProductOutputFailure::DiskOrPermission,
                                     "Unable to size product artifact");
        std::uint64_t derived_size = 0;
        if (derived) {
            derived_size = std::filesystem::file_size(derived_temporary,
                                                       size_error);
            if (size_error)
                throw ProductOutputError(
                    ProductOutputFailure::DiskOrPermission,
                    "Unable to size derived display artifact");
        }
        if (exr_size > byte_budget ||
            checkpoint.size() >
                byte_budget - exr_size ||
            derived_size > byte_budget - exr_size - checkpoint.size())
            throw std::length_error("Product output byte budget is exhausted");
        result.exr = directory / (std::string(stem) + "." +
            hex(result.exr_content_identity) + ".exr");
        result.measurement_checkpoint = directory / (std::string(stem) + "." +
            hex(result.measurement_content_identity) + ".measurement.v2");
        const auto manifest = manifest_bytes(result, identities, measurements,
                                             format, tone_map);
        if (manifest.size() > byte_budget -
                exr_size - checkpoint.size() - derived_size)
            throw std::length_error("Product output byte budget is exhausted");
        result.manifest_content_identity = content_identity(manifest);
        write_bytes(manifest_temporary, manifest);
        atomic_replace(exr_temporary, result.exr);
        atomic_replace(checkpoint_temporary, result.measurement_checkpoint);
        if (derived)
            atomic_replace(derived_temporary, result.derived_display);
        atomic_replace(manifest_temporary, result.manifest);
    } catch (...) {
        std::error_code error;
        std::filesystem::remove(exr_temporary, error);
        std::filesystem::remove(checkpoint_temporary, error);
        if (derived)
            std::filesystem::remove(derived_temporary, error);
        std::filesystem::remove(manifest_temporary, error);
        throw;
    }
    return result;
}

}
