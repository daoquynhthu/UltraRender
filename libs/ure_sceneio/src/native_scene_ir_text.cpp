#include <algorithm>
#include <memory>
#include <span>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <ure/native_scene_hash.hpp>
#include <ure/native_scene_text.hpp>

#include "native_scene_ir_internal.hpp"

namespace ure::native_scene {
namespace {

using Json = nlohmann::ordered_json;

Json vec2(const core::Vec2f& value) { return Json::array({value.x, value.y}); }
Json vec3(const core::Vec3f& value) { return Json::array({value.x, value.y, value.z}); }
Json quat(const core::Quat& value) { return Json::array({value.w, value.x, value.y, value.z}); }

core::Vec2f read_vec2(const Json& value) {
    if (!value.is_array() || value.size() != 2) throw std::invalid_argument("Expected Vec2 array");
    return {value[0].get<float>(), value[1].get<float>()};
}

core::Vec3f read_vec3(const Json& value) {
    if (!value.is_array() || value.size() != 3) throw std::invalid_argument("Expected Vec3 array");
    return {value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
}

core::Quat read_quat(const Json& value) {
    if (!value.is_array() || value.size() != 4) throw std::invalid_argument("Expected quaternion array");
    return {value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>()};
}

template <typename T>
std::string object_id(const std::unordered_map<const T*, std::string>& ids,
                      const std::shared_ptr<T>& value) {
    if (!value) return {};
    const auto found = ids.find(value.get());
    if (found == ids.end()) throw std::invalid_argument("Unregistered SceneIR reference");
    return found->second;
}

template <typename T>
std::shared_ptr<T> object_ref(const std::unordered_map<std::string, std::shared_ptr<T>>& values,
                              const Json& value) {
    const std::string id = value.get<std::string>();
    if (id.empty()) return {};
    const auto found = values.find(id);
    if (found == values.end()) throw std::invalid_argument("Dangling SceneIR reference");
    return found->second;
}

Json mie_ref(const detail::EncodedResources& resources,
             const std::shared_ptr<const scene_ir::MiePhaseResource>& value) {
    if (!value) return nullptr;
    const auto found = resources.mie.find(value.get());
    if (found == resources.mie.end()) throw std::invalid_argument("Missing Mie resource");
    return Json{{"content_hash", found->second.content_hash}, {"id", found->second.id}};
}

std::shared_ptr<const scene_ir::MiePhaseResource> read_mie_ref(
    const Json& value,
    const std::unordered_map<std::string, std::shared_ptr<const scene_ir::MiePhaseResource>>& resources,
    const std::unordered_map<std::string, std::string>& hashes,
    std::set<std::string>& used) {
    if (value.is_null()) return {};
    const std::string id = value.at("id").get<std::string>();
    const auto found = resources.find(id);
    const auto hash = hashes.find(id);
    if (found == resources.end() || hash == hashes.end() ||
        value.at("content_hash").get<std::string>() != hash->second) {
        throw std::invalid_argument("Dangling or mismatched Mie resource");
    }
    used.insert(id);
    return found->second;
}

template <typename T>
LoadResult<T> text_failure(std::string message) {
    LoadResult<T> result;
    result.diagnostics.push_back({"URE-Q3-TEXT-001", DiagnosticSeverity::Error, "scene_ir",
                                  std::move(message), {}});
    return result;
}

}

ExplodedSceneArchive write_scene_ir_text(const NativeSceneArchive& archive) {
    const ValidationReport validation = validate_scene_ir_archive(archive);
    if (!validation.ok()) throw std::invalid_argument(validation.diagnostics.front().message);
    if (archive.procedural_graph) {
        const ValidationReport graph_validation = validate_procedural_graph(*archive.procedural_graph, archive);
        if (!graph_validation.ok()) throw std::invalid_argument(graph_validation.diagnostics.front().message);
    }
    if (archive.resource_catalog) {
        if (std::ranges::none_of(archive.document.features, [](const FeatureDeclaration& feature) { return feature.name == kResourceCatalogFeature && feature.requirement == RequirementLevel::Required; })) {
            throw std::invalid_argument("Resource catalog requires ure.scene.resource feature declaration");
        }
        const ValidationReport catalog_validation = validate_resource_catalog(*archive.resource_catalog);
        if (!catalog_validation.ok()) throw std::invalid_argument(catalog_validation.diagnostics.front().message);
    }
    if (archive.solver_contract && std::ranges::none_of(archive.document.features, [](const FeatureDeclaration& feature) { return feature.name == kSolverContractFeature && feature.requirement == RequirementLevel::Required; })) throw std::invalid_argument("Solver contract requires ure.render.solver feature declaration");
    if (archive.simulation_contract && std::ranges::none_of(archive.document.features, [](const FeatureDeclaration& feature) { return feature.name == kSimulationFeature && feature.requirement == RequirementLevel::Required; })) throw std::invalid_argument("Simulation contract requires ure.scene.simulation feature declaration");
    detail::EncodedResources resources = detail::encode_resources(archive);
    SceneDocument document = archive.document;
    for (const auto& resource : resources.payloads) {
        if (std::ranges::find(document.resources, resource.id, &ResourceDescriptor::id) == document.resources.end()) {
            document.resources.push_back(resource.descriptor);
        }
    }
    Json root;
    root["document"] = Json::parse(write_scene_text(document));
    root["kind"] = "ure_scene_ir";
    root["version"] = 1;
    Json graph;

    std::unordered_map<const scene_ir::MaterialNode*, std::string> material_ids;
    std::unordered_map<const scene_ir::MeshResource*, std::string> mesh_ids;
    std::unordered_map<const scene_ir::ImageResource*, std::string> image_ids;
    std::unordered_map<const scene_ir::TextureResource*, std::string> texture_ids;
    for (std::size_t i = 0; i < archive.scene.materials.size(); ++i) material_ids.emplace(archive.scene.materials[i].get(), archive.source_ids.materials[i]);
    for (std::size_t i = 0; i < archive.scene.meshes.size(); ++i) mesh_ids.emplace(archive.scene.meshes[i].get(), archive.source_ids.meshes[i]);
    for (std::size_t i = 0; i < archive.scene.images.size(); ++i) image_ids.emplace(archive.scene.images[i].get(), archive.source_ids.images[i]);
    for (std::size_t i = 0; i < archive.scene.textures.size(); ++i) texture_ids.emplace(archive.scene.textures[i].get(), archive.source_ids.textures[i]);

    graph["images"] = Json::array();
    for (std::size_t i = 0; i < archive.scene.images.size(); ++i) {
        const auto& value = *archive.scene.images[i];
        graph["images"].push_back({{"color_space", static_cast<int>(value.color_space)}, {"id", archive.source_ids.images[i]},
                                    {"name", value.name}, {"uri", value.uri}});
    }
    graph["textures"] = Json::array();
    for (std::size_t i = 0; i < archive.scene.textures.size(); ++i) {
        const auto& value = *archive.scene.textures[i];
        graph["textures"].push_back({{"id", archive.source_ids.textures[i]}, {"image_id", object_id(image_ids, value.image)},
                                      {"name", value.name}, {"uv_set", value.uv_set}});
    }
    graph["materials"] = Json::array();
    for (std::size_t i = 0; i < archive.scene.materials.size(); ++i) {
        const auto& value = *archive.scene.materials[i];
        Json material{{"base_color", vec3(value.base_color)},
                      {"base_color_texture_id", object_id(texture_ids, value.base_color_texture)},
                      {"dispersion", value.dispersion}, {"emission", vec3(value.emission)},
                      {"emission_texture_id", object_id(texture_ids, value.emission_texture)},
                      {"id", archive.source_ids.materials[i]}, {"ior", value.ior},
                      {"medium_absorption", vec3(value.medium_absorption)}, {"medium_anisotropy", value.medium_anisotropy},
                      {"medium_density", value.medium_density}, {"medium_mie", mie_ref(resources, value.medium_mie_resource)},
                      {"medium_phase", static_cast<int>(value.medium_phase)}, {"medium_scattering", vec3(value.medium_scattering)},
                      {"metal_eta", vec3(value.metal_eta)}, {"metal_k", vec3(value.metal_k)},
                      {"model", static_cast<int>(value.model)}, {"name", value.name}, {"normal_scale", value.normal_scale},
                      {"normal_texture_id", object_id(texture_ids, value.normal_texture)}, {"roughness", value.roughness},
                      {"roughness_texture_id", object_id(texture_ids, value.roughness_texture)},
                      {"thin_film_ior", value.thin_film_ior}, {"thin_film_thickness", value.thin_film_thickness}};
        material["spectral_extension"] = value.spectral_extension
            ? Json{{"albedo_spd", value.spectral_extension->albedo_spd},
                   {"emission_spd", value.spectral_extension->emission_spd},
                   {"spectral_bands", value.spectral_extension->spectral_bands}}
            : Json(nullptr);
        if (value.graph) {
            Json material_graph{{"nodes", Json::array()}, {"output_node_id", value.graph->output_node_id}};
            for (const auto& node : value.graph->nodes) {
                Json inputs = Json::array();
                for (const auto& input : node.inputs) inputs.push_back({{"name", input.name}, {"node_id", input.node_id}, {"output", input.output}});
                Json node_json{{"color", vec3(node.color)}, {"id", node.id},
                               {"inputs", std::move(inputs)}, {"kind", static_cast<int>(node.kind)},
                               {"name", node.name}, {"texture_id", object_id(texture_ids, node.texture)},
                               {"value", node.value}};
                if (node.kind >= scene_ir::MaterialGraphNodeKind::BsdfGrating &&
                    node.kind <= scene_ir::MaterialGraphNodeKind::BsdfScatteringTable) {
                    if (node.diffraction.table.size() >
                        scene_ir::kMaxDiffractiveScatteringEntries) {
                        throw std::invalid_argument(
                            "Diffractive scattering table exceeds the text schema budget");
                    }
                    Json table = Json::array();
                    for (const auto& entry : node.diffraction.table) {
                        table.push_back({
                            {"incident_cosine", entry.incident_cosine},
                            {"jones_pp", {entry.jones_pp.real, entry.jones_pp.imag}},
                            {"jones_ps", {entry.jones_ps.real, entry.jones_ps.imag}},
                            {"jones_sp", {entry.jones_sp.real, entry.jones_sp.imag}},
                            {"jones_ss", {entry.jones_ss.real, entry.jones_ss.imag}},
                            {"order", entry.order},
                            {"side", static_cast<int>(entry.side)},
                            {"wavelength_nm", entry.wavelength_nm}});
                    }
                    node_json["diffraction"] = {
                        {"aperture_radius_m", node.diffraction.aperture_radius_m},
                        {"design_wavelength_nm", node.diffraction.design_wavelength_nm},
                        {"duty_cycle", node.diffraction.duty_cycle},
                        {"focal_length_m", node.diffraction.focal_length_m},
                        {"kind", static_cast<int>(node.diffraction.kind)},
                        {"max_order", node.diffraction.max_order},
                        {"orientation_rad", node.diffraction.orientation_rad},
                        {"period_m", node.diffraction.period_m},
                        {"phase_depth_rad", node.diffraction.phase_depth_rad},
                        {"side", static_cast<int>(node.diffraction.side)},
                        {"table", std::move(table)},
                        {"table_id", node.diffraction.table_id}};
                }
                if (node.kind ==
                    scene_ir::MaterialGraphNodeKind::
                        BsdfFluorescence) {
                    if (node.fluorescence
                            .emission_pdf_per_nm.size() >
                        scene_ir::
                            kMaxFluorescenceMatrixEntries) {
                        throw std::invalid_argument(
                            "Fluorescence matrix exceeds the text schema budget");
                    }
                    node_json["fluorescence"] = {
                        {"emission_pdf_per_nm", node.fluorescence.emission_pdf_per_nm},
                        {"emission_wavelengths_nm", node.fluorescence.emission_wavelengths_nm},
                        {"excitation_efficiency", node.fluorescence.excitation_efficiency},
                        {"excitation_wavelengths_nm", node.fluorescence.excitation_wavelengths_nm},
                        {"lifetime_seconds", node.fluorescence.lifetime_seconds},
                        {"quantum_yield", node.fluorescence.quantum_yield},
                        {"resource_id", node.fluorescence.resource_id}};
                }
                material_graph["nodes"].push_back(std::move(node_json));
            }
            material["graph"] = std::move(material_graph);
        } else {
            material["graph"] = nullptr;
        }
        graph["materials"].push_back(std::move(material));
    }
    graph["meshes"] = Json::array();
    for (std::size_t i = 0; i < archive.scene.meshes.size(); ++i) {
        const auto& value = *archive.scene.meshes[i];
        const auto payload = resources.meshes.at(value.mesh.get());
        graph["meshes"].push_back({{"id", archive.source_ids.meshes[i]}, {"name", value.name},
                                    {"payload", {{"content_hash", payload.content_hash}, {"id", payload.id}}}});
    }
    graph["instances"] = Json::array();
    for (std::size_t i = 0; i < archive.scene.instances.size(); ++i) {
        const auto& value = archive.scene.instances[i];
        const auto& rigid = value.rigid_body;
        graph["instances"].push_back({{"id", archive.source_ids.instances[i]}, {"material_id", object_id(material_ids, value.material)},
                                      {"mesh_id", object_id(mesh_ids, value.mesh)}, {"name", value.name},
                                      {"position", vec3(value.position)},
                                      {"rigid_body", {{"angular_damping", rigid.angular_damping}, {"collider_radius", rigid.collider_radius},
                                                       {"collider_size", vec3(rigid.collider_size)}, {"collider_type", rigid.collider_type},
                                                       {"enabled", rigid.enabled}, {"friction", rigid.friction},
                                                       {"linear_damping", rigid.linear_damping}, {"mass", rigid.mass},
                                                       {"material_id", rigid.material_id}, {"restitution", rigid.restitution},
                                                       {"velocity", vec3(rigid.velocity)}}},
                                      {"rotation", quat(value.rotation)}, {"scale", vec3(value.scale)}});
    }
    graph["spheres"] = Json::array();
    for (std::size_t i = 0; i < archive.scene.spheres.size(); ++i) {
        const auto& value = archive.scene.spheres[i];
        graph["spheres"].push_back({{"center", vec3(value.center)}, {"id", archive.source_ids.spheres[i]},
                                    {"material_id", object_id(material_ids, value.material)}, {"name", value.name}, {"radius", value.radius}});
    }
    graph["quad_lights"] = Json::array();
    for (std::size_t i = 0; i < archive.scene.quad_lights.size(); ++i) {
        const auto& value = archive.scene.quad_lights[i];
        graph["quad_lights"].push_back({{"corner", vec3(value.corner)}, {"edge_u", vec3(value.edge_u)},
                                        {"edge_v", vec3(value.edge_v)}, {"id", archive.source_ids.quad_lights[i]},
                                        {"material_id", object_id(material_ids, value.material)}, {"name", value.name}});
    }
    graph["camera"] = {{"aperture", archive.scene.camera.aperture}, {"aspect_ratio", archive.scene.camera.aspect_ratio},
                        {"focus_dist", archive.scene.camera.focus_dist}, {"fov", archive.scene.camera.fov},
                        {"look_at", vec3(archive.scene.camera.look_at)}, {"position", vec3(archive.scene.camera.position)},
                        {"up", vec3(archive.scene.camera.up)}};
    const auto& fluid = archive.scene.physics.fluid;
    graph["physics"] = {{"dt", archive.scene.physics.dt}, {"enabled", archive.scene.physics.enabled},
                         {"fluid", {{"bounds_max", vec3(fluid.bounds_max)}, {"bounds_min", vec3(fluid.bounds_min)},
                                     {"enabled", fluid.enabled}, {"fill_max", vec3(fluid.fill_max)},
                                     {"fill_min", vec3(fluid.fill_min)}, {"particle_spacing", fluid.particle_spacing}}},
                         {"spp_per_frame", archive.scene.physics.spp_per_frame}, {"total_frames", archive.scene.physics.total_frames}};
    graph["background_color"] = vec3(archive.scene.background_color);
    graph["height"] = archive.scene.height;
    graph["medium_absorption"] = vec3(archive.scene.medium_absorption);
    graph["medium_anisotropy"] = archive.scene.medium_anisotropy;
    graph["medium_density"] = archive.scene.medium_density;
    graph["medium_max_distance"] = archive.scene.medium_max_distance;
    graph["medium_mie"] = mie_ref(resources, archive.scene.medium_mie_resource);
    graph["medium_phase"] = static_cast<int>(archive.scene.medium_phase);
    graph["medium_scattering"] = vec3(archive.scene.medium_scattering);
    graph["spp"] = archive.scene.spp;
    graph["width"] = archive.scene.width;
    root["scene_ir"] = std::move(graph);
    if (archive.procedural_graph) {
        root["procedural_graph"] = Json::parse(detail::write_procedural_graph_text(*archive.procedural_graph));
    }
    if (archive.resource_catalog) root["resource_catalog"] = Json::parse(write_resource_catalog_text(*archive.resource_catalog));
    if (archive.solver_contract) root["solver_contract"] = Json::parse(write_solver_contract_text(*archive.solver_contract));
    if (archive.simulation_contract) root["simulation_contract"] = Json::parse(write_simulation_contract_text(*archive.simulation_contract));
    ExplodedSceneArchive result;
    result.manifest = root.dump(2) + "\n";
    result.resources = std::move(resources.payloads);
    return result;
}

LoadResult<NativeSceneArchive> read_scene_ir_text(
    const ExplodedSceneArchive& archive,
    const CapabilityRegistry& registry,
    const ValidationLimits& limits) {
    try {
        if (archive.manifest.starts_with("\xef\xbb\xbf")) throw std::invalid_argument("UTF-8 BOM is not canonical");
        const Json root = Json::parse(archive.manifest);
        if (root.at("kind") != "ure_scene_ir" || root.at("version") != 1) throw std::invalid_argument("Invalid Q.3 text identity");
        const auto document = read_scene_text(root.at("document").dump() + "\n", registry, limits);
        if (!document.value) {
            LoadResult<NativeSceneArchive> result;
            result.diagnostics = document.diagnostics;
            return result;
        }
        std::unordered_map<std::string, std::shared_ptr<Mesh>> mesh_payloads;
        std::unordered_map<std::string, std::shared_ptr<const scene_ir::MiePhaseResource>> mie_payloads;
        std::unordered_map<std::string, std::string> resource_hashes;
        std::set<std::string> used_resources;
        std::unordered_map<std::string, const NamedResourcePayload*> payloads;
        for (const auto& payload : archive.resources) {
            if (!payloads.emplace(payload.id, &payload).second || payload.id != payload.descriptor.id ||
                payload.payload.size() != payload.descriptor.byte_length || sha256_hex(payload.payload) != payload.descriptor.content_hash) {
                throw std::invalid_argument("Invalid or duplicate exploded resource");
            }
            resource_hashes.emplace(payload.id, payload.descriptor.content_hash);
            if (payload.descriptor.kind == ResourceKind::Geometry) {
                auto decoded = detail::decode_mesh_payload(payload.payload, limits);
                if (!decoded.value) throw std::invalid_argument("Invalid mesh payload");
                mesh_payloads.emplace(payload.id, *decoded.value);
            } else if (payload.descriptor.kind == ResourceKind::MiePhase) {
                auto decoded = detail::decode_mie_payload(payload.payload, limits);
                if (!decoded.value) throw std::invalid_argument("Invalid Mie payload");
                mie_payloads.emplace(payload.id, *decoded.value);
            } else {
                throw std::invalid_argument("Unexpected exploded resource kind");
            }
        }

        const Json& graph = root.at("scene_ir");
        NativeSceneArchive result;
        result.document = *document.value;
        std::unordered_map<std::string, std::shared_ptr<scene_ir::ImageResource>> images;
        for (const auto& source : graph.at("images")) {
            auto value = std::make_shared<scene_ir::ImageResource>();
            value->name = source.at("name").get<std::string>();
            value->uri = source.at("uri").get<std::string>();
            const int color_space = source.at("color_space").get<int>();
            if (color_space < 0 || color_space > 1) throw std::invalid_argument("Invalid image color space");
            value->color_space = static_cast<scene_ir::ImageColorSpace>(color_space);
            const std::string id = source.at("id").get<std::string>();
            if (!images.emplace(id, value).second) throw std::invalid_argument("Duplicate image ID");
            result.source_ids.images.push_back(id);
            result.scene.images.push_back(std::move(value));
        }
        std::unordered_map<std::string, std::shared_ptr<scene_ir::TextureResource>> textures;
        for (const auto& source : graph.at("textures")) {
            auto value = std::make_shared<scene_ir::TextureResource>();
            value->name = source.at("name").get<std::string>();
            value->image = object_ref(images, source.at("image_id"));
            value->uv_set = source.at("uv_set").get<int>();
            const std::string id = source.at("id").get<std::string>();
            if (!textures.emplace(id, value).second) throw std::invalid_argument("Duplicate texture ID");
            result.source_ids.textures.push_back(id);
            result.scene.textures.push_back(std::move(value));
        }
        std::unordered_map<std::string, std::shared_ptr<scene_ir::MaterialNode>> materials;
        for (const auto& source : graph.at("materials")) {
            auto value = std::make_shared<scene_ir::MaterialNode>();
            const int model = source.at("model").get<int>();
            const int medium_phase = source.at("medium_phase").get<int>();
            if (model < 0 || model > 4 || medium_phase < 0 || medium_phase > 2) throw std::invalid_argument("Invalid material enum");
            value->name = source.at("name").get<std::string>();
            value->model = static_cast<scene_ir::MaterialModel>(model);
            value->base_color = read_vec3(source.at("base_color"));
            value->roughness = source.at("roughness").get<float>();
            value->ior = source.at("ior").get<float>();
            value->dispersion = source.at("dispersion").get<float>();
            value->metal_eta = read_vec3(source.at("metal_eta"));
            value->metal_k = read_vec3(source.at("metal_k"));
            value->thin_film_thickness = source.at("thin_film_thickness").get<float>();
            value->thin_film_ior = source.at("thin_film_ior").get<float>();
            value->emission = read_vec3(source.at("emission"));
            value->medium_density = source.at("medium_density").get<float>();
            value->medium_anisotropy = source.at("medium_anisotropy").get<float>();
            value->medium_phase = static_cast<scene_ir::VolumePhaseFunction>(medium_phase);
            value->medium_mie_resource = read_mie_ref(source.at("medium_mie"), mie_payloads, resource_hashes, used_resources);
            value->medium_scattering = read_vec3(source.at("medium_scattering"));
            value->medium_absorption = read_vec3(source.at("medium_absorption"));
            value->base_color_texture = object_ref(textures, source.at("base_color_texture_id"));
            value->roughness_texture = object_ref(textures, source.at("roughness_texture_id"));
            value->emission_texture = object_ref(textures, source.at("emission_texture_id"));
            value->normal_texture = object_ref(textures, source.at("normal_texture_id"));
            value->normal_scale = source.at("normal_scale").get<float>();
            if (!source.at("spectral_extension").is_null()) {
                value->spectral_extension = std::make_shared<scene_ir::SpectralMaterialExtension>();
                value->spectral_extension->spectral_bands = source.at("spectral_extension").at("spectral_bands").get<int>();
                value->spectral_extension->albedo_spd = source.at("spectral_extension").at("albedo_spd").get<std::string>();
                value->spectral_extension->emission_spd = source.at("spectral_extension").at("emission_spd").get<std::string>();
            }
            if (!source.at("graph").is_null()) {
                value->graph = std::make_shared<scene_ir::MaterialGraph>();
                value->graph->output_node_id = source.at("graph").at("output_node_id").get<std::uint32_t>();
                for (const auto& source_node : source.at("graph").at("nodes")) {
                    scene_ir::MaterialGraphNode node;
                    const int kind = source_node.at("kind").get<int>();
                    if (kind < 0 || kind > 20) throw std::invalid_argument("Invalid graph node kind");
                    node.id = source_node.at("id").get<std::uint32_t>();
                    node.kind = static_cast<scene_ir::MaterialGraphNodeKind>(kind);
                    node.name = source_node.at("name").get<std::string>();
                    node.color = read_vec3(source_node.at("color"));
                    node.value = source_node.at("value").get<float>();
                    node.texture = object_ref(textures, source_node.at("texture_id"));
                    if (kind >= 15 && kind <= 19) {
                        const auto& diffraction = source_node.at("diffraction");
                        const int diffraction_kind = diffraction.at("kind").get<int>();
                        const int diffraction_side = diffraction.at("side").get<int>();
                        if (diffraction_kind < 0 || diffraction_kind > 4 ||
                            diffraction_side < 0 || diffraction_side > 1) {
                            throw std::invalid_argument("Invalid diffractive operator enum");
                        }
                        node.diffraction.kind =
                            static_cast<scene_ir::DiffractiveOperatorKind>(diffraction_kind);
                        node.diffraction.side =
                            static_cast<scene_ir::DiffractiveScatterSide>(diffraction_side);
                        node.diffraction.period_m = diffraction.at("period_m").get<double>();
                        node.diffraction.orientation_rad = diffraction.at("orientation_rad").get<double>();
                        node.diffraction.duty_cycle = diffraction.at("duty_cycle").get<double>();
                        node.diffraction.phase_depth_rad = diffraction.at("phase_depth_rad").get<double>();
                        node.diffraction.design_wavelength_nm = diffraction.at("design_wavelength_nm").get<double>();
                        node.diffraction.focal_length_m = diffraction.at("focal_length_m").get<double>();
                        node.diffraction.aperture_radius_m = diffraction.at("aperture_radius_m").get<double>();
                        node.diffraction.max_order = diffraction.at("max_order").get<int>();
                        node.diffraction.table_id = diffraction.at("table_id").get<std::string>();
                        if (diffraction.at("table").size() >
                            scene_ir::kMaxDiffractiveScatteringEntries) {
                            throw std::invalid_argument(
                                "Diffractive scattering table exceeds the text schema budget");
                        }
                        for (const auto& source_entry : diffraction.at("table")) {
                            scene_ir::DiffractiveScatteringEntry entry;
                            entry.wavelength_nm = source_entry.at("wavelength_nm").get<float>();
                            entry.incident_cosine = source_entry.at("incident_cosine").get<float>();
                            entry.order = source_entry.at("order").get<int>();
                            const int side = source_entry.at("side").get<int>();
                            if (side < 0 || side > 1) throw std::invalid_argument("Invalid diffractive table side");
                            entry.side = static_cast<scene_ir::DiffractiveScatterSide>(side);
                            auto coefficient = [](const Json& source) {
                                return scene_ir::ComplexCoefficient{
                                    source.at(0).get<float>(),
                                    source.at(1).get<float>()};
                            };
                            entry.jones_ss = coefficient(source_entry.at("jones_ss"));
                            entry.jones_sp = coefficient(source_entry.at("jones_sp"));
                            entry.jones_ps = coefficient(source_entry.at("jones_ps"));
                            entry.jones_pp = coefficient(source_entry.at("jones_pp"));
                            node.diffraction.table.push_back(entry);
                        }
                    }
                    if (kind == 20) {
                        const auto& fluorescence =
                            source_node.at("fluorescence");
                        node.fluorescence.resource_id =
                            fluorescence.at("resource_id").get<std::string>();
                        node.fluorescence.excitation_wavelengths_nm =
                            fluorescence.at("excitation_wavelengths_nm").get<std::vector<float>>();
                        node.fluorescence.emission_wavelengths_nm =
                            fluorescence.at("emission_wavelengths_nm").get<std::vector<float>>();
                        node.fluorescence.excitation_efficiency =
                            fluorescence.at("excitation_efficiency").get<std::vector<float>>();
                        node.fluorescence.quantum_yield =
                            fluorescence.at("quantum_yield").get<std::vector<float>>();
                        node.fluorescence.emission_pdf_per_nm =
                            fluorescence.at("emission_pdf_per_nm").get<std::vector<float>>();
                        node.fluorescence.lifetime_seconds =
                            fluorescence.at("lifetime_seconds").get<double>();
                        if (node.fluorescence.emission_pdf_per_nm.size() >
                            scene_ir::kMaxFluorescenceMatrixEntries) {
                            throw std::invalid_argument(
                                "Fluorescence matrix exceeds the text schema budget");
                        }
                    }
                    for (const auto& source_input : source_node.at("inputs")) {
                        node.inputs.push_back({source_input.at("name").get<std::string>(),
                                               source_input.at("node_id").get<std::uint32_t>(),
                                               source_input.at("output").get<std::string>()});
                    }
                    value->graph->nodes.push_back(std::move(node));
                }
                value->graph->validate();
            }
            const std::string id = source.at("id").get<std::string>();
            if (!materials.emplace(id, value).second) throw std::invalid_argument("Duplicate material ID");
            result.source_ids.materials.push_back(id);
            result.scene.materials.push_back(std::move(value));
        }
        std::unordered_map<std::string, std::shared_ptr<scene_ir::MeshResource>> meshes;
        for (const auto& source : graph.at("meshes")) {
            const std::string payload_id = source.at("payload").at("id").get<std::string>();
            const auto payload = mesh_payloads.find(payload_id);
            const auto payload_hash = resource_hashes.find(payload_id);
            if (payload == mesh_payloads.end() || payload_hash == resource_hashes.end() ||
                source.at("payload").at("content_hash").get<std::string>() != payload_hash->second) {
                throw std::invalid_argument("Dangling or mismatched mesh payload");
            }
            used_resources.insert(payload_id);
            auto value = std::make_shared<scene_ir::MeshResource>();
            value->name = source.at("name").get<std::string>();
            value->mesh = payload->second;
            const std::string id = source.at("id").get<std::string>();
            if (!meshes.emplace(id, value).second) throw std::invalid_argument("Duplicate mesh ID");
            result.source_ids.meshes.push_back(id);
            result.scene.meshes.push_back(std::move(value));
        }
        for (const auto& source : graph.at("instances")) {
            scene_ir::InstanceNode value;
            value.name = source.at("name").get<std::string>();
            value.mesh = object_ref(meshes, source.at("mesh_id"));
            value.material = object_ref(materials, source.at("material_id"));
            value.position = read_vec3(source.at("position"));
            value.scale = read_vec3(source.at("scale"));
            value.rotation = read_quat(source.at("rotation"));
            const auto& rigid = source.at("rigid_body");
            value.rigid_body.enabled = rigid.at("enabled").get<bool>();
            value.rigid_body.mass = rigid.at("mass").get<float>();
            value.rigid_body.friction = rigid.at("friction").get<float>();
            value.rigid_body.restitution = rigid.at("restitution").get<float>();
            value.rigid_body.linear_damping = rigid.at("linear_damping").get<float>();
            value.rigid_body.angular_damping = rigid.at("angular_damping").get<float>();
            value.rigid_body.velocity = read_vec3(rigid.at("velocity"));
            value.rigid_body.collider_type = rigid.at("collider_type").get<std::string>();
            value.rigid_body.collider_size = read_vec3(rigid.at("collider_size"));
            value.rigid_body.collider_radius = rigid.at("collider_radius").get<float>();
            value.rigid_body.material_id = rigid.at("material_id").get<int>();
            result.source_ids.instances.push_back(source.at("id").get<std::string>());
            result.scene.instances.push_back(std::move(value));
        }
        for (const auto& source : graph.at("spheres")) {
            result.source_ids.spheres.push_back(source.at("id").get<std::string>());
            result.scene.spheres.push_back({source.at("name").get<std::string>(), read_vec3(source.at("center")),
                                            source.at("radius").get<float>(), object_ref(materials, source.at("material_id"))});
        }
        for (const auto& source : graph.at("quad_lights")) {
            result.source_ids.quad_lights.push_back(source.at("id").get<std::string>());
            result.scene.quad_lights.push_back({source.at("name").get<std::string>(), read_vec3(source.at("corner")),
                                                 read_vec3(source.at("edge_u")), read_vec3(source.at("edge_v")),
                                                 object_ref(materials, source.at("material_id"))});
        }
        const auto& camera = graph.at("camera");
        result.scene.camera.position = read_vec3(camera.at("position"));
        result.scene.camera.look_at = read_vec3(camera.at("look_at"));
        result.scene.camera.up = read_vec3(camera.at("up"));
        result.scene.camera.fov = camera.at("fov").get<float>();
        result.scene.camera.aspect_ratio = camera.at("aspect_ratio").get<float>();
        result.scene.camera.aperture = camera.at("aperture").get<float>();
        result.scene.camera.focus_dist = camera.at("focus_dist").get<float>();
        const auto& physics = graph.at("physics");
        result.scene.physics.enabled = physics.at("enabled").get<bool>();
        result.scene.physics.dt = physics.at("dt").get<float>();
        result.scene.physics.total_frames = physics.at("total_frames").get<int>();
        result.scene.physics.spp_per_frame = physics.at("spp_per_frame").get<int>();
        const auto& fluid = physics.at("fluid");
        result.scene.physics.fluid.enabled = fluid.at("enabled").get<bool>();
        result.scene.physics.fluid.bounds_min = read_vec3(fluid.at("bounds_min"));
        result.scene.physics.fluid.bounds_max = read_vec3(fluid.at("bounds_max"));
        result.scene.physics.fluid.particle_spacing = fluid.at("particle_spacing").get<float>();
        result.scene.physics.fluid.fill_min = read_vec3(fluid.at("fill_min"));
        result.scene.physics.fluid.fill_max = read_vec3(fluid.at("fill_max"));
        result.scene.background_color = read_vec3(graph.at("background_color"));
        result.scene.medium_density = graph.at("medium_density").get<float>();
        result.scene.medium_anisotropy = graph.at("medium_anisotropy").get<float>();
        const int scene_phase = graph.at("medium_phase").get<int>();
        if (scene_phase < 0 || scene_phase > 2) throw std::invalid_argument("Invalid scene phase function");
        result.scene.medium_phase = static_cast<scene_ir::VolumePhaseFunction>(scene_phase);
        result.scene.medium_mie_resource = read_mie_ref(graph.at("medium_mie"), mie_payloads, resource_hashes, used_resources);
        result.scene.medium_scattering = read_vec3(graph.at("medium_scattering"));
        result.scene.medium_absorption = read_vec3(graph.at("medium_absorption"));
        result.scene.medium_max_distance = graph.at("medium_max_distance").get<float>();
        result.scene.width = graph.at("width").get<int>();
        result.scene.height = graph.at("height").get<int>();
        result.scene.spp = graph.at("spp").get<int>();
        if (root.contains("procedural_graph")) {
            auto procedural = detail::read_procedural_graph_text(root.at("procedural_graph").dump(), limits);
            if (!procedural.value) throw std::invalid_argument("Invalid procedural graph text projection");
            result.procedural_graph = std::move(*procedural.value);
            const ValidationReport graph_validation = validate_procedural_graph(*result.procedural_graph, result);
            if (!graph_validation.ok()) throw std::invalid_argument(graph_validation.diagnostics.front().message);
        }
        if (root.contains("resource_catalog")) {
            auto catalog = read_resource_catalog_text(root.at("resource_catalog").dump(), limits);
            if (!catalog.value) throw std::invalid_argument("Invalid resource catalog text projection");
            result.resource_catalog = std::make_shared<const NativeResourceCatalog>(std::move(*catalog.value));
            if (std::ranges::none_of(result.document.features, [](const FeatureDeclaration& feature) { return feature.name == kResourceCatalogFeature && feature.requirement == RequirementLevel::Required; })) {
                throw std::invalid_argument("Resource catalog lacks required ure.scene.resource feature declaration");
            }
        }
        if (root.contains("solver_contract")) { auto solver = read_solver_contract_text(root.at("solver_contract").dump()); if (!solver.value) throw std::invalid_argument("Invalid solver contract text projection"); result.solver_contract = std::make_shared<const NativeSolverContract>(std::move(*solver.value)); if (std::ranges::none_of(result.document.features, [](const FeatureDeclaration& feature) { return feature.name == kSolverContractFeature && feature.requirement == RequirementLevel::Required; })) throw std::invalid_argument("Solver contract lacks required feature declaration"); }
        if (root.contains("simulation_contract")) { auto simulation = read_simulation_contract_text(root.at("simulation_contract").dump()); if (!simulation.value) throw std::invalid_argument("Invalid simulation contract text projection"); result.simulation_contract = std::make_shared<const NativeSimulationContract>(std::move(*simulation.value)); if (std::ranges::none_of(result.document.features, [](const FeatureDeclaration& feature) { return feature.name == kSimulationFeature && feature.requirement == RequirementLevel::Required; })) throw std::invalid_argument("Simulation contract lacks required feature declaration"); }
        if (used_resources.size() != archive.resources.size()) {
            throw std::invalid_argument("Unreferenced required exploded resource");
        }
        const ValidationReport validation = validate_scene_ir_archive(result, limits);
        if (!validation.ok()) {
            LoadResult<NativeSceneArchive> failed;
            failed.diagnostics = validation.diagnostics;
            return failed;
        }
        LoadResult<NativeSceneArchive> loaded;
        loaded.value = std::move(result);
        loaded.diagnostics = document.diagnostics;
        return loaded;
    } catch (const std::exception& error) {
        return text_failure<NativeSceneArchive>(error.what());
    }
}

}
