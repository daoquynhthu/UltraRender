#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <ure/native_scene.hpp>

namespace ure::native_scene {

inline constexpr std::string_view kResourceCatalogSchemaIdentity = "ure.resource-catalog/1.0";
inline constexpr std::string_view kResourceCatalogFeature = "ure.scene.resource";

enum class SpectralSemantic : std::uint8_t { Reflectance, Emission, Ior, Extinction, Scattering, Absorption, Radiometric };
enum class SpectralRepresentation : std::uint8_t { Constant, RgbDerived, SampledTable, Basis, Tiled, SourceSampleGrid };
enum class SpectralInterpolation : std::uint8_t { Nearest, Linear, MonotoneCubic };
enum class SpectralExtrapolation : std::uint8_t { Zero, Clamp, Reject };
enum class TextureInterpretation : std::uint8_t { Spatial, Rgb, SourceSpectralGrid };
enum class MediumPhaseModel : std::uint8_t { HenyeyGreenstein, Rayleigh, Mie, Extension };
enum class ResourceResidency : std::uint8_t { Resident, Streamed, Tiled };

struct SpectralDomainContract {
    double wavelength_min_nm = 360.0;
    double wavelength_max_nm = 830.0;
    std::uint64_t domain_bins = 0;
    std::uint32_t packet_lanes_hint = 0;
};

struct SpectralResourceContract {
    SpectralSemantic semantic = SpectralSemantic::Radiometric;
    SpectralRepresentation representation = SpectralRepresentation::SampledTable;
    SpectralDomainContract domain;
    SpectralInterpolation interpolation = SpectralInterpolation::Linear;
    SpectralExtrapolation extrapolation = SpectralExtrapolation::Reject;
    std::uint64_t sample_count = 0;
    std::uint32_t basis_count = 0;
    std::uint64_t tile_bins = 0;
    double value_min = 0.0;
    double value_max = 0.0;
    bool normalized = false;
};

struct TextureResourceContract {
    TextureInterpretation interpretation = TextureInterpretation::Spatial;
    std::uint64_t width = 0;
    std::uint64_t height = 0;
    std::uint32_t spatial_channels = 0;
    std::uint64_t source_spectral_samples = 0;
    std::string color_encoding;
    std::string wavelength_axis_resource;
};

struct MaterialResourceContract {
    std::string graph_owner_id;
    std::vector<std::string> resource_dependencies;
};

struct MediumResourceContract {
    SpectralDomainContract domain;
    std::string scattering_resource;
    std::string absorption_resource;
    std::string emission_resource;
    MediumPhaseModel phase = MediumPhaseModel::HenyeyGreenstein;
    std::string phase_resource;
    std::string extension_owner;
};

struct VideoResourceContract {
    std::uint64_t frame_count = 0;
    std::uint64_t time_numerator = 1;
    std::uint64_t time_denominator = 24;
    std::uint64_t width = 0;
    std::uint64_t height = 0;
    TextureInterpretation interpretation = TextureInterpretation::Rgb;
    std::uint64_t source_spectral_samples = 0;
    std::string frame_index_resource;
    bool per_frame_hashes = true;
    std::uint64_t cache_budget_bytes = 0;
};

struct NativeResourceEntry {
    std::string id;
    ResourceKind kind = ResourceKind::Extension;
    Version schema_version;
    std::string schema_identity;
    std::string content_hash;
    std::string payload_uri;
    std::uint64_t payload_bytes = 0;
    std::uint64_t resident_bytes = 0;
    ResourceResidency residency = ResourceResidency::Resident;
    std::vector<std::string> dependencies;
    std::optional<SpectralResourceContract> spectral;
    std::optional<TextureResourceContract> texture;
    std::optional<MaterialResourceContract> material;
    std::optional<MediumResourceContract> medium;
    std::optional<VideoResourceContract> video;
};

struct NativeResourceCatalog {
    std::string id;
    Version schema_version;
    std::vector<NativeResourceEntry> resources;
};

ValidationReport validate_resource_catalog(const NativeResourceCatalog& catalog,
                                           const ValidationLimits& limits = {});
std::string resource_catalog_semantic_hash(const NativeResourceCatalog& catalog);
std::vector<std::uint8_t> write_resource_catalog_binary(const NativeResourceCatalog& catalog);
LoadResult<NativeResourceCatalog> read_resource_catalog_binary(std::span<const std::uint8_t> bytes,
                                                               const ValidationLimits& limits = {});
std::string write_resource_catalog_text(const NativeResourceCatalog& catalog);
LoadResult<NativeResourceCatalog> read_resource_catalog_text(std::string_view text,
                                                             const ValidationLimits& limits = {});

}
