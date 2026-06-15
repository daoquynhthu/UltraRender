#include <ure/session.hpp>
#include <ure/gpu_scene_compiler.hpp>
#include <ure/gpu_structs.hpp>
#include <ure/log.hpp>
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

    void update_transforms(const ure::gpu::GpuInstanceTransform* transforms, int count) override {
        transform_updates += 1;
        last_transform_count = count;
        last_transforms.assign(transforms, transforms + count);
        spp = 0;
    }

    void update_materials(const ure::gpu::GpuMaterialData* materials, int count) override {
        material_updates += 1;
        last_material_count = count;
        if (count > 0) {
            last_materials.assign(materials, materials + count);
        } else {
            last_materials.clear();
        }
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

    bool loaded = false;
    int spp = 0;
    int scene_ir_loads = 0;
    int scene_ir_reloads = 0;
    int transform_updates = 0;
    int last_transform_count = 0;
    int material_updates = 0;
    int last_material_count = 0;
    int resets = 0;
    int camera_updates = 0;
    int width = 1;
    int height = 1;
    std::vector<ure::gpu::GpuInstanceTransform> last_transforms;
    std::vector<ure::gpu::GpuMaterialData> last_materials;
    std::vector<float> framebuffer = {0.1f, 0.2f, 0.3f};
    std::vector<float> normal_aov = {0.0f, 1.0f, 0.0f};
    std::vector<float> albedo_aov = {0.8f, 0.7f, 0.6f};
    std::vector<float> depth_aov = {4.0f};
    std::vector<float> uv_aov = {0.25f, 0.75f};
    std::vector<float> motion_aov = {0.1f, 0.0f};
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
    CHECK(raw_engine->last_transforms.size() == 1);
    const auto& transform = raw_engine->last_transforms[0];
    CHECK(transform.transform.m[0][3] == 2.0f);
    CHECK(transform.transform.m[0][0] == 3.0f);
    CHECK(transform.inverse_transform.m[0][0] > 0.3333f && transform.inverse_transform.m[0][0] < 0.3334f);
    CHECK(transform.inverse_transform.m[0][3] < -0.6666f && transform.inverse_transform.m[0][3] > -0.6667f);
    CHECK(transform.min_pt.x == 2.0f);
    CHECK(transform.max_pt.x == 5.0f);
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
    CHECK(raw_engine->last_materials[0].header.type == ure::gpu::MaterialType::Metal);
    CHECK(raw_engine->last_materials[0].header.roughness == 0.2f);
    CHECK(raw_engine->last_materials[0].header.ior == 2.0f);
    CHECK(raw_engine->spp == 0);
    CHECK(session.state() == ure::RenderSessionState::Ready);
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

static int test_scene_diff_material_update_errors() {
    auto* raw_engine = new FakeRenderEngine();
    ure::RenderSession session{std::unique_ptr<ure::IRenderEngine>(raw_engine)};

    session.load_scene(make_scene_ir_with_instance());
    CHECK(throws_exception([&session] {
        session.mutate_scene(ure::SceneDiff::update_material(1, ure::scene_ir::MaterialNode{}));
    }));
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
    ure::RenderSession session = ure::RenderSession::create(config);
    ure::scene_ir::SceneIR scene_ir = make_scene_ir_with_instance();
    scene_ir.width = 4;
    scene_ir.height = 4;
    scene_ir.camera.position = {0.0f, 0.0f, 4.0f};
    scene_ir.camera.look_at = {0.0f, 0.0f, 0.0f};
    scene_ir.camera.fov = 45.0f;

    session.load_scene(scene_ir);
    session.start_render();
    CHECK(session.render_pass() >= 1);

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

    ure_wave_optics_config_t coherent_wave_config{};
    coherent_wave_config.mode = URE_WAVE_OPTICS_COHERENT_FIELD;
    coherent_wave_config.coherent_field_enabled = 1;
    CHECK(ure_session_create_wave_config(&spectral_config, &coherent_wave_config) == nullptr);

    ure_wave_optics_config_t invalid_wave_config{};
    invalid_wave_config.mode = 99;
    CHECK(ure_session_create_wave_config(&spectral_config, &invalid_wave_config) == nullptr);
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
    failed += run("test_scene_diff_instance_transform_errors", test_scene_diff_instance_transform_errors);
    failed += run("test_scene_diff_material_update_ir", test_scene_diff_material_update_ir);
    failed += run("test_scene_diff_material_texture_update_ir_full_reload", test_scene_diff_material_texture_update_ir_full_reload);
    failed += run("test_scene_diff_material_update_errors", test_scene_diff_material_update_errors);
    failed += run("test_scene_diff_topology_update_ir_full_reload", test_scene_diff_topology_update_ir_full_reload);
    failed += run("test_scene_diff_topology_errors", test_scene_diff_topology_errors);
    failed += run("test_scene_diff_topology_real_gpu_reload_smoke", test_scene_diff_topology_real_gpu_reload_smoke);
    failed += run("test_scene_diff_texture_material_real_gpu_reload_smoke", test_scene_diff_texture_material_real_gpu_reload_smoke);
    failed += run("test_c_session_lifecycle", test_c_session_lifecycle);

    std::fprintf(stderr, "  passed: %d, failed: %d\n", g_passed, g_failed);
    return failed || g_failed ? 1 : 0;
}
