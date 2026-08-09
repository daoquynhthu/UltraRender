#pragma once

#include <string>
#include <string_view>

#include <ure/native_scene.hpp>

namespace ure::native_scene {

bool is_nil_uuid(const Uuid& value) noexcept;
bool is_rfc9562_uuid(const Uuid& value) noexcept;
Uuid parse_uuid(std::string_view text);
std::string format_uuid(const Uuid& value);
Uuid deterministic_object_uuid(std::string_view document_id,
                               std::string_view object_kind,
                               std::string_view legacy_alias);

}
