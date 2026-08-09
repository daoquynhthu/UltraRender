#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/verifier.h>
#include <nlohmann/json.hpp>

#include <ure/native_resource_catalog.hpp>
#include <ure/native_scene_hash.hpp>

#include "ure_resource_catalog_v1_generated.h"

namespace ure::native_scene {
namespace {

namespace fb = ure::resource::schema;
using Json = nlohmann::ordered_json;

bool valid_hash(const std::string& value) {
    return value.size() == 64 && std::ranges::all_of(value, [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

bool valid_uri(const std::string& value) {
    return value.starts_with("content://sha256/") ||
           (!value.empty() && value.front() != '/' && value.find("..") == std::string::npos && value.find(':') == std::string::npos && value.find('\\') == std::string::npos);
}

void add_error(ValidationReport& report, std::string code, std::string path, std::string message) {
    report.diagnostics.push_back({std::move(code), DiagnosticSeverity::Error, std::move(path), std::move(message), {}});
}

Json domain_json(const SpectralDomainContract& value, bool include_packet_lanes) {
    Json result{{"domain_bins", value.domain_bins}, {"wavelength_max_nm", value.wavelength_max_nm}, {"wavelength_min_nm", value.wavelength_min_nm}};
    if (include_packet_lanes) result["packet_lanes_hint"] = value.packet_lanes_hint;
    return result;
}

SpectralDomainContract read_domain(const Json& value) {
    SpectralDomainContract result;
    result.domain_bins = value.at("domain_bins").get<std::uint64_t>();
    result.packet_lanes_hint = value.value("packet_lanes_hint", 0u);
    result.wavelength_max_nm = value.at("wavelength_max_nm").get<double>();
    result.wavelength_min_nm = value.at("wavelength_min_nm").get<double>();
    return result;
}

Json entry_json(const NativeResourceEntry& value, bool semantic) {
    auto dependencies = value.dependencies;
    std::ranges::sort(dependencies);
    Json result{{"content_hash", value.content_hash}, {"dependencies", dependencies}, {"id", value.id},
        {"kind", static_cast<std::uint32_t>(value.kind)}, {"payload_bytes", value.payload_bytes}, {"payload_uri", value.payload_uri},
        {"residency", static_cast<std::uint8_t>(value.residency)}, {"resident_bytes", value.resident_bytes},
        {"schema_identity", value.schema_identity}, {"schema_version", {{"major", value.schema_version.major}, {"minor", value.schema_version.minor}}}};
    if (value.spectral) result["spectral"] = {{"basis_count", value.spectral->basis_count}, {"domain", domain_json(value.spectral->domain, !semantic)},
        {"extrapolation", static_cast<std::uint8_t>(value.spectral->extrapolation)}, {"interpolation", static_cast<std::uint8_t>(value.spectral->interpolation)},
        {"normalized", value.spectral->normalized}, {"representation", static_cast<std::uint8_t>(value.spectral->representation)},
        {"sample_count", value.spectral->sample_count}, {"semantic", static_cast<std::uint8_t>(value.spectral->semantic)},
        {"tile_bins", value.spectral->tile_bins}, {"value_max", value.spectral->value_max}, {"value_min", value.spectral->value_min}};
    if (value.texture) result["texture"] = {{"color_encoding", value.texture->color_encoding}, {"height", value.texture->height},
        {"interpretation", static_cast<std::uint8_t>(value.texture->interpretation)}, {"source_spectral_samples", value.texture->source_spectral_samples},
        {"spatial_channels", value.texture->spatial_channels}, {"wavelength_axis_resource", value.texture->wavelength_axis_resource}, {"width", value.texture->width}};
    if (value.material) { auto material_dependencies = value.material->resource_dependencies; std::ranges::sort(material_dependencies); result["material"] = {{"graph_owner_id", value.material->graph_owner_id}, {"resource_dependencies", material_dependencies}}; }
    if (value.medium) result["medium"] = {{"absorption_resource", value.medium->absorption_resource}, {"domain", domain_json(value.medium->domain, !semantic)},
        {"emission_resource", value.medium->emission_resource}, {"extension_owner", value.medium->extension_owner}, {"phase", static_cast<std::uint8_t>(value.medium->phase)},
        {"phase_resource", value.medium->phase_resource}, {"scattering_resource", value.medium->scattering_resource}};
    if (value.video) result["video"] = {{"cache_budget_bytes", value.video->cache_budget_bytes}, {"frame_count", value.video->frame_count},
        {"frame_index_resource", value.video->frame_index_resource}, {"height", value.video->height}, {"interpretation", static_cast<std::uint8_t>(value.video->interpretation)},
        {"per_frame_hashes", value.video->per_frame_hashes}, {"source_spectral_samples", value.video->source_spectral_samples},
        {"time_denominator", value.video->time_denominator}, {"time_numerator", value.video->time_numerator}, {"width", value.video->width}};
    return result;
}

Json catalog_json(const NativeResourceCatalog& catalog, bool semantic) {
    std::vector<NativeResourceEntry> resources = catalog.resources;
    std::ranges::sort(resources, {}, &NativeResourceEntry::id);
    Json entries = Json::array();
    for (const auto& entry : resources) entries.push_back(entry_json(entry, semantic));
    return {{"id", catalog.id}, {"resources", std::move(entries)}, {"schema", kResourceCatalogSchemaIdentity},
            {"schema_version", {{"major", catalog.schema_version.major}, {"minor", catalog.schema_version.minor}}}};
}

NativeResourceEntry read_entry(const Json& value) {
    NativeResourceEntry result;
    result.id = value.at("id").get<std::string>(); result.kind = static_cast<ResourceKind>(value.at("kind").get<std::uint32_t>());
    result.schema_version = {value.at("schema_version").at("major").get<std::uint32_t>(), value.at("schema_version").at("minor").get<std::uint32_t>()};
    result.schema_identity = value.at("schema_identity").get<std::string>(); result.content_hash = value.at("content_hash").get<std::string>();
    result.payload_uri = value.at("payload_uri").get<std::string>(); result.payload_bytes = value.at("payload_bytes").get<std::uint64_t>();
    result.resident_bytes = value.at("resident_bytes").get<std::uint64_t>(); result.residency = static_cast<ResourceResidency>(value.at("residency").get<std::uint8_t>());
    result.dependencies = value.at("dependencies").get<std::vector<std::string>>();
    if (value.contains("spectral")) { const auto& source = value.at("spectral"); SpectralResourceContract item; item.semantic = static_cast<SpectralSemantic>(source.at("semantic").get<std::uint8_t>()); item.representation = static_cast<SpectralRepresentation>(source.at("representation").get<std::uint8_t>()); item.domain = read_domain(source.at("domain")); item.interpolation = static_cast<SpectralInterpolation>(source.at("interpolation").get<std::uint8_t>()); item.extrapolation = static_cast<SpectralExtrapolation>(source.at("extrapolation").get<std::uint8_t>()); item.sample_count = source.at("sample_count").get<std::uint64_t>(); item.basis_count = source.at("basis_count").get<std::uint32_t>(); item.tile_bins = source.at("tile_bins").get<std::uint64_t>(); item.value_min = source.at("value_min").get<double>(); item.value_max = source.at("value_max").get<double>(); item.normalized = source.at("normalized").get<bool>(); result.spectral = item; }
    if (value.contains("texture")) { const auto& source = value.at("texture"); TextureResourceContract item; item.interpretation = static_cast<TextureInterpretation>(source.at("interpretation").get<std::uint8_t>()); item.width = source.at("width").get<std::uint64_t>(); item.height = source.at("height").get<std::uint64_t>(); item.spatial_channels = source.at("spatial_channels").get<std::uint32_t>(); item.source_spectral_samples = source.at("source_spectral_samples").get<std::uint64_t>(); item.color_encoding = source.at("color_encoding").get<std::string>(); item.wavelength_axis_resource = source.at("wavelength_axis_resource").get<std::string>(); result.texture = item; }
    if (value.contains("material")) { const auto& source = value.at("material"); result.material = MaterialResourceContract{source.at("graph_owner_id").get<std::string>(), source.at("resource_dependencies").get<std::vector<std::string>>()}; }
    if (value.contains("medium")) { const auto& source = value.at("medium"); MediumResourceContract item; item.domain = read_domain(source.at("domain")); item.scattering_resource = source.at("scattering_resource").get<std::string>(); item.absorption_resource = source.at("absorption_resource").get<std::string>(); item.emission_resource = source.at("emission_resource").get<std::string>(); item.phase = static_cast<MediumPhaseModel>(source.at("phase").get<std::uint8_t>()); item.phase_resource = source.at("phase_resource").get<std::string>(); item.extension_owner = source.at("extension_owner").get<std::string>(); result.medium = item; }
    if (value.contains("video")) { const auto& source = value.at("video"); VideoResourceContract item; item.frame_count = source.at("frame_count").get<std::uint64_t>(); item.time_numerator = source.at("time_numerator").get<std::uint64_t>(); item.time_denominator = source.at("time_denominator").get<std::uint64_t>(); item.width = source.at("width").get<std::uint64_t>(); item.height = source.at("height").get<std::uint64_t>(); item.interpretation = static_cast<TextureInterpretation>(source.at("interpretation").get<std::uint8_t>()); item.source_spectral_samples = source.at("source_spectral_samples").get<std::uint64_t>(); item.frame_index_resource = source.at("frame_index_resource").get<std::string>(); item.per_frame_hashes = source.at("per_frame_hashes").get<bool>(); item.cache_budget_bytes = source.at("cache_budget_bytes").get<std::uint64_t>(); result.video = item; }
    return result;
}

fb::SpectralDomainT schema_domain(const SpectralDomainContract& value) { fb::SpectralDomainT result; result.wavelength_min_nm = value.wavelength_min_nm; result.wavelength_max_nm = value.wavelength_max_nm; result.domain_bins = value.domain_bins; result.packet_lanes_hint = value.packet_lanes_hint; return result; }
SpectralDomainContract model_domain(const fb::SpectralDomainT& value) { return {value.wavelength_min_nm, value.wavelength_max_nm, value.domain_bins, value.packet_lanes_hint}; }

fb::ResourceCatalogT to_schema(const NativeResourceCatalog& catalog) {
    fb::ResourceCatalogT result; result.id = catalog.id; result.schema_version = std::make_unique<fb::Version>(catalog.schema_version.major, catalog.schema_version.minor);
    std::vector<NativeResourceEntry> sorted = catalog.resources; std::ranges::sort(sorted, {}, &NativeResourceEntry::id);
    for (const auto& source : sorted) {
        auto item = std::make_unique<fb::ResourceEntryT>(); item->id = source.id; item->kind = static_cast<fb::ResourceKind>(source.kind); item->schema_version = std::make_unique<fb::Version>(source.schema_version.major, source.schema_version.minor); item->schema_identity = source.schema_identity; item->content_hash = source.content_hash; item->payload_uri = source.payload_uri; item->payload_bytes = source.payload_bytes; item->resident_bytes = source.resident_bytes; item->residency = static_cast<fb::ResourceResidency>(source.residency); item->dependencies = source.dependencies; std::ranges::sort(item->dependencies);
        if (source.spectral) { item->spectral = std::make_unique<fb::SpectralContractT>(); auto& target = *item->spectral; target.semantic = static_cast<fb::SpectralSemantic>(source.spectral->semantic); target.representation = static_cast<fb::SpectralRepresentation>(source.spectral->representation); target.domain = std::make_unique<fb::SpectralDomainT>(schema_domain(source.spectral->domain)); target.interpolation = static_cast<fb::SpectralInterpolation>(source.spectral->interpolation); target.extrapolation = static_cast<fb::SpectralExtrapolation>(source.spectral->extrapolation); target.sample_count = source.spectral->sample_count; target.basis_count = source.spectral->basis_count; target.tile_bins = source.spectral->tile_bins; target.value_min = source.spectral->value_min; target.value_max = source.spectral->value_max; target.normalized = source.spectral->normalized; }
        if (source.texture) { item->texture = std::make_unique<fb::TextureContractT>(); auto& target = *item->texture; target.interpretation = static_cast<fb::TextureInterpretation>(source.texture->interpretation); target.width = source.texture->width; target.height = source.texture->height; target.spatial_channels = source.texture->spatial_channels; target.source_spectral_samples = source.texture->source_spectral_samples; target.color_encoding = source.texture->color_encoding; target.wavelength_axis_resource = source.texture->wavelength_axis_resource; }
        if (source.material) { item->material = std::make_unique<fb::MaterialContractT>(); item->material->graph_owner_id = source.material->graph_owner_id; item->material->resource_dependencies = source.material->resource_dependencies; std::ranges::sort(item->material->resource_dependencies); }
        if (source.medium) { item->medium = std::make_unique<fb::MediumContractT>(); auto& target = *item->medium; target.domain = std::make_unique<fb::SpectralDomainT>(schema_domain(source.medium->domain)); target.scattering_resource = source.medium->scattering_resource; target.absorption_resource = source.medium->absorption_resource; target.emission_resource = source.medium->emission_resource; target.phase = static_cast<fb::MediumPhaseModel>(source.medium->phase); target.phase_resource = source.medium->phase_resource; target.extension_owner = source.medium->extension_owner; }
        if (source.video) { item->video = std::make_unique<fb::VideoContractT>(); auto& target = *item->video; target.frame_count = source.video->frame_count; target.time_numerator = source.video->time_numerator; target.time_denominator = source.video->time_denominator; target.width = source.video->width; target.height = source.video->height; target.interpretation = static_cast<fb::TextureInterpretation>(source.video->interpretation); target.source_spectral_samples = source.video->source_spectral_samples; target.frame_index_resource = source.video->frame_index_resource; target.per_frame_hashes = source.video->per_frame_hashes; target.cache_budget_bytes = source.video->cache_budget_bytes; }
        result.resources.push_back(std::move(item));
    }
    return result;
}

NativeResourceCatalog from_schema(const fb::ResourceCatalogT& source) {
    NativeResourceCatalog result; result.id = source.id; if (!source.schema_version) throw std::runtime_error("Missing catalog schema version"); result.schema_version = {source.schema_version->major(), source.schema_version->minor()};
    for (const auto& input : source.resources) { if (!input || !input->schema_version) throw std::runtime_error("Missing resource or version"); NativeResourceEntry item; item.id = input->id; item.kind = static_cast<ResourceKind>(input->kind); item.schema_version = {input->schema_version->major(), input->schema_version->minor()}; item.schema_identity = input->schema_identity; item.content_hash = input->content_hash; item.payload_uri = input->payload_uri; item.payload_bytes = input->payload_bytes; item.resident_bytes = input->resident_bytes; item.residency = static_cast<ResourceResidency>(input->residency); item.dependencies = input->dependencies;
        if (input->spectral) { SpectralResourceContract value; value.semantic = static_cast<SpectralSemantic>(input->spectral->semantic); value.representation = static_cast<SpectralRepresentation>(input->spectral->representation); if (!input->spectral->domain) throw std::runtime_error("Missing spectral domain"); value.domain = model_domain(*input->spectral->domain); value.interpolation = static_cast<SpectralInterpolation>(input->spectral->interpolation); value.extrapolation = static_cast<SpectralExtrapolation>(input->spectral->extrapolation); value.sample_count = input->spectral->sample_count; value.basis_count = input->spectral->basis_count; value.tile_bins = input->spectral->tile_bins; value.value_min = input->spectral->value_min; value.value_max = input->spectral->value_max; value.normalized = input->spectral->normalized; item.spectral = value; }
        if (input->texture) item.texture = TextureResourceContract{static_cast<TextureInterpretation>(input->texture->interpretation), input->texture->width, input->texture->height, input->texture->spatial_channels, input->texture->source_spectral_samples, input->texture->color_encoding, input->texture->wavelength_axis_resource};
        if (input->material) item.material = MaterialResourceContract{input->material->graph_owner_id, input->material->resource_dependencies};
        if (input->medium) { if (!input->medium->domain) throw std::runtime_error("Missing medium domain"); item.medium = MediumResourceContract{model_domain(*input->medium->domain), input->medium->scattering_resource, input->medium->absorption_resource, input->medium->emission_resource, static_cast<MediumPhaseModel>(input->medium->phase), input->medium->phase_resource, input->medium->extension_owner}; }
        if (input->video) item.video = VideoResourceContract{input->video->frame_count, input->video->time_numerator, input->video->time_denominator, input->video->width, input->video->height, static_cast<TextureInterpretation>(input->video->interpretation), input->video->source_spectral_samples, input->video->frame_index_resource, input->video->per_frame_hashes, input->video->cache_budget_bytes};
        result.resources.push_back(std::move(item)); }
    return result;
}

template <typename T> LoadResult<T> failure(std::string code, std::string message) { LoadResult<T> result; result.diagnostics.push_back({std::move(code), DiagnosticSeverity::Error, "/resource_catalog", std::move(message), {}}); return result; }

}

ValidationReport validate_resource_catalog(const NativeResourceCatalog& catalog, const ValidationLimits& limits) {
    ValidationReport report;
    if (catalog.id.empty()) add_error(report, "URE-Q6-ID-001", "/resource_catalog/id", "Catalog ID is empty");
    if (catalog.schema_version.major != 1) add_error(report, "URE-Q6-VERSION-001", "/resource_catalog/schema_version", "Unsupported catalog major version");
    if (catalog.resources.size() > limits.max_directory_entries) add_error(report, "URE-Q6-BUDGET-001", "/resource_catalog/resources", "Resource count exceeds budget");
    std::map<std::string, const NativeResourceEntry*> entries; std::uint64_t payload_total = 0;
    for (const auto& entry : catalog.resources) { const std::string path = "/resource_catalog/resources/" + entry.id;
        if (entry.id.empty() || !entries.emplace(entry.id, &entry).second) add_error(report, "URE-Q6-ID-002", path, "Resource IDs must be nonempty and unique");
        if (static_cast<std::uint32_t>(entry.kind) > static_cast<std::uint32_t>(ResourceKind::Extension) || static_cast<std::uint8_t>(entry.residency) > static_cast<std::uint8_t>(ResourceResidency::Tiled)) add_error(report, "URE-Q6-ENUM-001", path, "Unknown resource kind or residency");
        if (!valid_hash(entry.content_hash)) add_error(report, "URE-Q6-HASH-001", path + "/content_hash", "Invalid lowercase SHA-256");
        if (entry.schema_identity.empty() || entry.schema_version.major == 0) add_error(report, "URE-Q6-SCHEMA-001", path, "Resource schema identity/version is required");
        if (!valid_uri(entry.payload_uri)) add_error(report, "URE-Q6-PATH-001", path + "/payload_uri", "Unsafe or noncanonical payload URI");
        if (entry.payload_bytes > limits.max_total_uncompressed_bytes - std::min(payload_total, limits.max_total_uncompressed_bytes)) add_error(report, "URE-Q6-BUDGET-002", path, "Payload aggregate exceeds budget"); else payload_total += entry.payload_bytes;
        if (entry.resident_bytes > limits.max_resident_resource_bytes) add_error(report, "URE-Q6-BUDGET-003", path, "Resident resource exceeds budget");
        const int contracts = entry.spectral.has_value() + entry.texture.has_value() + entry.material.has_value() + entry.medium.has_value() + entry.video.has_value();
        if (contracts > 1) add_error(report, "URE-Q6-CONTRACT-001", path, "At most one typed resource contract is allowed");
        const bool contract_required = entry.kind == ResourceKind::SpectralTable || entry.kind == ResourceKind::Texture ||
            entry.kind == ResourceKind::MaterialGraph || entry.kind == ResourceKind::VolumeField || entry.kind == ResourceKind::Video;
        if (contract_required && contracts != 1) add_error(report, "URE-Q6-CONTRACT-002", path, "Resource kind requires a typed contract");
        if (entry.kind == ResourceKind::SpectralTable && !entry.spectral) add_error(report, "URE-Q6-CONTRACT-003", path, "Spectral resource requires a spectral contract");
        if (entry.kind == ResourceKind::Texture && !entry.texture) add_error(report, "URE-Q6-CONTRACT-004", path, "Texture resource requires a texture contract");
        if (entry.kind == ResourceKind::MaterialGraph && !entry.material) add_error(report, "URE-Q6-CONTRACT-005", path, "Material resource requires a MaterialGraph contract");
        if (entry.kind == ResourceKind::VolumeField && !entry.medium) add_error(report, "URE-Q6-CONTRACT-006", path, "Medium resource requires a medium contract");
        if (entry.kind == ResourceKind::Video && !entry.video) add_error(report, "URE-Q6-CONTRACT-007", path, "Video resource requires a video contract");
        const auto validate_domain = [&](const SpectralDomainContract& domain, const std::string& domain_path) { if (!std::isfinite(domain.wavelength_min_nm) || !std::isfinite(domain.wavelength_max_nm) || domain.wavelength_min_nm <= 0.0 || domain.wavelength_max_nm <= domain.wavelength_min_nm || domain.domain_bins == 0) add_error(report, "URE-Q6-DOMAIN-001", domain_path, "Invalid spectral domain"); if (domain.packet_lanes_hint > 32) add_error(report, "URE-Q6-DOMAIN-002", domain_path, "Packet lane hint exceeds runtime cap"); };
        if (entry.spectral && (static_cast<std::uint8_t>(entry.spectral->semantic) > static_cast<std::uint8_t>(SpectralSemantic::Radiometric) || static_cast<std::uint8_t>(entry.spectral->representation) > static_cast<std::uint8_t>(SpectralRepresentation::SourceSampleGrid) || static_cast<std::uint8_t>(entry.spectral->interpolation) > static_cast<std::uint8_t>(SpectralInterpolation::MonotoneCubic) || static_cast<std::uint8_t>(entry.spectral->extrapolation) > static_cast<std::uint8_t>(SpectralExtrapolation::Reject))) add_error(report, "URE-Q6-ENUM-002", path + "/spectral", "Unknown spectral enum");
        if (entry.texture && static_cast<std::uint8_t>(entry.texture->interpretation) > static_cast<std::uint8_t>(TextureInterpretation::SourceSpectralGrid)) add_error(report, "URE-Q6-ENUM-003", path + "/texture", "Unknown texture interpretation");
        if (entry.medium && static_cast<std::uint8_t>(entry.medium->phase) > static_cast<std::uint8_t>(MediumPhaseModel::Extension)) add_error(report, "URE-Q6-ENUM-004", path + "/medium", "Unknown medium phase model");
        if (entry.video && static_cast<std::uint8_t>(entry.video->interpretation) > static_cast<std::uint8_t>(TextureInterpretation::SourceSpectralGrid)) add_error(report, "URE-Q6-ENUM-005", path + "/video", "Unknown video interpretation");
        if (entry.spectral) { const auto& value = *entry.spectral; validate_domain(value.domain, path + "/spectral/domain"); if (!std::isfinite(value.value_min) || !std::isfinite(value.value_max) || value.value_max < value.value_min) add_error(report, "URE-Q6-VALUE-001", path + "/spectral", "Invalid value bounds"); if ((value.semantic == SpectralSemantic::Reflectance || value.semantic == SpectralSemantic::Extinction || value.semantic == SpectralSemantic::Scattering || value.semantic == SpectralSemantic::Absorption) && value.value_min < 0.0) add_error(report, "URE-Q6-VALUE-002", path + "/spectral", "Physical spectrum cannot be negative"); if (value.semantic == SpectralSemantic::Reflectance && value.value_max > 1.0) add_error(report, "URE-Q6-VALUE-003", path + "/spectral", "Reflectance exceeds one"); if (value.representation == SpectralRepresentation::SampledTable && value.sample_count < 2) add_error(report, "URE-Q6-LAYOUT-001", path + "/spectral", "Sampled table requires at least two samples"); if (value.representation == SpectralRepresentation::Basis && value.basis_count == 0) add_error(report, "URE-Q6-LAYOUT-002", path + "/spectral", "Basis representation requires basis functions"); if (value.representation == SpectralRepresentation::Tiled && (value.tile_bins == 0 || value.tile_bins > value.domain.domain_bins)) add_error(report, "URE-Q6-LAYOUT-003", path + "/spectral", "Invalid spectral tile size"); }
        if (entry.texture) { const auto& value = *entry.texture; if (!value.width || !value.height || !value.spatial_channels) add_error(report, "URE-Q6-TEXTURE-001", path + "/texture", "Texture dimensions/channels must be nonzero"); if (value.interpretation == TextureInterpretation::SourceSpectralGrid && (value.source_spectral_samples < 2 || value.wavelength_axis_resource.empty())) add_error(report, "URE-Q6-TEXTURE-002", path + "/texture", "Spectral grid requires a wavelength axis and samples"); if (value.interpretation == TextureInterpretation::Rgb && value.source_spectral_samples != 0) add_error(report, "URE-Q6-TEXTURE-003", path + "/texture", "RGB texture cannot declare spectral samples"); }
        if (entry.material && entry.material->graph_owner_id.empty()) add_error(report, "URE-Q6-MATERIAL-001", path + "/material", "MaterialGraph owner is required");
        if (entry.medium) { const auto& value = *entry.medium; validate_domain(value.domain, path + "/medium/domain"); if (value.scattering_resource.empty() || value.absorption_resource.empty()) add_error(report, "URE-Q6-MEDIUM-001", path + "/medium", "Medium sigma resources are required"); if (value.phase == MediumPhaseModel::Mie && value.phase_resource.empty()) add_error(report, "URE-Q6-MEDIUM-002", path + "/medium", "Mie medium requires a phase resource"); if (value.phase == MediumPhaseModel::Extension && value.extension_owner.empty()) add_error(report, "URE-Q6-MEDIUM-003", path + "/medium", "Extension phase requires an owner"); }
        if (entry.video) { const auto& value = *entry.video; if (!value.frame_count || !value.time_numerator || !value.time_denominator || !value.width || !value.height || value.frame_index_resource.empty()) add_error(report, "URE-Q6-VIDEO-001", path + "/video", "Invalid video stream contract"); if (value.interpretation == TextureInterpretation::SourceSpectralGrid && value.source_spectral_samples < 2) add_error(report, "URE-Q6-VIDEO-002", path + "/video", "Spectral video requires source samples"); }
    }
    for (const auto& entry : catalog.resources) { std::vector<std::string> references = entry.dependencies; if (entry.texture && !entry.texture->wavelength_axis_resource.empty()) references.push_back(entry.texture->wavelength_axis_resource); if (entry.material) references.insert(references.end(), entry.material->resource_dependencies.begin(), entry.material->resource_dependencies.end()); if (entry.medium) { references.push_back(entry.medium->scattering_resource); references.push_back(entry.medium->absorption_resource); if (!entry.medium->emission_resource.empty()) references.push_back(entry.medium->emission_resource); if (!entry.medium->phase_resource.empty()) references.push_back(entry.medium->phase_resource); } if (entry.video) references.push_back(entry.video->frame_index_resource); for (const auto& reference : references) if (!reference.empty() && !entries.contains(reference)) add_error(report, "URE-Q6-DEP-001", "/resource_catalog/resources/" + entry.id, "Missing resource dependency: " + reference); if (entry.medium && entry.medium->phase == MediumPhaseModel::Mie) { const auto found = entries.find(entry.medium->phase_resource); if (found != entries.end() && found->second->kind != ResourceKind::MiePhase) add_error(report, "URE-Q6-MEDIUM-004", "/resource_catalog/resources/" + entry.id, "Mie phase dependency has the wrong kind"); } }
    for (const auto& entry : catalog.resources) {
        if (!entry.medium) continue;
        for (const auto& id : {entry.medium->scattering_resource, entry.medium->absorption_resource, entry.medium->emission_resource}) {
            if (id.empty()) continue;
            const auto found = entries.find(id);
            if (found == entries.end() || !found->second->spectral) continue;
            const auto& domain = found->second->spectral->domain;
            if (domain.wavelength_min_nm > entry.medium->domain.wavelength_min_nm || domain.wavelength_max_nm < entry.medium->domain.wavelength_max_nm) {
                add_error(report, "URE-Q6-MEDIUM-005", "/resource_catalog/resources/" + entry.id, "Spectral dependency does not cover the medium domain");
            }
        }
    }
    std::map<std::string, int> state; const auto visit = [&](const auto& self, const NativeResourceEntry& entry) -> void { if (state[entry.id] == 1) { add_error(report, "URE-Q6-DEP-002", "/resource_catalog/resources/" + entry.id, "Cyclic resource dependency"); return; } if (state[entry.id] == 2) return; state[entry.id] = 1; for (const auto& dependency : entry.dependencies) { const auto found = entries.find(dependency); if (found != entries.end()) self(self, *found->second); } state[entry.id] = 2; }; for (const auto& entry : catalog.resources) visit(visit, entry);
    return report;
}

std::string resource_catalog_semantic_hash(const NativeResourceCatalog& catalog) { const std::string text = catalog_json(catalog, true).dump(); return sha256_hex(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()), text.size())); }

std::vector<std::uint8_t> write_resource_catalog_binary(const NativeResourceCatalog& catalog) { const auto validation = validate_resource_catalog(catalog); if (!validation.ok()) throw std::invalid_argument(validation.diagnostics.front().message); auto native = to_schema(catalog); flatbuffers::FlatBufferBuilder builder; fb::FinishResourceCatalogBuffer(builder, fb::ResourceCatalog::Pack(builder, &native)); return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()}; }

LoadResult<NativeResourceCatalog> read_resource_catalog_binary(std::span<const std::uint8_t> bytes, const ValidationLimits& limits) { if (bytes.size() > limits.max_total_uncompressed_bytes) return failure<NativeResourceCatalog>("URE-Q6-BUDGET-004", "Catalog bytes exceed budget"); const auto max_tables = static_cast<flatbuffers::uoffset_t>(std::min<std::uint64_t>(limits.max_object_count, std::numeric_limits<flatbuffers::uoffset_t>::max())); flatbuffers::Verifier verifier(bytes.data(), bytes.size(), limits.max_nesting_depth, max_tables); if (!fb::VerifyResourceCatalogBuffer(verifier)) return failure<NativeResourceCatalog>("URE-Q6-SCHEMA-001", "Invalid URRC payload"); try { std::unique_ptr<fb::ResourceCatalogT> native(fb::GetResourceCatalog(bytes.data())->UnPack()); auto value = from_schema(*native); auto validation = validate_resource_catalog(value, limits); LoadResult<NativeResourceCatalog> result; result.diagnostics = validation.diagnostics; if (validation.ok()) result.value = std::move(value); return result; } catch (const std::exception& error) { return failure<NativeResourceCatalog>("URE-Q6-SCHEMA-002", error.what()); } }

std::string write_resource_catalog_text(const NativeResourceCatalog& catalog) { const auto validation = validate_resource_catalog(catalog); if (!validation.ok()) throw std::invalid_argument(validation.diagnostics.front().message); return catalog_json(catalog, false).dump(2) + "\n"; }

LoadResult<NativeResourceCatalog> read_resource_catalog_text(std::string_view text, const ValidationLimits& limits) { if (text.size() > limits.max_total_uncompressed_bytes) return failure<NativeResourceCatalog>("URE-Q6-BUDGET-004", "Catalog text exceeds budget"); try { const Json root = Json::parse(text); if (root.at("schema").get<std::string>() != kResourceCatalogSchemaIdentity) return failure<NativeResourceCatalog>("URE-Q6-SCHEMA-003", "Wrong catalog schema identity"); NativeResourceCatalog catalog; catalog.id = root.at("id").get<std::string>(); catalog.schema_version = {root.at("schema_version").at("major").get<std::uint32_t>(), root.at("schema_version").at("minor").get<std::uint32_t>()}; for (const auto& entry : root.at("resources")) catalog.resources.push_back(read_entry(entry)); auto validation = validate_resource_catalog(catalog, limits); LoadResult<NativeResourceCatalog> result; result.diagnostics = validation.diagnostics; if (validation.ok()) result.value = std::move(catalog); return result; } catch (const std::exception& error) { return failure<NativeResourceCatalog>("URE-Q6-TEXT-001", error.what()); } }

}
