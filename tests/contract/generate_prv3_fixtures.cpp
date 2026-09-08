#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <ure/native_adapter.hpp>
#include <ure/native_scene_tooling.hpp>

namespace {

using ure::scene_ir::MaterialGraph;
using ure::scene_ir::MaterialGraphNode;
using ure::scene_ir::MaterialGraphNodeKind;

void write_checker_bmp(const std::filesystem::path& path) {
    std::filesystem::create_directories(path.parent_path());
    std::vector<std::uint8_t> bytes(70, 0);
    const auto u16 = [&bytes](std::size_t offset, std::uint16_t value) {
        bytes[offset] = static_cast<std::uint8_t>(value);
        bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
    };
    const auto u32 = [&bytes](std::size_t offset, std::uint32_t value) {
        for (std::size_t index = 0; index < 4; ++index)
            bytes[offset + index] = static_cast<std::uint8_t>(
                value >> (index * 8));
    };
    bytes[0] = 'B';
    bytes[1] = 'M';
    u32(2, static_cast<std::uint32_t>(bytes.size()));
    u32(10, 54);
    u32(14, 40);
    u32(18, 2);
    u32(22, 2);
    u16(26, 1);
    u16(28, 24);
    u32(34, 16);
    const std::uint8_t pixels[] = {
        255, 32, 16, 255, 255, 255, 0, 0,
        16, 32, 255, 16, 255, 32, 0, 0};
    std::ranges::copy(pixels, bytes.begin() + 54);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!output)
        throw std::runtime_error("Checker texture publication failed");
}

void write_reflectance_spd(const std::filesystem::path& path) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << "400 0.10\n500 0.35\n600 0.70\n700 0.25\n";
    if (!output)
        throw std::runtime_error("Spectral fixture publication failed");
}

MaterialGraph surface_graph(MaterialGraph graph,
                            ure::scene_ir::MaterialGraphNodeId surface) {
    MaterialGraphNode output;
    output.kind = MaterialGraphNodeKind::OutputSurface;
    output.inputs = {ure::scene_ir::material_graph_input("surface", surface)};
    graph.output_node_id = graph.add_node(std::move(output));
    return graph;
}

std::shared_ptr<ure::scene_ir::MaterialNode> material(
    std::string name, MaterialGraph graph) {
    auto result = std::make_shared<ure::scene_ir::MaterialNode>();
    result->name = std::move(name);
    result->graph = std::make_shared<MaterialGraph>(std::move(graph));
    return result;
}

std::shared_ptr<ure::scene_ir::MaterialNode> dielectric(std::string name) {
    MaterialGraph graph;
    MaterialGraphNode node;
    node.kind = MaterialGraphNodeKind::BsdfDielectric;
    const auto surface = graph.add_node(std::move(node));
    auto result = material(std::move(name),
                           surface_graph(std::move(graph), surface));
    result->model = ure::scene_ir::MaterialModel::Dielectric;
    result->base_color = {0.96f, 0.99f, 1.0f};
    result->roughness = 0.02f;
    result->ior = 1.52f;
    result->dispersion = 0.012f;
    return result;
}

std::shared_ptr<ure::scene_ir::MaterialNode> metal(std::string name) {
    MaterialGraph graph;
    MaterialGraphNode node;
    node.kind = MaterialGraphNodeKind::BsdfMetal;
    const auto surface = graph.add_node(std::move(node));
    auto result = material(std::move(name),
                           surface_graph(std::move(graph), surface));
    result->model = ure::scene_ir::MaterialModel::Metal;
    result->base_color = {0.92f, 0.63f, 0.22f};
    result->roughness = 0.16f;
    result->metal_eta = {0.18f, 0.45f, 1.35f};
    result->metal_k = {3.1f, 2.7f, 1.9f};
    return result;
}

