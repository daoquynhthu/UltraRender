#pragma once

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <ure/native_scene_ir.hpp>
#include <ure/native_resource_resolution.hpp>
#include <ure/product/product_service.hpp>
#include <ure/render_config.hpp>

namespace ure::product {

enum class ProductMaterialOrigin : std::uint32_t {
    NativeGraph,
    CanonicalizedLegacy,
    Gltf,
    MaterialX,
    Preset,
    Hydra
};

enum class ProductMaterialUpdateClass : std::uint32_t {
    HotUpdate,
    PartialRebuild,
    FullSnapshotReplacement,
    Rejected
};

enum ProductMaterialFeature : std::uint64_t {
    ProductMaterialFeatureNone = 0,
    ProductMaterialFeatureTexture = UINT64_C(1) << 0,
    ProductMaterialFeatureSpectral = UINT64_C(1) << 1,
    ProductMaterialFeatureMedium = UINT64_C(1) << 2,
    ProductMaterialFeatureMie = UINT64_C(1) << 3,
    ProductMaterialFeatureDiffractive = UINT64_C(1) << 4,
    ProductMaterialFeatureFluorescent = UINT64_C(1) << 5,
    ProductMaterialFeatureComposite = UINT64_C(1) << 6,
    ProductMaterialFeatureLayered = UINT64_C(1) << 7
};

struct ProductMaterialProgram {
    Identity identity{};
    Identity canonical_identity{};
    std::string material_uuid;
    std::string source_id;
    std::string name;
    std::string compiler_semantic;
    std::string backend_semantic;
    ProductMaterialOrigin origin{ProductMaterialOrigin::NativeGraph};
    ProductMaterialUpdateClass update_class{
        ProductMaterialUpdateClass::HotUpdate};
    std::uint64_t features{};
    std::uint64_t spectral_domain_bins{};
    std::uint32_t spectral_packet_lanes{};
    std::uint32_t node_count{};
    std::vector<std::string> resource_uris;
    std::vector<std::string> resource_content_hashes;
    std::vector<std::string> applicable_integrators;
};

struct ProductMaterialProgramSet {
    Identity identity{};
    std::vector<ProductMaterialProgram> programs;
};

struct ProductMaterialUpdatePlan {
    ProductMaterialUpdateClass update_class{
        ProductMaterialUpdateClass::HotUpdate};
    std::vector<std::string> changed_material_uuids;
};

class ProductMaterialError final : public std::runtime_error {
public:
    ProductMaterialError(std::uint32_t detail, std::string code,
                         std::string field_path, std::string message,
                         std::string recovery);

    std::uint32_t detail() const noexcept;
    const std::string& code() const noexcept;
    const std::string& field_path() const noexcept;
    const std::string& recovery() const noexcept;

private:
    std::uint32_t detail_{};
    std::string code_;
    std::string field_path_;
    std::string recovery_;
};

ProductMaterialProgramSet canonicalize_product_materials(
    scene_ir::SceneIR& scene,
    const native_scene::NativeSceneSourceIds& source_ids,
    const native_scene::NativeSceneObjectUuids& object_uuids,
    const RenderConfig& config,
    std::span<const native_scene::FeatureDeclaration> features = {});

void bind_product_material_resources(
    ProductMaterialProgramSet& programs,
    std::span<const native_scene::NativeResolvedResource> resources);

void configure_product_material_execution(
    const ProductMaterialProgramSet& programs,
    RenderConfig& config);

ProductMaterialUpdatePlan classify_product_material_update(
    const ProductMaterialProgramSet& before,
    const ProductMaterialProgramSet& after);

}
