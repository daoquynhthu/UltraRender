#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <ure/native_scene_container.hpp>
#include <ure/native_scene_hash.hpp>
#include <ure/native_scene_metadata.hpp>

namespace ure::native_scene {
namespace {

constexpr std::size_t kHeaderSize = 128;
constexpr std::uint32_t kEndianMarker = 0x01020304u;
constexpr std::array<std::uint8_t, 8> kSceneMagic{'U', 'R', 'E', 'S', '\r', '\n', 0x1a, '\n'};
constexpr std::array<std::uint8_t, 8> kPackageMagic{'U', 'R', 'E', 'P', '\r', '\n', 0x1a, '\n'};

bool checked_add(std::uint64_t left, std::uint64_t right, std::uint64_t& result) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) return false;
    result = left + right;
    return true;
}

bool valid_alignment(std::uint64_t alignment) {
    return alignment >= 1 && alignment <= 4096 && (alignment & (alignment - 1)) == 0;
}

std::uint64_t align_up(std::uint64_t value, std::uint64_t alignment) {
    if (!valid_alignment(alignment)) throw std::invalid_argument("Invalid chunk alignment");
    std::uint64_t expanded = 0;
    if (!checked_add(value, alignment - 1, expanded)) throw std::overflow_error("Alignment overflow");
    return expanded & ~(alignment - 1);
}

void append_u16(std::vector<std::uint8_t>& output, std::uint16_t value) {
    output.push_back(static_cast<std::uint8_t>(value));
    output.push_back(static_cast<std::uint8_t>(value >> 8u));
}

void append_u32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) output.push_back(static_cast<std::uint8_t>(value >> shift));
}

void append_u64(std::vector<std::uint8_t>& output, std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) output.push_back(static_cast<std::uint8_t>(value >> shift));
}

void append_string(std::vector<std::uint8_t>& output, const std::string& value) {
    if (value.size() > std::numeric_limits<std::uint16_t>::max()) throw std::invalid_argument("Container string too long");
    append_u16(output, static_cast<std::uint16_t>(value.size()));
    output.insert(output.end(), value.begin(), value.end());
}

void patch_u32(std::vector<std::uint8_t>& output, std::size_t offset, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) output[offset++] = static_cast<std::uint8_t>(value >> shift);
}

void patch_u64(std::vector<std::uint8_t>& output, std::size_t offset, std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) output[offset++] = static_cast<std::uint8_t>(value >> shift);
}

std::array<std::uint8_t, 32> hash_bytes(const std::string& hash) {
    if (hash.size() != 64) throw std::invalid_argument("Invalid SHA-256 text");
    auto nibble = [](char value) -> std::uint8_t {
        if (value >= '0' && value <= '9') return static_cast<std::uint8_t>(value - '0');
        if (value >= 'a' && value <= 'f') return static_cast<std::uint8_t>(value - 'a' + 10);
        throw std::invalid_argument("Invalid SHA-256 text");
    };
    std::array<std::uint8_t, 32> result{};
    for (std::size_t i = 0; i < result.size(); ++i) {
        result[i] = static_cast<std::uint8_t>((nibble(hash[i * 2]) << 4u) | nibble(hash[i * 2 + 1]));
    }
    return result;
}

std::string hash_text(std::span<const std::uint8_t, 32> bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (std::uint8_t byte : bytes) {
        result.push_back(digits[byte >> 4u]);
        result.push_back(digits[byte & 0x0fu]);
    }
    return result;
}

class Reader {
public:
    Reader(std::span<const std::uint8_t> bytes, std::size_t begin, std::size_t end)
        : bytes_(bytes), position_(begin), end_(end) {
        if (begin > end || end > bytes.size()) throw std::out_of_range("Invalid reader bounds");
    }

    std::uint16_t u16() {
        require(2);
        const std::uint16_t value = static_cast<std::uint16_t>(bytes_[position_]) |
                                    static_cast<std::uint16_t>(bytes_[position_ + 1] << 8u);
        position_ += 2;
        return value;
    }

