#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <ure/native_scene_hash.hpp>
#include <ure/native_scene_uuid.hpp>

namespace ure::native_scene {
namespace {

std::uint8_t hex(char value) {
    if (value >= '0' && value <= '9')
        return static_cast<std::uint8_t>(value - '0');
    const char lower = static_cast<char>(
        std::tolower(static_cast<unsigned char>(value)));
    if (lower >= 'a' && lower <= 'f')
        return static_cast<std::uint8_t>(lower - 'a' + 10);
    throw std::invalid_argument("UUID contains a non-hexadecimal character");
}

}

bool is_nil_uuid(const Uuid& value) noexcept {
    return std::ranges::all_of(value.bytes,
                               [](std::uint8_t byte) { return byte == 0; });
}

bool is_rfc9562_uuid(const Uuid& value) noexcept {
    if (is_nil_uuid(value) || (value.bytes[8] & UINT8_C(0xc0)) != UINT8_C(0x80))
        return false;
    const std::uint8_t version = value.bytes[6] >> 4U;
    return version == 1 || (version >= 3 && version <= 8);
}

Uuid parse_uuid(std::string_view text) {
    if (text.size() != 36 || text[8] != '-' || text[13] != '-' ||
        text[18] != '-' || text[23] != '-')
        throw std::invalid_argument("UUID text is not in canonical 8-4-4-4-12 form");
    Uuid output;
    std::size_t byte = 0;
    for (std::size_t index = 0; index < text.size();) {
        if (text[index] == '-') {
            ++index;
            continue;
        }
        output.bytes[byte++] = static_cast<std::uint8_t>(
            (hex(text[index]) << 4U) | hex(text[index + 1]));
        index += 2;
    }
    if (!is_rfc9562_uuid(output))
        throw std::invalid_argument("UUID variant or version is not RFC 9562");
    return output;
}

std::string format_uuid(const Uuid& value) {
    if (!is_rfc9562_uuid(value))
        throw std::invalid_argument("UUID variant or version is not RFC 9562");
    static constexpr char digits[] = "0123456789abcdef";
    std::string output;
    output.reserve(36);
    for (std::size_t index = 0; index < value.bytes.size(); ++index) {
        if (index == 4 || index == 6 || index == 8 || index == 10)
            output.push_back('-');
        output.push_back(digits[value.bytes[index] >> 4U]);
        output.push_back(digits[value.bytes[index] & UINT8_C(0x0f)]);
    }
    return output;
}

Uuid deterministic_object_uuid(std::string_view document_id,
                               std::string_view object_kind,
                               std::string_view legacy_alias) {
    if (document_id.empty() || object_kind.empty() || legacy_alias.empty())
        throw std::invalid_argument("Deterministic UUID inputs must be non-empty");
    std::vector<std::uint8_t> input;
    auto append = [&](std::string_view value) {
        input.insert(input.end(), value.begin(), value.end());
        input.push_back(0);
    };
    append("UltraRender.NativeObjectUUID.v1");
    append(document_id);
    append(object_kind);
    append(legacy_alias);
    const std::string digest = sha256_hex(input);
    Uuid output;
    for (std::size_t index = 0; index < output.bytes.size(); ++index) {
        output.bytes[index] = static_cast<std::uint8_t>(
            (hex(digest[index * 2]) << 4U) | hex(digest[index * 2 + 1]));
    }
    output.bytes[6] = static_cast<std::uint8_t>(
        (output.bytes[6] & UINT8_C(0x0f)) | UINT8_C(0x80));
    output.bytes[8] = static_cast<std::uint8_t>(
        (output.bytes[8] & UINT8_C(0x3f)) | UINT8_C(0x80));
    return output;
}

}
