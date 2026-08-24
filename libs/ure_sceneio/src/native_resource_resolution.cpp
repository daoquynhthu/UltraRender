#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <ranges>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

#include <ure/native_resource_resolution.hpp>
#include <ure/native_scene_hash.hpp>
#include <ure/native_scene_text.hpp>
#include <ure/native_scene_validation.hpp>

namespace ure::native_scene {
namespace {

struct ResourceRequest {
    ResourceDescriptor descriptor;
    std::string source_uri;
    ResourceResidency residency{ResourceResidency::Resident};
};

class ResolutionFailure final : public std::runtime_error {
public:
    ResolutionFailure(std::string code, std::string path,
                      std::string message)
        : std::runtime_error(message), code_(std::move(code)),
          path_(std::move(path)) {}

    const std::string& code() const noexcept { return code_; }
    const std::string& path() const noexcept { return path_; }

private:
    std::string code_;
    std::string path_;
};

void diagnostic(NativeResourceResolution& result, std::string code,
                std::string path, std::string message) {
    result.diagnostics.push_back({std::move(code), DiagnosticSeverity::Error,
                                  std::move(path), std::move(message), {}});
}

std::string content_hash_from_uri(std::string_view uri) {
    constexpr std::string_view prefix = "ure+sha256://";
    if (!uri.starts_with(prefix) || uri.size() != prefix.size() + 64)
        return {};
    return std::string(uri.substr(prefix.size()));
}

std::vector<std::uint8_t> read_payload(const std::filesystem::path& path,
                                       std::uint64_t maximum,
                                       std::string diagnostic_path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error)
        throw ResolutionFailure("URE-PRV2-RESOURCE-MISSING-001",
                                std::move(diagnostic_path),
                                "Resource payload is unavailable");
    if (size > maximum || size > std::numeric_limits<std::size_t>::max())
        throw ResolutionFailure("URE-PRV2-RESOURCE-BUDGET-001",
                                std::move(diagnostic_path),
                                "Resource exceeds the stored-byte budget");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    std::ifstream input(path, std::ios::binary);
    if (!input ||
        (!bytes.empty() &&
         !input.read(reinterpret_cast<char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()))))
        throw ResolutionFailure("URE-PRV2-RESOURCE-READ-001",
                                std::move(diagnostic_path),
                                "Resource payload read failed");
    return bytes;
}

std::vector<ResourceRequest> resource_requests(
    const NativeSceneArchive& archive) {
    std::vector<ResourceRequest> result;
    std::set<std::pair<std::string, std::string>> seen;
    const auto add = [&](ResourceDescriptor descriptor, std::string uri,
                         ResourceResidency residency) {
        const auto key = std::pair(descriptor.id, uri);
        if (uri.empty() || !seen.insert(key).second)
            return;
        result.push_back(
            {std::move(descriptor), std::move(uri), residency});
    };
    for (const auto& descriptor : archive.document.resources)
        add(descriptor, descriptor.uri, ResourceResidency::Resident);
    if (archive.resource_catalog) {
        for (const auto& entry : archive.resource_catalog->resources) {
            ResourceDescriptor descriptor;
            descriptor.id = entry.id;
            descriptor.content_hash = entry.content_hash;
            descriptor.kind = entry.kind;
            descriptor.schema_version = entry.schema_version;
            descriptor.uri = entry.payload_uri;
            descriptor.dependencies = entry.dependencies;
            descriptor.byte_length = entry.payload_bytes;
            descriptor.resident_bytes = entry.resident_bytes;
            add(std::move(descriptor), entry.payload_uri, entry.residency);
        }
    }
    const auto add_path = [&](std::string id, std::string uri,
                              ResourceKind kind) {
        if (uri.empty())
            return;
        ResourceDescriptor descriptor;
        descriptor.id = std::move(id);
        descriptor.kind = kind;
        descriptor.schema_version = {1, 0};
        descriptor.uri = uri;
        add(std::move(descriptor), std::move(uri),
            ResourceResidency::Resident);
    };
    const auto add_image = [&](const std::shared_ptr<scene_ir::ImageResource>& image,
                               std::string id) {
        if (!image)
            return;
        add_path(id, image->uri, ResourceKind::Texture);
    };
    const auto add_texture = [&](
        const std::shared_ptr<scene_ir::TextureResource>& texture,
        std::string id) {
        if (texture)
            add_image(texture->image, std::move(id));
    };
    const auto add_material = [&](
        const std::shared_ptr<scene_ir::MaterialNode>& material,
        std::string id) {
        if (!material)
            return;
        add_texture(material->base_color_texture, id + "/base-color");
        add_texture(material->roughness_texture, id + "/roughness");
        add_texture(material->emission_texture, id + "/emission");
        add_texture(material->normal_texture, id + "/normal");
        if (material->graph) {
            for (std::size_t index = 0;
                 index < material->graph->nodes.size(); ++index) {
                add_texture(material->graph->nodes[index].texture,
                            id + "/graph/" + std::to_string(index));
            }
        }
        if (material->spectral_extension) {
            add_path(id + "/albedo-spd",
                     material->spectral_extension->albedo_spd,
                     ResourceKind::SpectralTable);
            add_path(id + "/emission-spd",
                     material->spectral_extension->emission_spd,
                     ResourceKind::SpectralTable);
        }
    };
    for (std::size_t index = 0; index < archive.scene.images.size(); ++index) {
        const std::string id = index < archive.source_ids.images.size()
                                   ? archive.source_ids.images[index]
                                   : "image/" + std::to_string(index);
        add_image(archive.scene.images[index], id);
    }
    for (std::size_t index = 0; index < archive.scene.textures.size(); ++index) {
        add_texture(archive.scene.textures[index],
                    "texture/" + std::to_string(index));
    }
    for (std::size_t index = 0; index < archive.scene.materials.size(); ++index) {
        const std::string id = index < archive.source_ids.materials.size()
                                   ? archive.source_ids.materials[index]
                                   : "material/" + std::to_string(index);
        add_material(archive.scene.materials[index], id);
    }
    for (std::size_t index = 0; index < archive.scene.instances.size(); ++index)
        add_material(archive.scene.instances[index].material,
                     "instance/" + std::to_string(index) + "/material");
    for (std::size_t index = 0; index < archive.scene.spheres.size(); ++index)
        add_material(archive.scene.spheres[index].material,
                     "sphere/" + std::to_string(index) + "/material");
    for (std::size_t index = 0; index < archive.scene.quad_lights.size(); ++index)
        add_material(archive.scene.quad_lights[index].material,
                     "light/" + std::to_string(index) + "/material");
    return result;
}