    std::uint32_t u32() {
        require(4);
        std::uint32_t value = 0;
        for (int shift = 0; shift < 32; shift += 8) value |= static_cast<std::uint32_t>(bytes_[position_++]) << shift;
        return value;
    }

    std::uint64_t u64() {
        require(8);
        std::uint64_t value = 0;
        for (int shift = 0; shift < 64; shift += 8) value |= static_cast<std::uint64_t>(bytes_[position_++]) << shift;
        return value;
    }

    std::string string() {
        const std::size_t length = u16();
        require(length);
        std::string value(reinterpret_cast<const char*>(bytes_.data() + position_), length);
        position_ += length;
        return value;
    }

    std::array<std::uint8_t, 32> hash() {
        require(32);
        std::array<std::uint8_t, 32> result{};
        std::ranges::copy(bytes_.subspan(position_, 32), result.begin());
        position_ += 32;
        return result;
    }

    bool finished() const { return position_ == end_; }

private:
    void require(std::size_t count) {
        if (count > end_ - position_) throw std::out_of_range("Truncated directory");
    }

    std::span<const std::uint8_t> bytes_;
    std::size_t position_;
    std::size_t end_;
};

struct StoredChunk {
    ContainerChunk chunk;
    std::uint64_t offset = 0;
    std::uint64_t stored_size = 0;
    std::array<std::uint8_t, 32> expected_hash{};
};

template <typename T>
LoadResult<T> container_error(std::string code, std::string message) {
    LoadResult<T> result;
    result.diagnostics.push_back({std::move(code), DiagnosticSeverity::Error, "/container", std::move(message), {}});
    return result;
}

void add_warning(std::vector<ValidationDiagnostic>& diagnostics, std::string code, std::string path, std::string message) {
    diagnostics.push_back({std::move(code), DiagnosticSeverity::Warning, std::move(path), std::move(message), {}});
}

bool known_core_chunk(std::uint32_t type) {
    return type >= static_cast<std::uint32_t>(ChunkKind::Metadata) &&
           type <= static_cast<std::uint32_t>(ChunkKind::SolverContract);
}

bool valid_chunk_id(const std::string& id) {
    if (id.empty() || id.size() > 255 || id.front() == '/' || id.back() == '/') return false;
    if (!std::isalnum(static_cast<unsigned char>(id.front()))) return false;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= id.size(); ++i) {
        if (i == id.size() || id[i] == '/') {
            const std::string segment = id.substr(start, i - start);
            if (segment.empty() || segment == "." || segment == "..") return false;
            start = i + 1;
        } else if (!(std::isalnum(static_cast<unsigned char>(id[i])) || id[i] == '.' || id[i] == '_' || id[i] == '-')) {
            return false;
        }
    }
    return true;
}

bool reserved_chunk_type(std::uint32_t type) {
    return type == 0 || type == 16;
}

}