std::shared_ptr<ure::scene_ir::MaterialNode> mixed(std::string name) {
    MaterialGraph graph;
    MaterialGraphNode lambert;
    lambert.kind = MaterialGraphNodeKind::BsdfLambert;
    const auto lambert_id = graph.add_node(std::move(lambert));
    MaterialGraphNode conductor;
    conductor.kind = MaterialGraphNodeKind::BsdfMetal;
    const auto conductor_id = graph.add_node(std::move(conductor));
    MaterialGraphNode factor;
    factor.kind = MaterialGraphNodeKind::ConstantFloat;
    factor.value = 0.35f;
    const auto factor_id = graph.add_node(std::move(factor));
    MaterialGraphNode mix;
    mix.kind = MaterialGraphNodeKind::BsdfMix;
    mix.inputs = {
        ure::scene_ir::material_graph_input("a", lambert_id),
        ure::scene_ir::material_graph_input("b", conductor_id),
        ure::scene_ir::material_graph_input("factor", factor_id)};
    const auto surface = graph.add_node(std::move(mix));
    auto result = material(std::move(name),
                           surface_graph(std::move(graph), surface));
    result->base_color = {0.22f, 0.45f, 0.82f};
    result->roughness = 0.3f;
    result->metal_eta = {0.3f, 0.7f, 1.2f};
    result->metal_k = {2.8f, 2.2f, 1.5f};
    return result;
}

std::shared_ptr<ure::scene_ir::MaterialNode> layered(std::string name) {
    MaterialGraph graph;
    MaterialGraphNode coating;
    coating.kind = MaterialGraphNodeKind::BsdfDielectric;
    const auto coating_id = graph.add_node(std::move(coating));
    MaterialGraphNode substrate;
    substrate.kind = MaterialGraphNodeKind::BsdfLambert;
    const auto substrate_id = graph.add_node(std::move(substrate));
    MaterialGraphNode thickness;
    thickness.kind = MaterialGraphNodeKind::ConstantFloat;
    thickness.value = 4.0e-7f;
    const auto thickness_id = graph.add_node(std::move(thickness));
    MaterialGraphNode layer;
    layer.kind = MaterialGraphNodeKind::BsdfLayer;
    layer.inputs = {
        ure::scene_ir::material_graph_input("coating", coating_id),
        ure::scene_ir::material_graph_input("substrate", substrate_id),
        ure::scene_ir::material_graph_input("thickness", thickness_id)};
    const auto surface = graph.add_node(std::move(layer));
    auto result = material(std::move(name),
                           surface_graph(std::move(graph), surface));
    result->base_color = {0.65f, 0.12f, 0.08f};
    result->roughness = 0.22f;
    result->ior = 1.45f;
    return result;
}

std::shared_ptr<ure::scene_ir::MaterialNode> textured(
    std::string name, ure::scene_ir::SceneIR& scene) {
    auto image = scene.register_image(
        "prv3-checker", "textures/checker.bmp",
        ure::scene_ir::ImageColorSpace::SRGB);
    auto texture = scene.register_texture("prv3-checker", image);
    MaterialGraph graph;
    MaterialGraphNode texture_node;
    texture_node.kind = MaterialGraphNodeKind::Texture2D;
    texture_node.texture = texture;
    const auto texture_id = graph.add_node(std::move(texture_node));
    MaterialGraphNode lambert;
    lambert.kind = MaterialGraphNodeKind::BsdfLambert;
    lambert.inputs = {
        ure::scene_ir::material_graph_input("base_color", texture_id)};
    const auto surface = graph.add_node(std::move(lambert));
    return material(std::move(name),
                    surface_graph(std::move(graph), surface));
}

std::shared_ptr<ure::scene_ir::MaterialNode> spectral(std::string name) {
    MaterialGraph graph;
    MaterialGraphNode lambert;
    lambert.kind = MaterialGraphNodeKind::BsdfLambert;
    const auto surface = graph.add_node(std::move(lambert));
    auto result = material(std::move(name),
                           surface_graph(std::move(graph), surface));
    result->spectral_extension =
        std::make_shared<ure::scene_ir::SpectralMaterialExtension>();
    result->spectral_extension->spectral_bands = 4;
    result->spectral_extension->albedo_spd =
        "spectra/reflectance.spd";
    return result;
}

