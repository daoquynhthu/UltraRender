#define NOMINMAX

#include "scene_adapter.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <ure/native_scene_hash.hpp>
#include <ure/native_scene_tooling.hpp>

namespace ure::contract {
namespace {

using Digest = std::array<std::uint8_t, 32>;

class BudgetError final : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

class SchemaError final : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

Digest digest_from_hex(std::string_view text) {
    if (text.size() != 64)
        throw std::invalid_argument("invalid SHA-256 text");
    Digest output{};
    for (std::size_t index = 0; index < output.size(); ++index) {
        const auto value = [](char character) -> unsigned {
            if (character >= '0' && character <= '9')
                return static_cast<unsigned>(character - '0');
            const char lower = static_cast<char>(
                std::tolower(static_cast<unsigned char>(character)));
            if (lower >= 'a' && lower <= 'f')
                return static_cast<unsigned>(lower - 'a' + 10);
            throw std::invalid_argument("invalid SHA-256 text");
        };
        output[index] = static_cast<std::uint8_t>(
            value(text[index * 2]) * 16U + value(text[index * 2 + 1]));
    }
    return output;
}

Digest hash(std::span<const std::uint8_t> bytes) {
    return digest_from_hex(native_scene::sha256_hex(bytes));
}

Digest hash(std::string_view text) {
    return hash(std::span(reinterpret_cast<const std::uint8_t *>(text.data()),
                          text.size()));
}

Digest domain_hash(std::string_view domain,
                   std::span<const std::uint8_t> payload) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(domain.size() + 1 + payload.size());
    bytes.insert(bytes.end(), domain.begin(), domain.end());
    bytes.push_back(0);
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return hash(bytes);
}

void store(ure_digest256_t &output, const Digest &value) noexcept {
    std::memcpy(output.bytes, value.data(), value.size());
}

std::string text(ure_string_view_t value) {
    if (value.size == 0)
        return {};
    if (!value.data || value.size > UINT64_C(32768))
        throw std::invalid_argument("invalid UTF-8 string span");
    return {value.data, static_cast<std::size_t>(value.size)};
}

native_scene::ValidationLimits limits(const ure_scene_budget_t &budget) {
    native_scene::ValidationLimits output;
    output.max_total_stored_bytes = budget.max_content_bytes;
    output.max_total_uncompressed_bytes = budget.max_uncompressed_bytes;
    output.max_resident_resource_bytes = budget.max_resident_bytes;
    output.max_directory_entries = budget.max_resource_count;
    output.max_decompression_ratio = budget.max_decompression_ratio;
    output.max_nesting_depth = budget.max_nesting_depth;
    output.max_object_count = budget.max_object_count;
    return output;
}

void check_json_nesting(std::span<const std::uint8_t> bytes,
                        std::uint32_t maximum) {
    std::uint32_t depth = 0;
    bool quoted = false;
    bool escaped = false;
    for (const std::uint8_t byte : bytes) {
        const char character = static_cast<char>(byte);
        if (quoted) {
            if (escaped) {
                escaped = false;
            } else if (character == '\\') {
                escaped = true;
            } else if (character == '"') {
                quoted = false;
            }
            continue;
        }
        if (character == '"') {
            quoted = true;
        } else if (character == '{' || character == '[') {
            if (++depth > maximum)
                throw BudgetError("scene JSON exceeds the nesting-depth budget");
        } else if ((character == '}' || character == ']') && depth != 0) {
            --depth;
        }
    }
}

bool valid_budget(const ure_scene_budget_t &budget) noexcept {
    return budget.header.type == URE_STRUCTURE_SCENE_BUDGET &&
           budget.header.size >= core_1_0_size<ure_scene_budget_t>() && !budget.header.next &&
           budget.max_content_bytes >= 128 &&
           budget.max_content_bytes <= UINT64_C(17179869184) &&
           budget.max_uncompressed_bytes >= budget.max_content_bytes &&
           budget.max_uncompressed_bytes <= UINT64_C(34359738368) &&
           budget.max_resident_bytes >= 16 &&
           budget.max_resident_bytes <= UINT64_C(8589934592) &&
           budget.max_resource_count != 0 &&
           budget.max_resource_count <= UINT64_C(1000000) &&
           budget.max_object_count != 0 &&
           budget.max_object_count <= UINT64_C(10000000) &&
           budget.max_nesting_depth >= 8 && budget.max_nesting_depth <= 64 &&
           budget.max_decompression_ratio != 0 &&
           budget.max_decompression_ratio <= 256 && budget.reserved[0] == 0 &&
           budget.reserved[1] == 0;
}

std::vector<std::uint8_t> read_file(const std::filesystem::path &path,
                                    std::uint64_t maximum) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > maximum ||
        size > std::numeric_limits<std::size_t>::max())
        throw BudgetError("scene file is unavailable or exceeds the content budget");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    std::ifstream input(path, std::ios::binary);
    if (!input || (!bytes.empty() &&
                   !input.read(reinterpret_cast<char *>(bytes.data()),
                               static_cast<std::streamsize>(bytes.size()))))
        throw std::runtime_error("scene file read failed");
    return bytes;
}

