#include <ure/session.hpp>
#include <ure/log.hpp>
#include <ure/mie_phase_validation.hpp>
#include <ure/scene_ir.hpp>
#include <ure/ure_c_api.h>

#include <cstdio>
#include <chrono>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

static int g_passed = 0;
static int g_failed = 0;

static const unsigned char kTestBmp[] = {
    0x42, 0x4D, 0x4A, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x36, 0x00, 0x00, 0x00, 0x28, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0xFE, 0xFF,
    0xFF, 0xFF, 0x01, 0x00, 0x18, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x13, 0x0B,
    0x00, 0x00, 0x13, 0x0B, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF, 0x00,
    0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
};

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        ++g_failed; \
        return 1; \
    } \
    ++g_passed; \
} while (0)

class FakeRenderEngine final : public ure::IRenderEngine {
public:
    void load_scene_ir(const ure::scene_ir::SceneIR&) override {
        ++scene_ir_loads;
        loaded = true;
    }

    void reload_scene_ir(const ure::scene_ir::SceneIR&) override {
        ++scene_ir_reloads;
        loaded = true;
        spp = 0;
    }

    void update_transforms(const ure::scene_ir::SceneIR& scene_ir) override {
        transform_updates += 1;
        last_transform_count = static_cast<int>(scene_ir.instances.size());
        last_scene = scene_ir;
        spp = 0;
    }

    void update_materials(const ure::scene_ir::SceneIR& scene_ir) override {
        material_updates += 1;
        last_material_count = static_cast<int>(scene_ir.materials.size());
        last_materials.clear();
        for (const auto& material : scene_ir.materials) {
            if (material) last_materials.push_back(*material);
        }
        spp = 0;
    }

    void update_geometry(
        const ure::scene_ir::SceneIR& scene_ir) override {
        if (fail_geometry_update) {
            throw std::runtime_error(
                "injected geometry update failure");
        }
        ++geometry_updates;
        last_scene = scene_ir;
        spp = 0;
    }

    void render(const ure::RenderSettings&) override {
        ++spp;
    }

    int render_pass() override {
        if (!loaded) return 0;
        return ++spp;
    }

    void reset_accumulation() override {
        spp = 0;
        ++resets;
    }

    void update_camera(const ure::Camera&) override {
        spp = 0;
        ++camera_updates;
    }

    int get_current_spp() const override {
        return spp;
    }

    void get_framebuffer_size(int& out_width, int& out_height) const override {
        out_width = width;
        out_height = height;
    }

    const std::vector<float>& get_framebuffer() const override {
        return framebuffer;
    }

    const std::vector<float>& get_aov(ure::AovType type) const override {
        switch (type) {
        case ure::AovType::Beauty:
            return framebuffer;
        case ure::AovType::Normal:
            return normal_aov;
        case ure::AovType::Albedo:
            return albedo_aov;
        case ure::AovType::Depth:
            return depth_aov;
        case ure::AovType::Uv:
            return uv_aov;
        case ure::AovType::MotionVector:
            return motion_aov;
        }
        return framebuffer;
    }

    ure::IntegratorEstimatorMetadata get_estimator_metadata() const override {
        return estimator_metadata;
    }

    const ure::BackendSelection& get_backend_selection() const override {
        return backend_selection;
    }

    ure::AccelerationStats get_acceleration_stats() const override {
        return acceleration_stats;
    }

    ure::runtime::DynamicGeometryStats
    get_dynamic_geometry_stats() const override {
        return dynamic_geometry_stats;
    }

    bool loaded = false;
    int spp = 0;
    int scene_ir_loads = 0;
    int scene_ir_reloads = 0;
    int transform_updates = 0;
    int last_transform_count = 0;
    int material_updates = 0;
    int geometry_updates = 0;
    bool fail_geometry_update = false;
    int last_material_count = 0;
    int resets = 0;
    int camera_updates = 0;
    int width = 1;
    int height = 1;
    ure::scene_ir::SceneIR last_scene;
    ure::AccelerationStats acceleration_stats;
    ure::runtime::DynamicGeometryStats
        dynamic_geometry_stats;
    std::vector<ure::scene_ir::MaterialNode> last_materials;
    std::vector<float> framebuffer = {0.1f, 0.2f, 0.3f};
    std::vector<float> normal_aov = {0.0f, 1.0f, 0.0f};
    std::vector<float> albedo_aov = {0.8f, 0.7f, 0.6f};
    std::vector<float> depth_aov = {4.0f};
    std::vector<float> uv_aov = {0.25f, 0.75f};
    std::vector<float> motion_aov = {0.1f, 0.0f};
    ure::IntegratorEstimatorMetadata estimator_metadata = {};
    ure::BackendSelection backend_selection = {};
};

template <typename Fn>
static bool throws_runtime_error(Fn&& fn) {
    try {
        fn();
    } catch (const std::runtime_error&) {
        return true;
    } catch (...) {
        return false;
    }
    return false;
}

template <typename Fn>
static bool throws_exception(Fn&& fn) {
    try {
        fn();
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

template <typename Predicate>
static bool wait_until(Predicate&& predicate, int timeout_ms = 500) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return predicate();
}

static std::shared_ptr<ure::Mesh> make_triangle_mesh() {
    auto mesh = std::make_shared<ure::Mesh>();
    mesh->vertices = {
        {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}}
    };
    mesh->indices = {0, 1, 2};
    return mesh;
}

static ure::scene_ir::SceneIR make_scene_ir_with_instance() {
    ure::scene_ir::SceneIR scene_ir;
    auto material = std::make_shared<ure::scene_ir::MaterialNode>();
    material->name = "mat";
    scene_ir.materials.push_back(material);

    auto mesh_resource = std::make_shared<ure::scene_ir::MeshResource>();
    mesh_resource->name = "tri";
    mesh_resource->mesh = make_triangle_mesh();
    scene_ir.meshes.push_back(mesh_resource);

    ure::scene_ir::InstanceNode instance;
    instance.name = "tri_instance";
    instance.mesh = mesh_resource;
    instance.material = material;
    instance.position = {0.0f, 0.0f, 0.0f};
    scene_ir.instances.push_back(instance);
    return scene_ir;
}

static std::shared_ptr<const ure::scene_ir::MiePhaseResource> make_session_mie_resource() {
    auto resource = std::make_shared<ure::scene_ir::MiePhaseResource>();
    resource->wavelengths_nm = {360.0f, 830.0f};
    resource->cos_theta = {-1.0f, 0.0f, 1.0f};
    resource->phase.assign(6, 1.0f / (4.0f * 3.14159265359f));
    resource->scattering_cross_section_m2 = {1.0e-12f, 2.0e-12f};
    resource->extinction_cross_section_m2 = {1.5e-12f, 2.5e-12f};
    ure::scene_ir::validate_mie_phase_resource(*resource);
    return resource;
}

static ure::scene_ir::SceneIR make_scene_ir_with_mie_material() {
    auto scene = make_scene_ir_with_instance();
    scene.materials[0]->medium_phase = ure::scene_ir::VolumePhaseFunction::Mie;
    scene.materials[0]->medium_density = 1.0e12f;
    scene.materials[0]->medium_mie_resource = make_session_mie_resource();
    return scene;
}

