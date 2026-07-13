#include <memory>
#include <span>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/verifier.h>

#include "native_scene_ir_internal.hpp"
#include "ure_scene_ir_v1_generated.h"

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
    if (raw > static_cast<std::uint8_t>(scene_ir::MaterialGraphNodeKind::OutputSurface)) {
        throw std::invalid_argument("Invalid material graph node kind");
    }
    return static_cast<schema::MaterialGraphNodeKind>(raw);
}

scene_ir::MaterialGraphNodeKind graph_kind(schema::MaterialGraphNodeKind value) {
    const auto raw = static_cast<std::uint8_t>(value);
    if (raw > static_cast<std::uint8_t>(schema::MaterialGraphNodeKind::OutputSurface)) {
        throw std::invalid_argument("Invalid material graph node kind");
    }
    return static_cast<scene_ir::MaterialGraphNodeKind>(raw);
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
    for (std::size_t index = 0; index < archive.scene.materials.size(); ++index) material_ids.emplace(archive.scene.materials[index].get(), archive.source_ids.materials[index]);
    for (std::size_t index = 0; index < archive.scene.meshes.size(); ++index) mesh_ids.emplace(archive.scene.meshes[index].get(), archive.source_ids.meshes[index]);
    for (std::size_t index = 0; index < archive.scene.images.size(); ++index) image_ids.emplace(archive.scene.images[index].get(), archive.source_ids.images[index]);
    for (std::size_t index = 0; index < archive.scene.textures.size(); ++index) texture_ids.emplace(archive.scene.textures[index].get(), archive.source_ids.textures[index]);

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
    flatbuffers::Verifier verifier(bytes.data(), bytes.size());
    if (!schema::VerifySceneGraphBuffer(verifier)) return graph_failure<NativeSceneArchive>("Invalid URIG payload");
    try {
        std::unique_ptr<schema::SceneGraphT> graph(schema::GetSceneGraph(bytes.data())->UnPack());
        NativeSceneArchive archive;
        archive.document = document;
        std::unordered_map<std::string, std::shared_ptr<scene_ir::ImageResource>> images_by_id;
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
            archive.scene.images.push_back(image);
            images_by_id.emplace(source->id, std::move(image));
        }
        std::unordered_map<std::string, std::shared_ptr<scene_ir::TextureResource>> textures_by_id;
        for (const auto& source : graph->textures) {
            if (!source || textures_by_id.contains(source->id)) throw std::invalid_argument("Invalid texture identity");
            auto texture = std::make_shared<scene_ir::TextureResource>();
            texture->name = source->name;
            texture->image = lookup(images_by_id, source->image_id);
            texture->uv_set = source->uv_set;
            archive.source_ids.textures.push_back(source->id);
            archive.scene.textures.push_back(texture);
            textures_by_id.emplace(source->id, std::move(texture));
        }
        std::unordered_map<std::string, std::shared_ptr<scene_ir::MaterialNode>> materials_by_id;
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
            material->base_color_texture = lookup(textures_by_id, source->base_color_texture_id);
            material->roughness_texture = lookup(textures_by_id, source->roughness_texture_id);
            material->emission_texture = lookup(textures_by_id, source->emission_texture_id);
            material->normal_texture = lookup(textures_by_id, source->normal_texture_id);
            material->normal_scale = source->normal_scale;
            if (source->spectral_extension) {
                material->spectral_extension = std::make_shared<scene_ir::SpectralMaterialExtension>();
                material->spectral_extension->spectral_bands = source->spectral_extension->spectral_bands;
                material->spectral_extension->albedo_spd = source->spectral_extension->albedo_spd;
                material->spectral_extension->emission_spd = source->spectral_extension->emission_spd;
            }
            if (source->graph) material->graph = decode_graph(*source->graph, textures_by_id);
            archive.source_ids.materials.push_back(source->id);
            archive.scene.materials.push_back(material);
            materials_by_id.emplace(source->id, std::move(material));
        }
        std::unordered_map<std::string, std::shared_ptr<scene_ir::MeshResource>> meshes_by_id;
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
            archive.scene.meshes.push_back(mesh);
            meshes_by_id.emplace(source->id, std::move(mesh));
        }
        for (const auto& source : graph->instances) {
            if (!source) throw std::invalid_argument("Null instance");
            scene_ir::InstanceNode instance;
            instance.name = source->name;
            instance.mesh = lookup(meshes_by_id, source->mesh_id);
            instance.material = lookup(materials_by_id, source->material_id);
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
            archive.scene.instances.push_back(std::move(instance));
        }
        for (const auto& source : graph->spheres) {
            if (!source) throw std::invalid_argument("Null sphere");
            archive.source_ids.spheres.push_back(source->id);
            archive.scene.spheres.push_back({source->name, vec3(source->center), source->radius,
                                             lookup(materials_by_id, source->material_id)});
        }
        for (const auto& source : graph->quad_lights) {
            if (!source) throw std::invalid_argument("Null quad light");
            archive.source_ids.quad_lights.push_back(source->id);
            archive.scene.quad_lights.push_back({source->name, vec3(source->corner), vec3(source->edge_u),
                                                  vec3(source->edge_v), lookup(materials_by_id, source->material_id)});
        }
        if (graph->camera) {
            archive.scene.camera.position = vec3(graph->camera->position);
            archive.scene.camera.look_at = vec3(graph->camera->look_at);
            archive.scene.camera.up = vec3(graph->camera->up);
            archive.scene.camera.fov = graph->camera->fov;
            archive.scene.camera.aspect_ratio = graph->camera->aspect_ratio;
            archive.scene.camera.aperture = graph->camera->aperture;
            archive.scene.camera.focus_dist = graph->camera->focus_dist;
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
