#include <cctype>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <ure/native_scene_validation.hpp>

namespace ure::native_scene {
namespace {

void add_diagnostic(ValidationReport& report,
                    std::string code,
                    DiagnosticSeverity severity,
                    std::string path,
                    std::string message,
                    std::string guidance = {}) {
    report.diagnostics.push_back({std::move(code), severity, std::move(path), std::move(message), std::move(guidance)});
}

bool valid_stable_id(std::string_view id) {
    if (id.empty() || id.size() > 255 || id.front() == '/' || id.back() == '/') return false;
    if (!std::isalnum(static_cast<unsigned char>(id.front()))) return false;
    std::size_t segment_start = 0;
    for (std::size_t i = 0; i <= id.size(); ++i) {
        if (i == id.size() || id[i] == '/') {
            const std::string_view segment = id.substr(segment_start, i - segment_start);
            if (segment.empty() || segment == "." || segment == "..") return false;
            segment_start = i + 1;
            continue;
        }
        const unsigned char value = static_cast<unsigned char>(id[i]);
        if (!(std::isalnum(value) || id[i] == '.' || id[i] == '_' || id[i] == '-')) return false;
    }
    return true;
}

bool valid_sha256(std::string_view hash) {
    if (hash.size() != 64) return false;
    for (char value : hash) {
        if (!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'))) return false;
    }
    return true;
}

bool valid_relative_uri(std::string_view uri) {
    constexpr std::string_view content_prefix = "ure+sha256://";
    if (uri.starts_with(content_prefix)) return valid_sha256(uri.substr(content_prefix.size()));
    if (uri.empty() || uri.front() == '/' || uri.front() == '\\' || uri.find('\\') != std::string_view::npos) return false;
    if (uri.size() >= 2 && std::isalpha(static_cast<unsigned char>(uri[0])) && uri[1] == ':') return false;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= uri.size(); ++i) {
        if (i == uri.size() || uri[i] == '/') {
            const std::string_view segment = uri.substr(start, i - start);
            if (segment.empty() || segment == "." || segment == "..") return false;
            start = i + 1;
        }
    }
    return true;
}

bool checked_add(std::uint64_t left, std::uint64_t right, std::uint64_t& result) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) return false;
    result = left + right;
    return true;
}

void validate_version(ValidationReport& report, Version version, const std::string& path) {
    if (version.major != kSceneSchemaVersion.major) {
        add_diagnostic(report, "URE-Q-VERSION-001", DiagnosticSeverity::Error, path,
                       "Unsupported schema major version", "Run an explicit registered migration tool");
    }
}

void validate_conventions(ValidationReport& report, const SceneConventions& conventions) {
    if (conventions != SceneConventions{}) {
        add_diagnostic(report, "URE-Q-CONVENTION-001", DiagnosticSeverity::Error, "/conventions",
                       "Version 1 requires canonical production conventions", "Convert units and axes during import or build");
    }
}

void validate_resource_list(ValidationReport& report,
                            const std::vector<ResourceDescriptor>& resources,
                            const ValidationLimits& limits,
                            const std::string& root) {
    std::map<std::string, std::size_t> ids;
    std::uint64_t total_bytes = 0;
    std::uint64_t total_resident = 0;
    for (std::size_t i = 0; i < resources.size(); ++i) {
        const auto& resource = resources[i];
        const std::string path = root + "/" + std::to_string(i);
        if (!valid_stable_id(resource.id)) {
            add_diagnostic(report, "URE-Q-ID-001", DiagnosticSeverity::Error, path + "/id", "Invalid stable resource ID");
        } else if (!ids.emplace(resource.id, i).second) {
            add_diagnostic(report, "URE-Q-ID-002", DiagnosticSeverity::Error, path + "/id", "Duplicate stable resource ID");
        }
        if (!valid_sha256(resource.content_hash)) {
            add_diagnostic(report, "URE-Q-HASH-001", DiagnosticSeverity::Error, path + "/content_hash", "Invalid lowercase SHA-256 hash");
        }
        if (!valid_relative_uri(resource.uri)) {
            add_diagnostic(report, "URE-Q-PATH-001", DiagnosticSeverity::Error, path + "/uri", "Resource URI is not package-relative or content-addressed");
        }
        validate_version(report, resource.schema_version, path + "/schema_version");
        if (!checked_add(total_bytes, resource.byte_length, total_bytes) ||
            !checked_add(total_resident, resource.resident_bytes, total_resident)) {
            add_diagnostic(report, "URE-Q-BUDGET-002", DiagnosticSeverity::Error, path, "Resource aggregate size overflow");
            total_bytes = limits.max_total_uncompressed_bytes;
            total_resident = limits.max_resident_resource_bytes;
        }
    }
    if (total_bytes > limits.max_total_uncompressed_bytes || total_resident > limits.max_resident_resource_bytes) {
        add_diagnostic(report, "URE-Q-BUDGET-001", DiagnosticSeverity::Error, root, "Resource budget exceeded");
    }

    std::map<std::string, int> state;
    const auto visit = [&](const auto& self, const std::string& id) -> void {
        const auto index = ids.find(id);
        if (index == ids.end()) return;
        if (state[id] == 1) {
            add_diagnostic(report, "URE-Q-DEP-001", DiagnosticSeverity::Error, root, "Cyclic resource dependency");
            return;
        }
        if (state[id] == 2) return;
        state[id] = 1;
        for (const auto& dependency : resources[index->second].dependencies) {
            if (!ids.contains(dependency)) {
                add_diagnostic(report, "URE-Q-DEP-002", DiagnosticSeverity::Error, root, "Missing resource dependency: " + dependency);
            } else {
                self(self, dependency);
            }
        }
        state[id] = 2;
    };
    for (const auto& [id, index] : ids) {
        static_cast<void>(index);
        visit(visit, id);
    }
}