std::shared_ptr<ure::scene_ir::MaterialNode> diffractive(
    std::string name) {
    MaterialGraph graph;
    MaterialGraphNode grating;
    grating.kind = MaterialGraphNodeKind::BsdfGrating;
    grating.diffraction.kind =
        ure::scene_ir::DiffractiveOperatorKind::Grating;
    grating.diffraction.period_m = 1.25e-6;
    grating.diffraction.orientation_rad = 0.35;
    grating.diffraction.max_order = 2;
    const auto surface = graph.add_node(std::move(grating));
    return material(std::move(name),
                    surface_graph(std::move(graph), surface));
}

std::shared_ptr<ure::scene_ir::MaterialNode> fluorescent(
    std::string name) {
    MaterialGraph graph;
    MaterialGraphNode node;
    node.kind = MaterialGraphNodeKind::BsdfFluorescence;
    node.fluorescence.resource_id = "fluorescence/prv3";
    node.fluorescence.excitation_wavelengths_nm = {400.0f, 520.0f};
    node.fluorescence.emission_wavelengths_nm = {560.0f, 700.0f};
    node.fluorescence.excitation_efficiency = {0.85f, 0.55f};
    node.fluorescence.quantum_yield = {0.65f, 0.45f};
    node.fluorescence.emission_pdf_per_nm = {
        1.0f / 140.0f, 1.0f / 140.0f,
        1.0f / 140.0f, 1.0f / 140.0f};
    node.fluorescence.lifetime_seconds = 1.0e-6;
    const auto surface = graph.add_node(std::move(node));
    return material(std::move(name),
                    surface_graph(std::move(graph), surface));
}

void assign_instance(ure::scene_ir::SceneIR& scene,
                     std::string_view selector,
                     std::shared_ptr<ure::scene_ir::MaterialNode> value) {
    const auto found = std::ranges::find_if(
        scene.instances, [selector](const auto& instance) {
            return instance.name.find(selector) != std::string::npos;
        });
    if (found == scene.instances.end())
        throw std::runtime_error("Cornell instance is unavailable: " +
                                 std::string(selector));
    scene.materials.push_back(value);
    found->material = std::move(value);
}

ure::native_scene::NativeSceneArchive archive(
    const ure::scene_ir::SceneIR& scene,
    std::string id,
    const std::filesystem::path& root) {
    ure::native_scene::SceneDocument document;
    document.id = std::move(id);
    document.schema_version = {1, 0};
    document.features.push_back(
        {"ure.adapter.gltf", {1, 0},
         ure::native_scene::RequirementLevel::Optional, "ure", {}, "{}"});
    auto result = ure::native_scene::make_native_scene_archive(
        std::move(document), scene);
    result.execution_root = root;
    return result;
}

void configure_frame(ure::scene_ir::SceneIR& scene, int width, int height) {
    scene.width = width;
    scene.height = height;
    scene.spp = 0;
    scene.camera.aspect_ratio = static_cast<float>(width) /
                                static_cast<float>(height);
}

}