std::vector<std::uint8_t> write_container(const NativeContainer& container) {
    if (container.container_version.major != 1 || container.flags != 0) throw std::invalid_argument("Unsupported container version or flags");
    if (container.chunks.size() > std::numeric_limits<std::uint32_t>::max()) throw std::invalid_argument("Too many chunks");
    const auto semantic_bytes = hash_bytes(container.semantic_hash);

    std::vector<const ContainerChunk*> chunks;
    chunks.reserve(container.chunks.size());
    for (const auto& chunk : container.chunks) chunks.push_back(&chunk);
    std::ranges::sort(chunks, {}, [](const ContainerChunk* chunk) { return chunk->id; });
    for (std::size_t i = 0; i < chunks.size(); ++i) {
        if (!valid_chunk_id(chunks[i]->id) || (i > 0 && chunks[i - 1]->id == chunks[i]->id)) throw std::invalid_argument("Invalid or duplicate chunk ID");
        if (reserved_chunk_type(chunks[i]->type)) throw std::invalid_argument("Reserved chunk type");
        if (chunks[i]->requirement > RequirementLevel::Advisory) throw std::invalid_argument("Invalid chunk requirement level");
        if (!valid_alignment(chunks[i]->alignment)) throw std::invalid_argument("Invalid chunk alignment");
    }

    std::vector<std::uint8_t> output(kHeaderSize, 0);
    std::vector<StoredChunk> stored;
    stored.reserve(chunks.size());
    for (const ContainerChunk* source : chunks) {
        const std::uint64_t aligned = align_up(output.size(), source->alignment);
        output.resize(static_cast<std::size_t>(aligned), 0);
        StoredChunk entry;
        entry.chunk = *source;
        entry.offset = output.size();
        entry.stored_size = source->payload.size();
        if (entry.chunk.uncompressed_size == 0) entry.chunk.uncompressed_size = entry.stored_size;
        const std::string digest = sha256_hex(source->payload);
        entry.expected_hash = hash_bytes(digest);
        output.insert(output.end(), source->payload.begin(), source->payload.end());
        stored.push_back(std::move(entry));
    }

    output.resize(static_cast<std::size_t>(align_up(output.size(), 16)), 0);
    const std::uint64_t directory_offset = output.size();
    std::vector<std::uint8_t> directory;
    append_u64(directory, stored.size());
    for (const auto& entry : stored) {
        append_string(directory, entry.chunk.id);
        append_u32(directory, entry.chunk.type);
        append_u32(directory, entry.chunk.schema_version.major);
        append_u32(directory, entry.chunk.schema_version.minor);
        append_u32(directory, static_cast<std::uint32_t>(entry.chunk.requirement));
        append_u32(directory, entry.chunk.codec);
        append_u64(directory, entry.offset);
        append_u64(directory, entry.stored_size);
        append_u64(directory, entry.chunk.uncompressed_size);
        append_u64(directory, entry.chunk.alignment);
        directory.insert(directory.end(), entry.expected_hash.begin(), entry.expected_hash.end());
        if (entry.chunk.dependencies.size() > std::numeric_limits<std::uint16_t>::max()) throw std::invalid_argument("Too many chunk dependencies");
        append_u16(directory, static_cast<std::uint16_t>(entry.chunk.dependencies.size()));
        for (const auto& dependency : entry.chunk.dependencies) append_string(directory, dependency);
        append_string(directory, entry.chunk.extension_owner);
    }
    output.insert(output.end(), directory.begin(), directory.end());

    const auto& magic = container.kind == ContainerKind::Scene ? kSceneMagic : kPackageMagic;
    std::ranges::copy(magic, output.begin());
    patch_u32(output, 8, container.container_version.major);
    patch_u32(output, 12, container.container_version.minor);
    patch_u32(output, 16, kEndianMarker);
    patch_u32(output, 20, static_cast<std::uint32_t>(kHeaderSize));
    patch_u64(output, 24, directory_offset);
    patch_u64(output, 32, directory.size());
    std::ranges::copy(container.document_uuid, output.begin() + 40);
    std::ranges::copy(semantic_bytes, output.begin() + 56);
    patch_u32(output, 88, container.flags);
    patch_u32(output, 92, static_cast<std::uint32_t>(stored.size()));
    return output;
}