static ure::scene_ir::InstanceNode make_scene_ir_instance(const std::shared_ptr<ure::scene_ir::MeshResource>& mesh,
                                                          const std::shared_ptr<ure::scene_ir::MaterialNode>& material,
                                                          float x) {
    ure::scene_ir::InstanceNode instance;
    instance.name = "inserted_instance";
    instance.mesh = mesh;
    instance.material = material;
    instance.position = {x, 0.0f, 0.0f};
    return instance;
}

static std::shared_ptr<ure::scene_ir::TextureResource> write_scene_ir_texture(const std::string& path) {
    {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(kTestBmp), sizeof(kTestBmp));
    }
    auto image = std::make_shared<ure::scene_ir::ImageResource>();
    image->name = path;
    image->uri = path;
    image->color_space = ure::scene_ir::ImageColorSpace::Linear;
    auto texture = std::make_shared<ure::scene_ir::TextureResource>();
    texture->name = path;
    texture->image = image;
    return texture;
}

static int test_requires_engine() {
    CHECK(throws_runtime_error([] {
        ure::RenderSession session(nullptr);
    }));
    return 0;
}

static int test_requires_scene() {
    auto engine = std::make_unique<FakeRenderEngine>();
    ure::RenderSession session(std::move(engine));
    CHECK(throws_runtime_error([&session] {
        (void)session.render_pass();
    }));
    CHECK(throws_runtime_error([&session] {
        (void)session.get_framebuffer();
    }));
    return 0;
}

static int test_scene_lifecycle() {
    auto* raw_engine = new FakeRenderEngine();
    ure::RenderSession session{std::unique_ptr<ure::IRenderEngine>(raw_engine)};

    ure::scene_ir::SceneIR scene_ir;
    session.load_scene(scene_ir);
    CHECK(session.has_scene());
    CHECK(session.state() == ure::RenderSessionState::Ready);
    CHECK(raw_engine->scene_ir_loads == 1);

    session.start_render(false);
    CHECK(session.state() == ure::RenderSessionState::Running);
    CHECK(session.get_progress().spp == 1);
    CHECK(session.render_pass() == 2);
    CHECK(session.get_progress().spp == 2);

    session.pause();
    CHECK(session.state() == ure::RenderSessionState::Paused);
    CHECK(session.render_pass() == 2);

    session.resume();
    CHECK(session.state() == ure::RenderSessionState::Running);
    CHECK(session.render_pass() == 3);

    session.cancel();
    CHECK(session.state() == ure::RenderSessionState::Canceled);
    CHECK(session.render_pass() == 3);

    session.reset_accumulation();
    CHECK(session.state() == ure::RenderSessionState::Ready);
    CHECK(session.get_progress().spp == 0);
    CHECK(raw_engine->resets == 1);
    return 0;
}

static int test_progressive_background_scheduler() {
    auto* raw_engine = new FakeRenderEngine();
    ure::RenderSession session{std::unique_ptr<ure::IRenderEngine>(raw_engine)};

    ure::scene_ir::SceneIR scene_ir;
    session.load_scene(scene_ir);
    session.start_render(true);
    CHECK(session.state() == ure::RenderSessionState::Running);
    CHECK(wait_until([&session] {
        return session.get_progress().spp >= 2;
    }));

    session.pause();
    CHECK(session.state() == ure::RenderSessionState::Paused);
    const int paused_spp = session.get_progress().spp;
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    CHECK(session.get_progress().spp == paused_spp);

    session.resume();
    CHECK(session.state() == ure::RenderSessionState::Running);
    CHECK(wait_until([&session, paused_spp] {
        return session.get_progress().spp > paused_spp;
    }));

    session.cancel();
    CHECK(session.state() == ure::RenderSessionState::Canceled);
    const int canceled_spp = session.get_progress().spp;
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    CHECK(session.get_progress().spp == canceled_spp);
    return 0;
}

static int test_scene_ir_and_camera() {
    auto* raw_engine = new FakeRenderEngine();
    ure::RenderSession session{std::unique_ptr<ure::IRenderEngine>(raw_engine)};

    ure::scene_ir::SceneIR scene_ir;
    session.load_scene(scene_ir);
    CHECK(raw_engine->scene_ir_loads == 1);

    session.start_render(false);
    CHECK(raw_engine->spp == 1);
    CHECK(session.state() == ure::RenderSessionState::Running);

    ure::Camera camera;
    session.update_camera(camera);
    CHECK(raw_engine->camera_updates == 1);
    CHECK(session.state() == ure::RenderSessionState::Ready);
    CHECK(session.get_progress().spp == 0);

    const auto& framebuffer = session.get_framebuffer();
    CHECK(framebuffer.size() == 3);
    CHECK(framebuffer[1] == 0.2f);

    const auto& normal = session.get_aov(ure::AovType::Normal);
    CHECK(normal.size() == 3);
    CHECK(normal[1] == 1.0f);

    const auto& depth = session.get_aov(ure::AovType::Depth);
    CHECK(depth.size() == 1);
    CHECK(depth[0] == 4.0f);
    const auto& uv = session.get_aov(ure::AovType::Uv);
    CHECK(uv.size() == 2);
    CHECK(uv[0] == 0.25f);
    CHECK(uv[1] == 0.75f);
    const auto& motion = session.get_aov(ure::AovType::MotionVector);
    CHECK(motion.size() == 2);
    CHECK(motion[0] == 0.1f);
    CHECK(motion[1] == 0.0f);
    return 0;
}

static int test_scene_diff_mutation() {
    auto* raw_engine = new FakeRenderEngine();
    ure::RenderSession session{std::unique_ptr<ure::IRenderEngine>(raw_engine)};

    ure::scene_ir::SceneIR scene_ir;
    session.mutate_scene(ure::SceneDiff::replace_scene(scene_ir));
    CHECK(session.has_scene());
    CHECK(raw_engine->scene_ir_reloads == 1);
    CHECK(session.state() == ure::RenderSessionState::Ready);

    CHECK(session.render_pass() == 1);
    ure::Camera camera;
    session.mutate_scene(ure::SceneDiff::update_camera(camera));
    CHECK(raw_engine->camera_updates == 1);
    CHECK(raw_engine->spp == 0);
    CHECK(session.state() == ure::RenderSessionState::Ready);

    ure::SceneDiff reset_only;
    session.render_pass();
    CHECK(raw_engine->spp == 1);
    session.mutate_scene(reset_only);
    CHECK(raw_engine->resets == 1);
    CHECK(raw_engine->spp == 0);
    return 0;
}

static int test_scene_diff_replace_scene_forces_full_reload() {
    auto* raw_engine = new FakeRenderEngine();
    ure::RenderSession session{std::unique_ptr<ure::IRenderEngine>(raw_engine)};

    session.load_scene(make_scene_ir_with_instance());
    CHECK(raw_engine->scene_ir_loads == 1);
    ure::scene_ir::SceneIR replacement = make_scene_ir_with_instance();
    replacement.instances.push_back(make_scene_ir_instance(replacement.meshes[0], replacement.materials[0], 2.0f));
    session.mutate_scene(ure::SceneDiff::replace_scene(replacement));

    CHECK(raw_engine->scene_ir_loads == 1);
    CHECK(raw_engine->scene_ir_reloads == 1);
    CHECK(raw_engine->transform_updates == 0);
    CHECK(session.state() == ure::RenderSessionState::Ready);
    return 0;
}

