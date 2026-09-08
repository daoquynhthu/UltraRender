#include <algorithm>
#include <cstdio>
#include <numbers>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <ure/native_scene_ir.hpp>
#include <ure/native_scene_uuid.hpp>
#include <ure/product/product_material.hpp>

namespace {

int failures{};

void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

ure::native_scene::NativeSceneArchive make_archive(
    const std::vector<std::shared_ptr<ure::scene_ir::MaterialNode>>& materials,
    std::string document_id = "scene/product-material") {
    ure::scene_ir::SceneIR scene;
    scene.materials = materials;
    ure::native_scene::SceneDocument document;
    document.id = std::move(document_id);
    document.schema_version = {1, 0};
    return ure::native_scene::make_native_scene_archive(
        std::move(document), scene);
}

ure::native_scene::NativeSceneArchive make_archive(
    const std::shared_ptr<ure::scene_ir::MaterialNode>& material) {
    return make_archive(
        std::vector<std::shared_ptr<ure::scene_ir::MaterialNode>>{material});
}

ure::native_scene::NativeSceneArchive make_textured_archive(
    const std::string& uri) {
    ure::scene_ir::SceneIR scene;
    const auto image = scene.register_image("albedo", uri);
    const auto texture = scene.register_texture("albedo", image);
    auto material = std::make_shared<ure::scene_ir::MaterialNode>();
    material->name = "textured";
    ure::scene_ir::MaterialGraph graph;
    ure::scene_ir::MaterialGraphNode texture_node;
    texture_node.kind = ure::scene_ir::MaterialGraphNodeKind::Texture2D;
    texture_node.texture = texture;
    const auto texture_id = graph.add_node(std::move(texture_node));
    ure::scene_ir::MaterialGraphNode surface;
    surface.kind = ure::scene_ir::MaterialGraphNodeKind::BsdfLambert;
    surface.inputs = {
        ure::scene_ir::material_graph_input("base_color", texture_id)};
    const auto surface_id = graph.add_node(std::move(surface));
    ure::scene_ir::MaterialGraphNode output;
    output.kind = ure::scene_ir::MaterialGraphNodeKind::OutputSurface;
    output.inputs = {
        ure::scene_ir::material_graph_input("surface", surface_id)};
    graph.output_node_id = graph.add_node(std::move(output));
    material->graph =
        std::make_shared<ure::scene_ir::MaterialGraph>(std::move(graph));
    scene.materials.push_back(std::move(material));
    ure::native_scene::SceneDocument document;
    document.id = "scene/product-material";
    document.schema_version = {1, 0};
    return ure::native_scene::make_native_scene_archive(
        std::move(document), scene);
}

ure::native_scene::NativeSceneArchive make_spectral_archive(
    const std::string& uri) {
    auto material = std::make_shared<ure::scene_ir::MaterialNode>();
    material->name = "spectral";
    material->spectral_extension =
        std::make_shared<ure::scene_ir::SpectralMaterialExtension>();
    material->spectral_extension->spectral_bands = 2;
    material->spectral_extension->albedo_spd = uri;
    return make_archive(material);
}

bool nonzero_identity(const ure::product::Identity& identity) {
    return std::ranges::any_of(identity,
                               [](const auto value) { return value != 0; });
}

ure::scene_ir::MaterialGraph make_surface_graph(
    ure::scene_ir::MaterialGraphNode surface) {
    ure::scene_ir::MaterialGraph graph;
    const auto surface_id = graph.add_node(std::move(surface));
    ure::scene_ir::MaterialGraphNode output;
    output.kind = ure::scene_ir::MaterialGraphNodeKind::OutputSurface;
    output.inputs = {
        ure::scene_ir::material_graph_input("surface", surface_id)};
    graph.output_node_id = graph.add_node(std::move(output));
    return graph;
}

bool has_integrator(const ure::product::ProductMaterialProgram& program,
                    std::string_view name) {
    return std::ranges::find(program.applicable_integrators, name) !=
           program.applicable_integrators.end();
}

}