const native_scene::ContainerChunk *embedded_scene(
    const native_scene::NativeContainer &container, std::string_view id) {
    const std::string chunk_id = "scene/" + std::string(id);
    const auto found = std::ranges::find(container.chunks, chunk_id,
                                         &native_scene::ContainerChunk::id);
    return found == container.chunks.end() ? nullptr : &*found;
}

native_scene::LoadResult<native_scene::NativeSceneArchive>
load_package_memory(std::span<const std::uint8_t> bytes,
                    std::string_view selected_scene,
                    const native_scene::ValidationLimits &budget) {
    const auto registry = native_scene::native_tool_capabilities();
    const auto container = native_scene::read_container(bytes, registry, budget);
    const auto manifest = native_scene::read_package_binary(bytes, registry, budget);
    native_scene::LoadResult<native_scene::NativeSceneArchive> output;
    output.diagnostics = container.diagnostics;
    output.diagnostics.insert(output.diagnostics.end(), manifest.diagnostics.begin(),
                              manifest.diagnostics.end());
    if (!container.ok() || !container.value || !manifest.ok() || !manifest.value)
        return output;
    if (manifest.value->scenes.empty()) {
        output.diagnostics.push_back({"URE-PB5-PACKAGE-001",
                                      native_scene::DiagnosticSeverity::Error,
                                      "/scenes", "Package contains no scene", {}});
        return output;
    }
    if (selected_scene.empty() && manifest.value->scenes.size() != 1) {
        output.diagnostics.push_back({
            "URE-PB5-PACKAGE-002", native_scene::DiagnosticSeverity::Error,
            "/scenes", "Package scene selection is ambiguous", {}});
        return output;
    }
    auto selected = manifest.value->scenes.begin();
    if (!selected_scene.empty())
        selected = std::ranges::find(manifest.value->scenes, selected_scene,
                                     &native_scene::SceneReference::id);
    if (selected == manifest.value->scenes.end()) {
        output.diagnostics.push_back({"URE-PB5-PACKAGE-003",
                                      native_scene::DiagnosticSeverity::Error,
                                      "/scenes", "Selected package scene was not found", {}});
        return output;
    }
    const auto *chunk = embedded_scene(*container.value, selected->id);
    if (!chunk || selected->uri !=
                      "ure+sha256://" + native_scene::sha256_hex(chunk->payload)) {
        output.diagnostics.push_back({"URE-PB5-PACKAGE-004",
                                      native_scene::DiagnosticSeverity::Error,
                                      "/scenes", "Selected package scene payload is invalid", {}});
        return output;
    }
    auto scene = native_scene::read_scene_ir_binary(chunk->payload, registry, budget);
    output.diagnostics.insert(output.diagnostics.end(), scene.diagnostics.begin(),
                              scene.diagnostics.end());
    if (scene.value &&
        native_scene::scene_ir_semantic_hash(*scene.value) !=
            selected->content_hash) {
        output.diagnostics.push_back({"URE-PB5-PACKAGE-005",
                                      native_scene::DiagnosticSeverity::Error,
                                      "/scenes", "Selected scene semantic digest differs", {}});
        return output;
    }
    output.value = std::move(scene.value);
    return output;
}

