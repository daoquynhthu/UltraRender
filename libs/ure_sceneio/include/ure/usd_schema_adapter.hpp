#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <ure/native_adapter.hpp>
#include <ure/native_resource_catalog.hpp>

namespace ure::usd {

inline constexpr std::string_view
    kUsdSchemaAdapterIdentity =
        "ure.adapter.usd-schema/1.0";
inline constexpr native_scene::Version
    kUsdSchemaAdapterVersion{1, 0};
inline constexpr std::string_view
    kUsdPhysicsApiSchema = "UREPhysicsAPI";
inline constexpr std::string_view
    kUsdSpectralMaterialApiSchema =
        "URESpectralMaterialAPI";
inline constexpr std::string_view
    kUsdSpectralDomainBinsAttribute =
        "ure:spectral:domainBins";
inline constexpr std::string_view
    kUsdSpectralPacketLanesAttribute =
        "ure:spectral:packetLanes";
inline constexpr std::string_view
    kUsdSpectralResourceUriAttribute =
        "ure:spectral:resourceUri";
inline constexpr std::string_view
    kUsdSpectralContentHashAttribute =
        "ure:spectral:contentHash";
inline constexpr std::string_view
    kUsdSpectralBasisCountAttribute =
        "ure:spectral:basisCount";
inline constexpr std::string_view
    kUsdSpectralTileBinsAttribute =
        "ure:spectral:tileBins";

enum class UsdUpAxis : std::uint8_t {
    Y,
    Z
};

struct UsdTransform {
    core::Vec3f translation = {0.0f, 0.0f, 0.0f};
    core::Quat rotation = {};
    core::Vec3f scale = {1.0f, 1.0f, 1.0f};
    bool affine_trs_compatible = true;
};

struct UsdSpectralResourceBinding {
    std::string id;
    std::string content_hash;
    std::string uri;
    native_scene::SpectralSemantic semantic =
        native_scene::SpectralSemantic::Radiometric;
    native_scene::SpectralRepresentation representation =
        native_scene::SpectralRepresentation::SampledTable;
    double wavelength_min_nm = 360.0;
    double wavelength_max_nm = 830.0;
    std::uint64_t domain_bins = 0;
    std::uint32_t packet_lanes = 0;
    std::uint64_t sample_count = 0;
    std::uint32_t basis_count = 0;
    std::uint64_t tile_bins = 0;
    double value_min = 0.0;
    double value_max = 0.0;
    std::uint64_t payload_bytes = 0;
    std::uint64_t resident_bytes = 0;
};

struct UsdMaterialPrim {
    std::string path;
    std::string display_name;
    scene_ir::MaterialModel model =
        scene_ir::MaterialModel::Lambertian;
    core::Vec3f base_color = {0.8f, 0.8f, 0.8f};
    float roughness = 0.5f;
    float ior = 1.45f;
    core::Vec3f emission = {0.0f, 0.0f, 0.0f};
    std::vector<UsdSpectralResourceBinding>
        spectral_resources;
};

struct UsdMeshPrim {
    std::string path;
    std::string display_name;
    std::vector<core::Vec3f> points;
    std::vector<core::Vec3f> normals;
    std::vector<core::Vec2f> texcoords;
    std::vector<std::uint32_t> face_vertex_counts;
    std::vector<std::uint32_t> face_vertex_indices;
    UsdTransform transform;
    std::string material_path;
    RigidBodyConfig rigid_body;
};

struct UsdSpherePrim {
    std::string path;
    std::string display_name;
    core::Vec3f center = {0.0f, 0.0f, 0.0f};
    float radius = 0.5f;
    std::string material_path;
    RigidBodyConfig rigid_body;
};

struct UsdCameraPrim {
    std::string path;
    core::Vec3f position = {0.0f, 0.0f, 10.0f};
    core::Vec3f look_at = {0.0f, 0.0f, 0.0f};
    core::Vec3f up = {0.0f, 1.0f, 0.0f};
    float vertical_fov_degrees = 45.0f;
    float aspect_ratio = 16.0f / 9.0f;
    float aperture = 0.0f;
    float focus_distance = 10.0f;
};

struct UsdStageSnapshot {
    std::string source_identifier;
    double metres_per_unit = 1.0;
    UsdUpAxis up_axis = UsdUpAxis::Y;
    std::string camera_path;
    std::uint64_t authored_time_sample_count = 1;
    std::vector<std::string> required_schemas;
    std::vector<std::string> optional_schemas;
    std::vector<UsdMaterialPrim> materials;
    std::vector<UsdMeshPrim> meshes;
    std::vector<UsdSpherePrim> spheres;
    std::vector<UsdCameraPrim> cameras;
};

struct UsdSchemaAdapterLimits {
    std::size_t max_prims = 1'000'000;
    std::size_t max_vertices = 100'000'000;
    std::size_t max_indices = 300'000'000;
    native_scene::ValidationLimits native;
};

struct UsdPrimMapping {
    std::string usd_path;
    std::string native_id;
};

struct UsdSchemaAdapterResult {
    native_scene::NativeAdapterResult native;
    std::vector<UsdPrimMapping> mappings;

    bool ok() const;
};

UsdSchemaAdapterResult import_usd_schema_stage(
    const UsdStageSnapshot& stage,
    const UsdSchemaAdapterLimits& limits = {});

}
