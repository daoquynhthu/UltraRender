#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include <ultrarender/ure_loader.h>

#include "runtime_adapter.hpp"

namespace {

constexpr std::uint32_t kRuntimeMajor = 0;
constexpr std::uint32_t kRuntimeMinor = 1;
constexpr std::uint32_t kRuntimePatch = 0;
constexpr std::size_t kMaximumChainLength = 32;

struct ChainHeader {
    std::uint32_t type;
    std::uint32_t size;
    const void* next;
};

static_assert(sizeof(ChainHeader) == sizeof(ure_input_header_t));

bool validate_chain(const void* next, std::uint32_t root_type) noexcept {
    std::array<const void*, kMaximumChainLength> pointers{};
    std::array<std::uint32_t, kMaximumChainLength> types{};
    std::size_t count = 0;
    while (next != nullptr) {
        if (count == kMaximumChainLength) return false;
        ChainHeader header{};
        std::memcpy(&header, next, sizeof(header));
        if (header.type == 0 || header.type == root_type || header.size < sizeof(header)) return false;
        for (std::size_t index = 0; index < count; ++index) {
            if (pointers[index] == next || types[index] == header.type) return false;
        }
        pointers[count] = next;
        types[count] = header.type;
        ++count;
        next = header.next;
    }
    return true;
}

template <class T>
bool validate_input_root(const T* value, std::uint32_t expected_type) noexcept {
    if (value == nullptr) return false;
    if (value->header.type != expected_type || value->header.size < sizeof(T)) return false;
    return validate_chain(value->header.next, value->header.type);
}

template <class T>
bool validate_output_root(T* value, std::uint32_t expected_type) noexcept {
    if (value == nullptr) return false;
    if (value->header.type != expected_type || value->header.size < sizeof(T)) return false;
    return validate_chain(value->header.next, value->header.type);
}

bool validate_diagnostic(ure_bootstrap_diagnostic_t* diagnostic) noexcept {
    if (diagnostic == nullptr) return true;
    if (!validate_output_root(diagnostic, URE_STRUCTURE_BOOTSTRAP_DIAGNOSTIC)) return false;
    if (diagnostic->reserved != 0) return false;
    return diagnostic->message_capacity == 0 || diagnostic->message_data != nullptr;
}

void write_diagnostic(
    ure_bootstrap_diagnostic_t* diagnostic,
    ure_result_t result,
    std::uint32_t detail,
    std::string_view message) noexcept {
    if (diagnostic == nullptr) return;
    diagnostic->result = result;
    diagnostic->domain = URE_ERROR_DOMAIN_CORE;
    diagnostic->detail = detail;
    diagnostic->message_required = static_cast<std::uint32_t>(message.size());
    const std::size_t writable = diagnostic->message_capacity == 0
        ? 0
        : static_cast<std::size_t>(diagnostic->message_capacity - 1);
    const std::size_t written = message.size() < writable ? message.size() : writable;
    if (written != 0) std::memcpy(diagnostic->message_data, message.data(), written);
    if (diagnostic->message_capacity != 0) diagnostic->message_data[written] = '\0';
    diagnostic->message_written = static_cast<std::uint32_t>(written);
}

ure_result_t fail(
    ure_bootstrap_diagnostic_t* diagnostic,
    ure_result_t result,
    std::uint32_t detail,
    std::string_view message) noexcept {
    write_diagnostic(diagnostic, result, detail, message);
    return result;
}

bool version_contains(
    std::uint32_t minimum_major,
    std::uint32_t minimum_minor,
    std::uint32_t maximum_major,
    std::uint32_t maximum_minor) noexcept {
    const std::uint64_t minimum = (static_cast<std::uint64_t>(minimum_major) << 32U) | minimum_minor;
    const std::uint64_t maximum = (static_cast<std::uint64_t>(maximum_major) << 32U) | maximum_minor;
    const std::uint64_t runtime = (static_cast<std::uint64_t>(kRuntimeMajor) << 32U) | kRuntimeMinor;
    return minimum <= maximum && minimum <= runtime && runtime <= maximum;
}

bool digest_is_zero(const ure_digest256_t& digest) noexcept {
    for (const std::uint8_t byte : digest.bytes) {
        if (byte != 0) return false;
    }
    return true;
}

bool uuid_equal(const ure_uuid_t& left, const std::array<std::uint8_t, 16>& right) noexcept {
    return std::memcmp(left.bytes, right.data(), right.size()) == 0;
}

ure_result_t get_runtime_manifest(
    const ure_runtime_manifest_request_t* request,
    ure_runtime_manifest_t* manifest,
    ure_bootstrap_diagnostic_t* diagnostic) {
    if (!validate_diagnostic(diagnostic)) return URE_RESULT_INVALID_ARGUMENT;
    if (!validate_input_root(request, URE_STRUCTURE_RUNTIME_MANIFEST_REQUEST)) {
        return fail(diagnostic, URE_RESULT_INVALID_ARGUMENT, 1, "invalid runtime manifest request");
    }
    if (!validate_output_root(manifest, URE_STRUCTURE_RUNTIME_MANIFEST)) {
        return fail(diagnostic, URE_RESULT_INVALID_ARGUMENT, 2, "invalid runtime manifest output");
    }
    if (request->reserved[0] != 0 || request->reserved[1] != 0 || manifest->reserved != 0) {
        return fail(diagnostic, URE_RESULT_INVALID_ARGUMENT, 3, "reserved fields must be zero");
    }
    if (!version_contains(
            request->minimum_major,
            request->minimum_minor,
            request->maximum_major,
            request->maximum_minor)) {
        return fail(diagnostic, URE_RESULT_INCOMPATIBLE_VERSION, 4, "runtime version range is incompatible");
    }
    const auto& registry_digest = ure::contract::registry_digest();
    if (!digest_is_zero(request->expected_registry_digest) &&
        std::memcmp(request->expected_registry_digest.bytes, registry_digest.data(), registry_digest.size()) != 0) {
        return fail(diagnostic, URE_RESULT_INCOMPATIBLE_VERSION, 5, "registry digest is incompatible");
    }

    const std::string_view identity = ure::contract::runtime_identity();
    const std::string_view abi_manifest = ure::contract::abi_manifest_json();
    manifest->runtime_major = kRuntimeMajor;
    manifest->runtime_minor = kRuntimeMinor;
    manifest->runtime_patch = kRuntimePatch;
    std::memcpy(manifest->registry_digest.bytes, registry_digest.data(), registry_digest.size());
    manifest->runtime_identity = {identity.data(), identity.size()};
    manifest->abi_manifest_json = {
        reinterpret_cast<const std::uint8_t*>(abi_manifest.data()),
        abi_manifest.size()};
    write_diagnostic(diagnostic, URE_RESULT_SUCCESS, 0, {});
    return URE_RESULT_SUCCESS;
}

ure_result_t query_interface(
    const ure_interface_query_t* query,
    ure_interface_response_t* response,
    ure_bootstrap_diagnostic_t* diagnostic) noexcept {
    if (!validate_diagnostic(diagnostic)) return URE_RESULT_INVALID_ARGUMENT;
    if (!validate_input_root(query, URE_STRUCTURE_INTERFACE_QUERY)) {
        return fail(diagnostic, URE_RESULT_INVALID_ARGUMENT, 6, "invalid interface query");
    }
    if (!validate_output_root(response, URE_STRUCTURE_INTERFACE_RESPONSE)) {
        return fail(diagnostic, URE_RESULT_INVALID_ARGUMENT, 7, "invalid interface response");
    }
    if (query->reserved[0] != 0 || query->reserved[1] != 0 ||
        response->reserved[0] != 0 || response->reserved[1] != 0) {
        return fail(diagnostic, URE_RESULT_INVALID_ARGUMENT, 8, "reserved fields must be zero");
    }
    if (!version_contains(
            query->minimum_major,
            query->minimum_minor,
            query->maximum_major,
            query->maximum_minor)) {
        return fail(diagnostic, URE_RESULT_INCOMPATIBLE_VERSION, 9, "interface version range is incompatible");
    }

    static constexpr std::array<std::uint8_t, 16> runtime_id URE_INTERFACE_RUNTIME_UUID_BYTES;
    static constexpr std::array<std::uint8_t, 16> instance_id URE_INTERFACE_INSTANCE_UUID_BYTES;
    if (uuid_equal(query->interface_id, instance_id)) {
        return fail(diagnostic, URE_RESULT_CAPABILITY_UNAVAILABLE, 10, "instance interface begins at PB.3");
    }
    if (!uuid_equal(query->interface_id, runtime_id)) {
        return fail(diagnostic, URE_RESULT_CAPABILITY_UNAVAILABLE, 11, "interface is not available");
    }

    const ure_runtime_interface_t& table = ure::contract::runtime_interface();
    response->interface_id = query->interface_id;
    response->version_major = table.header.version_major;
    response->version_minor = table.header.version_minor;
    response->table_size = table.header.struct_size;
    response->table = &table;
    write_diagnostic(diagnostic, URE_RESULT_SUCCESS, 0, {});
    return URE_RESULT_SUCCESS;
}

}

extern "C" URE_PUBLIC_API ure_result_t URE_CALL ureGetRuntimeManifest(
    const ure_runtime_manifest_request_t* request,
    ure_runtime_manifest_t* manifest,
    ure_bootstrap_diagnostic_t* diagnostic) {
    try {
        return get_runtime_manifest(request, manifest, diagnostic);
    } catch (...) {
        if (validate_diagnostic(diagnostic)) {
            return fail(diagnostic, URE_RESULT_INTERNAL, 12, "runtime manifest construction failed");
        }
        return URE_RESULT_INTERNAL;
    }
}

extern "C" URE_PUBLIC_API ure_result_t URE_CALL ureQueryInterface(
    const ure_interface_query_t* query,
    ure_interface_response_t* response,
    ure_bootstrap_diagnostic_t* diagnostic) {
    try {
        return query_interface(query, response, diagnostic);
    } catch (...) {
        if (validate_diagnostic(diagnostic)) {
            return fail(diagnostic, URE_RESULT_INTERNAL, 13, "interface query failed");
        }
        return URE_RESULT_INTERNAL;
    }
}
