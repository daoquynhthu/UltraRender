#include <cstdlib>
#include <iostream>
#include <memory>

#include <ure/native_resource_catalog.hpp>
#include <ure/native_scene_hash.hpp>
#include <ure/native_scene_ir.hpp>

namespace {

int failures = 0;
void check(bool condition, const char* message) { if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; } }
std::string hash(std::string_view value) { return ure::native_scene::sha256_hex(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(value.data()), value.size())); }

ure::native_scene::NativeResourceEntry entry(std::string id, ure::native_scene::ResourceKind kind) {
    ure::native_scene::NativeResourceEntry value;
    value.id = std::move(id); value.kind = kind; value.schema_version = {1, 0};
    value.schema_identity = "ure.test/1.0"; value.content_hash = hash(value.id); value.payload_uri = "content://sha256/" + value.content_hash;
    value.payload_bytes = 64; value.resident_bytes = 64;
    return value;
}

ure::native_scene::NativeResourceCatalog catalog() {
    using namespace ure::native_scene;
    NativeResourceCatalog value; value.id = "resources/full"; value.schema_version = {1, 0};
    auto scattering = entry("spectrum/scattering", ResourceKind::SpectralTable);
    scattering.spectral = SpectralResourceContract{SpectralSemantic::Scattering, SpectralRepresentation::SampledTable,
        {360.0, 830.0, 1'000'000, 8}, SpectralInterpolation::Linear, SpectralExtrapolation::Reject, 471, 0, 0, 0.0, 4.0, false};
    auto absorption = entry("spectrum/absorption", ResourceKind::SpectralTable);
    absorption.spectral = SpectralResourceContract{SpectralSemantic::Absorption, SpectralRepresentation::Tiled,
        {360.0, 830.0, 1'000'000, 16}, SpectralInterpolation::Linear, SpectralExtrapolation::Reject, 0, 0, 4096, 0.0, 2.0, false};
    auto axis = entry("spectrum/axis", ResourceKind::SpectralTable);
    axis.spectral = SpectralResourceContract{SpectralSemantic::Radiometric, SpectralRepresentation::SampledTable,
        {360.0, 830.0, 471, 0}, SpectralInterpolation::Linear, SpectralExtrapolation::Reject, 471, 0, 0, 360.0, 830.0, false};
    auto texture = entry("texture/spectral", ResourceKind::Texture);
    texture.dependencies = {"spectrum/axis"}; texture.texture = TextureResourceContract{TextureInterpretation::SourceSpectralGrid, 64, 32, 1, 471, "linear_radiometric", "spectrum/axis"};
    auto graph = entry("material/graph", ResourceKind::MaterialGraph);
    graph.dependencies = {"texture/spectral"}; graph.material = MaterialResourceContract{"scene/material/0", {"texture/spectral"}};
    auto mie = entry("mie/cloud", ResourceKind::MiePhase); mie.schema_identity = "ure.mie/1.0";
    auto medium = entry("medium/cloud", ResourceKind::VolumeField);
    medium.dependencies = {"spectrum/scattering", "spectrum/absorption", "mie/cloud"};
    medium.medium = MediumResourceContract{{360.0, 830.0, 1'000'000, 32}, "spectrum/scattering", "spectrum/absorption", {}, MediumPhaseModel::Mie, "mie/cloud", {}};
    auto index = entry("video/index", ResourceKind::Extension);
    auto video = entry("video/spectral", ResourceKind::Video); video.dependencies = {"video/index"};
    video.video = VideoResourceContract{240, 1, 24, 1920, 1080, TextureInterpretation::SourceSpectralGrid, 471, "video/index", true, 256ull * 1024ull * 1024ull};
    auto basis = entry("spectrum/basis", ResourceKind::SpectralTable);
    basis.spectral = SpectralResourceContract{SpectralSemantic::Reflectance, SpectralRepresentation::Basis,
        {360.0, 830.0, 1'000'000, 0}, SpectralInterpolation::Linear, SpectralExtrapolation::Reject, 0, 12, 0, 0.0, 1.0, true};
    value.resources = {scattering, absorption, axis, texture, graph, mie, medium, index, video, basis};
    return value;
}