std::uint64_t object_count(const native_scene::NativeSceneArchive &archive) {
    const auto &scene = archive.scene;
    return static_cast<std::uint64_t>(archive.document.features.size()) +
           archive.document.extensions.size() + archive.document.resources.size() +
           archive.document.migrations.size() + scene.materials.size() +
           scene.meshes.size() + scene.images.size() + scene.textures.size() +
           scene.instances.size() + scene.spheres.size() + scene.quad_lights.size();
}

Digest resource_digest(const native_scene::NativeSceneArchive &archive) {
    auto resources = archive.document.resources;
    std::ranges::sort(resources, {}, &native_scene::ResourceDescriptor::id);
    std::string canonical;
    for (const auto &resource : resources) {
        canonical.append(resource.id).push_back('\0');
        canonical.append(resource.content_hash).push_back('\0');
        canonical.append(std::to_string(resource.byte_length)).push_back('\0');
        canonical.append(std::to_string(resource.resident_bytes)).push_back('\0');
    }
    return domain_hash("UltraRender.ResourceManifest.v1",
                       std::span(reinterpret_cast<const std::uint8_t *>(canonical.data()),
                                 canonical.size()));
}

std::string diagnostics_json(
    const std::vector<native_scene::ValidationDiagnostic> &diagnostics) {
    nlohmann::json output = nlohmann::json::array();
    for (const auto &diagnostic : diagnostics) {
        output.push_back({
            {"code", diagnostic.code},
            {"severity", static_cast<std::uint32_t>(diagnostic.severity)},
            {"path", diagnostic.path},
            {"message", diagnostic.message},
            {"migration_guidance", diagnostic.migration_guidance}});
    }
    return output.dump();
}

ure_result_t diagnostic_result(
    const std::vector<native_scene::ValidationDiagnostic> &diagnostics) {
    for (const auto &diagnostic : diagnostics) {
        if (diagnostic.severity != native_scene::DiagnosticSeverity::Error)
            continue;
        if (diagnostic.code.find("BUDGET") != std::string::npos)
            return URE_RESULT_BUDGET_EXHAUSTED;
        if (diagnostic.code.find("VERSION") != std::string::npos ||
            diagnostic.message.find("schema") != std::string::npos ||
            diagnostic.message.find("version") != std::string::npos)
            return URE_RESULT_INCOMPATIBLE_VERSION;
        return URE_RESULT_MALFORMED_DATA;
    }
    return URE_RESULT_SUCCESS;
}

