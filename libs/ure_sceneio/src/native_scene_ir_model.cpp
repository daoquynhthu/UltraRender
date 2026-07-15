#include <format>
#include <cmath>
#include <limits>
#include <memory>
#include <numbers>
#include <set>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <ure/native_scene_ir.hpp>
#include <ure/native_scene_hash.hpp>

#include "native_scene_ir_internal.hpp"

namespace ure::native_scene {
namespace {

std::vector<std::string> indexed_ids(std::string_view prefix, std::size_t count) {
    std::vector<std::string> ids;
    ids.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        ids.push_back(std::format("{}/{:08}", prefix, index));
    }
    return ids;
}

template <typename T>
std::shared_ptr<T> mapped(const std::unordered_map<const T*, std::shared_ptr<T>>& values,
                          const std::shared_ptr<T>& value) {
    if (!value) return {};
    const auto found = values.find(value.get());
    return found == values.end() ? std::shared_ptr<T>{} : found->second;
}

std::shared_ptr<const scene_ir::MiePhaseResource> clone_mie(
    const std::shared_ptr<const scene_ir::MiePhaseResource>& source,
    std::unordered_map<const scene_ir::MiePhaseResource*, std::shared_ptr<const scene_ir::MiePhaseResource>>& clones) {
    if (!source) return {};
    const auto found = clones.find(source.get());
    if (found != clones.end()) return found->second;
    auto clone = std::make_shared<const scene_ir::MiePhaseResource>(*source);
    clones.emplace(source.get(), clone);
    return clone;
}

void add_error(ValidationReport& report, std::string code, std::string path, std::string message) {
    report.diagnostics.push_back({std::move(code), DiagnosticSeverity::Error, std::move(path),
                                  std::move(message), {}});
}

void validate_id_vector(ValidationReport& report,
                        const std::vector<std::string>& ids,
                        std::size_t expected_size,
                        std::string_view path) {
    if (ids.size() != expected_size) {
        add_error(report, "URE-Q3-ID-001", std::string(path), "Source identity count does not match SceneIR registry");
        return;
    }
    std::set<std::string> unique;
    auto valid_id = [](std::string_view id) {
        if (id.empty() || id.find('/') == std::string_view::npos || id.front() < 'a' || id.front() > 'z') return false;
        bool segment_has_character = false;
        for (char character : id) {
            if (character == '/') {
                if (!segment_has_character) return false;
                segment_has_character = false;
            } else if ((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') ||
                       character == '.' || character == '_' || character == '-') {
                segment_has_character = true;
            } else {
                return false;
            }
        }
        return segment_has_character;
    };
    for (std::size_t index = 0; index < ids.size(); ++index) {
        if (ids[index].empty() || !unique.insert(ids[index]).second) {
            add_error(report, "URE-Q3-ID-002", std::format("{}[{}]", path, index),
                      "Source identity is empty or duplicated");
        }
        if (!valid_id(ids[index])) {
            add_error(report, "URE-Q3-ID-003", std::format("{}[{}]", path, index), "Source identity has invalid grammar");
        }
    }
}

bool finite(float value) {
    return std::isfinite(value);
}

bool finite(const core::Vec2f& value) {
    return finite(value.x) && finite(value.y);
}

bool finite(const core::Vec3f& value) {
    return finite(value.x) && finite(value.y) && finite(value.z);
}

bool finite(const core::Quat& value) {
    return finite(value.w) && finite(value.x) && finite(value.y) && finite(value.z);
}

template <typename T>
std::set<const T*> pointer_set(const std::vector<std::shared_ptr<T>>& values) {
    std::set<const T*> result;
    for (const auto& value : values) {
        if (value) result.insert(value.get());
    }
    return result;
}

template <typename T>
void require_registered(ValidationReport& report,
                        const std::set<const T*>& registered,
                        const std::shared_ptr<T>& value,
                        std::string path) {
    if (value && !registered.contains(value.get())) {
        add_error(report, "URE-Q3-REF-001", std::move(path), "Reference does not target a registered SceneIR object");
    }
}

bool finite_mie(const scene_ir::MiePhaseResource& value) {
    auto all_finite = [](const std::vector<float>& values) {
        return std::ranges::all_of(values, [](float item) { return std::isfinite(item); });
    };
    const std::size_t wavelengths = value.wavelengths_nm.size();
    const std::size_t angles = value.cos_theta.size();
    if (wavelengths < 2 || angles < 2 || wavelengths > std::numeric_limits<std::size_t>::max() / angles ||
        value.phase.size() != wavelengths * angles || value.cdf.size() != wavelengths * angles ||
        value.scattering_cross_section_m2.size() != wavelengths ||
        value.extinction_cross_section_m2.size() != wavelengths ||
        value.absorption_cross_section_m2.size() != wavelengths || value.asymmetry.size() != wavelengths ||
        value.polarization_model != scene_ir::MiePolarizationModel::ScalarDepolarizing ||
        !all_finite(value.wavelengths_nm) || !all_finite(value.cos_theta) || !all_finite(value.phase) ||
        !all_finite(value.cdf) || !all_finite(value.scattering_cross_section_m2) ||
        !all_finite(value.extinction_cross_section_m2) || !all_finite(value.absorption_cross_section_m2) ||
        !all_finite(value.asymmetry) || value.wavelengths_nm.front() <= 0.0f ||
        std::abs(value.cos_theta.front() + 1.0f) > 1.0e-6f ||
        std::abs(value.cos_theta.back() - 1.0f) > 1.0e-6f) return false;
    for (std::size_t index = 1; index < wavelengths; ++index) {
        if (value.wavelengths_nm[index] <= value.wavelengths_nm[index - 1]) return false;
    }
    for (std::size_t index = 1; index < angles; ++index) {
        if (value.cos_theta[index] <= value.cos_theta[index - 1]) return false;
    }
    for (std::size_t wavelength = 0; wavelength < wavelengths; ++wavelength) {
        if (value.scattering_cross_section_m2[wavelength] < 0.0f ||
            value.extinction_cross_section_m2[wavelength] < value.scattering_cross_section_m2[wavelength] ||
            value.absorption_cross_section_m2[wavelength] < 0.0f ||
            value.asymmetry[wavelength] < -1.00001f || value.asymmetry[wavelength] > 1.00001f) return false;
        const std::size_t offset = wavelength * angles;
        if (value.cdf[offset] != 0.0f || value.cdf[offset + angles - 1] != 1.0f) return false;
        double integral = 0.0;
        for (std::size_t angle = 0; angle < angles; ++angle) {
            if (value.phase[offset + angle] < 0.0f || value.cdf[offset + angle] < 0.0f ||
                value.cdf[offset + angle] > 1.0f ||
                (angle > 0 && value.cdf[offset + angle] < value.cdf[offset + angle - 1])) return false;
            if (angle > 0) {
                integral += 2.0 * std::numbers::pi_v<double> * 0.5 *
                    (value.phase[offset + angle - 1] + value.phase[offset + angle]) *
                    (value.cos_theta[angle] - value.cos_theta[angle - 1]);
            }
        }
        if (std::abs(integral - 1.0) > 1.0e-3) return false;
    }
    return true;
}

void normalize(float& value) {
    if (value == 0.0f) value = 0.0f;
}

void normalize(core::Vec2f& value) {
    normalize(value.x);
    normalize(value.y);
}

void normalize(core::Vec3f& value) {
    normalize(value.x);
    normalize(value.y);
    normalize(value.z);
}

void append_bytes(std::vector<std::uint8_t>& destination, std::string_view value) {
    destination.insert(destination.end(), value.begin(), value.end());
    destination.push_back(0);
}

}

NativeSceneArchive make_native_scene_archive(SceneDocument document,
                                             const scene_ir::SceneIR& scene) {
    NativeSceneArchive archive;
    archive.document = std::move(document);
    std::unordered_map<const scene_ir::ImageResource*, std::shared_ptr<scene_ir::ImageResource>> images;
    for (const auto& source : scene.images) {
        if (!source) {
            archive.scene.images.push_back({});
            continue;
        }
        auto clone = std::make_shared<scene_ir::ImageResource>(*source);
        images.emplace(source.get(), clone);
        archive.scene.images.push_back(std::move(clone));
    }

    std::unordered_map<const scene_ir::TextureResource*, std::shared_ptr<scene_ir::TextureResource>> textures;
    for (const auto& source : scene.textures) {
        if (!source) {
            archive.scene.textures.push_back({});
            continue;
        }
        auto clone = std::make_shared<scene_ir::TextureResource>(*source);
        clone->image = mapped(images, source->image);
        textures.emplace(source.get(), clone);
        archive.scene.textures.push_back(std::move(clone));
    }

    std::unordered_map<const scene_ir::MiePhaseResource*, std::shared_ptr<const scene_ir::MiePhaseResource>> mie_resources;
    std::unordered_map<const scene_ir::MaterialNode*, std::shared_ptr<scene_ir::MaterialNode>> materials;
    for (const auto& source : scene.materials) {
        if (!source) {
            archive.scene.materials.push_back({});
            continue;
        }
        auto clone = std::make_shared<scene_ir::MaterialNode>(*source);
        clone->base_color_texture = mapped(textures, source->base_color_texture);
        clone->roughness_texture = mapped(textures, source->roughness_texture);
        clone->emission_texture = mapped(textures, source->emission_texture);
        clone->normal_texture = mapped(textures, source->normal_texture);
        clone->medium_mie_resource = clone_mie(source->medium_mie_resource, mie_resources);
        if (source->spectral_extension) {
            clone->spectral_extension = std::make_shared<scene_ir::SpectralMaterialExtension>(*source->spectral_extension);
        }
        if (source->graph) {
            clone->graph = std::make_shared<scene_ir::MaterialGraph>(*source->graph);
            for (auto& node : clone->graph->nodes) {
                node.texture = mapped(textures, node.texture);
            }
        }
        materials.emplace(source.get(), clone);
        archive.scene.materials.push_back(std::move(clone));
    }

    std::unordered_map<const scene_ir::MeshResource*, std::shared_ptr<scene_ir::MeshResource>> meshes;
    for (const auto& source : scene.meshes) {
        if (!source) {
            archive.scene.meshes.push_back({});
            continue;
        }
        auto clone = std::make_shared<scene_ir::MeshResource>(*source);
        if (source->mesh) clone->mesh = std::make_shared<Mesh>(*source->mesh);
        meshes.emplace(source.get(), clone);
        archive.scene.meshes.push_back(std::move(clone));
    }

    archive.scene.instances = scene.instances;
    for (std::size_t index = 0; index < archive.scene.instances.size(); ++index) {
        archive.scene.instances[index].mesh = mapped(meshes, scene.instances[index].mesh);
        archive.scene.instances[index].material = mapped(materials, scene.instances[index].material);
    }
    archive.scene.spheres = scene.spheres;
    for (std::size_t index = 0; index < archive.scene.spheres.size(); ++index) {
        archive.scene.spheres[index].material = mapped(materials, scene.spheres[index].material);
    }
    archive.scene.quad_lights = scene.quad_lights;
    for (std::size_t index = 0; index < archive.scene.quad_lights.size(); ++index) {
        archive.scene.quad_lights[index].material = mapped(materials, scene.quad_lights[index].material);
    }
    archive.scene.camera = scene.camera;
    archive.scene.physics = scene.physics;
    archive.scene.background_color = scene.background_color;
    archive.scene.medium_density = scene.medium_density;
    archive.scene.medium_anisotropy = scene.medium_anisotropy;
    archive.scene.medium_phase = scene.medium_phase;
    archive.scene.medium_mie_resource = clone_mie(scene.medium_mie_resource, mie_resources);
    archive.scene.medium_scattering = scene.medium_scattering;
    archive.scene.medium_absorption = scene.medium_absorption;
    archive.scene.medium_max_distance = scene.medium_max_distance;
    archive.scene.width = scene.width;
    archive.scene.height = scene.height;
    archive.scene.spp = scene.spp;
    archive.source_ids.materials = indexed_ids("material", scene.materials.size());
    archive.source_ids.meshes = indexed_ids("mesh", scene.meshes.size());
    archive.source_ids.images = indexed_ids("image", scene.images.size());
    archive.source_ids.textures = indexed_ids("texture", scene.textures.size());
    archive.source_ids.instances = indexed_ids("instance", scene.instances.size());
    archive.source_ids.spheres = indexed_ids("sphere", scene.spheres.size());
    archive.source_ids.quad_lights = indexed_ids("light/quad", scene.quad_lights.size());
    return archive;
}

ValidationReport validate_scene_ir_archive(const NativeSceneArchive& archive,
                                           const ValidationLimits&) {
    ValidationReport report;
    validate_id_vector(report, archive.source_ids.materials, archive.scene.materials.size(), "source_ids.materials");
    validate_id_vector(report, archive.source_ids.meshes, archive.scene.meshes.size(), "source_ids.meshes");
    validate_id_vector(report, archive.source_ids.images, archive.scene.images.size(), "source_ids.images");
    validate_id_vector(report, archive.source_ids.textures, archive.scene.textures.size(), "source_ids.textures");
    validate_id_vector(report, archive.source_ids.instances, archive.scene.instances.size(), "source_ids.instances");
    validate_id_vector(report, archive.source_ids.spheres, archive.scene.spheres.size(), "source_ids.spheres");
    validate_id_vector(report, archive.source_ids.quad_lights, archive.scene.quad_lights.size(), "source_ids.quad_lights");

    const auto materials = pointer_set(archive.scene.materials);
    const auto meshes = pointer_set(archive.scene.meshes);
    const auto images = pointer_set(archive.scene.images);
    const auto textures = pointer_set(archive.scene.textures);
    for (std::size_t index = 0; index < archive.scene.images.size(); ++index) {
        if (!archive.scene.images[index]) add_error(report, "URE-Q3-REF-002", std::format("images[{}]", index), "Null registered image");
    }
    for (std::size_t index = 0; index < archive.scene.textures.size(); ++index) {
        const auto& texture = archive.scene.textures[index];
        if (!texture) {
            add_error(report, "URE-Q3-REF-002", std::format("textures[{}]", index), "Null registered texture");
            continue;
        }
        require_registered(report, images, texture->image, std::format("textures[{}].image", index));
    }
    for (std::size_t index = 0; index < archive.scene.materials.size(); ++index) {
        const auto& material = archive.scene.materials[index];
        if (!material) {
            add_error(report, "URE-Q3-REF-002", std::format("materials[{}]", index), "Null registered material");
            continue;
        }
        require_registered(report, textures, material->base_color_texture, std::format("materials[{}].base_color_texture", index));
        require_registered(report, textures, material->roughness_texture, std::format("materials[{}].roughness_texture", index));
        require_registered(report, textures, material->emission_texture, std::format("materials[{}].emission_texture", index));
        require_registered(report, textures, material->normal_texture, std::format("materials[{}].normal_texture", index));
        if (!finite(material->base_color) || !finite(material->roughness) || !finite(material->ior) ||
            !finite(material->dispersion) || !finite(material->metal_eta) || !finite(material->metal_k) ||
            !finite(material->thin_film_thickness) || !finite(material->thin_film_ior) || !finite(material->emission) ||
            !finite(material->medium_density) || !finite(material->medium_anisotropy) ||
            !finite(material->medium_scattering) || !finite(material->medium_absorption) || !finite(material->normal_scale) ||
            material->roughness < 0.0f || material->ior <= 0.0f || material->thin_film_thickness < 0.0f ||
            material->thin_film_ior <= 0.0f || material->medium_density < 0.0f) {
            add_error(report, "URE-Q3-SCALAR-001", std::format("materials[{}]", index), "Invalid material scalar");
        }
        if (material->medium_mie_resource && !finite_mie(*material->medium_mie_resource)) {
            add_error(report, "URE-Q3-MIE-003", std::format("materials[{}].medium_mie", index), "Non-finite Mie resource");
        }
        if (material->graph) {
            for (std::size_t node_index = 0; node_index < material->graph->nodes.size(); ++node_index) {
                const auto& node = material->graph->nodes[node_index];
                require_registered(report, textures, node.texture,
                                   std::format("materials[{}].graph.nodes[{}].texture", index, node_index));
                if (!finite(node.color) || !finite(node.value)) {
                    add_error(report, "URE-Q3-SCALAR-001", std::format("materials[{}].graph.nodes[{}]", index, node_index),
                              "Invalid material graph scalar");
                }
            }
            try {
                material->graph->validate();
            } catch (const std::exception& error) {
                add_error(report, "URE-Q3-GRAPH-002", std::format("materials[{}].graph", index), error.what());
            }
        }
    }
    for (std::size_t index = 0; index < archive.scene.meshes.size(); ++index) {
        const auto& record = archive.scene.meshes[index];
        if (!record || !record->mesh) {
            add_error(report, "URE-Q3-REF-002", std::format("meshes[{}]", index), "Null registered mesh");
            continue;
        }
        if (record->mesh->indices.size() % 3 != 0) {
            add_error(report, "URE-Q3-MESH-002", std::format("meshes[{}].indices", index), "Mesh index count is not divisible by three");
        }
        for (const Vertex& vertex : record->mesh->vertices) {
            if (!finite(vertex.position) || !finite(vertex.normal) || !finite(vertex.uv) || !finite(vertex.tangent)) {
                add_error(report, "URE-Q3-MESH-002", std::format("meshes[{}].vertices", index), "Mesh contains non-finite scalar");
                break;
            }
        }
        for (int mesh_index : record->mesh->indices) {
            if (mesh_index < 0 || static_cast<std::size_t>(mesh_index) >= record->mesh->vertices.size()) {
                add_error(report, "URE-Q3-MESH-003", std::format("meshes[{}].indices", index), "Mesh index is out of range");
                break;
            }
        }
    }
    for (std::size_t index = 0; index < archive.scene.instances.size(); ++index) {
        const auto& instance = archive.scene.instances[index];
        require_registered(report, meshes, instance.mesh, std::format("instances[{}].mesh", index));
        require_registered(report, materials, instance.material, std::format("instances[{}].material", index));
        if (!finite(instance.position) || !finite(instance.scale) || !finite(instance.rotation) ||
            instance.scale.x == 0.0f || instance.scale.y == 0.0f || instance.scale.z == 0.0f) {
            add_error(report, "URE-Q3-SCALAR-001", std::format("instances[{}]", index), "Invalid instance transform");
        }
    }
    for (std::size_t index = 0; index < archive.scene.spheres.size(); ++index) {
        const auto& sphere = archive.scene.spheres[index];
        require_registered(report, materials, sphere.material, std::format("spheres[{}].material", index));
        if (!finite(sphere.center) || !finite(sphere.radius) || sphere.radius <= 0.0f) {
            add_error(report, "URE-Q3-SCALAR-001", std::format("spheres[{}]", index), "Invalid sphere scalar");
        }
    }
    for (std::size_t index = 0; index < archive.scene.quad_lights.size(); ++index) {
        const auto& light = archive.scene.quad_lights[index];
        require_registered(report, materials, light.material, std::format("quad_lights[{}].material", index));
        if (!finite(light.corner) || !finite(light.edge_u) || !finite(light.edge_v)) {
            add_error(report, "URE-Q3-SCALAR-001", std::format("quad_lights[{}]", index), "Invalid quad light scalar");
        }
    }
    const Camera& camera = archive.scene.camera;
    if (!finite(camera.position) || !finite(camera.look_at) || !finite(camera.up) || !finite(camera.fov) ||
        !finite(camera.aspect_ratio) || !finite(camera.aperture) || !finite(camera.focus_dist) ||
        camera.fov <= 0.0f || camera.fov >= 180.0f || camera.aspect_ratio <= 0.0f ||
        camera.aperture < 0.0f || camera.focus_dist <= 0.0f) {
        add_error(report, "URE-Q3-SCALAR-001", "camera", "Invalid camera scalar");
    }
    if (!finite(archive.scene.background_color) || !finite(archive.scene.medium_density) ||
        !finite(archive.scene.medium_anisotropy) || !finite(archive.scene.medium_scattering) ||
        !finite(archive.scene.medium_absorption) || !finite(archive.scene.medium_max_distance) ||
        archive.scene.medium_density < 0.0f || archive.scene.medium_max_distance <= 0.0f ||
        archive.scene.width < 0 || archive.scene.height < 0 || archive.scene.spp < 0) {
        add_error(report, "URE-Q3-SCALAR-001", "scene", "Invalid scene scalar");
    }
    if (archive.scene.medium_mie_resource && !finite_mie(*archive.scene.medium_mie_resource)) {
        add_error(report, "URE-Q3-MIE-003", "scene.medium_mie", "Non-finite Mie resource");
    }
    return report;
}

std::string scene_ir_semantic_hash(const NativeSceneArchive& archive) {
    NativeSceneArchive canonical = make_native_scene_archive(archive.document, archive.scene);
    canonical.source_ids = archive.source_ids;
    std::unordered_map<const scene_ir::MiePhaseResource*, std::shared_ptr<const scene_ir::MiePhaseResource>> normalized_mie;
    auto normalize_mie_reference = [&](std::shared_ptr<const scene_ir::MiePhaseResource>& reference) {
        if (!reference) return;
        const auto found = normalized_mie.find(reference.get());
        if (found != normalized_mie.end()) {
            reference = found->second;
            return;
        }
        auto value = std::make_shared<scene_ir::MiePhaseResource>(*reference);
        auto normalize_values = [](std::vector<float>& values) {
            for (float& item : values) normalize(item);
        };
        normalize_values(value->wavelengths_nm);
        normalize_values(value->cos_theta);
        normalize_values(value->phase);
        normalize_values(value->cdf);
        normalize_values(value->scattering_cross_section_m2);
        normalize_values(value->extinction_cross_section_m2);
        normalize_values(value->absorption_cross_section_m2);
        normalize_values(value->asymmetry);
        std::shared_ptr<const scene_ir::MiePhaseResource> immutable = value;
        normalized_mie.emplace(reference.get(), immutable);
        reference = std::move(immutable);
    };
    for (auto& material : canonical.scene.materials) {
        if (!material) continue;
        normalize(material->base_color);
        normalize(material->roughness);
        normalize(material->ior);
        normalize(material->dispersion);
        normalize(material->metal_eta);
        normalize(material->metal_k);
        normalize(material->thin_film_thickness);
        normalize(material->thin_film_ior);
        normalize(material->emission);
        normalize(material->medium_density);
        normalize(material->medium_anisotropy);
        normalize(material->medium_scattering);
        normalize(material->medium_absorption);
        normalize(material->normal_scale);
        normalize_mie_reference(material->medium_mie_resource);
        if (material->graph) {
            for (auto& node : material->graph->nodes) {
                normalize(node.color);
                normalize(node.value);
            }
        }
    }
    for (auto& mesh : canonical.scene.meshes) {
        if (!mesh || !mesh->mesh) continue;
        for (auto& vertex : mesh->mesh->vertices) {
            normalize(vertex.position);
            normalize(vertex.normal);
            normalize(vertex.uv);
            normalize(vertex.tangent);
        }
    }
    for (auto& instance : canonical.scene.instances) {
        normalize(instance.position);
        normalize(instance.scale);
        normalize(instance.rotation.w);
        normalize(instance.rotation.x);
        normalize(instance.rotation.y);
        normalize(instance.rotation.z);
    }
    for (auto& sphere : canonical.scene.spheres) {
        normalize(sphere.center);
        normalize(sphere.radius);
    }
    for (auto& light : canonical.scene.quad_lights) {
        normalize(light.corner);
        normalize(light.edge_u);
        normalize(light.edge_v);
    }
    normalize(canonical.scene.camera.position);
    normalize(canonical.scene.camera.look_at);
    normalize(canonical.scene.camera.up);
    normalize(canonical.scene.camera.fov);
    normalize(canonical.scene.camera.aspect_ratio);
    normalize(canonical.scene.camera.aperture);
    normalize(canonical.scene.camera.focus_dist);
    normalize(canonical.scene.background_color);
    normalize(canonical.scene.medium_density);
    normalize(canonical.scene.medium_anisotropy);
    normalize(canonical.scene.medium_scattering);
    normalize(canonical.scene.medium_absorption);
    normalize(canonical.scene.medium_max_distance);
    normalize_mie_reference(canonical.scene.medium_mie_resource);

    SceneDocument document = canonical.document;
    std::erase_if(document.resources, [](const ResourceDescriptor& resource) {
        return resource.id.starts_with("mesh/") || resource.id.starts_with("mie/");
    });
    const detail::EncodedResources resources = detail::encode_resources(canonical);
    const std::vector<std::uint8_t> graph = detail::encode_scene_graph(canonical, resources);
    std::vector<std::uint8_t> stream;
    append_bytes(stream, semantic_hash(document));
    stream.insert(stream.end(), graph.begin(), graph.end());
    for (const auto& resource : resources.payloads) {
        append_bytes(stream, resource.id);
        append_bytes(stream, resource.descriptor.content_hash);
    }
    if (archive.procedural_graph) {
        append_bytes(stream, "ure.procedural.graph.v1");
        const auto procedural = detail::encode_procedural_graph(*archive.procedural_graph);
        stream.insert(stream.end(), procedural.begin(), procedural.end());
    }
    return sha256_hex(stream);
}

}
