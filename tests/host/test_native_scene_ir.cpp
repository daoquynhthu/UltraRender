#include <cstdio>
#include <cmath>
#include <memory>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <ure/native_scene_ir.hpp>
#include <ure/detail/cuda_scene_compiler.hpp>

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(condition) do { if (condition) { ++g_passed; } else { ++g_failed; std::fprintf(stderr, "CHECK failed: %s at line %d\n", #condition, __LINE__); } } while (0)

static bool has_code(const ure::native_scene::ValidationReport& report, const std::string& code) {
    for (const auto& diagnostic : report.diagnostics) {
        if (diagnostic.code == code) return true;
    }
    return false;
}

template <typename T>
static bool has_code(const ure::native_scene::LoadResult<T>& result, const std::string& code) {
    for (const auto& diagnostic : result.diagnostics) {
        if (diagnostic.code == code) return true;
    }
    return false;
}

static void test_archive_identity_defaults() {
    ure::scene_ir::SceneIR scene;
    scene.materials.push_back(std::make_shared<ure::scene_ir::MaterialNode>());

    ure::native_scene::SceneDocument document;
    document.id = "scene/q3";
    document.schema_version = ure::native_scene::kSceneSchemaVersion;

    const auto archive = ure::native_scene::make_native_scene_archive(document, scene);
    CHECK(archive.source_ids.materials == std::vector<std::string>{"material/00000000"});
}

static void test_archive_deep_freeze_and_identity_validation() {
    auto image = std::make_shared<ure::scene_ir::ImageResource>();
    image->name = "image";
    image->uri = "textures/source.ppm";
    auto texture = std::make_shared<ure::scene_ir::TextureResource>();
    texture->name = "texture";
    texture->image = image;
    auto material = std::make_shared<ure::scene_ir::MaterialNode>();
    material->name = "material";
    material->base_color_texture = texture;
    auto graph = std::make_shared<ure::scene_ir::MaterialGraph>();
    ure::scene_ir::MaterialGraphNode node;
    node.id = 1;
    node.texture = texture;
    graph->nodes.push_back(node);
    graph->output_node_id = 1;
    material->graph = graph;

    ure::scene_ir::SceneIR scene;
    scene.images.push_back(image);
    scene.textures.push_back(texture);
    scene.materials.push_back(material);
    ure::native_scene::SceneDocument document;
    document.id = "scene/freeze";
    document.schema_version = ure::native_scene::kSceneSchemaVersion;
    auto archive = ure::native_scene::make_native_scene_archive(document, scene);

    image->uri = "mutated.exr";
    texture->name = "mutated";
    material->name = "mutated";
    graph->nodes[0].name = "mutated";
    CHECK(archive.scene.images[0]->uri == "textures/source.ppm");
    CHECK(archive.scene.textures[0]->name == "texture");
    CHECK(archive.scene.materials[0]->name == "material");
    CHECK(archive.scene.materials[0]->graph->nodes[0].name.empty());
    CHECK(archive.scene.materials[0]->base_color_texture == archive.scene.textures[0]);
    CHECK(archive.scene.materials[0]->graph->nodes[0].texture == archive.scene.textures[0]);

    archive.source_ids.materials.clear();
    CHECK(has_code(ure::native_scene::validate_scene_ir_archive(archive), "URE-Q3-ID-001"));
    archive.source_ids.materials = {"Material/Bad"};
    CHECK(has_code(ure::native_scene::validate_scene_ir_archive(archive), "URE-Q3-ID-003"));
}

static ure::scene_ir::SceneIR make_roundtrip_scene() {
    ure::scene_ir::SceneIR scene;
    auto image = scene.register_image("albedo", "textures/albedo.ppm", ure::scene_ir::ImageColorSpace::Linear);
    auto texture = scene.register_texture("albedo", image, 2);
    auto material = std::make_shared<ure::scene_ir::MaterialNode>();
    material->name = "shared";
    material->model = ure::scene_ir::MaterialModel::Metal;
    material->base_color = {0.1f, -0.0f, 0.3f};
    material->roughness = 0.25f;
    material->metal_eta = {1.0f, 2.0f, 3.0f};
    material->metal_k = {4.0f, 5.0f, 6.0f};
    material->base_color_texture = texture;
    scene.materials.push_back(material);

    auto mesh = std::make_shared<ure::Mesh>();
    mesh->vertices = {
        {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},
        {{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},
        {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}}
    };
    mesh->indices = {0, 1, 2};
    auto mesh_resource = scene.register_mesh("triangle", mesh);
    ure::scene_ir::InstanceNode instance;
    instance.name = "instance";
    instance.mesh = mesh_resource;
    instance.material = material;
    instance.position = {1.0f, 2.0f, 3.0f};
    instance.rotation = {0.5f, 0.1f, 0.2f, 0.3f};
    scene.instances.push_back(instance);
    scene.width = 64;
    scene.height = 32;
    scene.spp = 8;
    return scene;
}

static void test_binary_roundtrip() {
    ure::native_scene::SceneDocument document;
    document.id = "scene/roundtrip";
    document.schema_version = ure::native_scene::kSceneSchemaVersion;
    const auto archive = ure::native_scene::make_native_scene_archive(document, make_roundtrip_scene());
    const auto first = ure::native_scene::write_scene_ir_binary(archive);
    const auto second = ure::native_scene::write_scene_ir_binary(archive);
    CHECK(first == second);
    const auto loaded = ure::native_scene::read_scene_ir_binary(first, {});
    CHECK(loaded.ok());
    if (!loaded.value) return;
    CHECK(loaded.value->scene.width == 64);
    CHECK(loaded.value->scene.height == 32);
    CHECK(loaded.value->scene.spp == 8);
    CHECK(loaded.value->scene.materials[0]->base_color_texture == loaded.value->scene.textures[0]);
    CHECK(loaded.value->scene.instances[0].material == loaded.value->scene.materials[0]);
    CHECK(loaded.value->scene.instances[0].mesh == loaded.value->scene.meshes[0]);
    CHECK(loaded.value->scene.meshes[0]->mesh->indices == std::vector<int>({0, 1, 2}));
    CHECK(std::signbit(loaded.value->scene.materials[0]->base_color.y));
    CHECK(ure::native_scene::scene_ir_semantic_hash(*loaded.value) ==
          ure::native_scene::scene_ir_semantic_hash(archive));

    auto positive_zero = archive;
    positive_zero.scene.materials[0]->base_color.y = 0.0f;
    CHECK(ure::native_scene::scene_ir_semantic_hash(positive_zero) ==
          ure::native_scene::scene_ir_semantic_hash(archive));
}

static void test_exploded_roundtrip() {
    ure::native_scene::SceneDocument document;
    document.id = "scene/text";
    document.schema_version = ure::native_scene::kSceneSchemaVersion;
    const auto archive = ure::native_scene::make_native_scene_archive(document, make_roundtrip_scene());
    const auto exploded = ure::native_scene::write_scene_ir_text(archive);
    CHECK(exploded.manifest.find("\"scene_ir\"") != std::string::npos);
    CHECK(exploded.manifest.find("base64") == std::string::npos);
    CHECK(!exploded.manifest.empty() && exploded.manifest.back() == '\n');
    const auto loaded = ure::native_scene::read_scene_ir_text(exploded, {});
    CHECK(loaded.ok());
    if (loaded.value) {
        CHECK(ure::native_scene::scene_ir_semantic_hash(*loaded.value) ==
              ure::native_scene::scene_ir_semantic_hash(archive));
    }
}

static void test_file_roundtrip() {
    ure::native_scene::SceneDocument document;
    document.id = "scene/file";
    document.schema_version = ure::native_scene::kSceneSchemaVersion;
    const auto archive = ure::native_scene::make_native_scene_archive(document, make_roundtrip_scene());
    const auto root = std::filesystem::temp_directory_path() / "ure_q3_file_roundtrip";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    const auto binary_path = root / "scene.urescene";
    const auto text_path = root / "scene.ure";
    ure::native_scene::save_native_scene(binary_path, archive);
    ure::native_scene::save_native_scene(text_path, archive);
    const auto binary = ure::native_scene::load_native_scene(binary_path, {});
    const auto text = ure::native_scene::load_native_scene(text_path, {});
    CHECK(binary.ok());
    CHECK(text.ok());
    if (binary.value && text.value) {
        CHECK(ure::native_scene::scene_ir_semantic_hash(*binary.value) ==
              ure::native_scene::scene_ir_semantic_hash(*text.value));
    }
    std::filesystem::remove_all(root, error);
}

static std::shared_ptr<const ure::scene_ir::MiePhaseResource> make_mie_resource() {
    auto value = std::make_shared<ure::scene_ir::MiePhaseResource>();
    constexpr float phase = 0.07957747154594767f;
    value->wavelengths_nm = {360.0f, 830.0f};
    value->cos_theta = {-1.0f, 1.0f};
    value->phase = {phase, phase, phase, phase};
    value->cdf = {0.0f, 1.0f, 0.0f, 1.0f};
    value->scattering_cross_section_m2 = {1.0e-12f, 2.0e-12f};
    value->extinction_cross_section_m2 = {2.0e-12f, 3.0e-12f};
    value->absorption_cross_section_m2 = {1.0e-12f, 1.0e-12f};
    value->asymmetry = {0.0f, 0.0f};
    value->provenance = "q3-test";
    value->source_hash = std::string(64, '1');
    value->content_hash = "source-content";
    return value;
}

static ure::native_scene::NativeSceneArchive make_full_archive() {
    auto scene = make_roundtrip_scene();
    auto& material = scene.materials[0];
    material->ior = 1.7f;
    material->dispersion = 0.02f;
    material->thin_film_thickness = 420.0f;
    material->thin_film_ior = 1.33f;
    material->emission = {1.0f, 2.0f, 3.0f};
    material->medium_density = 0.4f;
    material->medium_anisotropy = 0.0f;
    material->medium_phase = ure::scene_ir::VolumePhaseFunction::Mie;
    material->medium_scattering = {0.0f, 0.0f, 0.0f};
    material->medium_absorption = {0.0f, 0.0f, 0.0f};
    material->normal_scale = 0.75f;
    material->roughness_texture = scene.textures[0];
    material->emission_texture = scene.textures[0];
    material->normal_texture = scene.textures[0];
    material->spectral_extension = std::make_shared<ure::scene_ir::SpectralMaterialExtension>();
    material->spectral_extension->spectral_bands = 17;
    material->spectral_extension->albedo_spd = "spectra/albedo.spd";
    material->spectral_extension->emission_spd = "spectra/emission.spd";
    material->graph = std::make_shared<ure::scene_ir::MaterialGraph>();
    for (int kind = 0; kind <= 19; ++kind) {
        ure::scene_ir::MaterialGraphNode node;
        node.id = static_cast<std::uint32_t>(kind + 1);
        node.kind = static_cast<ure::scene_ir::MaterialGraphNodeKind>(kind);
        node.name = "node-" + std::to_string(kind);
        node.color = {static_cast<float>(kind), 0.25f, 0.5f};
        node.value = static_cast<float>(kind) * 0.1f;
        if (kind == 2) node.texture = scene.textures[0];
        if (kind >= 15) {
            node.diffraction.kind =
                static_cast<
                    ure::scene_ir::DiffractiveOperatorKind>(
                    kind - 15);
            if (kind == 19) {
                node.diffraction.table_id =
                    "rcwa/full-fixture";
                ure::scene_ir::DiffractiveScatteringEntry entry;
                entry.wavelength_nm = 550.0f;
                entry.incident_cosine = 1.0f;
                entry.jones_ss.real = 0.5f;
                entry.jones_pp.real = 0.5f;
                node.diffraction.table.push_back(entry);
            }
        }
        material->graph->nodes.push_back(std::move(node));
    }
    material->graph->nodes[14].inputs.push_back({"surface", 9, "out"});
    material->graph->output_node_id = 15;
    const auto mie = make_mie_resource();
    material->medium_mie_resource = mie;
    scene.medium_mie_resource = mie;
    scene.medium_phase = ure::scene_ir::VolumePhaseFunction::Mie;
    scene.background_color = {0.01f, 0.02f, 0.03f};
    scene.medium_density = 0.5f;
    scene.medium_anisotropy = 0.0f;
    scene.medium_scattering = {0.0f, 0.0f, 0.0f};
    scene.medium_absorption = {0.0f, 0.0f, 0.0f};
    scene.medium_max_distance = 123.0f;
    scene.camera.position = {2.0f, 3.0f, 4.0f};
    scene.camera.look_at = {0.0f, 1.0f, 0.0f};
    scene.camera.up = {0.0f, 0.0f, 1.0f};
    scene.camera.fov = 60.0f;
    scene.camera.aspect_ratio = 2.0f;
    scene.camera.aperture = 0.1f;
    scene.camera.focus_dist = 12.0f;
    scene.physics.enabled = true;
    scene.physics.dt = 0.02f;
    scene.physics.total_frames = 12;
    scene.physics.spp_per_frame = 7;
    scene.physics.fluid.enabled = true;
    scene.physics.fluid.bounds_min = {-2.0f, -3.0f, -4.0f};
    scene.physics.fluid.bounds_max = {2.0f, 3.0f, 4.0f};
    scene.physics.fluid.particle_spacing = 0.2f;
    scene.physics.fluid.fill_min = {-1.0f, -1.0f, -1.0f};
    scene.physics.fluid.fill_max = {1.0f, 1.0f, 1.0f};
    scene.instances[0].rigid_body.enabled = true;
    scene.instances[0].rigid_body.mass = 3.0f;
    scene.instances[0].rigid_body.collider_type = "mesh";
    auto light_material = std::make_shared<ure::scene_ir::MaterialNode>();
    light_material->name = "fixture-light";
    light_material->model = ure::scene_ir::MaterialModel::Light;
    light_material->emission = {8.0f, 7.0f, 6.0f};
    scene.materials.push_back(light_material);
    auto analytic_medium = std::make_shared<ure::scene_ir::MaterialNode>();
    analytic_medium->name = "fixture-analytic-medium";
    analytic_medium->medium_density = 0.3f;
    analytic_medium->medium_anisotropy = -0.2f;
    analytic_medium->medium_scattering = {0.2f, 0.3f, 0.4f};
    analytic_medium->medium_absorption = {0.02f, 0.03f, 0.04f};
    scene.materials.push_back(analytic_medium);
    scene.spheres.push_back({"sphere", {0.0f, 2.0f, 0.0f}, 0.75f, analytic_medium});
    scene.quad_lights.push_back({"quad", {-1.0f, 4.0f, -1.0f}, {2.0f, 0.0f, 0.0f},
                                 {0.0f, 0.0f, 2.0f}, light_material});

    ure::native_scene::SceneDocument document;
    document.id = "scene/full";
    document.schema_version = ure::native_scene::kSceneSchemaVersion;
    return ure::native_scene::make_native_scene_archive(document, scene);
}

static void test_full_current_field_roundtrip() {
    const auto archive = make_full_archive();
    const auto binary = ure::native_scene::read_scene_ir_binary(ure::native_scene::write_scene_ir_binary(archive), {});
    const auto text = ure::native_scene::read_scene_ir_text(ure::native_scene::write_scene_ir_text(archive), {});
    CHECK(binary.ok());
    CHECK(text.ok());
    if (binary.value && text.value) {
        CHECK(binary.value->scene.materials[0]->graph->nodes.size() == 20);
        const auto& binary_diffraction =
            binary.value->scene.materials[0]->graph->nodes[19].diffraction;
        const auto& text_diffraction =
            text.value->scene.materials[0]->graph->nodes[19].diffraction;
        CHECK(binary_diffraction.table_id == "rcwa/full-fixture");
        CHECK(text_diffraction.table.size() == 1);
        CHECK(text_diffraction.table[0].jones_pp.real == 0.5f);
        CHECK(binary.value->scene.materials[0]->medium_mie_resource == binary.value->scene.medium_mie_resource);
        CHECK(text.value->scene.materials[0]->medium_mie_resource == text.value->scene.medium_mie_resource);
        CHECK(ure::native_scene::scene_ir_semantic_hash(*binary.value) ==
              ure::native_scene::scene_ir_semantic_hash(*text.value));
        CHECK(binary.value->scene.physics.fluid.particle_spacing == 0.2f);
    }
}

static void test_validation_and_compiler_boundary() {
    ure::native_scene::SceneDocument document;
    document.id = "scene/compiler";
    document.schema_version = ure::native_scene::kSceneSchemaVersion;
    auto archive = ure::native_scene::make_native_scene_archive(document, make_roundtrip_scene());
    auto loaded = ure::native_scene::read_scene_ir_binary(ure::native_scene::write_scene_ir_binary(archive), {});
    CHECK(loaded.ok());
    if (loaded.value) {
        const auto compiled = ure::GpuSceneCompiler::compile(loaded.value->scene);
        CHECK(compiled.instances.size() == 1);
    }
    archive.scene.meshes[0]->mesh->indices[2] = 99;
    CHECK(has_code(ure::native_scene::validate_scene_ir_archive(archive), "URE-Q3-MESH-003"));
    archive.scene.meshes[0]->mesh->indices[2] = 2;
    archive.scene.camera.fov = 180.0f;
    CHECK(has_code(ure::native_scene::validate_scene_ir_archive(archive), "URE-Q3-SCALAR-001"));
    auto full = make_full_archive();
    auto invalid_mie = std::make_shared<ure::scene_ir::MiePhaseResource>(*full.scene.medium_mie_resource);
    invalid_mie->wavelengths_nm = {830.0f, 360.0f};
    full.scene.medium_mie_resource = invalid_mie;
    CHECK(has_code(ure::native_scene::validate_scene_ir_archive(full), "URE-Q3-MIE-003"));

    auto negative_zero_archive = make_full_archive();
    auto negative_zero_mie = std::make_shared<ure::scene_ir::MiePhaseResource>(*negative_zero_archive.scene.medium_mie_resource);
    negative_zero_mie->cdf[0] = -0.0f;
    negative_zero_archive.scene.medium_mie_resource = negative_zero_mie;
    negative_zero_archive.scene.materials[0]->medium_mie_resource = negative_zero_mie;
    auto positive_zero_archive = negative_zero_archive;
    auto positive_zero_mie = std::make_shared<ure::scene_ir::MiePhaseResource>(*negative_zero_mie);
    positive_zero_mie->cdf[0] = 0.0f;
    positive_zero_archive.scene.medium_mie_resource = positive_zero_mie;
    positive_zero_archive.scene.materials[0]->medium_mie_resource = positive_zero_mie;
    CHECK(ure::native_scene::scene_ir_semantic_hash(negative_zero_archive) ==
          ure::native_scene::scene_ir_semantic_hash(positive_zero_archive));
}

static void test_preservation_and_malformed_inputs() {
    ure::native_scene::SceneDocument document;
    document.id = "scene/preservation";
    document.schema_version = ure::native_scene::kSceneSchemaVersion;
    auto archive = ure::native_scene::make_native_scene_archive(document, make_roundtrip_scene());
    archive.source_ids.materials[0] = "material/custom-metal";
    archive.preserved_optional_chunks.push_back({"extension/opaque", 9001, {1, 0},
        ure::native_scene::RequirementLevel::Optional,
        static_cast<std::uint32_t>(ure::native_scene::CompressionCodec::None), 8,
        "org.ultrarender.future", {}, {0x00, 0x7f, 0xff}});
    const auto bytes = ure::native_scene::write_scene_ir_binary(archive);
    const auto loaded = ure::native_scene::read_scene_ir_binary(bytes, {});
    CHECK(loaded.ok());
    if (loaded.value) {
        CHECK(loaded.value->source_ids.materials[0] == "material/custom-metal");
        CHECK(loaded.value->preserved_optional_chunks.size() == 1);
        CHECK(loaded.value->preserved_optional_chunks[0].payload == std::vector<std::uint8_t>({0x00, 0x7f, 0xff}));
    }
    auto truncated = bytes;
    truncated.resize(truncated.size() - 1);
    CHECK(!ure::native_scene::read_scene_ir_binary(truncated, {}).ok());
    auto corrupt = bytes;
    corrupt.back() ^= 1;
    CHECK(!ure::native_scene::read_scene_ir_binary(corrupt, {}).ok());

    auto exploded = ure::native_scene::write_scene_ir_text(archive);
    const std::string payload_hash = exploded.resources[0].descriptor.content_hash;
    const std::string hash_field = "\"content_hash\": \"" + payload_hash + "\"";
    const std::size_t graph_hash_field = exploded.manifest.rfind(hash_field);
    CHECK(graph_hash_field != std::string::npos);
    exploded.manifest.replace(graph_hash_field + 17, payload_hash.size(), std::string(64, '0'));
    CHECK(!ure::native_scene::read_scene_ir_text(exploded, {}).ok());

    archive.preserved_optional_chunks[0].requirement = ure::native_scene::RequirementLevel::Required;
    bool rejected = false;
    try {
        static_cast<void>(ure::native_scene::write_scene_ir_binary(archive));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK(rejected);
}

static void test_retained_fixture() {
    const std::filesystem::path root = URE_TEST_ASSET_DIR;
    const auto binary = ure::native_scene::load_native_scene(root / "full_scene.urescene", {});
    const auto text = ure::native_scene::load_native_scene(root / "full_scene.ure", {});
    std::ifstream hash_input(root / "semantic_hash.txt", std::ios::binary);
    std::string expected_hash;
    hash_input >> expected_hash;
    CHECK(binary.ok());
    CHECK(text.ok());
    CHECK(expected_hash.size() == 64);
    if (binary.value && text.value) {
        CHECK(ure::native_scene::scene_ir_semantic_hash(*binary.value) == expected_hash);
        CHECK(ure::native_scene::scene_ir_semantic_hash(*text.value) == expected_hash);
        const auto previous_path = std::filesystem::current_path();
        std::filesystem::current_path(root);
        ure::CompiledGpuScene compiled;
        try {
            compiled = ure::GpuSceneCompiler::compile(binary.value->scene);
        } catch (...) {
            std::filesystem::current_path(previous_path);
            throw;
        }
        std::filesystem::current_path(previous_path);
        CHECK(compiled.instances.size() == 1);
    }
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--write-fixture") {
        const std::filesystem::path root = URE_TEST_ASSET_DIR;
        std::filesystem::create_directories(root);
        const auto archive = make_full_archive();
        ure::native_scene::save_native_scene(root / "full_scene.ure", archive);
        ure::native_scene::save_native_scene(root / "full_scene.urescene", archive);
        std::ofstream hash(root / "semantic_hash.txt", std::ios::binary | std::ios::trunc);
        hash << ure::native_scene::scene_ir_semantic_hash(archive) << '\n';
        return hash ? 0 : 1;
    }
    test_archive_identity_defaults();
    test_archive_deep_freeze_and_identity_validation();
    test_binary_roundtrip();
    test_exploded_roundtrip();
    test_file_roundtrip();
    test_full_current_field_roundtrip();
    test_validation_and_compiler_boundary();
    test_preservation_and_malformed_inputs();
    test_retained_fixture();
    std::printf("native SceneIR: %d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