void validate_capabilities(ValidationReport& report,
                           const SceneDocument& document,
                           const CapabilityRegistry& registry) {
    std::set<std::string> feature_names;
    std::map<std::string, std::size_t> feature_indices;
    for (std::size_t i = 0; i < document.features.size(); ++i) {
        const auto& feature = document.features[i];
        const std::string path = "/features/" + std::to_string(i);
        if (!valid_stable_id(feature.name) || !feature.name.starts_with("ure.")) {
            add_diagnostic(report, "URE-Q-ID-001", DiagnosticSeverity::Error, path + "/name", "Invalid canonical feature name");
        }
        if (!feature_names.insert(feature.name).second) {
            add_diagnostic(report, "URE-Q-ID-002", DiagnosticSeverity::Error, path + "/name", "Duplicate feature declaration");
        }
        feature_indices.emplace(feature.name, i);
        bool parameters_valid = false;
        try {
            const auto parsed = nlohmann::json::parse(feature.canonical_parameters);
            parameters_valid = parsed.is_object() && parsed.dump() == feature.canonical_parameters;
        } catch (const nlohmann::json::exception&) {
        }
        if (!parameters_valid) {
            add_diagnostic(report, "URE-Q-FEATURE-002", DiagnosticSeverity::Error, path + "/parameters", "Feature parameters are not canonical JSON object data");
        }
        const auto supported = registry.features.find(feature.name);
        const bool available = supported != registry.features.end() &&
                               supported->second.major == feature.minimum_version.major &&
                               supported->second.minor >= feature.minimum_version.minor;
        if (!available && feature.requirement == RequirementLevel::Required) {
            add_diagnostic(report, "URE-Q-FEATURE-001", DiagnosticSeverity::Error, path, "Required feature is unsupported");
        } else if (!available && feature.requirement != RequirementLevel::Required) {
            add_diagnostic(report, "URE-Q-FEATURE-101", DiagnosticSeverity::Warning, path, "Optional or advisory feature is unsupported and preserved");
        }
    }

    std::map<std::string, int> feature_state;
    const auto visit_feature = [&](const auto& self, const std::string& name) -> void {
        const auto found = feature_indices.find(name);
        if (found == feature_indices.end()) return;
        if (feature_state[name] == 1) {
            add_diagnostic(report, "URE-Q-DEP-004", DiagnosticSeverity::Error, "/features", "Cyclic feature dependency");
            return;
        }
        if (feature_state[name] == 2) return;
        feature_state[name] = 1;
        for (const auto& dependency : document.features[found->second].dependencies) {
            if (!feature_indices.contains(dependency)) {
                add_diagnostic(report, "URE-Q-DEP-006", DiagnosticSeverity::Error, "/features", "Missing feature dependency: " + dependency);
            } else {
                self(self, dependency);
            }
        }
        feature_state[name] = 2;
    };
    for (const auto& [name, index] : feature_indices) {
        static_cast<void>(index);
        visit_feature(visit_feature, name);
    }

    std::set<std::string> extension_names;
    for (std::size_t i = 0; i < document.extensions.size(); ++i) {
        const auto& extension = document.extensions[i];
        const std::string path = "/extensions/" + std::to_string(i);
        if (!valid_stable_id(extension.name)) {
            add_diagnostic(report, "URE-Q-ID-001", DiagnosticSeverity::Error, path + "/name", "Invalid extension name");
        }
        if (!extension_names.insert(extension.name).second) {
            add_diagnostic(report, "URE-Q-ID-002", DiagnosticSeverity::Error, path + "/name", "Duplicate extension declaration");
        }
        const auto supported = registry.extensions.find(extension.name);
        const bool available = supported != registry.extensions.end() &&
                               supported->second.major == extension.version.major &&
                               supported->second.minor >= extension.version.minor;
        if (!available && extension.name.starts_with("ure.")) {
            add_diagnostic(report, "URE-Q-EXT-002", DiagnosticSeverity::Error, path, "Unknown extension uses the reserved URE namespace");
        } else if (!available && extension.requirement == RequirementLevel::Required) {
            add_diagnostic(report, "URE-Q-EXT-001", DiagnosticSeverity::Error, path, "Required extension is unsupported");
        } else if (!available) {
            add_diagnostic(report, "URE-Q-EXT-101", DiagnosticSeverity::Warning, path, "Unknown optional extension payload is preserved");
        }
    }
}

}

