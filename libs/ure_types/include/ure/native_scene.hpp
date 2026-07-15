#pragma once

#include <algorithm>
#include <array>
#include <compare>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace ure::native_scene {

struct Version {
    std::uint32_t major = 1;
    std::uint32_t minor = 0;
    auto operator<=>(const Version&) const = default;
};

inline constexpr Version kSceneSchemaVersion{1, 0};
inline constexpr Version kSceneContainerVersion{1, 0};
inline constexpr Version kPackageFormatVersion{1, 0};
inline constexpr std::string_view kSceneSchemaIdentity = "ure.scene/1.0";
inline constexpr std::string_view kSceneContainerIdentity = "ure.container.scene/1.0";
inline constexpr std::string_view kPackageContainerIdentity = "ure.container.package/1.0";

enum class RequirementLevel : std::uint8_t {
    Required,
    Optional,
    Advisory
};

enum class DiagnosticSeverity : std::uint8_t {
    Error,
    Warning,
    Info
};

enum class ResourceKind : std::uint32_t {
    Scene,
    Geometry,
    MaterialGraph,
    Texture,
    SpectralTable,
    MiePhase,
    VolumeField,
    Animation,
    Physics,
    Acoustic,
    Video,
    Validation,
    Provenance,
    Cache,
    Extension
};

enum class ChunkKind : std::uint32_t {
    Metadata = 1,
    SceneGraph = 2,
    Geometry = 3,
    MaterialGraph = 4,
    Texture = 5,
    SpectralTable = 6,
    MiePhase = 7,
    VolumeField = 8,
    Animation = 9,
    Physics = 10,
    Acoustic = 11,
    Video = 12,
    Validation = 13,
    Provenance = 14,
    CacheReference = 15,
    ProceduralGraph = 17,
    ScriptBuild = 18,
    ResourceCatalog = 19,
    SolverContract = 20,
    SimulationContract = 21,
    Extension = 0x80000000u
};

enum class CompressionCodec : std::uint32_t {
    None = 0
};

enum class ContainerKind : std::uint8_t {
    Scene,
    Package
};

struct SceneConventions {
    std::string length_unit = "metre";
    std::string time_unit = "second";
    std::string mass_unit = "kilogram";
    std::string angle_unit = "radian";
    std::string wavelength_unit = "vacuum_nanometre";
    std::string handedness = "right";
    std::string up_axis = "+Y";
    std::string camera_forward = "-Z";
    std::string color_encoding = "linear_radiometric";
    auto operator<=>(const SceneConventions&) const = default;
};

struct FeatureDeclaration {
    std::string name;
    Version minimum_version;
    RequirementLevel requirement = RequirementLevel::Required;
    std::string provider;
    std::vector<std::string> dependencies;
    std::string canonical_parameters = "{}";
    auto operator<=>(const FeatureDeclaration&) const = default;
};

struct ExtensionRecord {
    std::string name;
    Version version;
    RequirementLevel requirement = RequirementLevel::Required;
    std::string payload_type;
    std::vector<std::uint8_t> opaque_payload;
    auto operator<=>(const ExtensionRecord&) const = default;
};

struct ResourceDescriptor {
    std::string id;
    std::string content_hash;
    ResourceKind kind = ResourceKind::Extension;
    Version schema_version;
    std::string uri;
    std::vector<std::string> dependencies;
    std::uint64_t byte_length = 0;
    std::uint64_t resident_bytes = 0;
    auto operator<=>(const ResourceDescriptor&) const = default;
};

struct MigrationRecord {
    Version source_version;
    Version target_version;
    std::string tool_id;
    Version tool_version;
    std::string input_hash;
    std::string output_hash;
    bool lossy = false;
    auto operator<=>(const MigrationRecord&) const = default;
};

struct SceneReference {
    std::string id;
    std::string content_hash;
    std::string uri;
    auto operator<=>(const SceneReference&) const = default;
};

struct PackageDependency {
    std::string package_id;
    std::string manifest_hash;
    auto operator<=>(const PackageDependency&) const = default;
};

struct SceneDocument {
    std::string id;
    Version schema_version;
    SceneConventions conventions;
    std::vector<FeatureDeclaration> features;
    std::vector<ExtensionRecord> extensions;
    std::vector<ResourceDescriptor> resources;
    std::vector<MigrationRecord> migrations;
    auto operator<=>(const SceneDocument&) const = default;
};

struct PackageManifest {
    std::string id;
    Version format_version;
    std::vector<SceneReference> scenes;
    std::vector<ResourceDescriptor> resources;
    std::vector<ResourceDescriptor> caches;
    std::vector<PackageDependency> dependencies;
    auto operator<=>(const PackageManifest&) const = default;
};

struct ValidationDiagnostic {
    std::string code;
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::string path;
    std::string message;
    std::string migration_guidance;
    auto operator<=>(const ValidationDiagnostic&) const = default;
};

struct ValidationReport {
    std::vector<ValidationDiagnostic> diagnostics;

    bool ok() const {
        return std::ranges::none_of(diagnostics, [](const ValidationDiagnostic& diagnostic) {
            return diagnostic.severity == DiagnosticSeverity::Error;
        });
    }
};

template <typename T>
struct LoadResult {
    std::optional<T> value;
    std::vector<ValidationDiagnostic> diagnostics;

    bool ok() const {
        return value.has_value() && std::ranges::none_of(diagnostics, [](const ValidationDiagnostic& diagnostic) {
            return diagnostic.severity == DiagnosticSeverity::Error;
        });
    }
};

struct ValidationLimits {
    std::uint64_t max_directory_entries = 1'000'000;
    std::uint64_t max_total_stored_bytes = 16ull * 1024ull * 1024ull * 1024ull;
    std::uint64_t max_total_uncompressed_bytes = 32ull * 1024ull * 1024ull * 1024ull;
    std::uint64_t max_resident_resource_bytes = 8ull * 1024ull * 1024ull * 1024ull;
    std::uint64_t max_inline_numeric_scalars = 64;
    std::uint64_t max_decompression_ratio = 256;
};

struct CapabilityRegistry {
    std::map<std::string, Version> features;
    std::map<std::string, Version> extensions;
    std::set<std::uint32_t> chunk_kinds;
    std::set<std::uint32_t> compression_codecs{static_cast<std::uint32_t>(CompressionCodec::None)};
};

inline constexpr std::array<std::uint32_t, 2> kReservedChunkKindIds{0, 16};
inline constexpr std::array<std::uint32_t, 2> kReservedContainerFlagBits{1u << 2, 1u << 3};

}