std::vector<std::uint8_t> resolve_payload(
    const NativeSceneArchive& archive, const ResourceRequest& request,
    std::span<const NamedResourcePayload> typed_resources,
    const ValidationLimits& limits) {
    const auto matches = [&](const NamedResourcePayload& payload) {
        return (!request.descriptor.id.empty() &&
                payload.id == request.descriptor.id) ||
               (!request.descriptor.content_hash.empty() &&
                payload.descriptor.content_hash ==
                    request.descriptor.content_hash) ||
               (!request.source_uri.empty() &&
                payload.source_uri == request.source_uri);
    };
    if (const auto found = std::ranges::find_if(archive.packaged_resources,
                                                matches);
        found != archive.packaged_resources.end())
        return found->payload;
    if (const auto found = std::ranges::find_if(typed_resources, matches);
        found != typed_resources.end())
        return found->payload;
    const std::string path = "/resources/" + request.descriptor.id;
    if (!content_hash_from_uri(request.source_uri).empty())
        throw ResolutionFailure("URE-PRV2-RESOURCE-MISSING-001", path,
                                "Content-addressed resource payload is unavailable");
    const auto validation = validate_exploded_resource_path(
        archive.execution_root, request.source_uri);
    if (!validation.ok())
        throw ResolutionFailure(validation.diagnostics.front().code, path,
                                validation.diagnostics.front().message);
    return read_payload(archive.execution_root / request.source_uri,
                        limits.max_total_stored_bytes, path);
}

}

bool NativeResourceResolution::ok() const {
    return std::ranges::none_of(diagnostics, [](const auto& item) {
        return item.severity == DiagnosticSeverity::Error;
    });
}