static int test_scene_diff_instance_transform_ir() {
    auto* raw_engine = new FakeRenderEngine();
    ure::RenderSession session{std::unique_ptr<ure::IRenderEngine>(raw_engine)};

    session.load_scene(make_scene_ir_with_instance());
    CHECK(raw_engine->scene_ir_loads == 1);
    session.render_pass();
    CHECK(raw_engine->spp == 1);

    session.mutate_scene(ure::SceneDiff::update_instance_transform(0, {2.0f, 0.0f, 0.0f}, {3.0f, 1.0f, 1.0f}));
    CHECK(raw_engine->scene_ir_loads == 1);
    CHECK(raw_engine->transform_updates == 1);
    CHECK(raw_engine->last_transform_count == 1);
    CHECK(raw_engine->spp == 0);
    CHECK(session.state() == ure::RenderSessionState::Ready);
    CHECK(raw_engine->last_scene.instances.size() == 1);
    const auto& transform = raw_engine->last_scene.instances[0];
    CHECK(transform.position.x == 2.0f);
    CHECK(transform.scale.x == 3.0f);
    return 0;
}

static int test_scene_diff_emissive_instance_transform_ir_full_reload() {
    auto* raw_engine = new FakeRenderEngine();
    ure::RenderSession session{std::unique_ptr<ure::IRenderEngine>(raw_engine)};

    ure::scene_ir::SceneIR scene_ir = make_scene_ir_with_instance();
    scene_ir.materials[0]->model = ure::scene_ir::MaterialModel::Light;
    scene_ir.materials[0]->emission = {4.0f, 2.0f, 1.0f};
    session.load_scene(scene_ir);
    session.render_pass();
    CHECK(raw_engine->spp == 1);

    session.mutate_scene(ure::SceneDiff::update_instance_transform(0, {2.0f, 0.0f, 0.0f}));

    CHECK(raw_engine->scene_ir_reloads == 1);
    CHECK(raw_engine->transform_updates == 0);
    CHECK(raw_engine->spp == 0);
    CHECK(session.state() == ure::RenderSessionState::Ready);
    return 0;
}

static int test_scene_diff_instance_transform_errors() {
    auto* raw_engine = new FakeRenderEngine();
    ure::RenderSession session{std::unique_ptr<ure::IRenderEngine>(raw_engine)};

    session.load_scene(make_scene_ir_with_instance());
    CHECK(throws_exception([&session] {
        session.mutate_scene(ure::SceneDiff::update_instance_transform(1, {0.0f, 0.0f, 0.0f}));
    }));

    ure::scene_ir::SceneIR unrenderable;
    unrenderable.instances.push_back({});
    session.load_scene(unrenderable);
    CHECK(throws_exception([&session] {
        session.mutate_scene(ure::SceneDiff::update_instance_transform(0, {0.0f, 0.0f, 0.0f}));
    }));
    return 0;
}

static int test_scene_diff_material_update_ir() {
    auto* raw_engine = new FakeRenderEngine();
    ure::RenderSession session{std::unique_ptr<ure::IRenderEngine>(raw_engine)};

    session.load_scene(make_scene_ir_with_instance());
    session.render_pass();
    CHECK(raw_engine->spp == 1);

    ure::scene_ir::MaterialNode material;
    material.name = "mat";
    material.model = ure::scene_ir::MaterialModel::Metal;
    material.base_color = {0.25f, 0.5f, 0.75f};
    material.roughness = 0.2f;
    material.ior = 2.0f;
    session.mutate_scene(ure::SceneDiff::update_material(0, material));

    CHECK(raw_engine->scene_ir_loads == 1);
    CHECK(raw_engine->material_updates == 1);
    CHECK(raw_engine->last_material_count == 1);
    CHECK(raw_engine->last_materials.size() == 1);
    CHECK(raw_engine->last_materials[0].model == ure::scene_ir::MaterialModel::Metal);
    CHECK(raw_engine->last_materials[0].roughness == 0.2f);
    CHECK(raw_engine->last_materials[0].ior == 2.0f);
    CHECK(raw_engine->spp == 0);
    CHECK(session.state() == ure::RenderSessionState::Ready);
    return 0;
}

static int test_scene_diff_equivalent_mie_resource_uses_material_update() {
    auto* raw_engine = new FakeRenderEngine();
    ure::RenderSession session{std::unique_ptr<ure::IRenderEngine>(raw_engine)};
    auto scene = make_scene_ir_with_mie_material();
    session.load_scene(scene);

    auto density_only = *scene.materials[0];
    density_only.medium_density = 2.0e12f;
    session.mutate_scene(ure::SceneDiff::update_material(0, density_only));
    CHECK(raw_engine->material_updates == 1);
    CHECK(raw_engine->scene_ir_reloads == 0);

    auto equal_resource = std::make_shared<ure::scene_ir::MiePhaseResource>(
        *scene.materials[0]->medium_mie_resource);
    equal_resource->content_hash = "forged-stale-hash";
    density_only.medium_mie_resource = equal_resource;
    session.mutate_scene(ure::SceneDiff::update_material(0, density_only));
    CHECK(raw_engine->material_updates == 2);
    CHECK(raw_engine->scene_ir_reloads == 0);
    CHECK(raw_engine->spp == 0);
    return 0;
}

static int test_scene_diff_changed_mie_resource_forces_full_reload() {
    auto* raw_engine = new FakeRenderEngine();
    ure::RenderSession session{std::unique_ptr<ure::IRenderEngine>(raw_engine)};
    auto scene = make_scene_ir_with_mie_material();
    session.load_scene(scene);

    auto changed = *scene.materials[0];
    auto changed_resource = std::make_shared<ure::scene_ir::MiePhaseResource>(
        *changed.medium_mie_resource);
    const float isotropic = 1.0f / (4.0f * 3.14159265359f);
    changed_resource->phase = {
        0.5f * isotropic, isotropic, 1.5f * isotropic,
        0.5f * isotropic, isotropic, 1.5f * isotropic};
    ure::scene_ir::validate_mie_phase_resource(*changed_resource);
    changed_resource->content_hash = scene.materials[0]->medium_mie_resource->content_hash;
    changed.medium_mie_resource = changed_resource;
    session.mutate_scene(ure::SceneDiff::update_material(0, changed));
    CHECK(raw_engine->scene_ir_reloads == 1);
    CHECK(raw_engine->material_updates == 0);
    CHECK(raw_engine->spp == 0);
    return 0;
}