LoadedSceneData load(const ure_native_scene_blob_t &blob) {
    if (!valid_budget(blob.budget) || blob.reserved[0] != 0 ||
        blob.reserved[1] != 0 || blob.header.next ||
        blob.schema_min_major > blob.schema_max_major ||
        (blob.schema_min_major == blob.schema_max_major &&
         blob.schema_min_minor > blob.schema_max_minor))
        throw std::invalid_argument("invalid native scene blob descriptor");
    if (blob.schema_min_major > 2 || blob.schema_max_major < 1)
        throw SchemaError("native scene schema range is unsupported");
    const std::string selected = text(blob.package_scene_id);
    const auto validation_limits = limits(blob.budget);
    std::vector<std::uint8_t> source_bytes;
    native_scene::LoadResult<native_scene::NativeSceneArchive> loaded;
    if (blob.source_kind == URE_SCENE_SOURCE_MEMORY) {
        if (blob.bytes.size > blob.budget.max_content_bytes)
            throw BudgetError("scene content exceeds the declared budget");
        if (!blob.bytes.data || blob.bytes.size == 0 ||
            blob.bytes.size > std::numeric_limits<std::size_t>::max() ||
            blob.path_utf8.size != 0)
            throw std::invalid_argument("invalid in-memory scene source");
        source_bytes.assign(blob.bytes.data, blob.bytes.data + blob.bytes.size);
        const auto bytes = std::span<const std::uint8_t>(source_bytes);
        if (blob.format == URE_SCENE_FORMAT_URESCENE) {
            loaded = native_scene::read_scene_ir_binary(
                bytes, native_scene::native_tool_capabilities(), validation_limits);
        } else if (blob.format == URE_SCENE_FORMAT_UREPKG) {
            loaded = load_package_memory(bytes, selected, validation_limits);
        } else if (blob.format == URE_SCENE_FORMAT_URE) {
            check_json_nesting(bytes, blob.budget.max_nesting_depth);
            native_scene::ExplodedSceneArchive exploded;
            exploded.manifest.assign(reinterpret_cast<const char *>(bytes.data()),
                                     bytes.size());
            loaded = native_scene::read_scene_ir_text(
                exploded, native_scene::native_tool_capabilities(), validation_limits);
        } else {
            throw std::invalid_argument("unknown native scene format");
        }
    } else if (blob.source_kind == URE_SCENE_SOURCE_FILE) {
        if (blob.bytes.size != 0 || blob.bytes.data || blob.path_utf8.size == 0)
            throw std::invalid_argument("invalid file scene source");
        const std::string path_text = text(blob.path_utf8);
        const auto path = std::filesystem::path(std::u8string(
            reinterpret_cast<const char8_t *>(path_text.data()),
            reinterpret_cast<const char8_t *>(path_text.data() + path_text.size())));
        const std::string expected = blob.format == URE_SCENE_FORMAT_URE       ? ".ure"
                                     : blob.format == URE_SCENE_FORMAT_URESCENE ? ".urescene"
                                     : blob.format == URE_SCENE_FORMAT_UREPKG   ? ".urepkg"
                                                                                : "";
        if (expected.empty() || path.extension() != expected)
            throw std::invalid_argument("scene path and declared format differ");
        source_bytes = read_file(path, blob.budget.max_content_bytes);
        if (blob.format == URE_SCENE_FORMAT_URE)
            check_json_nesting(source_bytes, blob.budget.max_nesting_depth);
        loaded = blob.format == URE_SCENE_FORMAT_UREPKG
                     ? native_scene::load_native_package_scene(path, selected,
                                                               validation_limits)
                     : native_scene::load_native_asset(path, validation_limits);
    } else {
        throw std::invalid_argument("unknown native scene source kind");
    }

    LoadedSceneData output;
    output.diagnostics = loaded.diagnostics;
    if (!loaded.ok() || !loaded.value)
        return output;
    const auto source_version = loaded.value->document.schema_version;
    const bool below_minimum =
        source_version.major < blob.schema_min_major ||
        (source_version.major == blob.schema_min_major &&
         source_version.minor < blob.schema_min_minor);
    const bool above_maximum =
        source_version.major > blob.schema_max_major ||
        (source_version.major == blob.schema_max_major &&
         source_version.minor > blob.schema_max_minor);
    if (below_minimum || above_maximum)
        throw SchemaError("native scene schema is outside the requested range");
    const std::uint64_t resources = loaded.value->document.resources.size();
    const std::uint64_t objects = object_count(*loaded.value);
    std::uint64_t resident_bytes = 0;
    for (const auto &resource : loaded.value->document.resources) {
        if (resource.resident_bytes >
            blob.budget.max_resident_bytes - resident_bytes) {
            output.diagnostics.push_back({"URE-PB5-BUDGET-003",
                                          native_scene::DiagnosticSeverity::Error,
                                          "/resources",
                                          "Scene resident-memory budget exceeded", {}});
            return output;
        }
        resident_bytes += resource.resident_bytes;
    }
    if (resources > blob.budget.max_resource_count ||
        objects > blob.budget.max_object_count) {
        output.diagnostics.push_back({"URE-PB5-BUDGET-001",
                                      native_scene::DiagnosticSeverity::Error,
                                      "/", "Scene object budget exceeded", {}});
        return output;
    }
    auto revision = std::make_shared<SceneRevisionData>();
    revision->archive = std::move(*loaded.value);
    revision->blob_digest = hash(source_bytes);
    revision->semantic_digest =
        digest_from_hex(native_scene::scene_ir_semantic_hash(revision->archive));
    revision->resource_manifest_digest = resource_digest(revision->archive);
    revision->selected_package_scene = selected;
    if (blob.format == URE_SCENE_FORMAT_UREPKG && selected.empty())
        revision->selected_package_scene = revision->archive.document.id;
    revision->source_schema_major = revision->archive.document.schema_version.major;
    revision->source_schema_minor = revision->archive.document.schema_version.minor;
    revision->resource_count = resources;
    revision->object_count = objects;
    revision->warning_count = static_cast<std::uint32_t>(std::ranges::count_if(
        output.diagnostics, [](const auto &diagnostic) {
            return diagnostic.severity == native_scene::DiagnosticSeverity::Warning;
        }));
    output.revision = std::move(revision);
    return output;
}