LoadResult<NativeContainer> read_container(std::span<const std::uint8_t> bytes,
                                           const CapabilityRegistry& registry,
                                           const ValidationLimits& limits) {
    if (bytes.size() < kHeaderSize) return container_error<NativeContainer>("URE-Q-CONTAINER-001", "Container header is truncated");
    ContainerKind kind;
    if (std::ranges::equal(bytes.first(8), kSceneMagic)) kind = ContainerKind::Scene;
    else if (std::ranges::equal(bytes.first(8), kPackageMagic)) kind = ContainerKind::Package;
    else return container_error<NativeContainer>("URE-Q-CONTAINER-001", "Container magic is invalid");

    try {
        Reader header(bytes, 8, kHeaderSize);
        const Version version{header.u32(), header.u32()};
        const std::uint32_t endian = header.u32();
        const std::uint32_t header_size = header.u32();
        const std::uint64_t directory_offset = header.u64();
        const std::uint64_t directory_size = header.u64();
        std::array<std::uint8_t, 16> uuid{};
        std::ranges::copy(bytes.subspan(40, 16), uuid.begin());
        std::array<std::uint8_t, 32> semantic_bytes{};
        std::ranges::copy(bytes.subspan(56, 32), semantic_bytes.begin());
        const std::uint32_t flags = [&] { Reader value(bytes, 88, 92); return value.u32(); }();
        const std::uint32_t chunk_count = [&] { Reader value(bytes, 92, 96); return value.u32(); }();
        if (version.major != 1 || endian != kEndianMarker || header_size != kHeaderSize || flags != 0) {
            return container_error<NativeContainer>("URE-Q-CONTAINER-002", "Unsupported version, endian marker, header size, or flags");
        }
        for (std::size_t i = 96; i < kHeaderSize; ++i) {
            if (bytes[i] != 0) return container_error<NativeContainer>("URE-Q-CONTAINER-002", "Reserved header bytes are nonzero");
        }
        std::uint64_t directory_end = 0;
        if (!checked_add(directory_offset, directory_size, directory_end) || directory_offset < kHeaderSize ||
            directory_end != bytes.size() || (directory_offset % 16) != 0) {
            return container_error<NativeContainer>("URE-Q-CONTAINER-003", "Directory range is invalid");
        }
        if (chunk_count > limits.max_directory_entries) {
            return container_error<NativeContainer>("URE-Q-BUDGET-001", "Directory entry budget exceeded");
        }

        Reader directory(bytes, static_cast<std::size_t>(directory_offset), static_cast<std::size_t>(directory_end));
        if (directory.u64() != chunk_count) return container_error<NativeContainer>("URE-Q-CONTAINER-003", "Directory count mismatch");
        std::vector<StoredChunk> stored;
        stored.reserve(chunk_count);
        std::set<std::string> ids;
        std::uint64_t total_stored = 0;
        std::uint64_t total_uncompressed = 0;
        for (std::uint32_t i = 0; i < chunk_count; ++i) {
            StoredChunk entry;
            entry.chunk.id = directory.string();
            entry.chunk.type = directory.u32();
            entry.chunk.schema_version = {directory.u32(), directory.u32()};
            entry.chunk.requirement = static_cast<RequirementLevel>(directory.u32());
            entry.chunk.codec = directory.u32();
            entry.offset = directory.u64();
            entry.stored_size = directory.u64();
            entry.chunk.uncompressed_size = directory.u64();
            entry.chunk.alignment = directory.u64();
            entry.expected_hash = directory.hash();
            const std::uint16_t dependency_count = directory.u16();
            entry.chunk.dependencies.reserve(dependency_count);
            for (std::uint16_t dependency = 0; dependency < dependency_count; ++dependency) entry.chunk.dependencies.push_back(directory.string());
            entry.chunk.extension_owner = directory.string();
            if (!valid_chunk_id(entry.chunk.id) || !ids.insert(entry.chunk.id).second || reserved_chunk_type(entry.chunk.type) ||
                entry.chunk.requirement > RequirementLevel::Advisory) {
                return container_error<NativeContainer>("URE-Q-ID-002", "Invalid or duplicate chunk ID");
            }
            std::uint64_t chunk_end = 0;
            if (!valid_alignment(entry.chunk.alignment) || (entry.offset % entry.chunk.alignment) != 0 ||
                !checked_add(entry.offset, entry.stored_size, chunk_end) || entry.offset < kHeaderSize || chunk_end > directory_offset) {
                return container_error<NativeContainer>("URE-Q-CONTAINER-004", "Chunk range or alignment is invalid");
            }
            if (!checked_add(total_stored, entry.stored_size, total_stored) ||
                !checked_add(total_uncompressed, entry.chunk.uncompressed_size, total_uncompressed)) {
                return container_error<NativeContainer>("URE-Q-BUDGET-002", "Chunk aggregate overflow");
            }
            const bool ratio_exceeded = entry.stored_size != 0 &&
                (entry.chunk.uncompressed_size / entry.stored_size > limits.max_decompression_ratio ||
                 (entry.chunk.uncompressed_size / entry.stored_size == limits.max_decompression_ratio &&
                  entry.chunk.uncompressed_size % entry.stored_size != 0));
            if ((entry.stored_size == 0 && entry.chunk.uncompressed_size != 0) || ratio_exceeded) {
                return container_error<NativeContainer>("URE-Q-BUDGET-003", "Decompression ratio budget exceeded");
            }
            stored.push_back(std::move(entry));
        }
        if (!directory.finished()) return container_error<NativeContainer>("URE-Q-CONTAINER-003", "Directory has trailing bytes");
        if (total_stored > limits.max_total_stored_bytes || total_uncompressed > limits.max_total_uncompressed_bytes) {
            return container_error<NativeContainer>("URE-Q-BUDGET-001", "Container byte budget exceeded");
        }

        std::map<std::string, std::size_t> chunk_indices;
        for (std::size_t i = 0; i < stored.size(); ++i) chunk_indices.emplace(stored[i].chunk.id, i);
        std::map<std::string, int> dependency_state;
        const auto visit_chunk = [&](const auto& self, const std::string& id) -> bool {
            if (dependency_state[id] == 1) return false;
            if (dependency_state[id] == 2) return true;
            dependency_state[id] = 1;
            for (const auto& dependency : stored[chunk_indices.at(id)].chunk.dependencies) {
                if (!chunk_indices.contains(dependency)) return false;
                if (!self(self, dependency)) return false;
            }
            dependency_state[id] = 2;
            return true;
        };
        for (const auto& [id, index] : chunk_indices) {
            static_cast<void>(index);
            if (!visit_chunk(visit_chunk, id)) {
                return container_error<NativeContainer>("URE-Q-DEP-005", "Missing or cyclic chunk dependency");
            }
        }

        std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges;
        ranges.reserve(stored.size());
        for (const auto& entry : stored) ranges.emplace_back(entry.offset, entry.offset + entry.stored_size);
        std::ranges::sort(ranges);
        for (std::size_t i = 1; i < ranges.size(); ++i) {
            if (ranges[i].first < ranges[i - 1].second) return container_error<NativeContainer>("URE-Q-CONTAINER-005", "Chunk ranges overlap");
        }

        LoadResult<NativeContainer> result;
        NativeContainer container;
        container.kind = kind;
        container.container_version = version;
        container.document_uuid = uuid;
        container.semantic_hash = hash_text(semantic_bytes);
        container.flags = flags;
        for (auto& entry : stored) {
            const bool known_type = known_core_chunk(entry.chunk.type) || registry.chunk_kinds.contains(entry.chunk.type);
            if (!known_type && entry.chunk.requirement == RequirementLevel::Required) {
                return container_error<NativeContainer>("URE-Q-CHUNK-001", "Unknown required chunk type");
            }
            if (!known_type) add_warning(result.diagnostics, "URE-Q-CHUNK-101", "/chunks/" + entry.chunk.id, "Unknown optional chunk is preserved");
            const bool known_codec = registry.compression_codecs.contains(entry.chunk.codec);
            if (!known_codec && entry.chunk.requirement == RequirementLevel::Required) {
                return container_error<NativeContainer>("URE-Q-CODEC-001", "Unknown required compression codec");
            }
            if (!known_codec) add_warning(result.diagnostics, "URE-Q-CODEC-101", "/chunks/" + entry.chunk.id, "Unknown optional codec payload is preserved");
            if (entry.chunk.codec == static_cast<std::uint32_t>(CompressionCodec::None) &&
                entry.stored_size != entry.chunk.uncompressed_size) {
                return container_error<NativeContainer>("URE-Q-CODEC-002", "Uncompressed chunk sizes differ");
            }
            const auto payload = bytes.subspan(static_cast<std::size_t>(entry.offset), static_cast<std::size_t>(entry.stored_size));
            if (hash_bytes(sha256_hex(payload)) != entry.expected_hash) {
                return container_error<NativeContainer>("URE-Q-HASH-002", "Chunk payload hash mismatch");
            }
            entry.chunk.payload.assign(payload.begin(), payload.end());
            container.chunks.push_back(std::move(entry.chunk));
        }
        result.value = std::move(container);
        return result;
    } catch (const std::exception& error) {
        return container_error<NativeContainer>("URE-Q-CONTAINER-003", error.what());
    }
}

