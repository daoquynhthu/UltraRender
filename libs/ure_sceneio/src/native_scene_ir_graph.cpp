#include <algorithm>
#include <memory>
#include <map>
#include <span>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/verifier.h>

#include <ure/native_scene_uuid.hpp>

#include "native_scene_ir_internal.hpp"
#include "ure_scene_ir_v2_generated.h"

namespace ure::native_scene::detail {
namespace {

namespace schema = ure::native::schema;

std::unique_ptr<schema::Vec3> vec3(const core::Vec3f& value) {
    return std::make_unique<schema::Vec3>(value.x, value.y, value.z);
}

core::Vec3f vec3(const std::unique_ptr<schema::Vec3>& value) {
    return value ? core::Vec3f{value->x(), value->y(), value->z()} : core::Vec3f{};
}

schema::MaterialModel material_model(scene_ir::MaterialModel value) {
    switch (value) {
        case scene_ir::MaterialModel::Lambertian: return schema::MaterialModel::Lambertian;
        case scene_ir::MaterialModel::Metal: return schema::MaterialModel::Metal;
        case scene_ir::MaterialModel::Dielectric: return schema::MaterialModel::Dielectric;
        case scene_ir::MaterialModel::Light: return schema::MaterialModel::Light;
        case scene_ir::MaterialModel::Cloth: return schema::MaterialModel::Cloth;
    }
    throw std::invalid_argument("Invalid material model");
}

scene_ir::MaterialModel material_model(schema::MaterialModel value) {
    switch (value) {
        case schema::MaterialModel::Lambertian: return scene_ir::MaterialModel::Lambertian;
        case schema::MaterialModel::Metal: return scene_ir::MaterialModel::Metal;
        case schema::MaterialModel::Dielectric: return scene_ir::MaterialModel::Dielectric;
        case schema::MaterialModel::Light: return scene_ir::MaterialModel::Light;
        case schema::MaterialModel::Cloth: return scene_ir::MaterialModel::Cloth;
        default: throw std::invalid_argument("Invalid material model");
    }
}

schema::VolumePhaseFunction phase(scene_ir::VolumePhaseFunction value) {
    switch (value) {
        case scene_ir::VolumePhaseFunction::HenyeyGreenstein: return schema::VolumePhaseFunction::HenyeyGreenstein;
        case scene_ir::VolumePhaseFunction::Rayleigh: return schema::VolumePhaseFunction::Rayleigh;
        case scene_ir::VolumePhaseFunction::Mie: return schema::VolumePhaseFunction::Mie;
    }
    throw std::invalid_argument("Invalid phase function");
}

scene_ir::VolumePhaseFunction phase(schema::VolumePhaseFunction value) {
    switch (value) {
        case schema::VolumePhaseFunction::HenyeyGreenstein: return scene_ir::VolumePhaseFunction::HenyeyGreenstein;
        case schema::VolumePhaseFunction::Rayleigh: return scene_ir::VolumePhaseFunction::Rayleigh;
        case schema::VolumePhaseFunction::Mie: return scene_ir::VolumePhaseFunction::Mie;
        default: throw std::invalid_argument("Invalid phase function");
    }
}

schema::MaterialGraphNodeKind graph_kind(scene_ir::MaterialGraphNodeKind value) {
    const auto raw = static_cast<std::uint8_t>(value);
    if (raw > static_cast<std::uint8_t>(scene_ir::MaterialGraphNodeKind::BsdfFluorescence)) {
        throw std::invalid_argument("Invalid material graph node kind");
    }
    return static_cast<schema::MaterialGraphNodeKind>(raw);
}

scene_ir::MaterialGraphNodeKind graph_kind(schema::MaterialGraphNodeKind value) {
    const auto raw = static_cast<std::uint8_t>(value);
    if (raw > static_cast<std::uint8_t>(schema::MaterialGraphNodeKind::BsdfFluorescence)) {
        throw std::invalid_argument("Invalid material graph node kind");
    }
    return static_cast<scene_ir::MaterialGraphNodeKind>(raw);
}

schema::DiffractiveOperatorKind diffraction_kind(
    scene_ir::DiffractiveOperatorKind value) {
    const auto raw = static_cast<std::uint8_t>(value);
    if (raw > static_cast<std::uint8_t>(
                  scene_ir::DiffractiveOperatorKind::
                      ScatteringTable)) {
        throw std::invalid_argument(
            "Invalid diffractive operator kind");
    }
    return static_cast<
        schema::DiffractiveOperatorKind>(raw);
}

scene_ir::DiffractiveOperatorKind diffraction_kind(
    schema::DiffractiveOperatorKind value) {
    const auto raw = static_cast<std::uint8_t>(value);
    if (raw > static_cast<std::uint8_t>(
                  schema::DiffractiveOperatorKind::
                      ScatteringTable)) {
        throw std::invalid_argument(
            "Invalid diffractive operator kind");
    }
    return static_cast<
        scene_ir::DiffractiveOperatorKind>(raw);
}

schema::DiffractiveScatterSide diffraction_side(
    scene_ir::DiffractiveScatterSide value) {
    const auto raw = static_cast<std::uint8_t>(value);
    if (raw > static_cast<std::uint8_t>(
                  scene_ir::DiffractiveScatterSide::
                      Transmission)) {
        throw std::invalid_argument(
            "Invalid diffractive scatter side");
    }
    return static_cast<
        schema::DiffractiveScatterSide>(raw);
}

scene_ir::DiffractiveScatterSide diffraction_side(
    schema::DiffractiveScatterSide value) {
    const auto raw = static_cast<std::uint8_t>(value);
    if (raw > static_cast<std::uint8_t>(
                  schema::DiffractiveScatterSide::
                      Transmission)) {
        throw std::invalid_argument(
            "Invalid diffractive scatter side");
    }
    return static_cast<
        scene_ir::DiffractiveScatterSide>(raw);
}

std::unique_ptr<schema::DiffractiveOperatorT>
encode_diffraction(
    const scene_ir::DiffractiveOperator& source) {
    if (source.table.size() >
        scene_ir::kMaxDiffractiveScatteringEntries) {
        throw std::invalid_argument(
            "Diffractive scattering table exceeds the schema budget");
    }
    auto result =
        std::make_unique<
            schema::DiffractiveOperatorT>();
    result->kind = diffraction_kind(source.kind);
    result->side = diffraction_side(source.side);
    result->period_m = source.period_m;
    result->orientation_rad =
        source.orientation_rad;
    result->duty_cycle = source.duty_cycle;
    result->phase_depth_rad =
        source.phase_depth_rad;
    result->design_wavelength_nm =
        source.design_wavelength_nm;
    result->focal_length_m =
        source.focal_length_m;
    result->aperture_radius_m =
        source.aperture_radius_m;
    result->max_order = source.max_order;
    result->table_id = source.table_id;
    for (const auto& source_entry :
         source.table) {
        auto entry =
            std::make_unique<
                schema::
                    DiffractiveScatteringEntryT>();
        entry->wavelength_nm =
            source_entry.wavelength_nm;
        entry->incident_cosine =
            source_entry.incident_cosine;
        entry->order = source_entry.order;
        entry->side =
            diffraction_side(source_entry.side);
        entry->jones_ss =
            std::make_unique<
                schema::ComplexCoefficient>(
                source_entry.jones_ss.real,
                source_entry.jones_ss.imag);
        entry->jones_sp =
            std::make_unique<
                schema::ComplexCoefficient>(
                source_entry.jones_sp.real,
                source_entry.jones_sp.imag);
        entry->jones_ps =
            std::make_unique<
                schema::ComplexCoefficient>(
                source_entry.jones_ps.real,
                source_entry.jones_ps.imag);
        entry->jones_pp =
            std::make_unique<
                schema::ComplexCoefficient>(
                source_entry.jones_pp.real,
                source_entry.jones_pp.imag);
        result->table.push_back(
            std::move(entry));
    }
    return result;
}

scene_ir::ComplexCoefficient
decode_coefficient(
    const std::unique_ptr<
        schema::ComplexCoefficient>& source) {
    if (!source) return {};
    return {source->real(), source->imag()};
}

scene_ir::DiffractiveOperator
decode_diffraction(
    const schema::DiffractiveOperatorT& source) {
    if (source.table.size() >
        scene_ir::kMaxDiffractiveScatteringEntries) {
        throw std::invalid_argument(
            "Diffractive scattering table exceeds the schema budget");
    }
    scene_ir::DiffractiveOperator result;
    result.kind = diffraction_kind(source.kind);
    result.side = diffraction_side(source.side);
    result.period_m = source.period_m;
    result.orientation_rad =
        source.orientation_rad;
    result.duty_cycle = source.duty_cycle;
    result.phase_depth_rad =
        source.phase_depth_rad;
    result.design_wavelength_nm =
        source.design_wavelength_nm;
    result.focal_length_m =
        source.focal_length_m;
    result.aperture_radius_m =
        source.aperture_radius_m;
    result.max_order = source.max_order;
    result.table_id = source.table_id;
    for (const auto& source_entry :
         source.table) {
        if (!source_entry) {
            throw std::invalid_argument(
                "Null diffractive scattering entry");
        }
        scene_ir::DiffractiveScatteringEntry entry;
        entry.wavelength_nm =
            source_entry->wavelength_nm;
        entry.incident_cosine =
            source_entry->incident_cosine;
        entry.order = source_entry->order;
        entry.side =
            diffraction_side(source_entry->side);
        entry.jones_ss =
            decode_coefficient(
                source_entry->jones_ss);
        entry.jones_sp =
            decode_coefficient(
                source_entry->jones_sp);
        entry.jones_ps =
            decode_coefficient(
                source_entry->jones_ps);
        entry.jones_pp =
            decode_coefficient(
                source_entry->jones_pp);
        result.table.push_back(entry);
    }
    return result;
}

template <typename T>
std::string reference_id(const std::unordered_map<const T*, std::string>& ids,
                         const std::shared_ptr<T>& value) {
    if (!value) return {};
    const auto found = ids.find(value.get());
    if (found == ids.end()) throw std::invalid_argument("Unregistered SceneIR reference");
    return found->second;
}

template <typename T>
std::shared_ptr<T> lookup(const std::unordered_map<std::string, std::shared_ptr<T>>& values,
                          const std::string& id) {
    if (id.empty()) return {};
    const auto found = values.find(id);
    if (found == values.end()) throw std::invalid_argument("Dangling SceneIR reference");
    return found->second;
}

std::vector<std::uint8_t> uuid_bytes(const Uuid& value) {
    return {value.bytes.begin(), value.bytes.end()};
}

Uuid uuid_value(const std::vector<std::uint8_t>& bytes,
                const SceneDocument& document,
                std::string_view kind,
                std::string_view alias) {
    if (document.schema_version.major < 2)
        return deterministic_object_uuid(document.id, kind, alias);
    if (bytes.size() != 16)
        throw std::invalid_argument("Canonical object UUID has invalid byte length");
    Uuid output;
    std::ranges::copy(bytes, output.bytes.begin());
    if (!is_rfc9562_uuid(output))
        throw std::invalid_argument("Canonical object UUID is not RFC 9562");
    return output;
}

template <typename T>
std::vector<std::uint8_t> reference_uuid(
    const std::unordered_map<const T*, Uuid>& values,
    const std::shared_ptr<T>& value) {
    if (!value)
        return {};
    const auto found = values.find(value.get());
    if (found == values.end())
        throw std::invalid_argument("Unregistered SceneIR UUID reference");
    return uuid_bytes(found->second);
}

template <typename T>
std::shared_ptr<T> lookup_uuid(
    const std::map<Uuid, std::shared_ptr<T>>& values,
    const std::vector<std::uint8_t>& uuid,
    const std::unordered_map<std::string, std::shared_ptr<T>>& aliases,
    const std::string& alias,
    bool canonical) {
    if (!canonical)
        return lookup(aliases, alias);
    static_cast<void>(aliases);
    static_cast<void>(alias);
    if (uuid.empty() && alias.empty())
        return {};
    if (uuid.size() != 16)
        throw std::invalid_argument("Canonical reference UUID has invalid byte length");
    Uuid key;
    std::ranges::copy(uuid, key.bytes.begin());
    const auto found = values.find(key);
    if (found == values.end())
        throw std::invalid_argument("Dangling canonical UUID reference");
    return found->second;
}

std::unique_ptr<schema::ResourceReferenceT> resource_reference(
    const ResourceReferenceValue& value) {
    auto result = std::make_unique<schema::ResourceReferenceT>();
    result->id = value.id;
    result->content_hash = value.content_hash;
    return result;
}

std::unique_ptr<schema::ResourceReferenceT> mie_reference(
    const EncodedResources& resources,
    const std::shared_ptr<const scene_ir::MiePhaseResource>& value) {
    if (!value) return {};
    const auto found = resources.mie.find(value.get());
    if (found == resources.mie.end()) throw std::invalid_argument("Unregistered Mie resource");
    return resource_reference(found->second);
}

std::unique_ptr<schema::FluorescenceResourceT>
encode_fluorescence(
    const scene_ir::FluorescenceResource& source) {
    if (source.emission_pdf_per_nm.size() >
        scene_ir::kMaxFluorescenceMatrixEntries) {
        throw std::invalid_argument(
            "Fluorescence matrix exceeds the schema budget");
    }
    auto result =
        std::make_unique<
            schema::FluorescenceResourceT>();
    result->resource_id = source.resource_id;
    result->excitation_wavelengths_nm =
        source.excitation_wavelengths_nm;
    result->emission_wavelengths_nm =
        source.emission_wavelengths_nm;
    result->excitation_efficiency =
        source.excitation_efficiency;
    result->quantum_yield = source.quantum_yield;
    result->emission_pdf_per_nm =
        source.emission_pdf_per_nm;
    result->lifetime_seconds =
        source.lifetime_seconds;
    return result;
}

scene_ir::FluorescenceResource decode_fluorescence(
    const schema::FluorescenceResourceT& source) {
    if (source.emission_pdf_per_nm.size() >
        scene_ir::kMaxFluorescenceMatrixEntries) {
        throw std::invalid_argument(
            "Fluorescence matrix exceeds the schema budget");
    }
    scene_ir::FluorescenceResource result;
    result.resource_id = source.resource_id;
    result.excitation_wavelengths_nm =
        source.excitation_wavelengths_nm;
    result.emission_wavelengths_nm =
        source.emission_wavelengths_nm;
    result.excitation_efficiency =
        source.excitation_efficiency;
    result.quantum_yield = source.quantum_yield;
    result.emission_pdf_per_nm =
        source.emission_pdf_per_nm;
    result.lifetime_seconds =
        source.lifetime_seconds;
    return result;
}

std::unique_ptr<schema::MaterialGraphT> encode_graph(
    const scene_ir::MaterialGraph& source,
    const std::unordered_map<const scene_ir::TextureResource*, std::string>& texture_ids) {
    auto graph = std::make_unique<schema::MaterialGraphT>();
    graph->output_node_id = source.output_node_id;
    for (const auto& source_node : source.nodes) {
        auto node = std::make_unique<schema::MaterialGraphNodeT>();
        node->id = source_node.id;
        node->kind = graph_kind(source_node.kind);
        node->name = source_node.name;
        node->color = vec3(source_node.color);
        node->value = source_node.value;
        node->texture_id = reference_id(texture_ids, source_node.texture);
        if (source_node.kind >=
                scene_ir::MaterialGraphNodeKind::
                    BsdfGrating &&
            source_node.kind <=
                scene_ir::MaterialGraphNodeKind::
                    BsdfScatteringTable) {
            node->diffraction =
                encode_diffraction(
                    source_node.diffraction);
        }
        if (source_node.kind ==
            scene_ir::MaterialGraphNodeKind::
                BsdfFluorescence) {
            node->fluorescence =
                encode_fluorescence(
                    source_node.fluorescence);
        }
        for (const auto& source_input : source_node.inputs) {
            auto input = std::make_unique<schema::MaterialGraphInputT>();
            input->name = source_input.name;
            input->node_id = source_input.node_id;
            input->output = source_input.output;
            node->inputs.push_back(std::move(input));
        }
        graph->nodes.push_back(std::move(node));
    }
    return graph;
}

std::shared_ptr<scene_ir::MaterialGraph> decode_graph(
    const schema::MaterialGraphT& source,
    const std::unordered_map<std::string, std::shared_ptr<scene_ir::TextureResource>>& textures) {
    auto graph = std::make_shared<scene_ir::MaterialGraph>();
    graph->output_node_id = source.output_node_id;
    for (const auto& source_node : source.nodes) {
        if (!source_node) throw std::invalid_argument("Null material graph node");
        scene_ir::MaterialGraphNode node;
        node.id = source_node->id;
        node.kind = graph_kind(source_node->kind);
        node.name = source_node->name;
        node.color = vec3(source_node->color);
        node.value = source_node->value;
        node.texture = lookup(textures, source_node->texture_id);
        if (source_node->diffraction) {
            node.diffraction =
                decode_diffraction(
                    *source_node->diffraction);
        }
        if (source_node->fluorescence) {
            node.fluorescence =
                decode_fluorescence(
                    *source_node->fluorescence);
        }
        for (const auto& source_input : source_node->inputs) {
            if (!source_input) throw std::invalid_argument("Null material graph input");
            node.inputs.push_back({source_input->name, source_input->node_id, source_input->output});
        }
        graph->nodes.push_back(std::move(node));
    }
    graph->validate();
    return graph;
}

template <typename T>
LoadResult<T> graph_failure(std::string message) {
    LoadResult<T> result;
    result.diagnostics.push_back({"URE-Q3-GRAPH-001", DiagnosticSeverity::Error, "scene_ir",
                                  std::move(message), {}});
    return result;
}

}

std::vector<std::uint8_t> encode_scene_graph(const NativeSceneArchive& archive,
                                             const EncodedResources& resources) {
    std::unordered_map<const scene_ir::MaterialNode*, std::string> material_ids;
    std::unordered_map<const scene_ir::MeshResource*, std::string> mesh_ids;
    std::unordered_map<const scene_ir::ImageResource*, std::string> image_ids;
    std::unordered_map<const scene_ir::TextureResource*, std::string> texture_ids;
    std::unordered_map<const scene_ir::MaterialNode*, Uuid> material_uuids;
    std::unordered_map<const scene_ir::MeshResource*, Uuid> mesh_uuids;
    std::unordered_map<const scene_ir::ImageResource*, Uuid> image_uuids;
    std::unordered_map<const scene_ir::TextureResource*, Uuid> texture_uuids;
    for (std::size_t index = 0; index < archive.scene.materials.size(); ++index) material_ids.emplace(archive.scene.materials[index].get(), archive.source_ids.materials[index]);
    for (std::size_t index = 0; index < archive.scene.meshes.size(); ++index) mesh_ids.emplace(archive.scene.meshes[index].get(), archive.source_ids.meshes[index]);
    for (std::size_t index = 0; index < archive.scene.images.size(); ++index) image_ids.emplace(archive.scene.images[index].get(), archive.source_ids.images[index]);
    for (std::size_t index = 0; index < archive.scene.textures.size(); ++index) texture_ids.emplace(archive.scene.textures[index].get(), archive.source_ids.textures[index]);
    for (std::size_t index = 0; index < archive.scene.materials.size(); ++index) material_uuids.emplace(archive.scene.materials[index].get(), archive.object_uuids.materials[index]);
    for (std::size_t index = 0; index < archive.scene.meshes.size(); ++index) mesh_uuids.emplace(archive.scene.meshes[index].get(), archive.object_uuids.meshes[index]);
    for (std::size_t index = 0; index < archive.scene.images.size(); ++index) image_uuids.emplace(archive.scene.images[index].get(), archive.object_uuids.images[index]);
    for (std::size_t index = 0; index < archive.scene.textures.size(); ++index) texture_uuids.emplace(archive.scene.textures[index].get(), archive.object_uuids.textures[index]);
    const bool canonical = archive.document.schema_version.major >= 2;

    schema::SceneGraphT graph;
    for (std::size_t index = 0; index < archive.scene.images.size(); ++index) {
        const auto& source = archive.scene.images[index];
        if (!source) throw std::invalid_argument("Null registered image");
        auto image = std::make_unique<schema::ImageT>();
        image->id = archive.source_ids.images[index];
        image->name = source->name;
        image->uri = source->uri;
        image->color_space = source->color_space == scene_ir::ImageColorSpace::Linear
            ? schema::ImageColorSpace::Linear : schema::ImageColorSpace::SRGB;
        if (canonical)
            image->uuid = uuid_bytes(archive.object_uuids.images[index]);
        graph.images.push_back(std::move(image));
    }
    for (std::size_t index = 0; index < archive.scene.textures.size(); ++index) {
        const auto& source = archive.scene.textures[index];
        if (!source) throw std::invalid_argument("Null registered texture");
        auto texture = std::make_unique<schema::TextureT>();
        texture->id = archive.source_ids.textures[index];
        texture->name = source->name;
        texture->image_id = reference_id(image_ids, source->image);
        texture->uv_set = source->uv_set;
        if (canonical) {
            texture->uuid = uuid_bytes(archive.object_uuids.textures[index]);
            texture->image_uuid = reference_uuid(image_uuids, source->image);
        }
        graph.textures.push_back(std::move(texture));
    }
    for (std::size_t index = 0; index < archive.scene.materials.size(); ++index) {
        const auto& source = archive.scene.materials[index];
        if (!source) throw std::invalid_argument("Null registered material");
        auto material = std::make_unique<schema::MaterialT>();
        material->id = archive.source_ids.materials[index];
        material->name = source->name;
        material->model = material_model(source->model);
        material->base_color = vec3(source->base_color);
        material->roughness = source->roughness;
        material->ior = source->ior;
        material->dispersion = source->dispersion;
        material->metal_eta = vec3(source->metal_eta);
        material->metal_k = vec3(source->metal_k);
        material->thin_film_thickness = source->thin_film_thickness;
        material->thin_film_ior = source->thin_film_ior;
        material->emission = vec3(source->emission);
        material->medium_density = source->medium_density;
        material->medium_anisotropy = source->medium_anisotropy;
        material->medium_phase = phase(source->medium_phase);
        material->medium_mie = mie_reference(resources, source->medium_mie_resource);
        material->medium_scattering = vec3(source->medium_scattering);
        material->medium_absorption = vec3(source->medium_absorption);
        material->base_color_texture_id = reference_id(texture_ids, source->base_color_texture);
        material->roughness_texture_id = reference_id(texture_ids, source->roughness_texture);
        material->emission_texture_id = reference_id(texture_ids, source->emission_texture);
        material->normal_texture_id = reference_id(texture_ids, source->normal_texture);
        material->normal_scale = source->normal_scale;
        if (source->spectral_extension) {
            material->spectral_extension = std::make_unique<schema::SpectralMaterialExtensionT>();
            material->spectral_extension->spectral_bands = source->spectral_extension->spectral_bands;
            material->spectral_extension->albedo_spd = source->spectral_extension->albedo_spd;
            material->spectral_extension->emission_spd = source->spectral_extension->emission_spd;
        }
        if (source->graph) material->graph = encode_graph(*source->graph, texture_ids);
        if (canonical) {
            material->uuid = uuid_bytes(archive.object_uuids.materials[index]);
            material->base_color_texture_uuid = reference_uuid(texture_uuids, source->base_color_texture);
            material->roughness_texture_uuid = reference_uuid(texture_uuids, source->roughness_texture);
            material->emission_texture_uuid = reference_uuid(texture_uuids, source->emission_texture);
            material->normal_texture_uuid = reference_uuid(texture_uuids, source->normal_texture);
        }
        graph.materials.push_back(std::move(material));
    }
    for (std::size_t index = 0; index < archive.scene.meshes.size(); ++index) {
        const auto& source = archive.scene.meshes[index];
        if (!source || !source->mesh) throw std::invalid_argument("Null registered mesh");
        const auto payload = resources.meshes.find(source->mesh.get());
        if (payload == resources.meshes.end()) throw std::invalid_argument("Missing mesh payload");
        auto mesh = std::make_unique<schema::MeshRecordT>();
        mesh->id = archive.source_ids.meshes[index];
        mesh->name = source->name;
        mesh->payload = resource_reference(payload->second);
        if (canonical)
            mesh->uuid = uuid_bytes(archive.object_uuids.meshes[index]);
        graph.meshes.push_back(std::move(mesh));
    }
    for (std::size_t index = 0; index < archive.scene.instances.size(); ++index) {
        const auto& source = archive.scene.instances[index];
        auto instance = std::make_unique<schema::InstanceT>();
        instance->id = archive.source_ids.instances[index];
        instance->name = source.name;
        instance->mesh_id = reference_id(mesh_ids, source.mesh);
        instance->material_id = reference_id(material_ids, source.material);
        instance->position = vec3(source.position);
        instance->scale = vec3(source.scale);
        instance->rotation = std::make_unique<schema::Quat>(source.rotation.w, source.rotation.x, source.rotation.y, source.rotation.z);
        instance->rigid_body = std::make_unique<schema::RigidBodyT>();
        instance->rigid_body->enabled = source.rigid_body.enabled;
        instance->rigid_body->mass = source.rigid_body.mass;
        instance->rigid_body->friction = source.rigid_body.friction;
        instance->rigid_body->restitution = source.rigid_body.restitution;
        instance->rigid_body->linear_damping = source.rigid_body.linear_damping;
        instance->rigid_body->angular_damping = source.rigid_body.angular_damping;
        instance->rigid_body->velocity = vec3(source.rigid_body.velocity);
        instance->rigid_body->collider_type = source.rigid_body.collider_type;
        instance->rigid_body->collider_size = vec3(source.rigid_body.collider_size);
        instance->rigid_body->collider_radius = source.rigid_body.collider_radius;
        instance->rigid_body->material_id = source.rigid_body.material_id;
        if (canonical) {
            instance->uuid = uuid_bytes(archive.object_uuids.instances[index]);
            instance->mesh_uuid = reference_uuid(mesh_uuids, source.mesh);
            instance->material_uuid = reference_uuid(material_uuids, source.material);
        }
        graph.instances.push_back(std::move(instance));
    }
    for (std::size_t index = 0; index < archive.scene.spheres.size(); ++index) {
        const auto& source = archive.scene.spheres[index];
        auto sphere = std::make_unique<schema::SphereT>();
        sphere->id = archive.source_ids.spheres[index];
        sphere->name = source.name;
        sphere->center = vec3(source.center);
        sphere->radius = source.radius;
        sphere->material_id = reference_id(material_ids, source.material);
        if (canonical) {
            sphere->uuid = uuid_bytes(archive.object_uuids.spheres[index]);
            sphere->material_uuid = reference_uuid(material_uuids, source.material);
        }
        graph.spheres.push_back(std::move(sphere));
    }
    for (std::size_t index = 0; index < archive.scene.quad_lights.size(); ++index) {
        const auto& source = archive.scene.quad_lights[index];
        auto light = std::make_unique<schema::QuadLightT>();
        light->id = archive.source_ids.quad_lights[index];
        light->name = source.name;
        light->corner = vec3(source.corner);
        light->edge_u = vec3(source.edge_u);
        light->edge_v = vec3(source.edge_v);
        light->material_id = reference_id(material_ids, source.material);
        if (canonical) {
            light->uuid = uuid_bytes(archive.object_uuids.quad_lights[index]);
            light->material_uuid = reference_uuid(material_uuids, source.material);
        }
        graph.quad_lights.push_back(std::move(light));
    }
    graph.camera = std::make_unique<schema::CameraT>();
    graph.camera->position = vec3(archive.scene.camera.position);
    graph.camera->look_at = vec3(archive.scene.camera.look_at);
    graph.camera->up = vec3(archive.scene.camera.up);
    graph.camera->fov = archive.scene.camera.fov;
    graph.camera->aspect_ratio = archive.scene.camera.aspect_ratio;
    graph.camera->aperture = archive.scene.camera.aperture;
    graph.camera->focus_dist = archive.scene.camera.focus_dist;
    if (canonical) {
        graph.camera->uuid = uuid_bytes(archive.object_uuids.camera);
        graph.camera->world_from_camera.assign(
            archive.canonical_camera.world_from_camera.begin(),
            archive.canonical_camera.world_from_camera.end());
        graph.camera->sensor_width_m = archive.canonical_camera.sensor_width_m;
        graph.camera->sensor_height_m = archive.canonical_camera.sensor_height_m;
        graph.camera->focal_length_m = archive.canonical_camera.focal_length_m;
        graph.camera->aperture_diameter_m = archive.canonical_camera.aperture_diameter_m;
        graph.camera->focus_distance_m = archive.canonical_camera.focus_distance_m;
        graph.camera->lens_shift_x_m = archive.canonical_camera.lens_shift_x_m;
        graph.camera->lens_shift_y_m = archive.canonical_camera.lens_shift_y_m;
        graph.camera->shutter_open_s = archive.canonical_camera.shutter_open_s;
        graph.camera->shutter_close_s = archive.canonical_camera.shutter_close_s;
        graph.camera->exposure_scale = archive.canonical_camera.exposure_scale;
    }
    graph.physics = std::make_unique<schema::PhysicsT>();
    graph.physics->enabled = archive.scene.physics.enabled;
    graph.physics->dt = archive.scene.physics.dt;
    graph.physics->total_frames = archive.scene.physics.total_frames;
    graph.physics->spp_per_frame = archive.scene.physics.spp_per_frame;
    graph.physics->fluid = std::make_unique<schema::FluidT>();
    graph.physics->fluid->enabled = archive.scene.physics.fluid.enabled;
    graph.physics->fluid->bounds_min = vec3(archive.scene.physics.fluid.bounds_min);
    graph.physics->fluid->bounds_max = vec3(archive.scene.physics.fluid.bounds_max);
    graph.physics->fluid->particle_spacing = archive.scene.physics.fluid.particle_spacing;
    graph.physics->fluid->fill_min = vec3(archive.scene.physics.fluid.fill_min);
    graph.physics->fluid->fill_max = vec3(archive.scene.physics.fluid.fill_max);
    graph.background_color = vec3(archive.scene.background_color);
    graph.medium_density = archive.scene.medium_density;
    graph.medium_anisotropy = archive.scene.medium_anisotropy;
    graph.medium_phase = phase(archive.scene.medium_phase);
    graph.medium_mie = mie_reference(resources, archive.scene.medium_mie_resource);
    graph.medium_scattering = vec3(archive.scene.medium_scattering);
    graph.medium_absorption = vec3(archive.scene.medium_absorption);
    graph.medium_max_distance = archive.scene.medium_max_distance;
    graph.width = archive.scene.width;
    graph.height = archive.scene.height;
    graph.spp = archive.scene.spp;
    if (canonical)
        graph.environment_uuid = uuid_bytes(archive.object_uuids.environment);

    flatbuffers::FlatBufferBuilder builder;
    schema::FinishSceneGraphBuffer(builder, schema::SceneGraph::Pack(builder, &graph));
    return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

LoadResult<NativeSceneArchive> decode_scene_graph(
    const SceneDocument& document,
    std::span<const std::uint8_t> bytes,
    const std::unordered_map<std::string, std::shared_ptr<Mesh>>& meshes,
    const std::unordered_map<std::string, std::shared_ptr<const scene_ir::MiePhaseResource>>& mie,
    const std::unordered_map<std::string, std::string>& resource_hashes,
    const ValidationLimits& limits) {
    const auto max_tables = static_cast<flatbuffers::uoffset_t>(std::min<std::uint64_t>(
        limits.max_object_count, std::numeric_limits<flatbuffers::uoffset_t>::max()));
    flatbuffers::Verifier verifier(bytes.data(), bytes.size(), limits.max_nesting_depth, max_tables);
    if (!schema::VerifySceneGraphBuffer(verifier)) return graph_failure<NativeSceneArchive>("Invalid URIG payload");
    try {
        std::unique_ptr<schema::SceneGraphT> graph(schema::GetSceneGraph(bytes.data())->UnPack());
        NativeSceneArchive archive;
        archive.document = document;
        const bool canonical = document.schema_version.major >= 2;
        std::unordered_map<std::string, std::shared_ptr<scene_ir::ImageResource>> images_by_id;
        std::map<Uuid, std::shared_ptr<scene_ir::ImageResource>> images_by_uuid;
        for (const auto& source : graph->images) {
            if (!source || images_by_id.contains(source->id)) throw std::invalid_argument("Invalid image identity");
            auto image = std::make_shared<scene_ir::ImageResource>();
            image->name = source->name;
            image->uri = source->uri;
            switch (source->color_space) {
                case schema::ImageColorSpace::Linear: image->color_space = scene_ir::ImageColorSpace::Linear; break;
                case schema::ImageColorSpace::SRGB: image->color_space = scene_ir::ImageColorSpace::SRGB; break;
                default: throw std::invalid_argument("Invalid image color space");
            }
            archive.source_ids.images.push_back(source->id);
            const Uuid object_uuid = uuid_value(
                source->uuid, document, "image", source->id);
            if (!images_by_uuid.emplace(object_uuid, image).second)
                throw std::invalid_argument("Duplicate image UUID");
            archive.object_uuids.images.push_back(object_uuid);
            archive.scene.images.push_back(image);
            images_by_id.emplace(source->id, std::move(image));
        }
        std::unordered_map<std::string, std::shared_ptr<scene_ir::TextureResource>> textures_by_id;
        std::map<Uuid, std::shared_ptr<scene_ir::TextureResource>> textures_by_uuid;
        for (const auto& source : graph->textures) {
            if (!source || textures_by_id.contains(source->id)) throw std::invalid_argument("Invalid texture identity");
            auto texture = std::make_shared<scene_ir::TextureResource>();
            texture->name = source->name;
            texture->image = lookup_uuid(images_by_uuid, source->image_uuid,
                                         images_by_id, source->image_id,
                                         canonical);
            texture->uv_set = source->uv_set;
            archive.source_ids.textures.push_back(source->id);
            const Uuid object_uuid = uuid_value(
                source->uuid, document, "texture", source->id);
            if (!textures_by_uuid.emplace(object_uuid, texture).second)
                throw std::invalid_argument("Duplicate texture UUID");
            archive.object_uuids.textures.push_back(object_uuid);
            archive.scene.textures.push_back(texture);
            textures_by_id.emplace(source->id, std::move(texture));
        }
        std::unordered_map<std::string, std::shared_ptr<scene_ir::MaterialNode>> materials_by_id;
        std::map<Uuid, std::shared_ptr<scene_ir::MaterialNode>> materials_by_uuid;
        std::set<std::string> used_mie;
        std::set<std::string> used_meshes;
        auto lookup_mie = [&](const std::unique_ptr<schema::ResourceReferenceT>& reference) {
            if (!reference) return std::shared_ptr<const scene_ir::MiePhaseResource>{};
            const auto found = mie.find(reference->id);
            const auto hash = resource_hashes.find(reference->id);
            if (found == mie.end() || hash == resource_hashes.end() || reference->content_hash != hash->second) {
                throw std::invalid_argument("Dangling or mismatched Mie resource");
            }
            used_mie.insert(reference->id);
            return found->second;
        };
        for (const auto& source : graph->materials) {
            if (!source || materials_by_id.contains(source->id)) throw std::invalid_argument("Invalid material identity");
            auto material = std::make_shared<scene_ir::MaterialNode>();
            material->name = source->name;
            material->model = material_model(source->model);
            material->base_color = vec3(source->base_color);
            material->roughness = source->roughness;
            material->ior = source->ior;
            material->dispersion = source->dispersion;
            material->metal_eta = vec3(source->metal_eta);
            material->metal_k = vec3(source->metal_k);
            material->thin_film_thickness = source->thin_film_thickness;
            material->thin_film_ior = source->thin_film_ior;
            material->emission = vec3(source->emission);
            material->medium_density = source->medium_density;
            material->medium_anisotropy = source->medium_anisotropy;
            material->medium_phase = phase(source->medium_phase);
            material->medium_mie_resource = lookup_mie(source->medium_mie);
            material->medium_scattering = vec3(source->medium_scattering);
            material->medium_absorption = vec3(source->medium_absorption);
            material->base_color_texture = lookup_uuid(
                textures_by_uuid, source->base_color_texture_uuid,
                textures_by_id, source->base_color_texture_id, canonical);
            material->roughness_texture = lookup_uuid(
                textures_by_uuid, source->roughness_texture_uuid,
                textures_by_id, source->roughness_texture_id, canonical);
            material->emission_texture = lookup_uuid(
                textures_by_uuid, source->emission_texture_uuid,
                textures_by_id, source->emission_texture_id, canonical);
            material->normal_texture = lookup_uuid(
                textures_by_uuid, source->normal_texture_uuid,
                textures_by_id, source->normal_texture_id, canonical);
            material->normal_scale = source->normal_scale;
            if (source->spectral_extension) {
                material->spectral_extension = std::make_shared<scene_ir::SpectralMaterialExtension>();
                material->spectral_extension->spectral_bands = source->spectral_extension->spectral_bands;
                material->spectral_extension->albedo_spd = source->spectral_extension->albedo_spd;
                material->spectral_extension->emission_spd = source->spectral_extension->emission_spd;
            }
            if (source->graph) material->graph = decode_graph(*source->graph, textures_by_id);
            archive.source_ids.materials.push_back(source->id);
            const Uuid object_uuid = uuid_value(
                source->uuid, document, "material", source->id);
            if (!materials_by_uuid.emplace(object_uuid, material).second)
                throw std::invalid_argument("Duplicate material UUID");
            archive.object_uuids.materials.push_back(object_uuid);
            archive.scene.materials.push_back(material);
            materials_by_id.emplace(source->id, std::move(material));
        }
        std::unordered_map<std::string, std::shared_ptr<scene_ir::MeshResource>> meshes_by_id;
        std::map<Uuid, std::shared_ptr<scene_ir::MeshResource>> meshes_by_uuid;
        for (const auto& source : graph->meshes) {
            if (!source || !source->payload || meshes_by_id.contains(source->id)) throw std::invalid_argument("Invalid mesh identity");
            const auto payload = meshes.find(source->payload->id);
            const auto payload_hash = resource_hashes.find(source->payload->id);
            if (payload == meshes.end() || payload_hash == resource_hashes.end() ||
                source->payload->content_hash != payload_hash->second) {
                throw std::invalid_argument("Dangling or mismatched mesh payload");
            }
            used_meshes.insert(source->payload->id);
            auto mesh = std::make_shared<scene_ir::MeshResource>();
            mesh->name = source->name;
            mesh->mesh = payload->second;
            archive.source_ids.meshes.push_back(source->id);
            const Uuid object_uuid = uuid_value(
                source->uuid, document, "mesh", source->id);
            if (!meshes_by_uuid.emplace(object_uuid, mesh).second)
                throw std::invalid_argument("Duplicate mesh UUID");
            archive.object_uuids.meshes.push_back(object_uuid);
            archive.scene.meshes.push_back(mesh);
            meshes_by_id.emplace(source->id, std::move(mesh));
        }
        for (const auto& source : graph->instances) {
            if (!source) throw std::invalid_argument("Null instance");
            scene_ir::InstanceNode instance;
            instance.name = source->name;
            instance.mesh = lookup_uuid(meshes_by_uuid, source->mesh_uuid,
                                        meshes_by_id, source->mesh_id,
                                        canonical);
            instance.material = lookup_uuid(
                materials_by_uuid, source->material_uuid, materials_by_id,
                source->material_id, canonical);
            instance.position = vec3(source->position);
            instance.scale = vec3(source->scale);
            if (source->rotation) instance.rotation = {source->rotation->w(), source->rotation->x(), source->rotation->y(), source->rotation->z()};
            if (source->rigid_body) {
                instance.rigid_body.enabled = source->rigid_body->enabled;
                instance.rigid_body.mass = source->rigid_body->mass;
                instance.rigid_body.friction = source->rigid_body->friction;
                instance.rigid_body.restitution = source->rigid_body->restitution;
                instance.rigid_body.linear_damping = source->rigid_body->linear_damping;
                instance.rigid_body.angular_damping = source->rigid_body->angular_damping;
                instance.rigid_body.velocity = vec3(source->rigid_body->velocity);
                instance.rigid_body.collider_type = source->rigid_body->collider_type;
                instance.rigid_body.collider_size = vec3(source->rigid_body->collider_size);
                instance.rigid_body.collider_radius = source->rigid_body->collider_radius;
                instance.rigid_body.material_id = source->rigid_body->material_id;
            }
            archive.source_ids.instances.push_back(source->id);
            archive.object_uuids.instances.push_back(uuid_value(
                source->uuid, document, "instance", source->id));
            archive.scene.instances.push_back(std::move(instance));
        }
        for (const auto& source : graph->spheres) {
            if (!source) throw std::invalid_argument("Null sphere");
            archive.source_ids.spheres.push_back(source->id);
            archive.object_uuids.spheres.push_back(uuid_value(
                source->uuid, document, "sphere", source->id));
            archive.scene.spheres.push_back({source->name, vec3(source->center), source->radius,
                                             lookup_uuid(materials_by_uuid,
                                                         source->material_uuid,
                                                         materials_by_id,
                                                         source->material_id,
                                                         canonical)});
        }
        for (const auto& source : graph->quad_lights) {
            if (!source) throw std::invalid_argument("Null quad light");
            archive.source_ids.quad_lights.push_back(source->id);
            archive.object_uuids.quad_lights.push_back(uuid_value(
                source->uuid, document, "quad_light", source->id));
            archive.scene.quad_lights.push_back({source->name, vec3(source->corner), vec3(source->edge_u),
                                                  vec3(source->edge_v),
                                                  lookup_uuid(materials_by_uuid,
                                                              source->material_uuid,
                                                              materials_by_id,
                                                              source->material_id,
                                                              canonical)});
        }
        if (graph->camera) {
            archive.object_uuids.camera = uuid_value(
                graph->camera->uuid, document, "camera", "camera");
            if (canonical) {
                if (graph->camera->world_from_camera.size() != 16)
                    throw std::invalid_argument(
                        "Canonical camera transform must contain 16 values");
                std::ranges::copy(graph->camera->world_from_camera,
                                  archive.canonical_camera.world_from_camera.begin());
                archive.canonical_camera.sensor_width_m = graph->camera->sensor_width_m;
                archive.canonical_camera.sensor_height_m = graph->camera->sensor_height_m;
                archive.canonical_camera.focal_length_m = graph->camera->focal_length_m;
                archive.canonical_camera.aperture_diameter_m = graph->camera->aperture_diameter_m;
                archive.canonical_camera.focus_distance_m = graph->camera->focus_distance_m;
                archive.canonical_camera.lens_shift_x_m = graph->camera->lens_shift_x_m;
                archive.canonical_camera.lens_shift_y_m = graph->camera->lens_shift_y_m;
                archive.canonical_camera.shutter_open_s = graph->camera->shutter_open_s;
                archive.canonical_camera.shutter_close_s = graph->camera->shutter_close_s;
                archive.canonical_camera.exposure_scale = graph->camera->exposure_scale;
                apply_canonical_camera(archive.canonical_camera,
                                       archive.scene.camera);
            } else {
                archive.scene.camera.position = vec3(graph->camera->position);
                archive.scene.camera.look_at = vec3(graph->camera->look_at);
                archive.scene.camera.up = vec3(graph->camera->up);
                archive.scene.camera.fov = graph->camera->fov;
                archive.scene.camera.aspect_ratio = graph->camera->aspect_ratio;
                archive.scene.camera.aperture = graph->camera->aperture;
                archive.scene.camera.focus_dist = graph->camera->focus_dist;
                archive.canonical_camera =
                    canonical_camera_from_scene(archive.scene.camera);
            }
        }
        if (graph->physics) {
            archive.scene.physics.enabled = graph->physics->enabled;
            archive.scene.physics.dt = graph->physics->dt;
            archive.scene.physics.total_frames = graph->physics->total_frames;
            archive.scene.physics.spp_per_frame = graph->physics->spp_per_frame;
            if (graph->physics->fluid) {
                archive.scene.physics.fluid.enabled = graph->physics->fluid->enabled;
                archive.scene.physics.fluid.bounds_min = vec3(graph->physics->fluid->bounds_min);
                archive.scene.physics.fluid.bounds_max = vec3(graph->physics->fluid->bounds_max);
                archive.scene.physics.fluid.particle_spacing = graph->physics->fluid->particle_spacing;
                archive.scene.physics.fluid.fill_min = vec3(graph->physics->fluid->fill_min);
                archive.scene.physics.fluid.fill_max = vec3(graph->physics->fluid->fill_max);
            }
        }
        archive.scene.background_color = vec3(graph->background_color);
        archive.scene.medium_density = graph->medium_density;
        archive.scene.medium_anisotropy = graph->medium_anisotropy;
        archive.scene.medium_phase = phase(graph->medium_phase);
        archive.scene.medium_mie_resource = lookup_mie(graph->medium_mie);
        archive.scene.medium_scattering = vec3(graph->medium_scattering);
        archive.scene.medium_absorption = vec3(graph->medium_absorption);
        archive.scene.medium_max_distance = graph->medium_max_distance;
        archive.scene.width = graph->width;
        archive.scene.height = graph->height;
        archive.scene.spp = graph->spp;
        archive.object_uuids.environment = uuid_value(
            graph->environment_uuid, document, "environment", "environment");
        if (used_meshes.size() != meshes.size() || used_mie.size() != mie.size()) {
            throw std::invalid_argument("Unreferenced required typed resource");
        }
        const ValidationReport validation = validate_scene_ir_archive(archive, limits);
        if (!validation.ok()) {
            LoadResult<NativeSceneArchive> result;
            result.diagnostics = validation.diagnostics;
            return result;
        }
        LoadResult<NativeSceneArchive> result;
        result.value = std::move(archive);
        return result;
    } catch (const std::exception& error) {
        return graph_failure<NativeSceneArchive>(error.what());
    }
}

}