void finish_revision(SceneRevisionData &revision, std::uint64_t number,
                     std::uint32_t reset_reason) {
    revision.revision = number;
    revision.reset_reason = reset_reason;
    std::array<std::uint8_t, 104> identity{};
    std::memcpy(identity.data(), revision.semantic_digest.data(), 32);
    std::memcpy(identity.data() + 32, revision.resource_manifest_digest.data(), 32);
    std::memcpy(identity.data() + 64, revision.blob_digest.data(), 32);
    std::memcpy(identity.data() + 96, &number, sizeof(number));
    revision.revision_identity =
        domain_hash("UltraRender.SceneRevision.v1", identity);
}

void write_validation(const LoadedSceneData &loaded,
                      ure_scene_validation_result_t &output) {
    const auto capacity = output.diagnostics_capacity;
    char *const destination = output.diagnostics_data;
    const std::string diagnostics = diagnostics_json(loaded.diagnostics);
    output.valid = loaded.revision ? 1U : 0U;
    output.error_count = static_cast<std::uint32_t>(std::ranges::count_if(
        loaded.diagnostics, [](const auto &item) {
            return item.severity == native_scene::DiagnosticSeverity::Error;
        }));
    output.warning_count = static_cast<std::uint32_t>(std::ranges::count_if(
        loaded.diagnostics, [](const auto &item) {
            return item.severity == native_scene::DiagnosticSeverity::Warning;
        }));
    output.diagnostics_capacity = capacity;
    output.diagnostics_data = destination;
    output.diagnostics_required = static_cast<std::uint32_t>(diagnostics.size());
    const std::size_t writable = capacity == 0 ? 0 : capacity - 1;
    const std::size_t written = std::min(writable, diagnostics.size());
    if (written != 0)
        std::memcpy(destination, diagnostics.data(), written);
    if (capacity != 0)
        destination[written] = '\0';
    output.diagnostics_written = static_cast<std::uint32_t>(written);
    if (!loaded.revision)
        return;
    output.source_schema_major = loaded.revision->source_schema_major;
    output.source_schema_minor = loaded.revision->source_schema_minor;
    store(output.blob_digest, loaded.revision->blob_digest);
    store(output.semantic_digest, loaded.revision->semantic_digest);
    store(output.resource_manifest_digest,
          loaded.revision->resource_manifest_digest);
    output.resource_count = loaded.revision->resource_count;
    output.object_count = loaded.revision->object_count;
}

bool valid_validation_output(ure_scene_validation_result_t *output) noexcept {
    return valid_output(output, URE_STRUCTURE_SCENE_VALIDATION_RESULT) &&
           output->reserved[0] == 0 && output->reserved[1] == 0 &&
           (output->diagnostics_capacity == 0 || output->diagnostics_data);
}

