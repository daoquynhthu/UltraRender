#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <ranges>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include <ure/native_scene_uuid.hpp>
#include <ure/mie_phase_validation.hpp>
#include <ure/product/product_material.hpp>
#include <ure/wave_optics.hpp>
#include <nlohmann/json.hpp>

namespace ure::product {
namespace {

constexpr std::uint32_t kInvalidGraph = 700;
constexpr std::uint32_t kUnsupportedNode = 701;
constexpr std::uint32_t kMissingResource = 702;
constexpr std::uint32_t kSpectralDomainMismatch = 703;
constexpr std::uint32_t kWaveContractInvalid = 704;

template <typename T>
void append_value(std::vector<std::uint8_t>& bytes, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    const auto* begin = reinterpret_cast<const std::uint8_t*>(&value);
    bytes.insert(bytes.end(), begin, begin + sizeof(value));
}

void append_text(std::vector<std::uint8_t>& bytes, std::string_view value) {
    append_value(bytes, static_cast<std::uint64_t>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
}

scene_ir::MaterialGraphNodeId add_color(scene_ir::MaterialGraph& graph,
                                        core::Vec3f value) {
    scene_ir::MaterialGraphNode node;
    node.kind = scene_ir::MaterialGraphNodeKind::ConstantColor;
    node.color = value;
    return graph.add_node(std::move(node));
}

scene_ir::MaterialGraphNodeId add_float(scene_ir::MaterialGraph& graph,
                                        float value) {
    scene_ir::MaterialGraphNode node;
    node.kind = scene_ir::MaterialGraphNodeKind::ConstantFloat;
    node.value = value;
    return graph.add_node(std::move(node));
}

scene_ir::MaterialGraphNodeId add_texture(
    scene_ir::MaterialGraph& graph,
    const std::shared_ptr<scene_ir::TextureResource>& texture) {
    scene_ir::MaterialGraphNode node;
    node.kind = scene_ir::MaterialGraphNodeKind::Texture2D;
    node.texture = texture;
    return graph.add_node(std::move(node));
}

scene_ir::MaterialGraphNodeId multiply(
    scene_ir::MaterialGraph& graph,
    scene_ir::MaterialGraphNodeId left,
    scene_ir::MaterialGraphNodeId right) {
    scene_ir::MaterialGraphNode node;
    node.kind = scene_ir::MaterialGraphNodeKind::Multiply;
    node.inputs = {scene_ir::material_graph_input("a", left),
                   scene_ir::material_graph_input("b", right)};
    return graph.add_node(std::move(node));
}

scene_ir::MaterialGraph canonical_graph(
    const scene_ir::MaterialNode& material) {
    scene_ir::MaterialGraph graph;
    auto color = add_color(graph, material.base_color);
    if (material.base_color_texture)
        color = multiply(graph, color,
                         add_texture(graph, material.base_color_texture));
    auto roughness = add_float(graph, material.roughness);
    if (material.roughness_texture)
        roughness = multiply(graph, roughness,
                             add_texture(graph,
                                         material.roughness_texture));
    scene_ir::MaterialGraphNode bsdf;
    switch (material.model) {
    case scene_ir::MaterialModel::Lambertian:
    case scene_ir::MaterialModel::Cloth:
        bsdf.kind = scene_ir::MaterialGraphNodeKind::BsdfLambert;
        bsdf.inputs = {
            scene_ir::material_graph_input("base_color", color),
            scene_ir::material_graph_input("roughness", roughness)};
        break;
    case scene_ir::MaterialModel::Metal:
        bsdf.kind = scene_ir::MaterialGraphNodeKind::BsdfMetal;
        bsdf.inputs = {
            scene_ir::material_graph_input("base_color", color),
            scene_ir::material_graph_input("roughness", roughness),
            scene_ir::material_graph_input(
                "eta", add_color(graph, material.metal_eta)),
            scene_ir::material_graph_input(
                "k", add_color(graph, material.metal_k))};
        break;
    case scene_ir::MaterialModel::Dielectric:
        bsdf.kind = scene_ir::MaterialGraphNodeKind::BsdfDielectric;
        bsdf.inputs = {
            scene_ir::material_graph_input("base_color", color),
            scene_ir::material_graph_input("roughness", roughness),
            scene_ir::material_graph_input(
                "ior", add_float(graph, material.ior))};
        break;
    case scene_ir::MaterialModel::Light: {
        bsdf.kind = scene_ir::MaterialGraphNodeKind::BsdfLight;
        auto emission = add_color(graph, material.emission);
        if (material.emission_texture)
            emission = multiply(graph, emission,
                                add_texture(graph,
                                            material.emission_texture));
        bsdf.inputs = {
            scene_ir::material_graph_input("emission", emission)};
        break;
    }
    }
    const auto bsdf_id = graph.add_node(std::move(bsdf));
    scene_ir::MaterialGraphNode output;
    output.kind = scene_ir::MaterialGraphNodeKind::OutputSurface;
    output.inputs = {scene_ir::material_graph_input("surface", bsdf_id)};
    graph.output_node_id = graph.add_node(std::move(output));
    return graph;
}

const scene_ir::MaterialGraphNode& surface_node(
    const scene_ir::MaterialGraph& graph) {
    const auto& output = graph.require_node(graph.output_node_id,
                                            "product output");
    if (output.kind != scene_ir::MaterialGraphNodeKind::OutputSurface)
        throw ProductMaterialError(
            kInvalidGraph, "URE-PRV3-MATERIAL-GRAPH-001", "/output",
            "MaterialGraph output is not OutputSurface",
            "connect one supported BSDF to an OutputSurface node");
    const auto input = std::ranges::find(output.inputs, "surface",
                                         &scene_ir::MaterialGraphInput::name);
    if (input == output.inputs.end() ||
        input->node_id == scene_ir::kInvalidMaterialGraphNode)
        throw ProductMaterialError(
            kInvalidGraph, "URE-PRV3-MATERIAL-GRAPH-002", "/output/surface",
            "MaterialGraph output has no surface input",
            "connect one supported BSDF to the surface input");
    return graph.require_node(input->node_id, "product surface");
}

bool finite(core::Vec3f value) {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

const scene_ir::MaterialGraphInput* graph_input(
    const scene_ir::MaterialGraphNode& node, std::string_view name) {
    const auto found = std::ranges::find(node.inputs, name,
                                         &scene_ir::MaterialGraphInput::name);
    return found == node.inputs.end() ? nullptr : &*found;
}

const scene_ir::MaterialGraphNode& required_graph_input(
    const scene_ir::MaterialGraph& graph,
    const scene_ir::MaterialGraphNode& node, std::string_view name) {
    const auto* input = graph_input(node, name);
    if (!input || input->node_id == scene_ir::kInvalidMaterialGraphNode)
        throw ProductMaterialError(
            kInvalidGraph, "URE-PRV3-MATERIAL-GRAMMAR-001",
            "/graph/nodes/" + std::to_string(node.id) + "/" +
                std::string(name),
            "MaterialGraph node is missing a required input",
            "connect every required canonical MaterialGraph input");
    return graph.require_node(input->node_id, std::string(name));
}

void validate_value_expression(const scene_ir::MaterialGraph& graph,
                               const scene_ir::MaterialGraphNode& node) {
    switch (node.kind) {
    case scene_ir::MaterialGraphNodeKind::ConstantColor:
    case scene_ir::MaterialGraphNodeKind::ConstantFloat:
        return;
    case scene_ir::MaterialGraphNodeKind::Texture2D:
        if (!node.texture || !node.texture->image ||
            node.texture->image->uri.empty())
            throw ProductMaterialError(
                kMissingResource, "URE-PRV3-MATERIAL-TEXTURE-001",
                "/graph/nodes/" + std::to_string(node.id) + "/texture",
                "Texture expression has no content-bound image",
                "bind the texture expression to a packaged image resource");
        return;
    case scene_ir::MaterialGraphNodeKind::Add:
    case scene_ir::MaterialGraphNodeKind::Multiply:
        validate_value_expression(
            graph, required_graph_input(graph, node, "a"));
        validate_value_expression(
            graph, required_graph_input(graph, node, "b"));
        return;
    case scene_ir::MaterialGraphNodeKind::Mix:
        validate_value_expression(
            graph, required_graph_input(graph, node, "a"));
        validate_value_expression(
            graph, required_graph_input(graph, node, "b"));
        validate_value_expression(
            graph, required_graph_input(graph, node, "factor"));
        return;
    case scene_ir::MaterialGraphNodeKind::Checker2D:
    case scene_ir::MaterialGraphNodeKind::Noise2D:
        validate_value_expression(
            graph, required_graph_input(graph, node, "a"));
        validate_value_expression(
            graph, required_graph_input(graph, node, "b"));
        validate_value_expression(
            graph, required_graph_input(graph, node, "scale"));
        return;
    default:
        throw ProductMaterialError(
            kUnsupportedNode, "URE-PRV3-MATERIAL-GRAMMAR-002",
            "/graph/nodes/" + std::to_string(node.id),
            "MaterialGraph BSDF input is not an executable value expression",
            "replace it with a supported constant, texture, arithmetic, mix, checker, or noise expression");
    }
}

void validate_surface_grammar(const scene_ir::MaterialGraph& graph,
                              const scene_ir::MaterialGraphNode& surface) {
    const auto validate_optional = [&](std::string_view name) {
        if (graph_input(surface, name))
            validate_value_expression(
                graph, required_graph_input(graph, surface, name));
    };
    switch (surface.kind) {
    case scene_ir::MaterialGraphNodeKind::BsdfLambert:
        validate_optional("base_color");
        validate_optional("roughness");
        return;
    case scene_ir::MaterialGraphNodeKind::BsdfMetal:
        validate_optional("base_color");
        validate_optional("roughness");
        validate_optional("eta");
        validate_optional("k");
        return;
    case scene_ir::MaterialGraphNodeKind::BsdfDielectric:
        validate_optional("base_color");
        validate_optional("roughness");
        validate_optional("ior");
        validate_optional("thickness");
        return;
    case scene_ir::MaterialGraphNodeKind::BsdfLight:
        validate_optional("emission");
        return;
    case scene_ir::MaterialGraphNodeKind::BsdfMix: {
        const auto& first = required_graph_input(graph, surface, "a");
        const auto& second = required_graph_input(graph, surface, "b");
        if ((first.kind != scene_ir::MaterialGraphNodeKind::BsdfLambert &&
             first.kind != scene_ir::MaterialGraphNodeKind::BsdfMetal) ||
            (second.kind != scene_ir::MaterialGraphNodeKind::BsdfLambert &&
             second.kind != scene_ir::MaterialGraphNodeKind::BsdfMetal))
            throw ProductMaterialError(
                kUnsupportedNode, "URE-PRV3-MATERIAL-MIX-001",
                "/graph/surface",
                "BSDF mix children are outside the executable Lambert/Metal set",
                "use Lambert or Metal children; express dielectric interfaces as a layer");
        validate_surface_grammar(graph, first);
        validate_surface_grammar(graph, second);
        validate_value_expression(
            graph, required_graph_input(graph, surface, "factor"));
        return;
    }
    case scene_ir::MaterialGraphNodeKind::BsdfLayer: {
        const auto& coating = required_graph_input(graph, surface, "coating");
        const auto& substrate = required_graph_input(graph, surface, "substrate");
        if (coating.kind != scene_ir::MaterialGraphNodeKind::BsdfDielectric ||
            substrate.kind != scene_ir::MaterialGraphNodeKind::BsdfLambert)
            throw ProductMaterialError(
                kUnsupportedNode, "URE-PRV3-MATERIAL-LAYER-001",
                "/graph/surface",
                "BSDF layer is outside the executable dielectric-over-Lambert grammar",
                "use a dielectric coating and opaque Lambert substrate");
        validate_surface_grammar(graph, coating);
        validate_surface_grammar(graph, substrate);
        validate_optional("thickness");
        validate_optional("absorption");
        return;
    }
    case scene_ir::MaterialGraphNodeKind::BsdfGrating:
    case scene_ir::MaterialGraphNodeKind::BsdfPhaseMask:
    case scene_ir::MaterialGraphNodeKind::BsdfZonePlate:
    case scene_ir::MaterialGraphNodeKind::BsdfDoe:
    case scene_ir::MaterialGraphNodeKind::BsdfScatteringTable:
    case scene_ir::MaterialGraphNodeKind::BsdfFluorescence:
        return;
    default:
        throw ProductMaterialError(
            kUnsupportedNode, "URE-PRV3-MATERIAL-SURFACE-002",
            "/graph/surface", "MaterialGraph surface is not executable",
            "select a supported canonical surface operator");
    }
}

std::uint64_t validate_and_classify(
    const scene_ir::MaterialNode& material,
    std::vector<std::string>& resource_uris) {
    if (!material.graph || material.graph->empty())
        throw ProductMaterialError(
            kInvalidGraph, "URE-PRV3-MATERIAL-GRAPH-003", "/graph",
            "Canonical material has no executable graph",
            "rebuild the material through a supported authoring adapter");
    try {
        material.graph->validate();
    } catch (const std::exception& error) {
        throw ProductMaterialError(
            kInvalidGraph, "URE-PRV3-MATERIAL-GRAPH-004", "/graph",
            error.what(), "repair the graph node identities and dependencies");
    }
    for (const auto& node : material.graph->nodes) {
        for (std::size_t left = 0; left < node.inputs.size(); ++left) {
            for (std::size_t right = left + 1;
                 right < node.inputs.size(); ++right) {
                if (node.inputs[left].name == node.inputs[right].name)
                    throw ProductMaterialError(
                        kInvalidGraph,
                        "URE-PRV3-MATERIAL-GRAMMAR-003",
                        "/graph/nodes/" + std::to_string(node.id),
                        "MaterialGraph node contains duplicate named inputs",
                        "retain exactly one connection for each canonical input name");
            }
        }
    }
    std::uint64_t features = ProductMaterialFeatureNone;
    if (material.normal_texture && material.normal_texture->image &&
        !material.normal_texture->image->uri.empty()) {
        features |= ProductMaterialFeatureTexture;
        resource_uris.push_back(material.normal_texture->image->uri);
    }
    const auto& surface = surface_node(*material.graph);
    validate_surface_grammar(*material.graph, surface);
    switch (surface.kind) {
    case scene_ir::MaterialGraphNodeKind::BsdfLambert:
    case scene_ir::MaterialGraphNodeKind::BsdfMetal:
    case scene_ir::MaterialGraphNodeKind::BsdfDielectric:
    case scene_ir::MaterialGraphNodeKind::BsdfLight:
        break;
    case scene_ir::MaterialGraphNodeKind::BsdfMix:
        features |= ProductMaterialFeatureComposite;
        break;
    case scene_ir::MaterialGraphNodeKind::BsdfLayer:
        features |= ProductMaterialFeatureLayered;
        break;
    case scene_ir::MaterialGraphNodeKind::BsdfGrating:
    case scene_ir::MaterialGraphNodeKind::BsdfPhaseMask:
    case scene_ir::MaterialGraphNodeKind::BsdfZonePlate:
    case scene_ir::MaterialGraphNodeKind::BsdfDoe:
    case scene_ir::MaterialGraphNodeKind::BsdfScatteringTable: {
        auto expected = scene_ir::DiffractiveOperatorKind::Grating;
        if (surface.kind == scene_ir::MaterialGraphNodeKind::BsdfPhaseMask)
            expected = scene_ir::DiffractiveOperatorKind::PhaseMask;
        else if (surface.kind ==
                 scene_ir::MaterialGraphNodeKind::BsdfZonePlate)
            expected = scene_ir::DiffractiveOperatorKind::ZonePlate;
        else if (surface.kind == scene_ir::MaterialGraphNodeKind::BsdfDoe)
            expected = scene_ir::DiffractiveOperatorKind::Doe;
        else if (surface.kind ==
                 scene_ir::MaterialGraphNodeKind::BsdfScatteringTable)
            expected = scene_ir::DiffractiveOperatorKind::ScatteringTable;
        if (surface.diffraction.kind != expected)
            throw ProductMaterialError(
                kWaveContractInvalid,
                "URE-PRV3-MATERIAL-DIFFRACTION-002", "/graph/surface",
                "Diffractive node kind conflicts with its operator contract",
                "make the surface node and diffractive operator kinds identical");
        if (!wave::is_valid(surface.diffraction))
            throw ProductMaterialError(
                kWaveContractInvalid,
                "URE-PRV3-MATERIAL-DIFFRACTION-001", "/graph/surface",
                "Diffractive material contract is invalid",
                "repair its period, aperture, order, side, or passive table");
        features |= ProductMaterialFeatureDiffractive |
                    ProductMaterialFeatureSpectral;
        break;
    }
    case scene_ir::MaterialGraphNodeKind::BsdfFluorescence:
        if (!wave::is_valid(surface.fluorescence))
            throw ProductMaterialError(
                kWaveContractInvalid,
                "URE-PRV3-MATERIAL-FLUORESCENCE-001", "/graph/surface",
                "Fluorescence material contract is invalid",
                "repair its wavelength grids, yield, PDF, or lifetime");
        features |= ProductMaterialFeatureFluorescent |
                    ProductMaterialFeatureSpectral;
        break;
    default:
        throw ProductMaterialError(
            kUnsupportedNode, "URE-PRV3-MATERIAL-SURFACE-001",
            "/graph/surface", "Material surface node is not executable",
            "replace it with a supported canonical MaterialGraph BSDF");
    }
    for (const auto& node : material.graph->nodes) {
        if (!finite(node.color) || !std::isfinite(node.value))
            throw ProductMaterialError(
                kInvalidGraph, "URE-PRV3-MATERIAL-VALUE-001",
                "/graph/nodes/" + std::to_string(node.id),
                "MaterialGraph contains a non-finite value",
                "replace non-finite authored values with finite values");
        if (node.kind == scene_ir::MaterialGraphNodeKind::Texture2D) {
            if (!node.texture || !node.texture->image ||
                node.texture->image->uri.empty())
                throw ProductMaterialError(
                    kMissingResource, "URE-PRV3-MATERIAL-TEXTURE-001",
                    "/graph/nodes/" + std::to_string(node.id) + "/texture",
                    "Texture node has no content-bound image resource",
                    "bind the texture to a packaged image resource");
            features |= ProductMaterialFeatureTexture;
            resource_uris.push_back(node.texture->image->uri);
        }
    }
    if (material.spectral_extension) {
        if (material.spectral_extension->spectral_bands < 0)
            throw ProductMaterialError(
                kSpectralDomainMismatch,
                "URE-PRV3-MATERIAL-SPECTRAL-001", "/spectral/bands",
                "Spectral material has a negative source domain",
                "author a non-negative source domain or inherit the product domain");
        features |= ProductMaterialFeatureSpectral;
        if (!material.spectral_extension->albedo_spd.empty())
            resource_uris.push_back(material.spectral_extension->albedo_spd);
        if (!material.spectral_extension->emission_spd.empty())
            resource_uris.push_back(material.spectral_extension->emission_spd);
    }
    if (material.medium_density > 0.0f) {
        features |= ProductMaterialFeatureMedium;
        if (material.medium_phase == scene_ir::VolumePhaseFunction::Mie) {
            if (!material.medium_mie_resource)
                throw ProductMaterialError(
                    kMissingResource, "URE-PRV3-MATERIAL-MIE-001",
                    "/medium/mie", "Mie medium has no phase resource",
                    "bind a validated content-addressed Mie resource");
            features |= ProductMaterialFeatureMie |
                        ProductMaterialFeatureSpectral;
        }
    }
    std::ranges::sort(resource_uris);
    resource_uris.erase(std::unique(resource_uris.begin(),
                                    resource_uris.end()),
                        resource_uris.end());
    return features;
}

std::vector<std::string> applicable_integrators(std::uint64_t features) {
    if ((features & ProductMaterialFeatureDiffractive) != 0 ||
        (features & ProductMaterialFeatureFluorescent) != 0)
        return {"wavefront"};
    if ((features & ProductMaterialFeatureMedium) != 0)
        return {"wavefront", "path_guided", "restir_pt", "mlt"};
    if ((features & ProductMaterialFeatureLayered) != 0 ||
        (features & ProductMaterialFeatureComposite) != 0)
        return {"wavefront", "path_guided", "restir_di", "restir_pt",
                "mlt"};
    return {"wavefront", "path_guided", "restir_di", "restir_pt",
            "specular_manifold", "bdpt", "vcm", "mlt"};
}

std::uint64_t integrator_mask(std::span<const std::string> names) {
    std::uint64_t result{};
    for (const auto& name : names) {
        if (name == "wavefront")
            result |= integrator_mode_bit(IntegratorMode::Wavefront);
        else if (name == "path_guided")
            result |= integrator_mode_bit(IntegratorMode::PathGuided);
        else if (name == "restir_di")
            result |= integrator_mode_bit(IntegratorMode::RestirDI);
        else if (name == "specular_manifold")
            result |= integrator_mode_bit(IntegratorMode::SpecularManifold);
        else if (name == "mlt")
            result |= integrator_mode_bit(IntegratorMode::MLT);
        else if (name == "restir_pt")
            result |= integrator_mode_bit(IntegratorMode::RestirPT);
        else if (name == "bdpt")
            result |= integrator_mode_bit(IntegratorMode::BDPT);
        else if (name == "vcm")
            result |= integrator_mode_bit(IntegratorMode::VCM);
    }
    return result;
}

ProductMaterialOrigin material_origin(
    std::string_view material_uuid,
    std::span<const native_scene::FeatureDeclaration> features,
    bool canonicalized) {
    ProductMaterialOrigin result =
        canonicalized ? ProductMaterialOrigin::CanonicalizedLegacy
                      : ProductMaterialOrigin::NativeGraph;
    for (const auto& feature : features) {
        if (feature.name == "ure.adapter.gltf")
            result = ProductMaterialOrigin::Gltf;
        else if (feature.name == "ure.adapter.hydra")
            result = ProductMaterialOrigin::Hydra;
        else if (feature.name == "ure.adapter.materialx" ||
                 feature.name == "ure.adapter.material-preset") {
            try {
                const auto parameters = nlohmann::json::parse(
                    feature.canonical_parameters);
                if (parameters.value("material_uuid", std::string{}) !=
                    material_uuid)
                    continue;
                result = feature.name == "ure.adapter.materialx"
                             ? ProductMaterialOrigin::MaterialX
                             : ProductMaterialOrigin::Preset;
            } catch (const nlohmann::json::exception&) {
                throw ProductMaterialError(
                    kInvalidGraph, "URE-PRV3-MATERIAL-PROVENANCE-001",
                    "/features/" + feature.name,
                    "Material adapter provenance is not canonical JSON",
                    "rebuild the authored scene with Scene Tool 0.2");
            }
        }
    }
    return result;
}

Identity program_identity(const ProductMaterialProgram& program,
                          const scene_ir::MaterialNode& material,
                          const scene_ir::MaterialGraph& graph) {
    std::vector<std::uint8_t> bytes;
    append_text(bytes, "UltraRender.ProductMaterialProgram.v1");
    append_text(bytes, program.compiler_semantic);
    append_text(bytes, program.backend_semantic);
    append_text(bytes, program.material_uuid);
    append_text(bytes, program.source_id);
    append_value(bytes, program.features);
    append_value(bytes, program.spectral_domain_bins);
    append_value(bytes, program.spectral_packet_lanes);
    append_value(bytes, graph.output_node_id);
    append_value(bytes, material.model);
    append_value(bytes, material.base_color);
    append_value(bytes, material.roughness);
    append_value(bytes, material.ior);
    append_value(bytes, material.dispersion);
    append_value(bytes, material.metal_eta);
    append_value(bytes, material.metal_k);
    append_value(bytes, material.thin_film_thickness);
    append_value(bytes, material.thin_film_ior);
    append_value(bytes, material.emission);
    append_value(bytes, material.medium_density);
    append_value(bytes, material.medium_anisotropy);
    append_value(bytes, material.medium_phase);
    append_value(bytes, material.medium_scattering);
    append_value(bytes, material.medium_absorption);
    append_value(bytes, material.normal_scale);
    if (material.spectral_extension) {
        append_text(bytes, "albedo_spd");
        append_text(bytes, material.spectral_extension->albedo_spd);
        append_text(bytes, "emission_spd");
        append_text(bytes, material.spectral_extension->emission_spd);
        append_value(bytes, material.spectral_extension->spectral_bands);
    }
    if (material.medium_mie_resource)
        append_text(bytes, scene_ir::mie_phase_content_hash(
                               *material.medium_mie_resource));
    if (material.normal_texture && material.normal_texture->image) {
        append_text(bytes, "preserved-normal-texture");
        append_text(bytes, material.normal_texture->image->uri);
        append_value(bytes, material.normal_texture->image->color_space);
        append_value(bytes, material.normal_texture->uv_set);
    }
    auto nodes = graph.nodes;
    std::ranges::sort(nodes, {}, &scene_ir::MaterialGraphNode::id);
    for (auto& node : nodes) {
        append_value(bytes, node.id);
        append_value(bytes, node.kind);
        append_text(bytes, node.name);
        append_value(bytes, node.color);
        append_value(bytes, node.value);
        auto inputs = node.inputs;
        std::ranges::sort(inputs, [](const auto& left, const auto& right) {
            return std::tie(left.name, left.output, left.node_id) <
                   std::tie(right.name, right.output, right.node_id);
        });
        for (const auto& input : inputs) {
            append_text(bytes, input.name);
            append_value(bytes, input.node_id);
            append_text(bytes, input.output);
        }
        if (node.texture && node.texture->image) {
            append_text(bytes, node.texture->image->uri);
            append_text(bytes, node.texture->image->name);
            append_value(bytes, node.texture->image->color_space);
            append_text(bytes, node.texture->name);
            append_value(bytes, node.texture->uv_set);
        }
        if ((program.features & ProductMaterialFeatureDiffractive) != 0) {
            append_value(bytes, node.diffraction.kind);
            append_value(bytes, node.diffraction.side);
            append_value(bytes, node.diffraction.period_m);
            append_value(bytes, node.diffraction.orientation_rad);
            append_value(bytes, node.diffraction.duty_cycle);
            append_value(bytes, node.diffraction.phase_depth_rad);
            append_value(bytes, node.diffraction.design_wavelength_nm);
            append_value(bytes, node.diffraction.focal_length_m);
            append_value(bytes, node.diffraction.aperture_radius_m);
            append_value(bytes, node.diffraction.max_order);
            append_text(bytes, node.diffraction.table_id);
            for (const auto& entry : node.diffraction.table) {
                append_value(bytes, entry.wavelength_nm);
                append_value(bytes, entry.incident_cosine);
                append_value(bytes, entry.order);
                append_value(bytes, entry.side);
                append_value(bytes, entry.jones_ss.real);
                append_value(bytes, entry.jones_ss.imag);
                append_value(bytes, entry.jones_sp.real);
                append_value(bytes, entry.jones_sp.imag);
                append_value(bytes, entry.jones_ps.real);
                append_value(bytes, entry.jones_ps.imag);
                append_value(bytes, entry.jones_pp.real);
                append_value(bytes, entry.jones_pp.imag);
            }
        }
        if ((program.features & ProductMaterialFeatureFluorescent) != 0) {
            append_text(bytes, node.fluorescence.resource_id);
            for (const auto value : node.fluorescence.excitation_wavelengths_nm)
                append_value(bytes, value);
            for (const auto value : node.fluorescence.emission_wavelengths_nm)
                append_value(bytes, value);
            for (const auto value : node.fluorescence.excitation_efficiency)
                append_value(bytes, value);
            for (const auto value : node.fluorescence.quantum_yield)
                append_value(bytes, value);
            for (const auto value : node.fluorescence.emission_pdf_per_nm)
                append_value(bytes, value);
            append_value(bytes, node.fluorescence.lifetime_seconds);
        }
    }
    for (const auto& uri : program.resource_uris)
        append_text(bytes, uri);
    return content_identity(bytes);
}

Identity world_medium_identity(const ProductMaterialProgram& program,
                               const scene_ir::SceneIR& scene) {
    std::vector<std::uint8_t> bytes;
    append_text(bytes, "UltraRender.ProductWorldMediumProgram.v1");
    append_text(bytes, program.compiler_semantic);
    append_text(bytes, program.backend_semantic);
    append_text(bytes, program.material_uuid);
    append_text(bytes, program.source_id);
    append_value(bytes, program.features);
    append_value(bytes, program.spectral_domain_bins);
    append_value(bytes, program.spectral_packet_lanes);
    append_value(bytes, scene.medium_density);
    append_value(bytes, scene.medium_anisotropy);
    append_value(bytes, scene.medium_phase);
    append_value(bytes, scene.medium_scattering);
    append_value(bytes, scene.medium_absorption);
    append_value(bytes, scene.medium_max_distance);
    if (scene.medium_mie_resource)
        append_text(bytes, scene_ir::mie_phase_content_hash(
                               *scene.medium_mie_resource));
    return content_identity(bytes);
}

}

ProductMaterialError::ProductMaterialError(
    std::uint32_t detail, std::string code, std::string field_path,
    std::string message, std::string recovery)
    : std::runtime_error(std::move(message)), detail_(detail),
      code_(std::move(code)), field_path_(std::move(field_path)),
      recovery_(std::move(recovery)) {}

std::uint32_t ProductMaterialError::detail() const noexcept {
    return detail_;
}

const std::string& ProductMaterialError::code() const noexcept {
    return code_;
}

const std::string& ProductMaterialError::field_path() const noexcept {
    return field_path_;
}

const std::string& ProductMaterialError::recovery() const noexcept {
    return recovery_;
}

ProductMaterialProgramSet canonicalize_product_materials(
    scene_ir::SceneIR& scene,
    const native_scene::NativeSceneSourceIds& source_ids,
    const native_scene::NativeSceneObjectUuids& object_uuids,
    const RenderConfig& config,
    std::span<const native_scene::FeatureDeclaration> features) {
    if (source_ids.materials.size() != scene.materials.size() ||
        object_uuids.materials.size() != scene.materials.size())
        throw ProductMaterialError(
            kInvalidGraph, "URE-PRV3-MATERIAL-IDENTITY-001", "/materials",
            "Material source and UUID identities are incomplete",
            "migrate the scene to the current native schema");
    ProductMaterialProgramSet result;
    for (std::size_t index = 0; index < scene.materials.size(); ++index) {
        auto& material = scene.materials[index];
        if (!material)
            throw ProductMaterialError(
                kInvalidGraph, "URE-PRV3-MATERIAL-NULL-001",
                "/materials/" + std::to_string(index),
                "Scene contains a null material",
                "bind every material slot to a canonical material");
        const bool canonicalized = !material->graph || material->graph->empty();
        if (canonicalized)
            material->graph = std::make_shared<scene_ir::MaterialGraph>(
                canonical_graph(*material));
        ProductMaterialProgram program;
        program.material_uuid = native_scene::format_uuid(
            object_uuids.materials[index]);
        program.source_id = source_ids.materials[index];
        program.name = material->name;
        program.compiler_semantic =
            "ure.materialgraph.compiler/1+spd-clamp-400-700/1";
        program.backend_semantic = "cuda.complete-scene/1";
        program.origin = material_origin(program.material_uuid, features,
                                         canonicalized);
        program.spectral_domain_bins = spectral_domain_bins(config);
        program.spectral_packet_lanes = static_cast<std::uint32_t>(
            spectral_packet_lanes(config));
        program.node_count = static_cast<std::uint32_t>(
            material->graph->nodes.size());
        program.features = validate_and_classify(
            *material, program.resource_uris);
        program.applicable_integrators = applicable_integrators(
            program.features);
        if ((program.features & ProductMaterialFeatureMie) != 0)
            program.update_class =
                ProductMaterialUpdateClass::FullSnapshotReplacement;
        else if ((program.features &
                  (ProductMaterialFeatureTexture |
                   ProductMaterialFeatureSpectral |
                   ProductMaterialFeatureMedium |
                   ProductMaterialFeatureDiffractive |
                   ProductMaterialFeatureFluorescent)) != 0)
            program.update_class =
                ProductMaterialUpdateClass::PartialRebuild;
        program.identity = program_identity(program, *material,
                                            *material->graph);
        program.canonical_identity = program.identity;
        result.programs.push_back(std::move(program));
    }
    if (scene.medium_density > 0.0f || scene.medium_mie_resource) {
        ProductMaterialProgram program;
        program.material_uuid = native_scene::format_uuid(
            object_uuids.environment);
        program.source_id = "environment/medium";
        program.name = "World Medium";
        program.compiler_semantic =
            "ure.materialgraph.compiler/1+world-medium/1+spd-clamp-400-700/1";
        program.backend_semantic = "cuda.complete-scene/1";
        program.origin = material_origin(program.material_uuid, features, false);
        program.update_class = scene.medium_mie_resource
                                   ? ProductMaterialUpdateClass::FullSnapshotReplacement
                                   : ProductMaterialUpdateClass::PartialRebuild;
        program.features = ProductMaterialFeatureMedium;
        if (scene.medium_phase == scene_ir::VolumePhaseFunction::Mie) {
            if (!scene.medium_mie_resource)
                throw ProductMaterialError(
                    kMissingResource, "URE-PRV3-MATERIAL-MIE-002",
                    "/scene/medium/mie",
                    "World Mie medium has no phase resource",
                    "bind a validated content-addressed Mie phase resource");
            program.features |= ProductMaterialFeatureMie |
                                ProductMaterialFeatureSpectral;
        }
        program.spectral_domain_bins = spectral_domain_bins(config);
        program.spectral_packet_lanes = static_cast<std::uint32_t>(
            spectral_packet_lanes(config));
        program.applicable_integrators = applicable_integrators(
            program.features);
        program.identity = world_medium_identity(program, scene);
        program.canonical_identity = program.identity;
        result.programs.push_back(std::move(program));
    }
    std::vector<std::uint8_t> set_bytes;
    append_text(set_bytes, "UltraRender.ProductMaterialProgramSet.v1");
    auto ordered_programs = result.programs;
    std::ranges::sort(ordered_programs, {},
                      &ProductMaterialProgram::material_uuid);
    for (const auto& program : ordered_programs) {
        append_text(set_bytes, program.material_uuid);
        set_bytes.insert(set_bytes.end(), program.identity.begin(),
                         program.identity.end());
    }
    result.identity = content_identity(set_bytes);
    return result;
}

void bind_product_material_resources(
    ProductMaterialProgramSet& programs,
    std::span<const native_scene::NativeResolvedResource> resources) {
    for (auto& program : programs.programs) {
        program.resource_content_hashes.clear();
        std::vector<std::uint8_t> bytes;
        append_text(bytes, "UltraRender.ProductMaterialResourceBinding.v1");
        bytes.insert(bytes.end(), program.canonical_identity.begin(),
                     program.canonical_identity.end());
        for (const auto& uri : program.resource_uris) {
            const auto found = std::ranges::find(
                resources, uri,
                &native_scene::NativeResolvedResource::source_uri);
            if (found == resources.end())
                throw ProductMaterialError(
                    kMissingResource, "URE-PRV3-MATERIAL-RESOURCE-001",
                    "/materials/resources",
                    "Material program resource has no resolved content identity",
                    "package every material resource with its declared content hash");
            append_text(bytes, uri);
            append_text(bytes, found->descriptor.content_hash);
            program.resource_content_hashes.push_back(
                found->descriptor.content_hash);
        }
        program.identity = content_identity(bytes);
    }
    std::vector<std::uint8_t> set_bytes;
    append_text(set_bytes,
                "UltraRender.ProductMaterialProgramSet.ResourceBound.v1");
    auto ordered_programs = programs.programs;
    std::ranges::sort(ordered_programs, {},
                      &ProductMaterialProgram::material_uuid);
    for (const auto& program : ordered_programs) {
        append_text(set_bytes, program.material_uuid);
        set_bytes.insert(set_bytes.end(), program.identity.begin(),
                         program.identity.end());
    }
    programs.identity = content_identity(set_bytes);
}

void configure_product_material_execution(
    const ProductMaterialProgramSet& programs,
    RenderConfig& config) {
    std::uint64_t eligible = kAllProductionIntegratorModes;
    std::uint64_t features{};
    for (const auto& program : programs.programs) {
        eligible &= integrator_mask(program.applicable_integrators);
        features |= program.features;
    }
    if (eligible == 0)
        throw ProductMaterialError(
            705, "URE-PRV3-MATERIAL-ESTIMATOR-001", "/materials",
            "No estimator can execute the complete material program set",
            "split incompatible material semantics or select a supported product route");
    if (config.wave_optics.mode == WaveOpticsMode::CoherentField ||
        config.wave_optics.mode == WaveOpticsMode::PartialCoherence ||
        config.wave_optics.coherent_field_enabled ||
        config.wave_optics.partial_coherence_enabled)
        throw ProductMaterialError(
            704, "URE-PRV3-MATERIAL-WAVE-001", "/solver/wave",
            "General coherent material execution is not a Preview production capability",
            "use the radiometric or bounded camera-diffraction product route");
    const bool diffractive =
        (features & ProductMaterialFeatureDiffractive) != 0;
    const bool fluorescent =
        (features & ProductMaterialFeatureFluorescent) != 0;
    if (diffractive && fluorescent)
        throw ProductMaterialError(
            704, "URE-PRV3-MATERIAL-WAVE-002", "/materials",
            "Diffractive and fluorescent operators cannot share the current bounded session",
            "render the two bounded wave mechanisms in separate product jobs");
    if ((diffractive || fluorescent) &&
        (config.wave_optics.mode != WaveOpticsMode::Radiometric ||
         config.wave_optics.camera_diffraction_enabled ||
         config.wave_optics.local_fullwave_enabled ||
         config.wave_optics.specular_manifold_enabled))
        throw ProductMaterialError(
            704, "URE-PRV3-MATERIAL-WAVE-003", "/solver/wave",
            "Material wave operator conflicts with the requested wave session",
            "use one ordinary radiometric wave-material mechanism per product job");
    config.wave_optics.diffractive_materials_enabled = diffractive;
    config.wave_optics.fluorescence_enabled = fluorescent;
    config.automatic_integrator.eligible_integrator_modes = eligible;
    if (config.integrator.mode == IntegratorMode::Automatic &&
        (diffractive || fluorescent)) {
        config.integrator.mode = IntegratorMode::Wavefront;
        config.automatic_integrator.enabled = false;
    } else if (config.integrator.mode != IntegratorMode::Automatic &&
               (eligible & integrator_mode_bit(config.integrator.mode)) == 0) {
        throw ProductMaterialError(
            705, "URE-PRV3-MATERIAL-ESTIMATOR-002", "/solver/integrator",
            "Requested estimator is not applicable to the material program set",
            "use an estimator reported by the material applicability set");
    }
}

ProductMaterialUpdatePlan classify_product_material_update(
    const ProductMaterialProgramSet& before,
    const ProductMaterialProgramSet& after) {
    ProductMaterialUpdatePlan result;
    const auto severity = [](ProductMaterialUpdateClass value) {
        return static_cast<std::uint32_t>(value);
    };
    const auto raise = [&result, &severity](ProductMaterialUpdateClass value) {
        if (severity(value) > severity(result.update_class))
            result.update_class = value;
    };
    for (const auto& old_program : before.programs) {
        const auto found = std::ranges::find(
            after.programs, old_program.material_uuid,
            &ProductMaterialProgram::material_uuid);
        if (found == after.programs.end()) {
            result.changed_material_uuids.push_back(
                old_program.material_uuid);
            raise(ProductMaterialUpdateClass::FullSnapshotReplacement);
        } else if (found->identity != old_program.identity) {
            result.changed_material_uuids.push_back(
                old_program.material_uuid);
            raise(old_program.update_class);
            raise(found->update_class);
        }
    }
    for (const auto& new_program : after.programs) {
        if (std::ranges::find(
                before.programs, new_program.material_uuid,
                &ProductMaterialProgram::material_uuid) ==
            before.programs.end()) {
            result.changed_material_uuids.push_back(
                new_program.material_uuid);
            raise(ProductMaterialUpdateClass::FullSnapshotReplacement);
        }
    }
    std::ranges::sort(result.changed_material_uuids);
    result.changed_material_uuids.erase(
        std::unique(result.changed_material_uuids.begin(),
                    result.changed_material_uuids.end()),
        result.changed_material_uuids.end());
    return result;
}

}