NativeResourceResolution resolve_native_scene_resources(
    const NativeSceneArchive& archive, const ValidationLimits& limits) {
    NativeResourceResolution result;
    try {
        if (archive.resource_catalog) {
            const auto validation =
                validate_resource_catalog(*archive.resource_catalog, limits);
            if (!validation.ok()) {
                result.diagnostics = validation.diagnostics;
                return result;
            }
        }
        const auto typed = write_scene_ir_text(archive).resources;
        std::map<std::string, ResourceKind> content_kinds;
        std::map<std::string, std::uint64_t> content_residency;
        std::map<std::string, std::string> logical_content;
        std::map<std::string, std::string> source_content;
        std::set<std::string> stored_content;
        std::set<std::string> streamed_content;
        for (auto request : resource_requests(archive)) {
            auto payload = resolve_payload(archive, request, typed, limits);
            const std::string hash = sha256_hex(payload);
            const std::string path = "/resources/" + request.descriptor.id;
            if (!request.descriptor.content_hash.empty() &&
                request.descriptor.content_hash != hash) {
                diagnostic(result, "URE-PRV2-RESOURCE-HASH-001", path,
                           "Resource content hash mismatch");
                continue;
            }
            if (request.descriptor.byte_length != 0 &&
                request.descriptor.byte_length != payload.size()) {
                diagnostic(result, "URE-PRV2-RESOURCE-SIZE-001", path,
                           "Resource byte length mismatch");
                continue;
            }
            if (const auto found = content_kinds.find(hash);
                found != content_kinds.end() &&
                found->second != request.descriptor.kind) {
                diagnostic(result, "URE-PRV2-RESOURCE-DOMAIN-001", path,
                           "One content identity is declared in incompatible resource domains");
                continue;
            }
            content_kinds.emplace(hash, request.descriptor.kind);
            if (const auto found = logical_content.find(request.descriptor.id);
                found != logical_content.end() && found->second != hash) {
                diagnostic(result, "URE-PRV2-RESOURCE-AMBIGUOUS-001", path,
                           "One logical resource identity resolves to multiple contents");
                continue;
            }
            if (const auto found = source_content.find(request.source_uri);
                found != source_content.end() && found->second != hash) {
                diagnostic(result, "URE-PRV2-RESOURCE-AMBIGUOUS-002", path,
                           "One resource URI resolves to multiple contents");
                continue;
            }
            logical_content.emplace(request.descriptor.id, hash);
            source_content.emplace(request.source_uri, hash);
            request.descriptor.content_hash = hash;
            request.descriptor.uri = "ure+sha256://" + hash;
            request.descriptor.byte_length = payload.size();
            if (request.descriptor.resident_bytes == 0)
                request.descriptor.resident_bytes = payload.size();
            if (!stored_content.contains(hash)) {
                if (payload.size() >
                    limits.max_total_stored_bytes - result.budget.stored_bytes) {
                    diagnostic(result, "URE-PRV2-RESOURCE-BUDGET-001", path,
                               "Resource stored-byte budget exceeded");
                    continue;
                }
                stored_content.insert(hash);
                result.budget.stored_bytes += payload.size();
                result.budget.decompressed_bytes = result.budget.stored_bytes;
            }
            if (request.residency == ResourceResidency::Resident) {
                content_residency[hash] = std::max(
                    content_residency[hash],
                    request.descriptor.resident_bytes);
                result.budget.resident_bytes = 0;
                for (const auto& [content, bytes] : content_residency) {
                    static_cast<void>(content);
                    if (bytes > std::numeric_limits<std::uint64_t>::max() -
                                    result.budget.resident_bytes) {
                        diagnostic(result, "URE-PRV2-RESOURCE-BUDGET-003",
                                   path, "Resource resident-byte budget overflowed");
                        break;
                    }
                    result.budget.resident_bytes += bytes;
                }
            } else if (streamed_content.insert(hash).second) {
                if (payload.size() >
                    std::numeric_limits<std::uint64_t>::max() -
                        result.budget.streamed_bytes) {
                    diagnostic(result, "URE-PRV2-RESOURCE-BUDGET-005", path,
                               "Resource streamed-byte budget overflowed");
                    continue;
                }
                result.budget.streamed_bytes += payload.size();
            }
            result.resources.push_back(
                {request.descriptor.id, request.source_uri,
                 std::move(request.descriptor), request.residency,
                 std::move(payload)});
        }
        if (result.budget.decompressed_bytes >
            limits.max_total_uncompressed_bytes) {
            diagnostic(result, "URE-PRV2-RESOURCE-BUDGET-002", "/resources",
                       "Resource decompressed-byte budget exceeded");
        }
        if (result.budget.resident_bytes >
            limits.max_resident_resource_bytes) {
            diagnostic(result, "URE-PRV2-RESOURCE-BUDGET-003", "/resources",
                       "Resource resident-byte budget exceeded");
        }
    } catch (const ResolutionFailure& error) {
        diagnostic(result, error.code(), error.path(), error.what());
    } catch (const std::exception& error) {
        diagnostic(result, "URE-PRV2-RESOURCE-RESOLVE-001", "/resources",
                   error.what());
    }
    return result;
}

}