ure_result_t validate_impl(ure_handle_t instance_handle,
                           const ure_native_scene_blob_t *blob,
                           ure_scene_validation_result_t *validation,
                           ure_handle_t *error) {
    clear_error(error);
    const auto instance =
        handles().get<InstanceObject>(instance_handle, ObjectType::Instance);
    if (!instance)
        return make_error(URE_RESULT_INVALID_HANDLE, 400,
                          "invalid instance handle", error);
    if (!instance->scene_enabled ||
        !valid_input(blob, URE_STRUCTURE_NATIVE_SCENE_BLOB) ||
        !valid_validation_output(validation))
        return make_error(URE_RESULT_INVALID_ARGUMENT, 401,
                          "invalid scene validation request", error);
    LoadedSceneData loaded;
    try {
        loaded = load(*blob);
    } catch (const BudgetError &exception) {
        loaded.diagnostics.push_back({"URE-PB5-BUDGET-002",
                                      native_scene::DiagnosticSeverity::Error,
                                      "/", exception.what(), {}});
        write_validation(loaded, *validation);
        return make_error(URE_RESULT_BUDGET_EXHAUSTED, 402, exception.what(), error);
    } catch (const SchemaError &exception) {
        loaded.diagnostics.push_back({"URE-PB5-VERSION-001",
                                      native_scene::DiagnosticSeverity::Error,
                                      "/schema", exception.what(), {}});
        write_validation(loaded, *validation);
        return make_error(URE_RESULT_INCOMPATIBLE_VERSION, 402, exception.what(), error);
    } catch (const std::invalid_argument &exception) {
        return make_error(URE_RESULT_INVALID_ARGUMENT, 402, exception.what(), error);
    } catch (const std::exception &exception) {
        return make_error(URE_RESULT_MALFORMED_DATA, 403, exception.what(), error);
    }
    write_validation(loaded, *validation);
    const ure_result_t result = diagnostic_result(loaded.diagnostics);
    if (result != URE_RESULT_SUCCESS)
        return make_error(result, 404, "native scene validation failed", error);
    if (validation->diagnostics_written < validation->diagnostics_required)
        return make_error(URE_RESULT_BUFFER_TOO_SMALL, 405,
                          "scene diagnostics buffer is too small", error);
    return URE_RESULT_SUCCESS;
}

ure_result_t create_impl(ure_handle_t instance_handle,
                         const ure_native_scene_blob_t *blob,
                         ure_handle_t *output,
                         ure_scene_revision_info_t *revision,
                         ure_handle_t *error) {
    clear_error(error);
    if (output)
        *output = nullptr;
    const auto instance =
        handles().get<InstanceObject>(instance_handle, ObjectType::Instance);
    if (!instance)
        return make_error(URE_RESULT_INVALID_HANDLE, 406,
                          "invalid instance handle", error);
    if (!instance->scene_enabled || !output ||
        !valid_input(blob, URE_STRUCTURE_NATIVE_SCENE_BLOB) ||
        !valid_output(revision, URE_STRUCTURE_SCENE_REVISION_INFO) ||
        revision->reserved[0] != 0 || revision->reserved[1] != 0)
        return make_error(URE_RESULT_INVALID_ARGUMENT, 407,
                          "invalid scene create request", error);
    LoadedSceneData loaded;
    try {
        loaded = load(*blob);
    } catch (const BudgetError &exception) {
        return make_error(URE_RESULT_BUDGET_EXHAUSTED, 408, exception.what(), error);
    } catch (const SchemaError &exception) {
        return make_error(URE_RESULT_INCOMPATIBLE_VERSION, 408, exception.what(), error);
    } catch (const std::invalid_argument &exception) {
        return make_error(URE_RESULT_INVALID_ARGUMENT, 408, exception.what(), error);
    } catch (const std::exception &exception) {
        return make_error(URE_RESULT_MALFORMED_DATA, 409, exception.what(), error);
    }
    const ure_result_t result = diagnostic_result(loaded.diagnostics);
    if (result != URE_RESULT_SUCCESS || !loaded.revision)
        return make_error(result == URE_RESULT_SUCCESS ? URE_RESULT_MALFORMED_DATA
                                                       : result,
                          410, "native scene creation failed", error);
    finish_revision(*loaded.revision, 1, URE_SCENE_RESET_FULL_REPLACEMENT);
    auto scene = std::make_shared<SceneObject>();
    scene->type = ObjectType::Scene;
    scene->owner = instance_handle;
    scene->parent = instance_handle;
    scene->thread_policy = URE_THREAD_POLICY_CONCURRENT_READ;
    scene->instance = instance;
    scene->current = loaded.revision;
    *output = handles().insert(scene);
    write_scene_revision(*loaded.revision, *revision);
    emit_event(instance, URE_EVENT_SCENE_REPLACED, nullptr);
    return URE_RESULT_SUCCESS;
}