int main() {
    auto legacy_material = std::make_shared<ure::scene_ir::MaterialNode>();
    legacy_material->name = "legacy-lambert";
    auto legacy = make_archive(legacy_material);
    ure::RenderConfig config;
    const auto first = ure::product::canonicalize_product_materials(
        legacy.scene, legacy.source_ids, legacy.object_uuids, config);
    check(first.programs.size() == 1,
          "legacy material did not produce one canonical program");
    check(legacy.scene.materials.front()->graph &&
              !legacy.scene.materials.front()->graph->empty(),
          "legacy material was not converted to MaterialGraph");
    if (!first.programs.empty()) {
        const auto& program = first.programs.front();
        check(program.origin ==
                  ure::product::ProductMaterialOrigin::CanonicalizedLegacy,
              "legacy material origin was not retained");
        check(program.update_class ==
                  ure::product::ProductMaterialUpdateClass::HotUpdate,
              "ordinary material update class is not hot update");
        check(has_integrator(program, "wavefront") &&
                  has_integrator(program, "bdpt"),
              "ordinary material integrator applicability is incomplete");
    }
    auto repeated_scene = legacy.scene;
    const auto repeated = ure::product::canonicalize_product_materials(
        repeated_scene, legacy.source_ids, legacy.object_uuids, config);
    check(first.identity == repeated.identity,
          "canonical material program identity is nondeterministic");
    const auto material_uuid =
        ure::native_scene::format_uuid(legacy.object_uuids.materials.front());
    const std::vector<ure::native_scene::FeatureDeclaration> preset_features{
        {"ure.adapter.material-preset", {1, 0},
         ure::native_scene::RequirementLevel::Optional, "ure", {},
         "{\"material_uuid\":\"" + material_uuid + "\"}"}};
    auto preset_scene = legacy.scene;
    const auto preset_programs =
        ure::product::canonicalize_product_materials(
            preset_scene, legacy.source_ids, legacy.object_uuids, config,
            preset_features);
    check(preset_programs.programs.front().origin ==
                  ure::product::ProductMaterialOrigin::Preset,
          "targeted preset provenance was not retained");
    check(!preset_programs.programs.front().compiler_semantic.empty() &&
              !preset_programs.programs.front().backend_semantic.empty(),
          "material program omitted compiler or backend semantic");
    auto spectral_config = config;
    spectral_config.spectral_domain_bins = 64;
    const auto spectral_domain = ure::product::canonicalize_product_materials(
        repeated_scene, legacy.source_ids, legacy.object_uuids,
        spectral_config);
    check(first.identity != spectral_domain.identity,
          "material program identity does not bind spectral domain");

    auto output_scene = repeated_scene;
    auto& output_graph = *output_scene.materials.front()->graph;
    const auto old_output = output_graph.require_node(
        output_graph.output_node_id, "old output");
    auto alternate_output = old_output;
    alternate_output.id = ure::scene_ir::kInvalidMaterialGraphNode;
    output_graph.output_node_id = output_graph.add_node(
        std::move(alternate_output));
    const auto output_changed = ure::product::canonicalize_product_materials(
        output_scene, legacy.source_ids, legacy.object_uuids, config);
    check(first.identity != output_changed.identity,
          "material program identity omitted graph output selection");

    auto dispersion_material = std::make_shared<ure::scene_ir::MaterialNode>();
    dispersion_material->dispersion = 0.025f;
    dispersion_material->thin_film_thickness = 4.0e-7f;
    dispersion_material->thin_film_ior = 1.7f;
    auto dispersion_archive = make_archive(dispersion_material);
    const auto dispersion_programs =
        ure::product::canonicalize_product_materials(
            dispersion_archive.scene, dispersion_archive.source_ids,
            dispersion_archive.object_uuids, config);
    auto plain_archive = make_archive(
        std::make_shared<ure::scene_ir::MaterialNode>());
    auto plain_programs = ure::product::canonicalize_product_materials(
        plain_archive.scene, plain_archive.source_ids,
        plain_archive.object_uuids, config);
    check(plain_programs.identity != dispersion_programs.identity,
          "material identity omitted dispersion or thin-film execution state");
    const auto bound_once = plain_programs.identity;
    ure::product::bind_product_material_resources(plain_programs, {});
    const auto rebound_once = plain_programs.identity;
    ure::product::bind_product_material_resources(plain_programs, {});
    check(plain_programs.identity == rebound_once &&
              rebound_once != bound_once,
          "material resource binding is not deterministic and idempotent");

    auto world_medium = legacy.scene;
    world_medium.medium_density = 0.01f;
    world_medium.medium_phase = ure::scene_ir::VolumePhaseFunction::Mie;
    auto mie = std::make_shared<ure::scene_ir::MiePhaseResource>();
    mie->wavelengths_nm = {500.0f, 600.0f};
    mie->cos_theta = {-1.0f, 1.0f};
    const float isotropic = 1.0f /
                            (4.0f * std::numbers::pi_v<float>);
    mie->phase = {isotropic, isotropic, isotropic, isotropic};
    mie->scattering_cross_section_m2 = {1.0e-12f, 1.0e-12f};
    mie->extinction_cross_section_m2 = {1.1e-12f, 1.1e-12f};
    world_medium.medium_mie_resource = mie;
    const auto world_programs =
        ure::product::canonicalize_product_materials(
            world_medium, legacy.source_ids, legacy.object_uuids, config);
    check(world_programs.programs.size() == 2,
          "world medium did not produce a canonical program");
    if (world_programs.programs.size() == 2) {
        const auto& program = world_programs.programs.back();
        check((program.features &
               ure::product::ProductMaterialFeatureMie) != 0 &&
                  program.update_class ==
                      ure::product::ProductMaterialUpdateClass::FullSnapshotReplacement,
              "world Mie program lost feature or update semantics");
        check(!has_integrator(program, "restir_di") &&
                  has_integrator(program, "restir_pt"),
              "world medium integrator applicability is incorrect");
    }
    auto missing_world_mie = legacy.scene;
    missing_world_mie.medium_density = 0.01f;
    missing_world_mie.medium_phase =
        ure::scene_ir::VolumePhaseFunction::Mie;
    try {
        (void)ure::product::canonicalize_product_materials(
            missing_world_mie, legacy.source_ids,
            legacy.object_uuids, config);
        check(false, "world Mie medium without a resource was accepted");
    } catch (const ure::product::ProductMaterialError& error) {
        check(error.detail() == 702,
              "missing world Mie resource used the wrong detail");
    }
    auto changed_scene = legacy.scene;
    auto& changed_nodes = changed_scene.materials.front()->graph->nodes;
    const auto color_node = std::ranges::find(
        changed_nodes, ure::scene_ir::MaterialGraphNodeKind::ConstantColor,
        &ure::scene_ir::MaterialGraphNode::kind);
    check(color_node != changed_nodes.end(),
          "canonical legacy graph has no color node");
    if (color_node != changed_nodes.end())
        color_node->color.x = 0.25f;
    const auto changed = ure::product::canonicalize_product_materials(
        changed_scene, legacy.source_ids, legacy.object_uuids, config);
    const auto hot_update = ure::product::classify_product_material_update(
        first, changed);
    check(hot_update.update_class ==
                  ure::product::ProductMaterialUpdateClass::HotUpdate &&
              hot_update.changed_material_uuids.size() == 1 &&
              hot_update.changed_material_uuids.front() == material_uuid,
          "UUID-bound constant material update was not classified as hot");
    check(first.programs.front().material_uuid ==
              changed.programs.front().material_uuid,
          "constant material update changed stable UUID binding");

    auto texture_before_archive =
        make_textured_archive("textures/albedo-a.png");
    auto texture_after_archive =
        make_textured_archive("textures/albedo-b.png");
    const auto texture_before = ure::product::canonicalize_product_materials(
        texture_before_archive.scene, texture_before_archive.source_ids,
        texture_before_archive.object_uuids, config);
    const auto texture_after = ure::product::canonicalize_product_materials(
        texture_after_archive.scene, texture_after_archive.source_ids,
        texture_after_archive.object_uuids, config);
    texture_after_archive.scene.materials.front()->graph->nodes.front()
        .texture->image->color_space = ure::scene_ir::ImageColorSpace::Linear;
    const auto texture_linear = ure::product::canonicalize_product_materials(
        texture_after_archive.scene, texture_after_archive.source_ids,
        texture_after_archive.object_uuids, config);
    check(texture_after.identity != texture_linear.identity,
          "material identity omitted texture color space");
    const auto texture_update = ure::product::classify_product_material_update(
        texture_before, texture_after);
    check(texture_update.update_class ==
                  ure::product::ProductMaterialUpdateClass::PartialRebuild &&
              texture_update.changed_material_uuids.size() == 1 &&
              texture_before.programs.front().material_uuid ==
                  texture_after.programs.front().material_uuid,
          "texture identity change was not classified as a UUID-bound partial rebuild");

    auto spectral_before_archive =
        make_spectral_archive("spectra/albedo-a.spd");
    auto spectral_after_archive =
        make_spectral_archive("spectra/albedo-b.spd");
    const auto spectral_before = ure::product::canonicalize_product_materials(
        spectral_before_archive.scene, spectral_before_archive.source_ids,
        spectral_before_archive.object_uuids, config);
    const auto spectral_after = ure::product::canonicalize_product_materials(
        spectral_after_archive.scene, spectral_after_archive.source_ids,
        spectral_after_archive.object_uuids, config);
    const auto spectral_update = ure::product::classify_product_material_update(
        spectral_before, spectral_after);
    check(spectral_update.update_class ==
                  ure::product::ProductMaterialUpdateClass::PartialRebuild &&
              spectral_update.changed_material_uuids.size() == 1 &&
              spectral_before.programs.front().material_uuid ==
                  spectral_after.programs.front().material_uuid,
          "spectral resource identity change was not classified as a UUID-bound partial rebuild");
    auto spectral_roles_a = std::make_shared<ure::scene_ir::MaterialNode>();
    spectral_roles_a->spectral_extension =
        std::make_shared<ure::scene_ir::SpectralMaterialExtension>();
    spectral_roles_a->spectral_extension->albedo_spd = "spectra/a.spd";
    spectral_roles_a->spectral_extension->emission_spd = "spectra/b.spd";
    auto spectral_roles_b = std::make_shared<ure::scene_ir::MaterialNode>(
        *spectral_roles_a);
    spectral_roles_b->spectral_extension =
        std::make_shared<ure::scene_ir::SpectralMaterialExtension>(
            *spectral_roles_a->spectral_extension);
    std::swap(spectral_roles_b->spectral_extension->albedo_spd,
              spectral_roles_b->spectral_extension->emission_spd);
    auto role_archive_a = make_archive(
        std::vector<std::shared_ptr<ure::scene_ir::MaterialNode>>{
            spectral_roles_a},
        "scene/roles");
    auto role_archive_b = make_archive(
        std::vector<std::shared_ptr<ure::scene_ir::MaterialNode>>{
            spectral_roles_b},
        "scene/roles");
    const auto role_programs_a = ure::product::canonicalize_product_materials(
        role_archive_a.scene, role_archive_a.source_ids,
        role_archive_a.object_uuids, config);
    const auto role_programs_b = ure::product::canonicalize_product_materials(
        role_archive_b.scene, role_archive_b.source_ids,
        role_archive_b.object_uuids, config);
    check(role_programs_a.identity != role_programs_b.identity,
          "material identity omitted spectral resource roles");

    auto medium_material = std::make_shared<ure::scene_ir::MaterialNode>();
    medium_material->name = "medium";
    medium_material->medium_density = 0.1f;
    auto medium_base_archive = make_archive(
        std::make_shared<ure::scene_ir::MaterialNode>());
    auto medium_after_archive = make_archive(
        std::vector<std::shared_ptr<ure::scene_ir::MaterialNode>>{
            std::make_shared<ure::scene_ir::MaterialNode>(), medium_material});
    const auto medium_base = ure::product::canonicalize_product_materials(
        medium_base_archive.scene, medium_base_archive.source_ids,
        medium_base_archive.object_uuids, config);
    const auto medium_after = ure::product::canonicalize_product_materials(
        medium_after_archive.scene, medium_after_archive.source_ids,
        medium_after_archive.object_uuids, config);
    const auto medium_add = ure::product::classify_product_material_update(
        medium_base, medium_after);
    const auto medium_remove = ure::product::classify_product_material_update(
        medium_after, medium_base);
    check(medium_add.update_class ==
                  ure::product::ProductMaterialUpdateClass::FullSnapshotReplacement &&
              medium_remove.update_class ==
                  ure::product::ProductMaterialUpdateClass::FullSnapshotReplacement,
          "medium structural add/remove was not classified as full replacement");

    auto mie_material = std::make_shared<ure::scene_ir::MaterialNode>();
    mie_material->name = "mie";
    mie_material->medium_density = 0.1f;
    mie_material->medium_phase = ure::scene_ir::VolumePhaseFunction::Mie;
    mie_material->medium_mie_resource =
        std::make_shared<ure::scene_ir::MiePhaseResource>();
    auto mie_base_archive = make_archive(
        std::make_shared<ure::scene_ir::MaterialNode>());
    auto mie_after_archive = make_archive(
        std::vector<std::shared_ptr<ure::scene_ir::MaterialNode>>{
            std::make_shared<ure::scene_ir::MaterialNode>(), mie_material});
    const auto mie_base = ure::product::canonicalize_product_materials(
        mie_base_archive.scene, mie_base_archive.source_ids,
        mie_base_archive.object_uuids, config);
    const auto mie_after = ure::product::canonicalize_product_materials(
        mie_after_archive.scene, mie_after_archive.source_ids,
        mie_after_archive.object_uuids, config);
    const auto mie_add = ure::product::classify_product_material_update(
        mie_base, mie_after);
    const auto mie_remove = ure::product::classify_product_material_update(
        mie_after, mie_base);
    check(mie_add.update_class ==
                  ure::product::ProductMaterialUpdateClass::FullSnapshotReplacement &&
              mie_remove.update_class ==
                  ure::product::ProductMaterialUpdateClass::FullSnapshotReplacement,
          "Mie structural add/remove was not classified as full replacement");

    auto invalid_mix_material =
        std::make_shared<ure::scene_ir::MaterialNode>();
    ure::scene_ir::MaterialGraph invalid_mix_graph;
    ure::scene_ir::MaterialGraphNode dielectric_child;
    dielectric_child.kind =
        ure::scene_ir::MaterialGraphNodeKind::BsdfDielectric;
    const auto dielectric_id =
        invalid_mix_graph.add_node(std::move(dielectric_child));
    ure::scene_ir::MaterialGraphNode lambert_child;
    lambert_child.kind = ure::scene_ir::MaterialGraphNodeKind::BsdfLambert;
    const auto lambert_id =
        invalid_mix_graph.add_node(std::move(lambert_child));
    ure::scene_ir::MaterialGraphNode factor;
    factor.kind = ure::scene_ir::MaterialGraphNodeKind::ConstantFloat;
    factor.value = 0.5f;
    const auto factor_id = invalid_mix_graph.add_node(std::move(factor));
    ure::scene_ir::MaterialGraphNode invalid_mix;
    invalid_mix.kind = ure::scene_ir::MaterialGraphNodeKind::BsdfMix;
    invalid_mix.inputs = {
        ure::scene_ir::material_graph_input("a", dielectric_id),
        ure::scene_ir::material_graph_input("b", lambert_id),
        ure::scene_ir::material_graph_input("factor", factor_id)};
    const auto invalid_mix_id =
        invalid_mix_graph.add_node(std::move(invalid_mix));
    ure::scene_ir::MaterialGraphNode invalid_mix_output;
    invalid_mix_output.kind =
        ure::scene_ir::MaterialGraphNodeKind::OutputSurface;
    invalid_mix_output.inputs = {
        ure::scene_ir::material_graph_input("surface", invalid_mix_id)};
    invalid_mix_graph.output_node_id =
        invalid_mix_graph.add_node(std::move(invalid_mix_output));
    invalid_mix_material->graph =
        std::make_shared<ure::scene_ir::MaterialGraph>(
            std::move(invalid_mix_graph));
    auto invalid_mix_archive = make_archive(invalid_mix_material);
    try {
        (void)ure::product::canonicalize_product_materials(
            invalid_mix_archive.scene, invalid_mix_archive.source_ids,
            invalid_mix_archive.object_uuids, config);
        check(false, "GPU-inapplicable BSDF mix reached renderer lowering");
    } catch (const ure::product::ProductMaterialError& error) {
        check(error.detail() == 701,
              "GPU-inapplicable BSDF mix used the wrong detail");
    }

    auto diffractive_material =
        std::make_shared<ure::scene_ir::MaterialNode>();
    diffractive_material->name = "grating";
    ure::scene_ir::MaterialGraphNode grating;
    grating.kind = ure::scene_ir::MaterialGraphNodeKind::BsdfGrating;
    grating.diffraction.kind =
        ure::scene_ir::DiffractiveOperatorKind::Grating;
    grating.diffraction.period_m = 1.0e-6;
    grating.diffraction.max_order = 1;
    diffractive_material->graph =
        std::make_shared<ure::scene_ir::MaterialGraph>(
            make_surface_graph(std::move(grating)));
    auto diffractive = make_archive(diffractive_material);
    const auto diffractive_programs =
        ure::product::canonicalize_product_materials(
            diffractive.scene, diffractive.source_ids,
            diffractive.object_uuids, config);
    const auto& diffractive_program = diffractive_programs.programs.front();
    check((diffractive_program.features &
           ure::product::ProductMaterialFeatureDiffractive) != 0,
          "diffractive feature was not classified");
    check(diffractive_program.update_class ==
              ure::product::ProductMaterialUpdateClass::PartialRebuild,
          "diffractive update did not require partial rebuild");
    check(diffractive_program.applicable_integrators.size() == 1 &&
              has_integrator(diffractive_program, "wavefront"),
          "diffractive material admitted an unsupported integrator");
    auto wave_base_archive = make_archive(
        std::make_shared<ure::scene_ir::MaterialNode>());
    auto wave_full_archive = make_archive(
        std::vector<std::shared_ptr<ure::scene_ir::MaterialNode>>{
            std::make_shared<ure::scene_ir::MaterialNode>(),
            diffractive_material});
    const auto wave_base = ure::product::canonicalize_product_materials(
        wave_base_archive.scene, wave_base_archive.source_ids,
        wave_base_archive.object_uuids, config);
    const auto wave_full = ure::product::canonicalize_product_materials(
        wave_full_archive.scene, wave_full_archive.source_ids,
        wave_full_archive.object_uuids, config);
    const auto wave_add = ure::product::classify_product_material_update(
        wave_base, wave_full);
    const auto wave_remove = ure::product::classify_product_material_update(
        wave_full, wave_base);
    check(wave_add.update_class ==
                  ure::product::ProductMaterialUpdateClass::FullSnapshotReplacement &&
              wave_remove.update_class ==
                  ure::product::ProductMaterialUpdateClass::FullSnapshotReplacement,
          "wave structural add/remove was not classified as full replacement");
    auto diffractive_config = config;
    diffractive_config.integrator.mode = ure::IntegratorMode::Automatic;
    diffractive_config.automatic_integrator.enabled = true;
    ure::product::configure_product_material_execution(
        diffractive_programs, diffractive_config);
    check(diffractive_config.integrator.mode ==
                  ure::IntegratorMode::Wavefront &&
              !diffractive_config.automatic_integrator.enabled &&
              diffractive_config.wave_optics.diffractive_materials_enabled,
          "bounded diffractive execution was not planned as wavefront");
    auto incompatible_estimator = config;
    incompatible_estimator.integrator.mode = ure::IntegratorMode::RestirDI;
    try {
        ure::product::configure_product_material_execution(
            diffractive_programs, incompatible_estimator);
        check(false, "inapplicable diffractive estimator was accepted");
    } catch (const ure::product::ProductMaterialError& error) {
        check(error.detail() == 705,
              "estimator rejection used the wrong diagnostic detail");
    }

    auto fluorescent_material =
        std::make_shared<ure::scene_ir::MaterialNode>();
    fluorescent_material->name = "fluorescent";
    ure::scene_ir::MaterialGraphNode fluorescence;
    fluorescence.kind =
        ure::scene_ir::MaterialGraphNodeKind::BsdfFluorescence;
    fluorescence.fluorescence.resource_id = "fluorescence/reference";
    fluorescence.fluorescence.excitation_wavelengths_nm = {400.0f, 500.0f};
    fluorescence.fluorescence.emission_wavelengths_nm = {600.0f, 700.0f};
    fluorescence.fluorescence.excitation_efficiency = {0.8f, 0.6f};
    fluorescence.fluorescence.quantum_yield = {0.5f, 0.4f};
    fluorescence.fluorescence.emission_pdf_per_nm = {
        0.01f, 0.01f, 0.01f, 0.01f};
    fluorescence.fluorescence.lifetime_seconds = 0.002;
    fluorescent_material->graph =
        std::make_shared<ure::scene_ir::MaterialGraph>(
            make_surface_graph(std::move(fluorescence)));
    auto fluorescent = make_archive(fluorescent_material);
    const auto fluorescent_programs =
        ure::product::canonicalize_product_materials(
            fluorescent.scene, fluorescent.source_ids,
            fluorescent.object_uuids, config);
    check((fluorescent_programs.programs.front().features &
           ure::product::ProductMaterialFeatureFluorescent) != 0,
          "fluorescence feature was not classified");
    auto coherent_config = config;
    coherent_config.wave_optics.mode = ure::WaveOpticsMode::CoherentField;
    coherent_config.wave_optics.coherent_field_enabled = true;
    try {
        ure::product::configure_product_material_execution(
            fluorescent_programs, coherent_config);
        check(false, "coherent production material session was accepted");
    } catch (const ure::product::ProductMaterialError& error) {
        check(error.detail() == 704,
              "coherent rejection used the wrong diagnostic detail");
    }

    auto invalid_material =
        std::make_shared<ure::scene_ir::MaterialNode>();
    ure::scene_ir::MaterialGraphNode invalid_surface;
    invalid_surface.kind =
        ure::scene_ir::MaterialGraphNodeKind::BsdfFluorescence;
    invalid_material->graph =
        std::make_shared<ure::scene_ir::MaterialGraph>(
            make_surface_graph(std::move(invalid_surface)));
    auto invalid = make_archive(invalid_material);
    try {
        (void)ure::product::canonicalize_product_materials(
            invalid.scene, invalid.source_ids, invalid.object_uuids, config);
        check(false, "invalid fluorescence contract was accepted");
    } catch (const ure::product::ProductMaterialError& error) {
        check(error.detail() == 704 && !error.field_path().empty() &&
                  !error.recovery().empty(),
              "invalid wave material diagnostic is incomplete");
    }

    auto incomplete = make_archive(
        std::make_shared<ure::scene_ir::MaterialNode>());
    incomplete.source_ids.materials.clear();
    try {
        (void)ure::product::canonicalize_product_materials(
            incomplete.scene, incomplete.source_ids,
            incomplete.object_uuids, config);
        check(false, "incomplete material identity was accepted");
    } catch (const ure::product::ProductMaterialError& error) {
        check(error.detail() == 700,
              "incomplete identity used the wrong diagnostic detail");
    }

    auto hydra_material = std::make_shared<ure::scene_ir::MaterialNode>();
    hydra_material->name = "hydra-derived";
    auto hydra = make_archive(hydra_material);
    hydra.document.features.push_back(
        {"ure.adapter.hydra", {1, 0},
         ure::native_scene::RequirementLevel::Optional, "ure", {}, "{}"});
    const auto hydra_programs = ure::product::canonicalize_product_materials(
        hydra.scene, hydra.source_ids, hydra.object_uuids, config,
        hydra.document.features);
    check(hydra.scene.materials.front()->graph &&
              !hydra.scene.materials.front()->graph->empty(),
          "Hydra-derived archive did not produce a canonical MaterialGraph");
    check(hydra_programs.programs.size() == 1 &&
              hydra_programs.programs.front().origin ==
                  ure::product::ProductMaterialOrigin::Hydra &&
              nonzero_identity(hydra_programs.programs.front().identity) &&
              nonzero_identity(hydra_programs.identity),
          "Hydra-derived canonical material did not retain artifact identity");

    auto unsupported_material =
        std::make_shared<ure::scene_ir::MaterialNode>();
    ure::scene_ir::MaterialGraphNode unsupported_surface;
    unsupported_surface.kind =
        ure::scene_ir::MaterialGraphNodeKind::Checker2D;
    unsupported_material->graph =
        std::make_shared<ure::scene_ir::MaterialGraph>(
            make_surface_graph(std::move(unsupported_surface)));
    auto unsupported = make_archive(unsupported_material);
    try {
        (void)ure::product::canonicalize_product_materials(
            unsupported.scene, unsupported.source_ids,
            unsupported.object_uuids, config);
        check(false, "unsupported surface entered a fallback material");
    } catch (const ure::product::ProductMaterialError& error) {
        check(error.detail() == 701 &&
                  error.code() == "URE-PRV3-MATERIAL-SURFACE-002" &&
                  error.field_path() == "/graph/surface" &&
                  !error.recovery().empty(),
              "unsupported surface fallback rejection diagnostic is incomplete");
    }

    std::printf("PRV.3 product material checks: %s\n",
                failures == 0 ? "PASSED" : "FAILED");
    return failures == 0 ? 0 : 1;
}
