#include <algorithm>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <ure/native_scene_text.hpp>
#include <ure/native_scene_validation.hpp>

namespace ure::native_scene {
namespace {

using Json = nlohmann::json;

void require_exact_keys(const Json& value, const std::set<std::string>& keys);

Json write_version(Version version) {
    return Json{{"major", version.major}, {"minor", version.minor}};
}

Version read_version(const Json& value) {
    require_exact_keys(value, {"major", "minor"});
    return {value.at("major").get<std::uint32_t>(), value.at("minor").get<std::uint32_t>()};
}

std::string requirement_name(RequirementLevel level) {
    switch (level) {
        case RequirementLevel::Required: return "required";
        case RequirementLevel::Optional: return "optional";
        case RequirementLevel::Advisory: return "advisory";
    }
    throw std::invalid_argument("Invalid requirement level");
}

RequirementLevel read_requirement(const std::string& value) {
    if (value == "required") return RequirementLevel::Required;
    if (value == "optional") return RequirementLevel::Optional;
    if (value == "advisory") return RequirementLevel::Advisory;
    throw std::invalid_argument("Invalid requirement level");
}

std::string resource_kind_name(ResourceKind kind) {
    switch (kind) {
        case ResourceKind::Scene: return "scene";
        case ResourceKind::Geometry: return "geometry";
        case ResourceKind::MaterialGraph: return "material_graph";
        case ResourceKind::Texture: return "texture";
        case ResourceKind::SpectralTable: return "spectral_table";
        case ResourceKind::MiePhase: return "mie_phase";
        case ResourceKind::VolumeField: return "volume_field";
        case ResourceKind::Animation: return "animation";
        case ResourceKind::Physics: return "physics";
        case ResourceKind::Acoustic: return "acoustic";
        case ResourceKind::Video: return "video";
        case ResourceKind::Validation: return "validation";
        case ResourceKind::Provenance: return "provenance";
        case ResourceKind::Cache: return "cache";
        case ResourceKind::Extension: return "extension";
    }
    throw std::invalid_argument("Invalid resource kind");
}

ResourceKind read_resource_kind(const std::string& value) {
    static const std::map<std::string, ResourceKind> values{
        {"scene", ResourceKind::Scene}, {"geometry", ResourceKind::Geometry},
        {"material_graph", ResourceKind::MaterialGraph}, {"texture", ResourceKind::Texture},
        {"spectral_table", ResourceKind::SpectralTable}, {"mie_phase", ResourceKind::MiePhase},
        {"volume_field", ResourceKind::VolumeField}, {"animation", ResourceKind::Animation},
        {"physics", ResourceKind::Physics}, {"acoustic", ResourceKind::Acoustic},
        {"video", ResourceKind::Video}, {"validation", ResourceKind::Validation},
        {"provenance", ResourceKind::Provenance}, {"cache", ResourceKind::Cache},
        {"extension", ResourceKind::Extension}
    };
    const auto found = values.find(value);
    if (found == values.end()) throw std::invalid_argument("Invalid resource kind");
    return found->second;
}

std::string hex_encode(const std::vector<std::uint8_t>& bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2u);
    for (std::uint8_t byte : bytes) {
        result.push_back(digits[byte >> 4u]);
        result.push_back(digits[byte & 0x0fu]);
    }
    return result;
}

std::vector<std::uint8_t> hex_decode(const std::string& text) {
    if ((text.size() & 1u) != 0u) throw std::invalid_argument("Odd hexadecimal payload length");
    auto value = [](char digit) -> std::uint8_t {
        if (digit >= '0' && digit <= '9') return static_cast<std::uint8_t>(digit - '0');
        if (digit >= 'a' && digit <= 'f') return static_cast<std::uint8_t>(digit - 'a' + 10);
        throw std::invalid_argument("Non-canonical hexadecimal payload");
    };
    std::vector<std::uint8_t> result(text.size() / 2u);
    for (std::size_t i = 0; i < result.size(); ++i) {
        result[i] = static_cast<std::uint8_t>((value(text[i * 2u]) << 4u) | value(text[i * 2u + 1u]));
    }
    return result;
}

Json write_conventions(const SceneConventions& value) {
    return Json{
        {"angle_unit", value.angle_unit}, {"camera_forward", value.camera_forward},
        {"color_encoding", value.color_encoding}, {"handedness", value.handedness},
        {"length_unit", value.length_unit}, {"mass_unit", value.mass_unit},
        {"time_unit", value.time_unit}, {"up_axis", value.up_axis},
        {"wavelength_unit", value.wavelength_unit}
    };
}

SceneConventions read_conventions(const Json& value) {
    require_exact_keys(value, {"angle_unit", "camera_forward", "color_encoding", "handedness", "length_unit",
                               "mass_unit", "time_unit", "up_axis", "wavelength_unit"});
    SceneConventions result;
    result.angle_unit = value.at("angle_unit").get<std::string>();
    result.camera_forward = value.at("camera_forward").get<std::string>();
    result.color_encoding = value.at("color_encoding").get<std::string>();
    result.handedness = value.at("handedness").get<std::string>();
    result.length_unit = value.at("length_unit").get<std::string>();
    result.mass_unit = value.at("mass_unit").get<std::string>();
    result.time_unit = value.at("time_unit").get<std::string>();
    result.up_axis = value.at("up_axis").get<std::string>();
    result.wavelength_unit = value.at("wavelength_unit").get<std::string>();
    return result;
}

Json write_resource(const ResourceDescriptor& resource) {
    return Json{
        {"byte_length", resource.byte_length}, {"content_hash", resource.content_hash},
        {"dependencies", resource.dependencies}, {"id", resource.id},
        {"kind", resource_kind_name(resource.kind)}, {"resident_bytes", resource.resident_bytes},
        {"schema_version", write_version(resource.schema_version)}, {"uri", resource.uri}
    };
}

ResourceDescriptor read_resource(const Json& value) {
    require_exact_keys(value, {"byte_length", "content_hash", "dependencies", "id", "kind", "resident_bytes",
                               "schema_version", "uri"});
    ResourceDescriptor result;
    result.byte_length = value.at("byte_length").get<std::uint64_t>();
    result.content_hash = value.at("content_hash").get<std::string>();
    result.dependencies = value.at("dependencies").get<std::vector<std::string>>();
    result.id = value.at("id").get<std::string>();
    result.kind = read_resource_kind(value.at("kind").get<std::string>());
    result.resident_bytes = value.at("resident_bytes").get<std::uint64_t>();
    result.schema_version = read_version(value.at("schema_version"));
    result.uri = value.at("uri").get<std::string>();
    return result;
}

bool has_forbidden_key(const Json& value) {
    if (value.is_object()) {
        for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
            std::string key = iterator.key();
            std::ranges::transform(key, key.begin(), [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
            if (key.find("base64") != std::string::npos || has_forbidden_key(iterator.value())) return true;
        }
    } else if (value.is_array()) {
        for (const auto& element : value) if (has_forbidden_key(element)) return true;
    }
    return false;
}

bool numeric_array_limit_exceeded(const Json& value, std::uint64_t limit) {
    if (value.is_array()) {
        const bool numeric_array = std::ranges::all_of(value, [](const Json& element) { return element.is_number(); });
        if (numeric_array && value.size() > limit) return true;
    }
    if (value.is_array() || value.is_object()) {
        for (const auto& element : value) if (numeric_array_limit_exceeded(element, limit)) return true;
    }
    return false;
}

void require_exact_keys(const Json& value, const std::set<std::string>& keys) {
    if (!value.is_object() || value.size() != keys.size()) throw std::invalid_argument("Object fields do not match schema");
    for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
        if (!keys.contains(iterator.key())) throw std::invalid_argument("Unknown core field: " + iterator.key());
    }
}

template <typename T>
LoadResult<T> text_error(std::string code, std::string message) {
    LoadResult<T> result;
    result.diagnostics.push_back({std::move(code), DiagnosticSeverity::Error, "/", std::move(message), {}});
    return result;
}

Json parse_text(std::string_view text, const ValidationLimits& limits) {
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xefu &&
        static_cast<unsigned char>(text[1]) == 0xbbu && static_cast<unsigned char>(text[2]) == 0xbfu) {
        throw std::domain_error("BOM is forbidden");
    }
    Json root = Json::parse(text.begin(), text.end());
    if (has_forbidden_key(root)) throw std::logic_error("Base64 fields are forbidden");
    if (numeric_array_limit_exceeded(root, limits.max_inline_numeric_scalars)) {
        throw std::length_error("Inline numeric scalar budget exceeded");
    }
    return root;
}