ure_result_t replace_impl(ure_handle_t scene_handle,
                          const ure_native_scene_blob_t *blob,
                          ure_scene_revision_info_t *revision,
                          ure_handle_t *error) {
    clear_error(error);
    const auto scene = handles().get<SceneObject>(scene_handle, ObjectType::Scene);
    if (!scene)
        return make_error(URE_RESULT_INVALID_HANDLE, 411,
                          "invalid scene handle", error);
    if (!valid_input(blob, URE_STRUCTURE_NATIVE_SCENE_BLOB) ||
        !valid_output(revision, URE_STRUCTURE_SCENE_REVISION_INFO) ||
        revision->reserved[0] != 0 || revision->reserved[1] != 0)
        return make_error(URE_RESULT_INVALID_ARGUMENT, 412,
                          "invalid scene replacement request", error);
    LoadedSceneData loaded;
    try {
        loaded = load(*blob);
    } catch (const BudgetError &exception) {
        return make_error(URE_RESULT_BUDGET_EXHAUSTED, 413, exception.what(), error);
    } catch (const SchemaError &exception) {
        return make_error(URE_RESULT_INCOMPATIBLE_VERSION, 413, exception.what(), error);
    } catch (const std::invalid_argument &exception) {
        return make_error(URE_RESULT_INVALID_ARGUMENT, 413, exception.what(), error);
    } catch (const std::exception &exception) {
        return make_error(URE_RESULT_MALFORMED_DATA, 414, exception.what(), error);
    }
    const ure_result_t result = diagnostic_result(loaded.diagnostics);
    if (result != URE_RESULT_SUCCESS || !loaded.revision)
        return make_error(result == URE_RESULT_SUCCESS ? URE_RESULT_MALFORMED_DATA
                                                       : result,
                          415, "native scene replacement failed", error);
    {
        std::scoped_lock lock(scene->mutex);
        finish_revision(*loaded.revision, scene->current->revision + 1,
                        URE_SCENE_RESET_FULL_REPLACEMENT);
        scene->current = loaded.revision;
    }
    write_scene_revision(*loaded.revision, *revision);
    emit_event(scene->instance, URE_EVENT_SCENE_REPLACED, nullptr);
    return URE_RESULT_SUCCESS;
}

ure_result_t scene_retain_impl(ure_handle_t scene, ure_handle_t *error) {
    clear_error(error);
    return handles().retain(scene, ObjectType::Scene)
               ? URE_RESULT_SUCCESS
               : make_error(URE_RESULT_INVALID_HANDLE, 416,
                            "invalid scene handle", error);
}

ure_result_t scene_release_impl(ure_handle_t scene, ure_handle_t *error) {
    clear_error(error);
    return handles().release(scene, ObjectType::Scene)
               ? URE_RESULT_SUCCESS
               : make_error(URE_RESULT_INVALID_HANDLE, 417,
                            "invalid scene handle", error);
}

ure_result_t get_revision_impl(ure_handle_t scene_handle,
                               ure_scene_revision_info_t *revision,
                               ure_handle_t *error) {
    clear_error(error);
    if (!valid_output(revision, URE_STRUCTURE_SCENE_REVISION_INFO) ||
        revision->reserved[0] != 0 || revision->reserved[1] != 0)
        return make_error(URE_RESULT_INVALID_ARGUMENT, 418,
                          "invalid scene revision output", error);
    const auto current = scene_revision(scene_handle, error);
    if (!current)
        return URE_RESULT_INVALID_HANDLE;
    write_scene_revision(*current, *revision);
    return URE_RESULT_SUCCESS;
}

ure_result_t URE_CALL validate_scene(ure_handle_t instance,
                                     const ure_native_scene_blob_t *blob,
                                     ure_scene_validation_result_t *validation,
                                     ure_handle_t *error) noexcept {
    return guard_entry(error,
                       [&] { return validate_impl(instance, blob, validation, error); });
}