static int test_scene_diff_mutable_mie_alias_forces_full_reload() {
    auto* raw_engine = new FakeRenderEngine();
    ure::RenderSession session{std::unique_ptr<ure::IRenderEngine>(raw_engine)};
    auto scene = make_scene_ir_with_mie_material();
    auto mutable_alias = std::make_shared<ure::scene_ir::MiePhaseResource>(
        *scene.materials[0]->medium_mie_resource);
    scene.materials[0]->medium_mie_resource = mutable_alias;
    session.load_scene(scene);

    const float isotropic = 1.0f / (4.0f * 3.14159265359f);
    mutable_alias->phase = {
        0.5f * isotropic, isotropic, 1.5f * isotropic,
        0.5f * isotropic, isotropic, 1.5f * isotropic};
    ure::scene_ir::validate_mie_phase_resource(*mutable_alias);
    auto changed = *scene.materials[0];
    session.mutate_scene(ure::SceneDiff::update_material(0, changed));
    CHECK(raw_engine->scene_ir_reloads == 1);
    CHECK(raw_engine->material_updates == 0);
    return 0;
}

static int test_scene_diff_material_texture_update_ir_full_reload() {
    auto* raw_engine = new FakeRenderEngine();
    ure::RenderSession session{std::unique_ptr<ure::IRenderEngine>(raw_engine)};

    session.load_scene(make_scene_ir_with_instance());
    ure::scene_ir::MaterialNode material;
    material.name = "textured";
    material.base_color_texture = std::make_shared<ure::scene_ir::TextureResource>();
    material.base_color_texture->image = std::make_shared<ure::scene_ir::ImageResource>();
    material.base_color_texture->image->uri = "missing-but-retained-for-fake-engine.png";
    session.mutate_scene(ure::SceneDiff::update_material(0, material));

    CHECK(raw_engine->scene_ir_reloads == 1);
    CHECK(raw_engine->material_updates == 0);
    CHECK(raw_engine->spp == 0);
    CHECK(session.state() == ure::RenderSessionState::Ready);
    return 0;
}

static int test_scene_diff_material_graph_update_ir_full_reload() {
    auto* raw_engine = new FakeRenderEngine();
    ure::RenderSession session{std::unique_ptr<ure::IRenderEngine>(raw_engine)};

    session.load_scene(make_scene_ir_with_instance());
    ure::scene_ir::MaterialNode material;
    material.name = "graph_resource";
    material.model = ure::scene_ir::MaterialModel::Light;
    material.emission = {1.0f, 1.0f, 1.0f};
    auto graph = std::make_shared<ure::scene_ir::MaterialGraph>();
    ure::scene_ir::MaterialGraphNode emission;
    emission.kind = ure::scene_ir::MaterialGraphNodeKind::BsdfLight;
    emission.color = {2.0f, 1.0f, 0.5f};
    const auto emission_id = graph->add_node(emission);
    ure::scene_ir::MaterialGraphNode output;
    output.kind = ure::scene_ir::MaterialGraphNodeKind::OutputSurface;
    output.inputs.push_back(ure::scene_ir::material_graph_input("surface", emission_id));
    graph->output_node_id = graph->add_node(output);
    material.graph = graph;

    session.mutate_scene(ure::SceneDiff::update_material(0, material));

    CHECK(raw_engine->scene_ir_reloads == 1);
    CHECK(raw_engine->material_updates == 0);
    CHECK(raw_engine->spp == 0);
    CHECK(session.state() == ure::RenderSessionState::Ready);
    return 0;
}

static int test_scene_diff_material_spd_update_ir_full_reload() {
    auto* raw_engine = new FakeRenderEngine();
    ure::RenderSession session{std::unique_ptr<ure::IRenderEngine>(raw_engine)};

    session.load_scene(make_scene_ir_with_instance());
    ure::scene_ir::MaterialNode material;
    material.name = "spectral_resource";
    material.model = ure::scene_ir::MaterialModel::Light;
    material.spectral_extension = std::make_shared<ure::scene_ir::SpectralMaterialExtension>();
    material.spectral_extension->emission_spd = "resource_change_requires_reload.spd";

    session.mutate_scene(ure::SceneDiff::update_material(0, material));

    CHECK(raw_engine->scene_ir_reloads == 1);
    CHECK(raw_engine->material_updates == 0);
    CHECK(raw_engine->spp == 0);
    CHECK(session.state() == ure::RenderSessionState::Ready);
    return 0;
}

static int test_scene_diff_material_update_errors() {
    auto* raw_engine = new FakeRenderEngine();
    ure::RenderSession session{std::unique_ptr<ure::IRenderEngine>(raw_engine)};

    session.load_scene(make_scene_ir_with_instance());
    CHECK(throws_exception([&session] {
        session.mutate_scene(ure::SceneDiff::update_material(1, ure::scene_ir::MaterialNode{}));
    }));
    return 0;
}