void append_report(std::vector<ValidationDiagnostic>& target, ValidationReport report) {
    target.insert(target.end(), std::make_move_iterator(report.diagnostics.begin()),
                  std::make_move_iterator(report.diagnostics.end()));
}

}

std::string write_scene_text(const SceneDocument& document) {
    auto sorted_features = document.features;
    auto sorted_extensions = document.extensions;
    auto sorted_resources = document.resources;
    std::ranges::sort(sorted_features, {}, &FeatureDeclaration::name);
    std::ranges::sort(sorted_extensions, {}, &ExtensionRecord::name);
    std::ranges::sort(sorted_resources, {}, &ResourceDescriptor::id);
    Json features = Json::array();
    for (const auto& feature : sorted_features) {
        features.push_back(Json{
            {"dependencies", feature.dependencies}, {"minimum_version", write_version(feature.minimum_version)},
            {"name", feature.name}, {"parameters", Json::parse(feature.canonical_parameters)},
            {"provider", feature.provider}, {"requirement", requirement_name(feature.requirement)}
        });
    }
    Json extensions = Json::array();
    for (const auto& extension : sorted_extensions) {
        extensions.push_back(Json{
            {"name", extension.name}, {"opaque_payload_hex", hex_encode(extension.opaque_payload)},
            {"payload_type", extension.payload_type}, {"requirement", requirement_name(extension.requirement)},
            {"version", write_version(extension.version)}
        });
    }
    Json resources = Json::array();
    for (const auto& resource : sorted_resources) resources.push_back(write_resource(resource));
    Json migrations = Json::array();
    for (const auto& migration : document.migrations) {
        migrations.push_back(Json{
            {"input_hash", migration.input_hash}, {"lossy", migration.lossy},
            {"output_hash", migration.output_hash}, {"source_version", write_version(migration.source_version)},
            {"target_version", write_version(migration.target_version)}, {"tool_id", migration.tool_id},
            {"tool_version", write_version(migration.tool_version)}
        });
    }
    const Json root{
        {"conventions", write_conventions(document.conventions)}, {"extensions", std::move(extensions)},
        {"features", std::move(features)}, {"format", std::string(
            document.schema_version.major >= 2 ? kSceneSchemaIdentityV2 :
                                                 kSceneSchemaIdentity)},
        {"id", document.id}, {"kind", "scene"}, {"migrations", std::move(migrations)},
        {"resources", std::move(resources)}, {"schema_version", write_version(document.schema_version)}
    };
    return root.dump(2) + "\n";
}