ure_result_t URE_CALL create_scene(ure_handle_t instance,
                                   const ure_native_scene_blob_t *blob,
                                   ure_handle_t *scene,
                                   ure_scene_revision_info_t *revision,
                                   ure_handle_t *error) noexcept {
    return guard_entry(error, [&] {
        return create_impl(instance, blob, scene, revision, error);
    });
}

ure_result_t URE_CALL replace_scene(ure_handle_t scene,
                                    const ure_native_scene_blob_t *blob,
                                    ure_scene_revision_info_t *revision,
                                    ure_handle_t *error) noexcept {
    return guard_entry(error,
                       [&] { return replace_impl(scene, blob, revision, error); });
}

ure_result_t URE_CALL retain_scene(ure_handle_t scene,
                                   ure_handle_t *error) noexcept {
    return guard_entry(error, [&] { return scene_retain_impl(scene, error); });
}

ure_result_t URE_CALL release_scene(ure_handle_t scene,
                                    ure_handle_t *error) noexcept {
    return guard_entry(error, [&] { return scene_release_impl(scene, error); });
}

ure_result_t URE_CALL get_scene_revision(
    ure_handle_t scene, ure_scene_revision_info_t *revision,
    ure_handle_t *error) noexcept {
    return guard_entry(error,
                       [&] { return get_revision_impl(scene, revision, error); });
}

}

LoadedSceneData load_scene_blob(const ure_native_scene_blob_t &blob) {
    return load(blob);
}

std::shared_ptr<SceneRevisionData> finalize_scene_revision(
    native_scene::NativeSceneArchive archive,
    const std::array<std::uint8_t, 32> &blob_digest,
    std::uint64_t revision_number,
    std::uint32_t reset_reason) {
    auto revision = std::make_shared<SceneRevisionData>();
    revision->archive = std::move(archive);
    revision->blob_digest = blob_digest;
    revision->semantic_digest = digest_from_hex(
        native_scene::scene_ir_semantic_hash(revision->archive));
    revision->resource_manifest_digest = resource_digest(revision->archive);
    revision->source_schema_major = revision->archive.document.schema_version.major;
    revision->source_schema_minor = revision->archive.document.schema_version.minor;
    revision->resource_count = revision->archive.document.resources.size();
    revision->object_count = object_count(revision->archive);
    finish_revision(*revision, revision_number, reset_reason);
    return revision;
}

std::shared_ptr<const SceneRevisionData>
scene_revision(ure_handle_t scene_handle, ure_handle_t *error) noexcept {
    const auto scene = handles().get<SceneObject>(scene_handle, ObjectType::Scene);
    if (!scene) {
        make_error(URE_RESULT_INVALID_HANDLE, 419, "invalid scene handle", error);
        return {};
    }
    std::scoped_lock lock(scene->mutex);
    return scene->current;
}

void write_scene_revision(const SceneRevisionData &source,
                          ure_scene_revision_info_t &output) noexcept {
    output.revision = source.revision;
    store(output.revision_identity, source.revision_identity);
    store(output.blob_digest, source.blob_digest);
    store(output.semantic_digest, source.semantic_digest);
    store(output.resource_manifest_digest, source.resource_manifest_digest);
    output.source_schema_major = source.source_schema_major;
    output.source_schema_minor = source.source_schema_minor;
    output.reset_reason = source.reset_reason;
    output.warning_count = source.warning_count;
    output.loss_count = source.loss_count;
    output.reserved32 = 0;
    output.resource_count = source.resource_count;
    output.object_count = source.object_count;
    output.selected_package_scene = {source.selected_package_scene.data(),
                                     source.selected_package_scene.size()};
}

const ure_scene_interface_t &scene_interface() noexcept {
    static const ure_scene_interface_t table{
        {sizeof(table), 1, 0}, validate_scene, create_scene, replace_scene,
        retain_scene, release_scene, get_scene_revision};
    return table;
}

const ure_scene_transaction_interface_t &scene_transaction_interface() noexcept {
    static const ure_scene_transaction_interface_t table{
        {sizeof(table), 1, 0}, apply_scene_transaction};
    return table;
}

}
