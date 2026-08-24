#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <ure/product/product_scene.hpp>

namespace ure::product {

enum class SceneToolOperation : std::uint32_t {
    Validate = 1,
    Inspect = 2,
    Build = 3,
    Migrate = 4,
    Pack = 5,
    Unpack = 6,
    Realize = 7
};

struct SceneToolRequest {
    SceneToolOperation operation{SceneToolOperation::Validate};
    std::vector<std::filesystem::path> inputs;
    std::filesystem::path output;
    std::string package_scene_id;
    native_scene::ValidationLimits validation;
    std::uint64_t temporary_bytes{UINT64_C(1073741824)};
    bool allow_script_execution{};
};

struct SceneToolResult {
    SceneToolOperation operation{SceneToolOperation::Validate};
    Identity snapshot_identity{};
    std::string semantic_identity;
    std::uint64_t stored_bytes{};
    std::uint64_t resident_bytes{};
    std::size_t scene_count{};
    std::size_t resource_count{};
    std::size_t cache_count{};
    std::size_t dependency_count{};
    native_scene::NativeResourceBudgetBreakdown resource_budget;
    std::vector<ProductFeatureRecord> dispositions;
    std::vector<ProductSceneDiagnostic> diagnostics;

    bool ok() const noexcept;
    std::string report_json() const;
};

SceneToolResult execute_scene_tool(const SceneToolRequest& request);

}