ValidationReport validate_scene_document(const SceneDocument& document,
                                         const CapabilityRegistry& registry,
                                         const ValidationLimits& limits) {
    ValidationReport report;
    if (!valid_stable_id(document.id)) {
        add_diagnostic(report, "URE-Q-ID-001", DiagnosticSeverity::Error, "/id", "Invalid stable scene ID");
    }
    validate_version(report, document.schema_version, "/schema_version");
    validate_conventions(report, document.conventions);
    validate_resource_list(report, document.resources, limits, "/resources");
    validate_capabilities(report, document, registry);
    for (std::size_t i = 0; i < document.migrations.size(); ++i) {
        const auto& migration = document.migrations[i];
        if (!valid_stable_id(migration.tool_id) || !valid_sha256(migration.input_hash) || !valid_sha256(migration.output_hash)) {
            add_diagnostic(report, "URE-Q-MIGRATION-001", DiagnosticSeverity::Error,
                           "/migrations/" + std::to_string(i), "Invalid migration provenance");
        }
    }
    return report;
}

ValidationReport validate_package_manifest(const PackageManifest& manifest,
                                           const CapabilityRegistry& registry,
                                           const ValidationLimits& limits) {
    static_cast<void>(registry);
    ValidationReport report;
    if (!valid_stable_id(manifest.id)) {
        add_diagnostic(report, "URE-Q-ID-001", DiagnosticSeverity::Error, "/id", "Invalid stable package ID");
    }
    validate_version(report, manifest.format_version, "/format_version");
    validate_resource_list(report, manifest.resources, limits, "/resources");
    validate_resource_list(report, manifest.caches, limits, "/caches");
    std::set<std::string> scene_ids;
    for (std::size_t i = 0; i < manifest.scenes.size(); ++i) {
        const auto& scene = manifest.scenes[i];
        const std::string path = "/scenes/" + std::to_string(i);
        if (!valid_stable_id(scene.id) || !scene_ids.insert(scene.id).second) {
            add_diagnostic(report, "URE-Q-ID-002", DiagnosticSeverity::Error, path + "/id", "Invalid or duplicate scene reference ID");
        }
        if (!valid_sha256(scene.content_hash)) {
            add_diagnostic(report, "URE-Q-HASH-001", DiagnosticSeverity::Error, path + "/content_hash", "Invalid scene content hash");
        }
        if (!valid_relative_uri(scene.uri)) {
            add_diagnostic(report, "URE-Q-PATH-001", DiagnosticSeverity::Error, path + "/uri", "Invalid scene reference URI");
        }
    }
    std::set<std::string> dependency_ids;
    for (std::size_t i = 0; i < manifest.dependencies.size(); ++i) {
        const auto& dependency = manifest.dependencies[i];
        if (!valid_stable_id(dependency.package_id) || !dependency_ids.insert(dependency.package_id).second ||
            !valid_sha256(dependency.manifest_hash) || dependency.package_id == manifest.id) {
            add_diagnostic(report, "URE-Q-DEP-003", DiagnosticSeverity::Error,
                           "/dependencies/" + std::to_string(i), "Invalid package dependency");
        }
    }
    return report;
}

ValidationReport validate_exploded_resource_path(const std::filesystem::path& package_root,
                                                 std::string_view resource_uri) {
    ValidationReport report;
    if (!valid_relative_uri(resource_uri) || resource_uri.starts_with("ure+sha256://")) {
        add_diagnostic(report, "URE-Q-PATH-002", DiagnosticSeverity::Error, "/resource_uri",
                       "Exploded resource path is not a safe package-relative path");
        return report;
    }

    std::error_code error;
    const std::filesystem::path canonical_root = std::filesystem::weakly_canonical(package_root, error);
    if (error || canonical_root.empty()) {
        add_diagnostic(report, "URE-Q-PATH-002", DiagnosticSeverity::Error, "/package_root",
                       "Package root cannot be canonicalized");
        return report;
    }
    const std::filesystem::path candidate = std::filesystem::weakly_canonical(
        canonical_root / std::filesystem::path(resource_uri), error);
    if (error) {
        add_diagnostic(report, "URE-Q-PATH-002", DiagnosticSeverity::Error, "/resource_uri",
                       "Resource path cannot be canonicalized");
        return report;
    }
    const std::filesystem::path relative = std::filesystem::relative(candidate, canonical_root, error);
    if (error || relative.empty() || relative.is_absolute() || *relative.begin() == "..") {
        add_diagnostic(report, "URE-Q-PATH-002", DiagnosticSeverity::Error, "/resource_uri",
                       "Resource symlink or reparse point escapes the package root");
    }
    return report;
}

}