std::vector<std::uint8_t> write_scene_binary(const SceneDocument& document,
                                             std::vector<ContainerChunk> extra_chunks) {
    ContainerChunk metadata{"metadata", static_cast<std::uint32_t>(ChunkKind::Metadata), {1, 0},
                            RequirementLevel::Required, static_cast<std::uint32_t>(CompressionCodec::None),
                            8, {}, {}, encode_scene_metadata(document)};
    extra_chunks.push_back(std::move(metadata));
    NativeContainer container;
    container.kind = ContainerKind::Scene;
    container.semantic_hash = semantic_hash(document);
    container.chunks = std::move(extra_chunks);
    return write_container(container);
}

LoadResult<SceneDocument> read_scene_binary(std::span<const std::uint8_t> bytes,
                                           const CapabilityRegistry& registry,
                                           const ValidationLimits& limits) {
    const auto container = read_container(bytes, registry, limits);
    if (!container.ok() || !container.value) {
        LoadResult<SceneDocument> result;
        result.diagnostics = container.diagnostics;
        return result;
    }
    if (container.value->kind != ContainerKind::Scene) return container_error<SceneDocument>("URE-Q-CONTAINER-006", "Expected scene container");
    for (const auto& chunk : container.value->chunks) {
        if (chunk.type != static_cast<std::uint32_t>(ChunkKind::Metadata)) continue;
        auto result = decode_scene_metadata(chunk.payload, registry, limits);
        result.diagnostics.insert(result.diagnostics.end(), container.diagnostics.begin(), container.diagnostics.end());
        if (result.value && semantic_hash(*result.value) != container.value->semantic_hash) {
            return container_error<SceneDocument>("URE-Q-HASH-003", "Scene semantic hash mismatch");
        }
        return result;
    }
    return container_error<SceneDocument>("URE-Q-METADATA-002", "Scene metadata chunk is missing");
}

