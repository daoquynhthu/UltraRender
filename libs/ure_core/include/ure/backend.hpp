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
const char* acceleration_provider_name(AccelerationProviderKind provider);
std::optional<AccelerationProviderKind> parse_acceleration_provider(
    std::string_view name);
const char* acceleration_quality_name(AccelerationBuildQuality quality);
std::optional<AccelerationBuildQuality> parse_acceleration_quality(
    std::string_view name);
const char* acceleration_update_policy_name(
    AccelerationUpdatePolicy policy);
std::optional<AccelerationUpdatePolicy> parse_acceleration_update_policy(
    std::string_view name);
BackendFeatureSet required_backend_features(const RenderConfig& config);
std::vector<BackendAdapterInfo> enumerate_backend_adapters(
    BackendKind kind = BackendKind::Auto);
BackendSelection select_backend(const RenderConfig& config);

}