ure::native_scene::NativeSceneArchive scene_archive() {
    ure::scene_ir::SceneIR scene;
    ure::native_scene::SceneDocument document; document.id = "scene/q6"; document.schema_version = {1, 0};
    document.features.push_back({"ure.scene.resource", {1, 0}, ure::native_scene::RequirementLevel::Required, "ure", {}, "{}"});
    auto archive = ure::native_scene::make_native_scene_archive(std::move(document), scene);
    archive.resource_catalog = std::make_shared<const ure::native_scene::NativeResourceCatalog>(catalog());
    return archive;
}

}

int main() {
    using namespace ure::native_scene;
    auto value = catalog();
    check(validate_resource_catalog(value).ok(), "valid resource catalog rejected");
    const auto binary = write_resource_catalog_binary(value);
    check(binary.size() > 8 && std::string(reinterpret_cast<const char*>(binary.data() + 4), 4) == "URRC", "URRC identifier missing");
    const auto binary_loaded = read_resource_catalog_binary(binary);
    check(binary_loaded.ok() && resource_catalog_semantic_hash(*binary_loaded.value) == resource_catalog_semantic_hash(value), "binary roundtrip changed semantics");
    const auto text = write_resource_catalog_text(value);
    const auto text_loaded = read_resource_catalog_text(text);
    check(text_loaded.ok() && resource_catalog_semantic_hash(*text_loaded.value) == resource_catalog_semantic_hash(value), "text roundtrip changed semantics");
    auto packet_variant = value; packet_variant.resources[0].spectral->domain.packet_lanes_hint = 32;
    check(resource_catalog_semantic_hash(packet_variant) == resource_catalog_semantic_hash(value), "packet lanes changed source identity");
    auto invalid = value; invalid.resources[0].spectral->value_min = -1.0;
    check(!validate_resource_catalog(invalid).ok(), "negative physical spectrum accepted");
    invalid = value; invalid.resources[6].medium->phase_resource = "spectrum/axis";
    check(!validate_resource_catalog(invalid).ok(), "non-Mie phase resource accepted");
    invalid = value; invalid.resources[3].texture->source_spectral_samples = 0;
    check(!validate_resource_catalog(invalid).ok(), "spectral texture without samples accepted");
    invalid = value; invalid.resources[9].spectral->basis_count = 0;
    check(!validate_resource_catalog(invalid).ok(), "basis resource without basis functions accepted");
    invalid = value; invalid.resources[0].spectral->domain.domain_bins = 0;
    check(!validate_resource_catalog(invalid).ok(), "empty spectral domain accepted");
    invalid = value; invalid.resources[0].dependencies = {"spectrum/absorption"}; invalid.resources[1].dependencies = {"spectrum/scattering"};
    check(!validate_resource_catalog(invalid).ok(), "dependency cycle accepted");
    auto archive = scene_archive(); CapabilityRegistry registry; registry.features.emplace("ure.scene.resource", Version{1, 0});
    const auto archive_binary = write_scene_ir_binary(archive); const auto archive_loaded = read_scene_ir_binary(archive_binary, registry);
    for (const auto& diagnostic : archive_loaded.diagnostics) if (diagnostic.severity == DiagnosticSeverity::Error) std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    check(archive_loaded.ok() && archive_loaded.value->resource_catalog != nullptr, "binary scene lost resource catalog");
    const auto exploded = write_scene_ir_text(archive); const auto exploded_loaded = read_scene_ir_text(exploded, registry);
    for (const auto& diagnostic : exploded_loaded.diagnostics) if (diagnostic.severity == DiagnosticSeverity::Error) std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    check(exploded_loaded.ok() && exploded_loaded.value->resource_catalog != nullptr, "text scene lost resource catalog");
    check(scene_ir_semantic_hash(*archive_loaded.value) == scene_ir_semantic_hash(*exploded_loaded.value), "binary/text scene hashes differ");
    auto missing_feature = archive; missing_feature.document.features.clear();
    bool feature_rejected = false; try { (void)write_scene_ir_binary(missing_feature); } catch (const std::invalid_argument&) { feature_rejected = true; }
    check(feature_rejected, "resource catalog without required feature accepted");
    std::cout << "Phase Q.6 native resource checks: " << (failures ? "FAILED" : "PASSED") << '\n';
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