std::vector<std::uint8_t> write_package_binary(const PackageManifest& manifest,
                                               std::vector<ContainerChunk> extra_chunks) {
    ContainerChunk metadata{"manifest", static_cast<std::uint32_t>(ChunkKind::Metadata), {1, 0},
                            RequirementLevel::Required, static_cast<std::uint32_t>(CompressionCodec::None),
                            8, {}, {}, encode_package_metadata(manifest)};
    extra_chunks.push_back(std::move(metadata));
    NativeContainer container;
    container.kind = ContainerKind::Package;
    container.semantic_hash = semantic_hash(manifest);
    container.chunks = std::move(extra_chunks);
    return write_container(container);
}

LoadResult<PackageManifest> read_package_binary(std::span<const std::uint8_t> bytes,
                                               const CapabilityRegistry& registry,
                                               const ValidationLimits& limits) {
    const auto container = read_container(bytes, registry, limits);
    if (!container.ok() || !container.value) {
        LoadResult<PackageManifest> result;
        result.diagnostics = container.diagnostics;
        return result;
    }
    if (container.value->kind != ContainerKind::Package) return container_error<PackageManifest>("URE-Q-CONTAINER-006", "Expected package container");
    for (const auto& chunk : container.value->chunks) {
        if (chunk.type != static_cast<std::uint32_t>(ChunkKind::Metadata)) continue;
        auto result = decode_package_metadata(chunk.payload, registry, limits);
        result.diagnostics.insert(result.diagnostics.end(), container.diagnostics.begin(), container.diagnostics.end());
        if (result.value && semantic_hash(*result.value) != container.value->semantic_hash) {
            return container_error<PackageManifest>("URE-Q-HASH-003", "Package semantic hash mismatch");
        }
        return result;
    }
    return container_error<PackageManifest>("URE-Q-METADATA-002", "Package metadata chunk is missing");
}

}