LoadResult<SceneDocument> read_scene_text(std::string_view text,
                                         const CapabilityRegistry& registry,
                                         const ValidationLimits& limits) {
    try {
        const Json root = parse_text(text, limits);
        require_exact_keys(root, {"conventions", "extensions", "features", "format", "id", "kind", "migrations", "resources", "schema_version"});
        const std::string format = root.at("format").get<std::string>();
        if (root.at("kind").get<std::string>() != "scene" ||
            (format != kSceneSchemaIdentity && format != kSceneSchemaIdentityV2)) {
            return text_error<SceneDocument>("URE-Q-TEXT-002", "Text document identity mismatch");
        }
        SceneDocument document;
        document.id = root.at("id").get<std::string>();
        document.schema_version = read_version(root.at("schema_version"));
        if ((document.schema_version.major >= 2) !=
            (format == kSceneSchemaIdentityV2)) {
            return text_error<SceneDocument>(
                "URE-Q-TEXT-002",
                "Text document identity differs from schema version");
        }
        document.conventions = read_conventions(root.at("conventions"));
        for (const auto& value : root.at("features")) {
            require_exact_keys(value, {"dependencies", "minimum_version", "name", "parameters", "provider", "requirement"});
            FeatureDeclaration feature;
            feature.dependencies = value.at("dependencies").get<std::vector<std::string>>();
            feature.minimum_version = read_version(value.at("minimum_version"));
            feature.name = value.at("name").get<std::string>();
            feature.canonical_parameters = value.at("parameters").dump();
            feature.provider = value.at("provider").get<std::string>();
            feature.requirement = read_requirement(value.at("requirement").get<std::string>());
            document.features.push_back(std::move(feature));
        }
        for (const auto& value : root.at("extensions")) {
            require_exact_keys(value, {"name", "opaque_payload_hex", "payload_type", "requirement", "version"});
            ExtensionRecord extension;
            extension.name = value.at("name").get<std::string>();
            extension.opaque_payload = hex_decode(value.at("opaque_payload_hex").get<std::string>());
            extension.payload_type = value.at("payload_type").get<std::string>();
            extension.requirement = read_requirement(value.at("requirement").get<std::string>());
            extension.version = read_version(value.at("version"));
            document.extensions.push_back(std::move(extension));
        }
        for (const auto& value : root.at("resources")) document.resources.push_back(read_resource(value));
        for (const auto& value : root.at("migrations")) {
            require_exact_keys(value, {"input_hash", "lossy", "output_hash", "source_version", "target_version",
                                       "tool_id", "tool_version"});
            MigrationRecord migration;
            migration.input_hash = value.at("input_hash").get<std::string>();
            migration.lossy = value.at("lossy").get<bool>();
            migration.output_hash = value.at("output_hash").get<std::string>();
            migration.source_version = read_version(value.at("source_version"));
            migration.target_version = read_version(value.at("target_version"));
            migration.tool_id = value.at("tool_id").get<std::string>();
            migration.tool_version = read_version(value.at("tool_version"));
            document.migrations.push_back(std::move(migration));
        }
        LoadResult<SceneDocument> result;
        result.value = std::move(document);
        append_report(result.diagnostics, validate_scene_document(*result.value, registry, limits));
        return result;
    } catch (const std::domain_error& error) {
        return text_error<SceneDocument>("URE-Q-TEXT-001", error.what());
    } catch (const std::length_error& error) {
        return text_error<SceneDocument>("URE-Q-TEXT-003", error.what());
    } catch (const std::exception& error) {
        return text_error<SceneDocument>("URE-Q-TEXT-002", error.what());
    }
}

