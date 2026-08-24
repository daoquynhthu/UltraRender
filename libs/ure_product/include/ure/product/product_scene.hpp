#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ure/native_resource_resolution.hpp>
#include <ure/product/product_service.hpp>

namespace ure::product {

enum class ProductFeatureDisposition : std::uint32_t {
    Executed,
    Rejected,
    PreservedForTooling
};

enum class ProductSceneDiagnosticDomain : std::uint32_t {
    Parse = 1,
    Schema = 2,
    Feature = 3,
    Resource = 4,
    Procedural = 5,
    Solver = 6,
    Simulation = 7,
    Package = 8,
    Publication = 9
};

struct ProductFeatureRecord {
    std::string id;
    native_scene::RequirementLevel requirement{
        native_scene::RequirementLevel::Required};
    ProductFeatureDisposition disposition{
        ProductFeatureDisposition::Executed};
    std::uint32_t detail{};
};

struct ProductSceneDiagnostic {
    ProductSceneDiagnosticDomain domain{ProductSceneDiagnosticDomain::Schema};
    std::uint32_t detail{};
    std::string code;
    std::string field_path;
    std::string message;
    std::string recovery;
    native_scene::RequirementLevel requirement{
        native_scene::RequirementLevel::Required};
    ProductFeatureDisposition disposition{
        ProductFeatureDisposition::Rejected};
};

struct ProductRealizationLimits {
    native_scene::ValidationLimits validation;
    std::uint64_t temporary_bytes{UINT64_C(1073741824)};
};

class ProductSnapshot {
public:
    const Identity& identity() const noexcept;
    const Identity& source_identity() const noexcept;
    std::string_view semantic_identity() const noexcept;
    int declared_samples() const noexcept;
    scene_ir::SceneIR render_scene() const;
    const std::vector<native_scene::NativeResolvedResource>& resources()
        const noexcept;
    const native_scene::NativeResourceBudgetBreakdown& resource_budget()
        const noexcept;
    const std::vector<ProductFeatureRecord>& feature_dispositions()
        const noexcept;
    const std::optional<RenderConfig>& solver_config() const noexcept;
    const std::optional<PhysicsConfig>& simulation_plan() const noexcept;
    const std::filesystem::path& materialization_root() const noexcept;

private:
    friend struct ProductSnapshotBuilder;
    Identity identity_{};
    Identity source_identity_{};
    std::string semantic_identity_;
    native_scene::NativeSceneArchive archive_;
    scene_ir::SceneIR render_scene_;
    std::vector<native_scene::NativeResolvedResource> resources_;
    native_scene::NativeResourceBudgetBreakdown resource_budget_;
    std::vector<ProductFeatureRecord> feature_dispositions_;
    std::optional<RenderConfig> solver_config_;
    std::optional<PhysicsConfig> simulation_plan_;
    std::filesystem::path materialization_root_;
    std::shared_ptr<void> materialization_owner_;
};

struct ProductRealizationResult {
    std::shared_ptr<const ProductSnapshot> snapshot;
    std::vector<ProductSceneDiagnostic> diagnostics;

    bool ok() const noexcept;
};

ProductRealizationResult realize_product_scene(
    native_scene::NativeSceneArchive archive,
    const ProductRealizationLimits& limits = {});

ProductRealizationResult realize_product_scene(
    native_scene::NativeSceneArchive archive,
    Identity accepted_source_identity,
    const ProductRealizationLimits& limits = {});

}
