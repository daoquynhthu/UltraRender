#pragma once

#include <array>
#include <cstdint>

namespace ure::resource {

struct ResourceId {
    std::uint64_t namespace_id = 0;
    std::uint64_t local_id = 0;

    constexpr explicit operator bool() const noexcept {
        return namespace_id != 0 || local_id != 0;
    }

    constexpr bool operator==(const ResourceId&) const = default;

    constexpr bool operator<(const ResourceId& other) const noexcept {
        return namespace_id < other.namespace_id ||
               (namespace_id == other.namespace_id &&
                local_id < other.local_id);
    }
};

enum class ResidencyMode : std::uint8_t {
    Resident,
    Streamed,
    SparseTiled
};

struct ResourceSetMetadata {
    std::array<std::uint8_t, 32> content_hash = {};
    std::uint64_t descriptor_count = 0;
    std::uint64_t logical_bytes = 0;
    std::uint64_t minimum_resident_bytes = 0;

    bool operator==(const ResourceSetMetadata&) const = default;
};

}
