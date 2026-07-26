#pragma once

#include "ure/backend_types.hpp"
#include "ure/render_config.hpp"

#include <optional>
#include <string_view>
#include <vector>

namespace ure {

const char* backend_kind_name(BackendKind kind);
std::optional<BackendKind> parse_backend_kind(std::string_view name);
const char* backend_feature_name(BackendFeature feature);
std::optional<BackendFeature> parse_backend_feature(std::string_view name);
BackendFeatureSet required_backend_features(const RenderConfig& config);
std::vector<BackendAdapterInfo> enumerate_backend_adapters(
    BackendKind kind = BackendKind::Auto);
BackendSelection select_backend(const RenderConfig& config);

}