int main(int argc, char** argv) {
    try {
        if (argc != 4)
            throw std::invalid_argument(
                "usage: generate_prv3_fixtures <cornell.gltf> <q3-scene> <output-directory>");
        const auto imported = ure::native_scene::import_gltf_native(
            std::filesystem::absolute(argv[1]));
        if (!imported.ok())
            throw std::runtime_error(imported.diagnostics.empty()
                                         ? "Cornell import failed"
                                         : imported.diagnostics.front().message);
        const auto q3 = ure::native_scene::load_native_asset(
            std::filesystem::absolute(argv[2]));
        if (!q3.ok() || !q3.value)
            throw std::runtime_error("Mie source fixture failed to load");
        std::shared_ptr<const ure::scene_ir::MiePhaseResource> mie;
        for (const auto& source : q3.value->scene.materials) {
            if (source && source->medium_mie_resource) {
                mie = source->medium_mie_resource;
                break;
            }
        }
        if (!mie)
            throw std::runtime_error("Mie source fixture has no phase resource");
        const auto output = std::filesystem::absolute(argv[3]);
        std::filesystem::create_directories(output);
        write_checker_bmp(output / "textures/checker.bmp");
        write_reflectance_spd(output / "spectra/reflectance.spd");

        auto matrix = imported.archive.scene;
        configure_frame(matrix, 854, 480);
        assign_instance(matrix, "tall_box", dielectric("prv3-glass"));
        assign_instance(matrix, "short_box", metal("prv3-metal"));
        assign_instance(matrix, "back_wall", mixed("prv3-mix"));
        assign_instance(matrix, "left_wall", layered("prv3-layer"));
        assign_instance(matrix, "right_wall", textured("prv3-texture", matrix));
        assign_instance(matrix, "floor", spectral("prv3-spectral"));
        ure::native_scene::save_native_scene(
            output / "material_matrix_854x480.urescene",
            archive(matrix, "scene/preview/prv3-material-matrix", output));
        configure_frame(matrix, 1280, 720);
        ure::native_scene::save_native_scene(
            output / "material_matrix_1280x720.urescene",
            archive(matrix, "scene/preview/prv3-material-matrix-quality",
                    output));

        auto glass = imported.archive.scene;
        configure_frame(glass, 854, 480);
        assign_instance(glass, "tall_box", dielectric("prv3-glass-tall"));
        assign_instance(glass, "short_box", dielectric("prv3-glass-short"));
        ure::native_scene::save_native_scene(
            output / "glass_854x480.urescene",
            archive(glass, "scene/preview/prv3-glass-functional", output));
        configure_frame(glass, 1280, 720);
        ure::native_scene::save_native_scene(
            output / "glass_1280x720.urescene",
            archive(glass, "scene/preview/prv3-glass", output));

        auto volume = imported.archive.scene;
        configure_frame(volume, 1280, 720);
        volume.medium_density = 2.5e10f;
        volume.medium_phase = ure::scene_ir::VolumePhaseFunction::Mie;
        volume.medium_mie_resource = mie;
        volume.medium_scattering = {};
        volume.medium_absorption = {};
        volume.medium_max_distance = 12.0f;
        configure_frame(volume, 854, 480);
        ure::native_scene::save_native_scene(
            output / "mie_volume_854x480.urescene",
            archive(volume, "scene/preview/prv3-mie-volume-functional", output));
        configure_frame(volume, 1280, 720);
        ure::native_scene::save_native_scene(
            output / "mie_volume_1280x720.urescene",
            archive(volume, "scene/preview/prv3-mie-volume", output));

        auto grating = imported.archive.scene;
        configure_frame(grating, 1280, 720);
        assign_instance(grating, "tall_box",
                        diffractive("prv3-diffractive"));
        configure_frame(grating, 854, 480);
        ure::native_scene::save_native_scene(
            output / "diffractive_854x480.urescene",
            archive(grating, "scene/preview/prv3-diffractive-functional", output));
        configure_frame(grating, 1280, 720);
        ure::native_scene::save_native_scene(
            output / "diffractive_1280x720.urescene",
            archive(grating, "scene/preview/prv3-diffractive", output));

        auto fluorescence = imported.archive.scene;
        configure_frame(fluorescence, 1280, 720);
        assign_instance(fluorescence, "tall_box",
                        fluorescent("prv3-fluorescent"));
        configure_frame(fluorescence, 854, 480);
        ure::native_scene::save_native_scene(
            output / "fluorescent_854x480.urescene",
            archive(fluorescence,
                    "scene/preview/prv3-fluorescent-functional", output));
        configure_frame(fluorescence, 1280, 720);
        ure::native_scene::save_native_scene(
            output / "fluorescent_1280x720.urescene",
            archive(fluorescence, "scene/preview/prv3-fluorescent", output));
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