std::string write_package_text(const PackageManifest& manifest) {
    auto sorted_scenes = manifest.scenes;
    auto sorted_resources = manifest.resources;
    auto sorted_caches = manifest.caches;
    auto sorted_dependencies = manifest.dependencies;
    std::ranges::sort(sorted_scenes, {}, &SceneReference::id);
    std::ranges::sort(sorted_resources, {}, &ResourceDescriptor::id);
    std::ranges::sort(sorted_caches, {}, &ResourceDescriptor::id);
    std::ranges::sort(sorted_dependencies, {}, &PackageDependency::package_id);
    Json scenes = Json::array();
    for (const auto& scene : sorted_scenes) {
        scenes.push_back(Json{{"content_hash", scene.content_hash}, {"id", scene.id}, {"uri", scene.uri}});
    }
    Json resources = Json::array();
    for (const auto& resource : sorted_resources) resources.push_back(write_resource(resource));
    Json caches = Json::array();
    for (const auto& cache : sorted_caches) caches.push_back(write_resource(cache));
    Json dependencies = Json::array();
    for (const auto& dependency : sorted_dependencies) {
        dependencies.push_back(Json{{"manifest_hash", dependency.manifest_hash}, {"package_id", dependency.package_id}});
    }
    const Json root{
        {"caches", std::move(caches)}, {"dependencies", std::move(dependencies)},
        {"format", std::string(kPackageContainerIdentity)}, {"format_version", write_version(manifest.format_version)},
        {"id", manifest.id}, {"kind", "package"}, {"resources", std::move(resources)},
        {"scenes", std::move(scenes)}
    };
    return root.dump(2) + "\n";
}

LoadResult<PackageManifest> read_package_text(std::string_view text,
                                             const CapabilityRegistry& registry,
                                             const ValidationLimits& limits) {
    try {
        const Json root = parse_text(text, limits);
        require_exact_keys(root, {"caches", "dependencies", "format", "format_version", "id", "kind", "resources", "scenes"});
        if (root.at("kind").get<std::string>() != "package" ||
            root.at("format").get<std::string>() != kPackageContainerIdentity) {
            return text_error<PackageManifest>("URE-Q-TEXT-002", "Text package identity mismatch");
        }
        PackageManifest manifest;
        manifest.id = root.at("id").get<std::string>();
        manifest.format_version = read_version(root.at("format_version"));
        for (const auto& value : root.at("scenes")) {
            require_exact_keys(value, {"content_hash", "id", "uri"});
            manifest.scenes.push_back({value.at("id").get<std::string>(),
                                       value.at("content_hash").get<std::string>(),
                                       value.at("uri").get<std::string>()});
        }
        for (const auto& value : root.at("resources")) manifest.resources.push_back(read_resource(value));
        for (const auto& value : root.at("caches")) manifest.caches.push_back(read_resource(value));
        for (const auto& value : root.at("dependencies")) {
            require_exact_keys(value, {"manifest_hash", "package_id"});
            manifest.dependencies.push_back({value.at("package_id").get<std::string>(),
                                             value.at("manifest_hash").get<std::string>()});
        }
        LoadResult<PackageManifest> result;
        result.value = std::move(manifest);
        append_report(result.diagnostics, validate_package_manifest(*result.value, registry, limits));
        return result;
    } catch (const std::domain_error& error) {
        return text_error<PackageManifest>("URE-Q-TEXT-001", error.what());
    } catch (const std::length_error& error) {
        return text_error<PackageManifest>("URE-Q-TEXT-003", error.what());
    } catch (const std::exception& error) {
        return text_error<PackageManifest>("URE-Q-TEXT-002", error.what());
    }
}

}