static int test_scene_diff_mesh_update_ir() {
    auto engine = std::make_unique<FakeRenderEngine>();
    FakeRenderEngine* raw = engine.get();
    ure::RenderSession session(std::move(engine));
    session.load_scene(make_scene_ir_with_instance());

    auto deformed = make_triangle_mesh();
    deformed->vertices[2].position.z = 0.5f;
    session.mutate_scene(
        ure::SceneDiff::update_scene_ir_mesh(
            0, deformed));
    CHECK(raw->geometry_updates == 1);
    CHECK(raw->scene_ir_reloads == 0);
    CHECK(raw->last_scene.meshes[0]->mesh->
        vertices[2].position.z == 0.5f);

    auto topology = make_triangle_mesh();
    topology->vertices.push_back({
        {1.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
        {1.0f, 1.0f}});
    topology->indices = {0, 1, 2, 1, 3, 2};
    session.mutate_scene(
        ure::SceneDiff::update_scene_ir_mesh(
            0, topology));
    CHECK(raw->geometry_updates == 2);
    CHECK(raw->last_scene.meshes[0]->mesh->
        indices.size() == 6);
    return 0;
}

static int test_scene_diff_mesh_update_errors() {
    auto engine = std::make_unique<FakeRenderEngine>();
    FakeRenderEngine* raw = engine.get();
    ure::RenderSession session(std::move(engine));
    session.load_scene(make_scene_ir_with_instance());

    CHECK(throws_exception([&] {
        session.mutate_scene(
            ure::SceneDiff::update_scene_ir_mesh(
                2, make_triangle_mesh()));
    }));
    CHECK(throws_exception([&] {
        session.mutate_scene(
            ure::SceneDiff::update_scene_ir_mesh(
                0, nullptr));
    }));
    auto invalid = make_triangle_mesh();
    invalid->indices[2] = 99;
    CHECK(throws_exception([&] {
        session.mutate_scene(
            ure::SceneDiff::update_scene_ir_mesh(
                0, invalid));
    }));

    auto deformed = make_triangle_mesh();
    deformed->vertices[0].position.z = 0.75f;
    raw->fail_geometry_update = true;
    CHECK(throws_exception([&] {
        session.mutate_scene(
            ure::SceneDiff::update_scene_ir_mesh(
                0, deformed));
    }));
    raw->fail_geometry_update = false;
    session.mutate_scene(
        ure::SceneDiff::update_instance_transform(
            0, {1.0f, 0.0f, 0.0f}));
    CHECK(raw->last_scene.meshes[0]->mesh->
        vertices[0].position.z == 0.0f);
    return 0;
}

static int test_scene_diff_topology_update_ir_full_reload() {
    auto* raw_engine = new FakeRenderEngine();
    ure::RenderSession session{std::unique_ptr<ure::IRenderEngine>(raw_engine)};
    ure::scene_ir::SceneIR scene_ir = make_scene_ir_with_instance();

    session.load_scene(scene_ir);
    CHECK(raw_engine->scene_ir_loads == 1);
    session.render_pass();
    CHECK(raw_engine->spp == 1);

    ure::SceneDiff diff = ure::SceneDiff::add_scene_ir_instance(make_scene_ir_instance(scene_ir.meshes[0], scene_ir.materials[0], 3.0f));
    diff.instance_transforms.push_back({1, {4.0f, 0.0f, 0.0f}, {2.0f, 1.0f, 1.0f}, {}});
    session.mutate_scene(diff);

    CHECK(raw_engine->scene_ir_reloads == 1);
    CHECK(raw_engine->transform_updates == 0);
    CHECK(raw_engine->material_updates == 0);
    CHECK(raw_engine->spp == 0);
    CHECK(session.state() == ure::RenderSessionState::Ready);

    session.mutate_scene(ure::SceneDiff::remove_scene_ir_instance(0));
    CHECK(raw_engine->scene_ir_reloads == 2);
    CHECK(raw_engine->transform_updates == 0);
    return 0;
}

static int test_scene_diff_topology_errors() {
    auto* raw_engine = new FakeRenderEngine();
    ure::RenderSession session{std::unique_ptr<ure::IRenderEngine>(raw_engine)};

    session.load_scene(make_scene_ir_with_instance());
    CHECK(throws_exception([&session] {
        session.mutate_scene(ure::SceneDiff::remove_scene_ir_instance(4));
    }));
    CHECK(throws_exception([&session] {
        ure::scene_ir::InstanceNode invalid;
        session.mutate_scene(ure::SceneDiff::add_scene_ir_instance(invalid));
    }));
    CHECK(throws_exception([&session] {
        ure::scene_ir::SphereNode sphere;
        sphere.radius = 1.0f;
        session.mutate_scene(ure::SceneDiff::add_scene_ir_sphere(sphere));
    }));

    CHECK(throws_exception([&session] {
        ure::scene_ir::SphereNode sphere;
        sphere.radius = 0.0f;
        sphere.material = std::make_shared<ure::scene_ir::MaterialNode>();
        session.mutate_scene(ure::SceneDiff::add_scene_ir_sphere(sphere));
    }));
    return 0;
}

static int test_scene_diff_topology_real_gpu_reload_smoke() {
    struct LogLevelGuard {
        ure::log::Level previous;
        explicit LogLevelGuard(ure::log::Level next) : previous(ure::log::min_level()) {
            ure::log::set_min_level(next);
        }
        ~LogLevelGuard() {
            ure::log::set_min_level(previous);
        }
    } log_guard(ure::log::Level::Warn);
    ure::RenderConfig config;
    config.num_wavelengths = 8;
    config.max_trace_depth = 2;
    config.acceleration.collect_stats = true;
    ure::RenderSession session = ure::RenderSession::create(config);
    ure::scene_ir::SceneIR scene_ir = make_scene_ir_with_instance();
    scene_ir.width = 4;
    scene_ir.height = 4;
    scene_ir.camera.position = {0.0f, 0.0f, 4.0f};
    scene_ir.camera.look_at = {0.0f, 0.0f, 0.0f};
    scene_ir.camera.fov = 45.0f;

    session.load_scene(scene_ir);
    const auto build_stats = session.get_acceleration_stats();
    CHECK(build_stats.mesh_count == 1);
    CHECK(build_stats.triangle_count > 0);
    CHECK(build_stats.node_count > 0);
    CHECK(build_stats.leaf_count > 0);
    CHECK(build_stats.max_depth > 0);
    CHECK(build_stats.blas_node_bytes > 0);
    CHECK(build_stats.tlas_node_count > 0);
    CHECK(build_stats.tlas_leaf_count > 0);
    CHECK(build_stats.tlas_max_depth > 0);
    CHECK(build_stats.tlas_bytes > 0);
    session.start_render();
    CHECK(session.render_pass() >= 1);
    const auto traversal_stats =
        session.get_acceleration_stats();
    CHECK(traversal_stats.stack_overflow_count == 0);
    CHECK(traversal_stats.invalid_acceleration_count == 0);

    session.mutate_scene(ure::SceneDiff::add_scene_ir_instance(make_scene_ir_instance(scene_ir.meshes[0], scene_ir.materials[0], 1.5f)));
    CHECK(session.state() == ure::RenderSessionState::Ready);
    CHECK(session.get_progress().spp == 0);
    CHECK(session.render_pass() >= 1);

    session.mutate_scene(ure::SceneDiff::remove_scene_ir_instance(1));
    CHECK(session.state() == ure::RenderSessionState::Ready);
    CHECK(session.get_progress().spp == 0);
    int width = 0;
    int height = 0;
    session.get_framebuffer_size(width, height);
    CHECK(width == 4);
    CHECK(height == 4);
    return 0;
}

static int test_acceleration_async_budget_real_gpu() {
    ure::RenderConfig config;
    config.num_wavelengths = 8;
    config.max_trace_depth = 2;
    config.acceleration.collect_stats = true;
    config.acceleration.scratch_budget_bytes =
        1024ull * 1024ull;
    ure::scene_ir::SceneIR scene_ir =
        make_scene_ir_with_instance();
    scene_ir.width = 4;
    scene_ir.height = 4;
    auto second_mesh =
        std::make_shared<ure::scene_ir::MeshResource>();
    second_mesh->name = "tri_second";
    second_mesh->mesh = make_triangle_mesh();
    scene_ir.meshes.push_back(second_mesh);
    scene_ir.instances.push_back(make_scene_ir_instance(
        second_mesh, scene_ir.materials[0], 1.5f));

    ure::RenderSession session =
        ure::RenderSession::create(config);
    session.load_scene(scene_ir);
    const auto stats = session.get_acceleration_stats();
    CHECK(stats.mesh_count == 2);
    CHECK(stats.blas_build_peak_concurrency == 2);
    CHECK(stats.blas_build_wall_nanoseconds > 0);
    CHECK(stats.acceleration_upload_nanoseconds > 0);
    CHECK(stats.acceleration_upload_bytes ==
          stats.compacted_bytes);
    CHECK(stats.build_temporary_bytes_peak > 0);
    CHECK(stats.build_temporary_bytes_peak <=
          config.acceleration.scratch_budget_bytes);
    CHECK(stats.uncompacted_bytes >= stats.compacted_bytes);
    CHECK(stats.compacted_bytes ==
          stats.blas_node_bytes + stats.tlas_bytes);
    std::fprintf(
        stderr,
        "V.5 async build: build_ms=%.3f upload_ms=%.3f "
        "temporary_bytes=%llu uncompacted_bytes=%llu "
        "compacted_bytes=%llu upload_bytes=%llu concurrency=%u\n",
        static_cast<double>(
            stats.blas_build_wall_nanoseconds) / 1.0e6,
        static_cast<double>(
            stats.acceleration_upload_nanoseconds) / 1.0e6,
        static_cast<unsigned long long>(
            stats.build_temporary_bytes_peak),
        static_cast<unsigned long long>(
            stats.uncompacted_bytes),
        static_cast<unsigned long long>(
            stats.compacted_bytes),
        static_cast<unsigned long long>(
            stats.acceleration_upload_bytes),
        stats.blas_build_peak_concurrency);

    ure::RenderConfig rejected = config;
    rejected.acceleration.scratch_budget_bytes = 1;
    ure::RenderSession rejected_session =
        ure::RenderSession::create(rejected);
    CHECK(throws_exception([&] {
        rejected_session.load_scene(scene_ir);
    }));
    return 0;
}

static int test_scene_diff_texture_material_real_gpu_reload_smoke() {
    struct LogLevelGuard {
        ure::log::Level previous;
        explicit LogLevelGuard(ure::log::Level next) : previous(ure::log::min_level()) {
            ure::log::set_min_level(next);
        }
        ~LogLevelGuard() {
            ure::log::set_min_level(previous);
        }
    } log_guard(ure::log::Level::Warn);

    ure::RenderConfig config;
    config.num_wavelengths = 8;
    config.max_trace_depth = 2;
    ure::RenderSession session = ure::RenderSession::create(config);
    ure::scene_ir::SceneIR scene_ir = make_scene_ir_with_instance();
    scene_ir.width = 4;
    scene_ir.height = 4;
    scene_ir.camera.position = {0.0f, 0.0f, 4.0f};
    scene_ir.camera.look_at = {0.0f, 0.0f, 0.0f};
    scene_ir.camera.fov = 45.0f;

    session.load_scene(scene_ir);
    CHECK(session.render_pass() >= 1);

    const std::string texture_path = "test_session_texture_reload.bmp";
    ure::scene_ir::MaterialNode textured;
    textured.base_color_texture = write_scene_ir_texture(texture_path);
    session.mutate_scene(ure::SceneDiff::update_material(0, textured));
    std::remove(texture_path.c_str());
    CHECK(session.state() == ure::RenderSessionState::Ready);
    CHECK(session.get_progress().spp == 0);
    CHECK(session.render_pass() >= 1);
    int width = 0;
    int height = 0;
    session.get_framebuffer_size(width, height);
    CHECK(width == 4);
    CHECK(height == 4);
    return 0;
}

static int test_c_session_lifecycle() {
    CHECK(ure_session_render_pass(nullptr) == -1);
    CHECK(ure_session_get_framebuffer(nullptr) == nullptr);
    CHECK(ure_session_get_aov(nullptr, URE_AOV_NORMAL) == nullptr);
    CHECK(ure_session_save_bmp(nullptr, "null.bmp") == -1);
    CHECK(ure_session_save_hdr(nullptr, "null.hdr") == -1);
    CHECK(ure_session_update_camera(nullptr, nullptr, nullptr, 45.0f) == -1);
    float vec[3] = {0.0f, 0.0f, 0.0f};
    CHECK(ure_session_update_instance_transform(nullptr, 0, vec, vec) == -1);
    CHECK(ure_session_update_material(nullptr,
                                      0,
                                      URE_MATERIAL_LAMBERTIAN,
                                      vec,
                                      0.5f,
                                      1.45f,
                                      vec) == -1);
    CHECK(ure_session_update_material_texture(nullptr, 0, 1, 1, 3, vec) == -1);
    CHECK(ure_aov_channel_count(URE_AOV_BEAUTY) == 3);
    CHECK(ure_aov_channel_count(URE_AOV_NORMAL) == 3);
    CHECK(ure_aov_channel_count(URE_AOV_ALBEDO) == 3);
    CHECK(ure_aov_channel_count(URE_AOV_DEPTH) == 1);
    CHECK(ure_aov_channel_count(URE_AOV_UV) == 2);
    CHECK(ure_aov_channel_count(URE_AOV_MOTION_VECTOR) == 2);
    CHECK(ure_aov_channel_count(static_cast<ure_aov_type_t>(999)) == 0);
    ure_session_progress_t null_progress = ure_session_get_progress(nullptr);
    CHECK(null_progress.spp == 0);
    CHECK(null_progress.has_scene == 0);
    int width = -1;
    int height = -1;
    ure_session_get_framebuffer_size(nullptr, &width, &height);
    CHECK(width == 0);
    CHECK(height == 0);

    ure_session_t* session = ure_session_create_config(8, 64, 12);
    CHECK(session != nullptr);
    ure_session_progress_t progress = ure_session_get_progress(session);
    CHECK(progress.spp == 0);
    CHECK(progress.has_scene == 0);
    CHECK(ure_session_render_pass(session) == -1);
    CHECK(ure_session_start(session, 1) == -1);
    CHECK(ure_session_get_framebuffer(session) == nullptr);
    CHECK(ure_session_get_aov(session, URE_AOV_DEPTH) == nullptr);
    CHECK(ure_session_save_bmp(session, "empty.bmp") == -1);
    CHECK(ure_session_save_hdr(session, "empty.hdr") == -1);
    CHECK(ure_session_update_camera(session, vec, vec, 45.0f) == -1);
    CHECK(ure_session_update_instance_transform(session, 0, vec, vec) == -1);
    CHECK(ure_session_update_instance_transform(session, 0, nullptr, vec) == -1);
    CHECK(ure_session_update_material(session,
                                      0,
                                      URE_MATERIAL_METAL,
                                      vec,
                                      0.25f,
                                      1.45f,
                                      vec) == -1);
    CHECK(ure_session_update_material_texture(session, 0, 1, 1, 3, vec) == -1);
    CHECK(ure_session_update_material_texture(session, 0, 0, 1, 3, vec) == -1);
    CHECK(ure_session_update_material_texture(session, 0, 1, 1, 2, vec) == -1);
    CHECK(ure_session_update_material_texture(session, 0, 1, 1, 3, nullptr) == -1);
    ure_session_pause(session);
    ure_session_resume(session);
    ure_session_cancel(session);
    ure_session_reset_accumulation(session);
    ure_session_destroy(session);

    ure_spectral_config_t spectral_config{};
    spectral_config.domain_bins = 1000000ULL;
    spectral_config.packet_lanes = 8;
    spectral_config.queue_capacity = 64;
    spectral_config.max_trace_depth = 12;
    ure_session_t* spectral_session = ure_session_create_spectral_config(&spectral_config);
    CHECK(spectral_session != nullptr);
    ure_session_destroy(spectral_session);

    ure_spectral_config_t sampled_config{};
    sampled_config.domain_bins = 1000000ULL;
    sampled_config.packet_lanes = 1;
    sampled_config.queue_capacity = 64;
    sampled_config.max_trace_depth = 12;
    ure_session_t* sampled_session = ure_session_create_spectral_config(&sampled_config);
    CHECK(sampled_session != nullptr);
    ure_session_destroy(sampled_session);

    ure_spectral_config_t invalid_four_lane_config{};
    invalid_four_lane_config.domain_bins = 1000000ULL;
    invalid_four_lane_config.packet_lanes = 4;
    CHECK(ure_session_create_spectral_config(&invalid_four_lane_config) == nullptr);

    ure_spectral_config_t invalid_spectral_config{};
    invalid_spectral_config.domain_bins = 4;
    invalid_spectral_config.packet_lanes = 8;
    CHECK(ure_session_create_spectral_config(&invalid_spectral_config) == nullptr);

    ure_wave_optics_config_t radiometric_wave_config{};
    radiometric_wave_config.mode = URE_WAVE_OPTICS_RADIOMETRIC;
    ure_session_t* radiometric_wave_session =
        ure_session_create_wave_config(&spectral_config, &radiometric_wave_config);
    CHECK(radiometric_wave_session != nullptr);
    ure_session_destroy(radiometric_wave_session);

    ure_wave_optics_config_t preview_only_wave_config{};
    preview_only_wave_config.mode = URE_WAVE_OPTICS_RADIOMETRIC;
    preview_only_wave_config.experimental_allow_preview_degradation = 1;
    ure_session_t* preview_only_session =
        ure_session_create_wave_config(&spectral_config, &preview_only_wave_config);
    CHECK(preview_only_session != nullptr);
    ure_session_destroy(preview_only_session);

    ure_wave_optics_config_t diffraction_wave_config{};
    diffraction_wave_config.mode =
        URE_WAVE_OPTICS_CAMERA_DIFFRACTION;
    diffraction_wave_config.camera_diffraction_enabled = 1;
    ure_session_t* diffraction_wave_session =
        ure_session_create_wave_config(
            &spectral_config,
            &diffraction_wave_config);
    CHECK(diffraction_wave_session != nullptr);
    ure_session_destroy(diffraction_wave_session);

    ure_wave_optics_config_t diffractive_wave_config{};
    diffractive_wave_config.mode =
        URE_WAVE_OPTICS_RADIOMETRIC;
    diffractive_wave_config
        .diffractive_materials_enabled = 1;
    ure_session_t* diffractive_wave_session =
        ure_session_create_wave_config(
            &spectral_config,
            &diffractive_wave_config);
    CHECK(diffractive_wave_session != nullptr);
    ure_session_destroy(diffractive_wave_session);

    ure_wave_optics_config_t fluorescence_wave_config{};
    fluorescence_wave_config.mode =
        URE_WAVE_OPTICS_RADIOMETRIC;
    fluorescence_wave_config.fluorescence_enabled = 1;
    ure_session_t* fluorescence_wave_session =
        ure_session_create_wave_config(
            &spectral_config,
            &fluorescence_wave_config);
    CHECK(fluorescence_wave_session != nullptr);
    ure_session_destroy(fluorescence_wave_session);

    ure_wave_optics_config_v2_t diffraction_v2{};
    diffraction_v2.struct_size =
        sizeof(diffraction_v2);
    diffraction_v2.version = 2;
    diffraction_v2.base = diffraction_wave_config;
    diffraction_v2.camera_aperture_diameter_m =
        8.0e-3;
    diffraction_v2.camera_focal_length_m =
        50.0e-3;
    diffraction_v2.sensor_pixel_pitch_m =
        4.0e-6;
    diffraction_v2.camera_aperture_blade_count = 6;
    diffraction_v2.camera_psf_radius_pixels = 6;
    diffraction_v2.camera_wavelength_bin_count = 12;
    diffraction_v2.camera_pupil_sample_count = 24;
    ure_session_t* diffraction_v2_session =
        ure_session_create_wave_config_v2(
            &spectral_config,
            &diffraction_v2);
    CHECK(diffraction_v2_session != nullptr);
    ure_session_destroy(diffraction_v2_session);
    diffraction_v2.version = 1;
    CHECK(ure_session_create_wave_config_v2(
              &spectral_config,
              &diffraction_v2) == nullptr);

    ure_wave_optics_config_t coherent_wave_config{};
    coherent_wave_config.mode = URE_WAVE_OPTICS_COHERENT_FIELD;
    coherent_wave_config.coherent_field_enabled = 1;
    CHECK(ure_session_create_wave_config(&spectral_config, &coherent_wave_config) == nullptr);

    ure_wave_optics_config_t invalid_wave_config{};
    invalid_wave_config.mode = 99;
    CHECK(ure_session_create_wave_config(&spectral_config, &invalid_wave_config) == nullptr);

    ure_integrator_config_t wavefront_integrator{};
    wavefront_integrator.mode = URE_INTEGRATOR_WAVEFRONT;
    wavefront_integrator.sampler = URE_INTEGRATOR_SAMPLER_LOW_DISCREPANCY;
    wavefront_integrator.quality_preset = URE_INTEGRATOR_QUALITY_FINAL;
    ure_session_t* integrator_session =
        ure_session_create_integrator_config(&spectral_config, &radiometric_wave_config, &wavefront_integrator);
    CHECK(integrator_session != nullptr);
    const ure_integrator_estimator_metadata_t c_metadata =
        ure_session_get_estimator_metadata(integrator_session);
    CHECK(c_metadata.mode == URE_INTEGRATOR_WAVEFRONT);
    CHECK(c_metadata.policy == URE_ESTIMATOR_STANDARD);
    CHECK(c_metadata.biased == 0);
    CHECK(c_metadata.sample_space_version == 0);
    ure_session_destroy(integrator_session);

    ure_integrator_config_t restir_integrator{};
    restir_integrator.mode = URE_INTEGRATOR_RESTIR_DI;
    CHECK(ure_session_create_integrator_config(
              &spectral_config,
              &fluorescence_wave_config,
              &restir_integrator) == nullptr);

    ure_integrator_config_t invalid_integrator{};
    invalid_integrator.mode = 99;
    invalid_integrator.sampler = URE_INTEGRATOR_SAMPLER_DEFAULT;
    invalid_integrator.quality_preset = URE_INTEGRATOR_QUALITY_DEFAULT;
    CHECK(ure_session_create_integrator_config(&spectral_config, &radiometric_wave_config, &invalid_integrator) == nullptr);

    ure_backend_config_t backend_config{};
    backend_config.kind = URE_BACKEND_CUDA;
    ure_acceleration_config_t acceleration_config{};
    acceleration_config.provider =
        URE_ACCELERATION_PROVIDER_SELF_COMPUTE;
    acceleration_config.quality = URE_ACCELERATION_QUALITY_AUTO;
    acceleration_config.update_policy =
        URE_ACCELERATION_UPDATE_STATIC;
    ure_session_t* execution_session =
        ure_session_create_execution_config(
            &spectral_config,
            &radiometric_wave_config,
            nullptr,
            &backend_config,
            &acceleration_config);
    CHECK(execution_session != nullptr);
    ure_acceleration_stats_t c_acceleration_stats{};
    CHECK(
        ure_session_get_acceleration_stats(
            execution_session,
            &c_acceleration_stats) == 0);
    CHECK(c_acceleration_stats.node_count == 0);
    ure_acceleration_stats_v2_t
        c_acceleration_stats_v2{};
    CHECK(
        ure_session_get_acceleration_stats_v2(
            execution_session,
            &c_acceleration_stats_v2) == 0);
    CHECK(
        c_acceleration_stats_v2.baseline.node_count == 0);
    CHECK(c_acceleration_stats_v2.tlas_node_count == 0);
    ure_session_destroy(execution_session);
    acceleration_config.quality =
        URE_ACCELERATION_QUALITY_HIGH;
    execution_session =
        ure_session_create_execution_config(
            &spectral_config,
            &radiometric_wave_config,
            nullptr,
            &backend_config,
            &acceleration_config);
    CHECK(execution_session != nullptr);
    ure_acceleration_stats_v3_t c_acceleration_stats_v3{};
    CHECK(
        ure_session_get_acceleration_stats_v3(
            execution_session,
            &c_acceleration_stats_v3) == 0);
    CHECK(c_acceleration_stats_v3.blas_node_arity == 2);
    ure_acceleration_stats_v4_t c_acceleration_stats_v4{};
    CHECK(
        ure_session_get_acceleration_stats_v4(
            execution_session,
            &c_acceleration_stats_v4) == 0);
    CHECK(
        c_acceleration_stats_v4
            .quality.blas_node_arity == 2);
    ure_session_destroy(execution_session);
    acceleration_config.quality = 99;
    CHECK(
        ure_session_create_execution_config(
            &spectral_config,
            &radiometric_wave_config,
            nullptr,
            &backend_config,
            &acceleration_config) == nullptr);
    acceleration_config.quality = URE_ACCELERATION_QUALITY_AUTO;
    acceleration_config.collect_stats = 2;
    CHECK(
        ure_session_create_execution_config(
            &spectral_config,
            &radiometric_wave_config,
            nullptr,
            &backend_config,
            &acceleration_config) == nullptr);
    return 0;
}

static int test_restir_estimator_metadata_contract() {
    ure::RenderConfig preview;
    preview.integrator.mode = ure::IntegratorMode::RestirDI;
    preview.integrator.allow_biased_reuse = true;
    preview.restir_di.enabled = true;
    preview.restir_di.temporal_reuse = true;
    const auto preview_metadata =
        ure::make_integrator_estimator_metadata(preview, 7);
    CHECK(preview_metadata.policy ==
          ure::IntegratorEstimatorPolicy::RestirDIBiasedPreview);
    CHECK(preview_metadata.biased);
    CHECK(preview_metadata.temporal_reuse);
    CHECK(!preview_metadata.spatial_reuse);
    CHECK(preview_metadata.sample_space_version == ure::kRestirDISampleSpaceVersion);
    CHECK(ure::validate_integrator_estimator_metadata(preview_metadata));

    ure::RenderConfig production = preview;
    production.integrator.allow_biased_reuse = false;
    production.restir_di.unbiased = true;
    production.restir_di.spatial_reuse = true;
    const auto production_metadata =
        ure::make_integrator_estimator_metadata(production, 7);
    CHECK(production_metadata.policy ==
          ure::IntegratorEstimatorPolicy::RestirDIUnbiasedProduction);
    CHECK(!production_metadata.biased);
    CHECK(production_metadata.spatial_reuse);
    CHECK(ure::validate_integrator_estimator_metadata(production_metadata));
    CHECK(!ure::compatible_integrator_estimator_metadata(
        preview_metadata, production_metadata));

    auto engine = std::make_unique<FakeRenderEngine>();
    engine->estimator_metadata = production_metadata;
    engine->acceleration_stats.node_count = 17;
    engine->acceleration_stats.max_depth = 4;
    engine->dynamic_geometry_stats.
        deforming_update_count = 3;
    ure::RenderSession session(std::move(engine), production);
    const auto session_metadata = session.get_estimator_metadata();
    CHECK(ure::compatible_integrator_estimator_metadata(
        session_metadata, production_metadata));
    const auto acceleration_stats =
        session.get_acceleration_stats();
    CHECK(acceleration_stats.node_count == 17);
    CHECK(acceleration_stats.max_depth == 4);
    const auto geometry_stats =
        session.get_dynamic_geometry_stats();
    CHECK(geometry_stats.deforming_update_count == 3);
    return 0;
}

int main() {
    std::fprintf(stderr, "[Session API Test]\n");
    auto run = [](const char* name, int (*fn)()) {
        std::fprintf(stderr, "  test: %s ... ", name);
        int result = fn();
        std::fprintf(stderr, "%s\n", result == 0 ? "PASS" : "FAIL");
        return result;
    };

    int failed = 0;
    failed += run("test_requires_engine", test_requires_engine);
    failed += run("test_requires_scene", test_requires_scene);
    failed += run("test_scene_lifecycle", test_scene_lifecycle);
    failed += run("test_progressive_background_scheduler", test_progressive_background_scheduler);
    failed += run("test_scene_ir_and_camera", test_scene_ir_and_camera);
    failed += run("test_scene_diff_mutation", test_scene_diff_mutation);
    failed += run("test_scene_diff_replace_scene_forces_full_reload", test_scene_diff_replace_scene_forces_full_reload);
    failed += run("test_scene_diff_instance_transform_ir", test_scene_diff_instance_transform_ir);
    failed += run("test_scene_diff_emissive_instance_transform_ir_full_reload", test_scene_diff_emissive_instance_transform_ir_full_reload);
    failed += run("test_scene_diff_instance_transform_errors", test_scene_diff_instance_transform_errors);
    failed += run("test_scene_diff_material_update_ir", test_scene_diff_material_update_ir);
    failed += run("test_scene_diff_equivalent_mie_resource_uses_material_update", test_scene_diff_equivalent_mie_resource_uses_material_update);
    failed += run("test_scene_diff_changed_mie_resource_forces_full_reload", test_scene_diff_changed_mie_resource_forces_full_reload);
    failed += run("test_scene_diff_mutable_mie_alias_forces_full_reload", test_scene_diff_mutable_mie_alias_forces_full_reload);
    failed += run("test_scene_diff_material_texture_update_ir_full_reload", test_scene_diff_material_texture_update_ir_full_reload);
    failed += run("test_scene_diff_material_graph_update_ir_full_reload", test_scene_diff_material_graph_update_ir_full_reload);
    failed += run("test_scene_diff_material_spd_update_ir_full_reload", test_scene_diff_material_spd_update_ir_full_reload);
    failed += run("test_scene_diff_material_update_errors", test_scene_diff_material_update_errors);
    failed += run("test_scene_diff_mesh_update_ir", test_scene_diff_mesh_update_ir);
    failed += run("test_scene_diff_mesh_update_errors", test_scene_diff_mesh_update_errors);
    failed += run("test_scene_diff_topology_update_ir_full_reload", test_scene_diff_topology_update_ir_full_reload);
    failed += run("test_scene_diff_topology_errors", test_scene_diff_topology_errors);
    failed += run("test_scene_diff_topology_real_gpu_reload_smoke", test_scene_diff_topology_real_gpu_reload_smoke);
    failed += run("test_acceleration_async_budget_real_gpu", test_acceleration_async_budget_real_gpu);
    failed += run("test_scene_diff_texture_material_real_gpu_reload_smoke", test_scene_diff_texture_material_real_gpu_reload_smoke);
    failed += run("test_c_session_lifecycle", test_c_session_lifecycle);
    failed += run("test_restir_estimator_metadata_contract", test_restir_estimator_metadata_contract);

    std::fprintf(stderr, "  passed: %d, failed: %d\n", g_passed, g_failed);
    return failed || g_failed ? 1 : 0;
}
