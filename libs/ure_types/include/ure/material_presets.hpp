#pragma once

#include "ure/scene_ir.hpp"
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ure::scene_ir {

enum class MaterialPresetKind {
    Gold,
    Copper,
    Aluminum,
    ClearGlass,
    DiamondGlass,
    WovenFabric,
    AutomotivePaint,
    SkinVolume
};

inline std::vector<std::string_view> material_preset_names() {
    return {
        "gold",
        "copper",
        "aluminum",
        "clear_glass",
        "diamond_glass",
        "woven_fabric",
        "automotive_paint",
        "skin"
    };
}

namespace detail {

inline MaterialGraphNodeId add_color(MaterialGraph& graph, core::Vec3f color) {
    MaterialGraphNode node;
    node.kind = MaterialGraphNodeKind::ConstantColor;
    node.color = color;
    return graph.add_node(std::move(node));
}

inline MaterialGraphNodeId add_float(MaterialGraph& graph, float value) {
    MaterialGraphNode node;
    node.kind = MaterialGraphNodeKind::ConstantFloat;
    node.value = value;
    return graph.add_node(std::move(node));
}

inline MaterialGraphNodeId add_output(MaterialGraph& graph, MaterialGraphNodeId surface) {
    MaterialGraphNode output;
    output.kind = MaterialGraphNodeKind::OutputSurface;
    output.inputs.push_back(material_graph_input("surface", surface));
    MaterialGraphNodeId id = graph.add_node(std::move(output));
    graph.output_node_id = id;
    return id;
}

inline std::shared_ptr<MaterialNode> material_with_graph(std::string name, MaterialGraph graph, MaterialModel model) {
    auto material = std::make_shared<MaterialNode>();
    material->name = std::move(name);
    material->model = model;
    material->graph = std::make_shared<MaterialGraph>(std::move(graph));
    return material;
}

} // namespace detail

inline std::shared_ptr<MaterialNode> make_metal_preset(std::string name,
                                                       core::Vec3f eta,
                                                       core::Vec3f k,
                                                       float roughness) {
    MaterialGraph graph;
    MaterialGraphNode bsdf;
    bsdf.kind = MaterialGraphNodeKind::BsdfMetal;
    bsdf.inputs.push_back(material_graph_input("eta", detail::add_color(graph, eta)));
    bsdf.inputs.push_back(material_graph_input("k", detail::add_color(graph, k)));
    bsdf.inputs.push_back(material_graph_input("roughness", detail::add_float(graph, roughness)));
    MaterialGraphNodeId bsdf_id = graph.add_node(std::move(bsdf));
    detail::add_output(graph, bsdf_id);
    return detail::material_with_graph(std::move(name), std::move(graph), MaterialModel::Metal);
}

inline std::shared_ptr<MaterialNode> make_dielectric_preset(std::string name, float ior, float dispersion) {
    MaterialGraph graph;
    MaterialGraphNode bsdf;
    bsdf.kind = MaterialGraphNodeKind::BsdfDielectric;
    bsdf.inputs.push_back(material_graph_input("ior", detail::add_float(graph, ior)));
    MaterialGraphNodeId bsdf_id = graph.add_node(std::move(bsdf));
    detail::add_output(graph, bsdf_id);
    auto material = detail::material_with_graph(std::move(name), std::move(graph), MaterialModel::Dielectric);
    material->ior = ior;
    material->dispersion = dispersion;
    return material;
}

inline std::shared_ptr<MaterialNode> make_woven_fabric_preset() {
    MaterialGraph graph;
    MaterialGraphNode dark;
    dark.kind = MaterialGraphNodeKind::ConstantColor;
    dark.color = {0.22f, 0.24f, 0.26f};
    MaterialGraphNodeId dark_id = graph.add_node(std::move(dark));
    MaterialGraphNode light;
    light.kind = MaterialGraphNodeKind::ConstantColor;
    light.color = {0.62f, 0.65f, 0.68f};
    MaterialGraphNodeId light_id = graph.add_node(std::move(light));
    MaterialGraphNode scale;
    scale.kind = MaterialGraphNodeKind::ConstantFloat;
    scale.value = 48.0f;
    MaterialGraphNodeId scale_id = graph.add_node(std::move(scale));
    MaterialGraphNode checker;
    checker.kind = MaterialGraphNodeKind::Checker2D;
    checker.inputs = {
        material_graph_input("a", dark_id),
        material_graph_input("b", light_id),
        material_graph_input("scale", scale_id)};
    MaterialGraphNodeId checker_id = graph.add_node(std::move(checker));
    MaterialGraphNode bsdf;
    bsdf.kind = MaterialGraphNodeKind::BsdfLambert;
    bsdf.inputs.push_back(material_graph_input("base_color", checker_id));
    bsdf.inputs.push_back(material_graph_input("roughness", detail::add_float(graph, 0.85f)));
    MaterialGraphNodeId bsdf_id = graph.add_node(std::move(bsdf));
    detail::add_output(graph, bsdf_id);
    return detail::material_with_graph("woven_fabric", std::move(graph), MaterialModel::Lambertian);
}

inline std::shared_ptr<MaterialNode> make_automotive_paint_preset() {
    MaterialGraph graph;
    MaterialGraphNode coating;
    coating.kind = MaterialGraphNodeKind::BsdfDielectric;
    coating.inputs.push_back(material_graph_input("ior", detail::add_float(graph, 1.52f)));
    MaterialGraphNodeId coating_id = graph.add_node(std::move(coating));
    MaterialGraphNode base;
    base.kind = MaterialGraphNodeKind::BsdfLambert;
    base.inputs.push_back(material_graph_input("base_color", detail::add_color(graph, {0.75f, 0.03f, 0.015f})));
    base.inputs.push_back(material_graph_input("roughness", detail::add_float(graph, 0.38f)));
    MaterialGraphNodeId base_id = graph.add_node(std::move(base));
    MaterialGraphNode layer;
    layer.kind = MaterialGraphNodeKind::BsdfLayer;
    layer.inputs = {
        material_graph_input("coating", coating_id),
        material_graph_input("substrate", base_id),
        material_graph_input("thickness", detail::add_float(graph, 0.035f)),
        material_graph_input("absorption", detail::add_color(graph, {0.04f, 0.02f, 0.015f}))};
    MaterialGraphNodeId layer_id = graph.add_node(std::move(layer));
    detail::add_output(graph, layer_id);
    return detail::material_with_graph("automotive_paint", std::move(graph), MaterialModel::Lambertian);
}

inline std::shared_ptr<MaterialNode> make_skin_volume_preset() {
    auto material = make_dielectric_preset("skin", 1.40f, 0.0f);
    material->medium_density = 1.0f;
    material->medium_anisotropy = 0.75f;
    material->medium_scattering = {1.45f, 1.10f, 0.85f};
    material->medium_absorption = {0.18f, 0.32f, 0.55f};
    return material;
}

inline std::shared_ptr<MaterialNode> make_material_preset(MaterialPresetKind kind) {
    switch (kind) {
    case MaterialPresetKind::Gold:
        return make_metal_preset("gold", {0.17f, 0.35f, 1.50f}, {3.10f, 2.70f, 1.90f}, 0.08f);
    case MaterialPresetKind::Copper:
        return make_metal_preset("copper", {0.20f, 0.92f, 1.10f}, {3.61f, 2.62f, 2.29f}, 0.04f);
    case MaterialPresetKind::Aluminum:
        return make_metal_preset("aluminum", {1.20f, 0.95f, 0.80f}, {7.00f, 6.00f, 5.00f}, 0.03f);
    case MaterialPresetKind::ClearGlass:
        return make_dielectric_preset("clear_glass", 1.5f, 0.0f);
    case MaterialPresetKind::DiamondGlass:
        return make_dielectric_preset("diamond_glass", 2.4f, 0.15f);
    case MaterialPresetKind::WovenFabric:
        return make_woven_fabric_preset();
    case MaterialPresetKind::AutomotivePaint:
        return make_automotive_paint_preset();
    case MaterialPresetKind::SkinVolume:
        return make_skin_volume_preset();
    }
    throw std::runtime_error("unknown material preset kind");
}

inline std::shared_ptr<MaterialNode> make_material_preset(std::string_view name) {
    if (name == "gold") return make_material_preset(MaterialPresetKind::Gold);
    if (name == "copper") return make_material_preset(MaterialPresetKind::Copper);
    if (name == "aluminum") return make_material_preset(MaterialPresetKind::Aluminum);
    if (name == "clear_glass") return make_material_preset(MaterialPresetKind::ClearGlass);
    if (name == "diamond_glass") return make_material_preset(MaterialPresetKind::DiamondGlass);
    if (name == "woven_fabric") return make_material_preset(MaterialPresetKind::WovenFabric);
    if (name == "automotive_paint") return make_material_preset(MaterialPresetKind::AutomotivePaint);
    if (name == "skin") return make_material_preset(MaterialPresetKind::SkinVolume);
    throw std::runtime_error("unknown material preset: " + std::string(name));
}

} // namespace ure::scene_ir
