#include "product_build_config.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <functional>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <ure/backend.hpp>
#include <ure/native_scene_hash.hpp>
#include <ure/product/product_service.hpp>
#include <ure/session.hpp>

namespace ure::product {
namespace {

template <class T>
void append_bytes(std::vector<std::uint8_t>& bytes, const T& value) {
    const auto* begin = reinterpret_cast<const std::uint8_t*>(&value);
    bytes.insert(bytes.end(), begin, begin + sizeof(value));
}

void append_identity(std::vector<std::uint8_t>& bytes,
                     const Identity& identity) {
    bytes.insert(bytes.end(), identity.begin(), identity.end());
}

std::uint64_t checked_add(std::uint64_t left, std::uint64_t right) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left)
        throw ProductError(ProductFailureCode::MemoryNotApplicable,
                           "product memory estimate overflowed");
    return left + right;
}

std::uint64_t checked_multiply(std::uint64_t left, std::uint64_t right) {
    if (left != 0 &&
        right > std::numeric_limits<std::uint64_t>::max() / left)
        throw ProductError(ProductFailureCode::MemoryNotApplicable,
                           "product memory estimate overflowed");
    return left * right;
}

Identity plan_identity(const ProductIdentitySet& identities,
                       const ProductObjective& objective,
                       const ProductMemoryPlan& memory_plan) {
    std::vector<std::uint8_t> bytes;
    constexpr std::string_view domain = "UltraRender.ProductPlan.v0";
    bytes.insert(bytes.end(), domain.begin(), domain.end());
    bytes.push_back(0);
    append_identity(bytes, identities.build);
    append_identity(bytes, identities.snapshot);
    append_identity(bytes, objective.identity);
    append_bytes(bytes, objective.wall_time_budget_ns);
    append_bytes(bytes, objective.memory_budget_bytes);
    append_bytes(bytes, objective.requested_samples);
    append_bytes(bytes, objective.latency_budget_ns);
    append_bytes(bytes, objective.determinism_policy);
    append_bytes(bytes, objective.usage_policy);
    for (const auto output : objective.output_semantics)
        append_bytes(bytes, output);
    append_bytes(bytes, memory_plan.framebuffer_bytes);
    append_bytes(bytes, memory_plan.spectral_plane_bytes);
    append_bytes(bytes, memory_plan.queue_bytes);
    append_bytes(bytes, memory_plan.acceleration_bytes);
    append_bytes(bytes, memory_plan.scene_resource_bytes);
    append_bytes(bytes, memory_plan.executor_state_bytes);
    append_bytes(bytes, memory_plan.scratch_bytes);
    append_bytes(bytes, memory_plan.output_bytes);
    append_bytes(bytes, memory_plan.estimated_peak_bytes);
    append_bytes(bytes, memory_plan.persistent_executor_count);
    return content_identity(bytes);
}

RenderConfig render_config(const ProductObjective& objective) {
    RenderConfig config;
    config.integrator.mode = IntegratorMode::Automatic;
    config.automatic_integrator.enabled = true;
    config.automatic_integrator.time_budget_milliseconds =
        objective.wall_time_budget_ns / UINT64_C(1000000);
    const std::uint64_t memory_mb =
        objective.memory_budget_bytes / UINT64_C(1048576);
    config.automatic_integrator.memory_budget_mb = static_cast<int>(
        std::min<std::uint64_t>(
            memory_mb,
            static_cast<std::uint64_t>(std::numeric_limits<int>::max())));
    config.samples_per_pass = 1;
    config.sample_index_offset = 0;
    return config;
}

std::string resolve_resource_path(const std::filesystem::path& root,
                                  std::string_view value) {
    if (value.empty())
        return {};
    if (root.empty())
        throw ProductError(ProductFailureCode::ResourceMissing,
                           "product scene resource has no execution root");
    std::error_code error;
    const auto canonical_root = std::filesystem::weakly_canonical(root, error);
    if (error || canonical_root.empty())
        throw ProductError(ProductFailureCode::ResourceMissing,
                           "product scene has no valid execution root");
    const std::filesystem::path input(value);
    const auto candidate = std::filesystem::weakly_canonical(
        input.is_absolute() ? input : canonical_root / input, error);
    if (error || !std::filesystem::is_regular_file(candidate, error) || error)
        throw ProductError(ProductFailureCode::ResourceMissing,
                           "product scene resource is missing");
    const auto relative = std::filesystem::relative(
        candidate, canonical_root, error);
    if (error || relative.empty() || relative.is_absolute() ||
        *relative.begin() == "..")
        throw ProductError(ProductFailureCode::ResourceEscape,
                           "product scene resource escapes its execution root");
    return candidate.string();
}

scene_ir::SceneIR realize_scene(
    const native_scene::NativeSceneArchive& archive) {
    auto scene = archive.scene;
    std::unordered_map<const scene_ir::ImageResource*,
                       std::shared_ptr<scene_ir::ImageResource>> images;
    std::unordered_map<const scene_ir::TextureResource*,
                       std::shared_ptr<scene_ir::TextureResource>> textures;
    std::unordered_map<const scene_ir::MaterialNode*,
                       std::shared_ptr<scene_ir::MaterialNode>> materials;
    const auto clone_image = [&](const std::shared_ptr<scene_ir::ImageResource>& input) {
        if (!input)
            return std::shared_ptr<scene_ir::ImageResource>{};
        if (const auto found = images.find(input.get()); found != images.end())
            return found->second;
        auto output = std::make_shared<scene_ir::ImageResource>(*input);
        output->uri = resolve_resource_path(archive.execution_root, output->uri);
        images.emplace(input.get(), output);
        return output;
    };
    std::function<std::shared_ptr<scene_ir::TextureResource>(
        const std::shared_ptr<scene_ir::TextureResource>&)> clone_texture;
    clone_texture = [&](const std::shared_ptr<scene_ir::TextureResource>& input) {
        if (!input)
            return std::shared_ptr<scene_ir::TextureResource>{};
        if (const auto found = textures.find(input.get());
            found != textures.end())
            return found->second;
        auto output = std::make_shared<scene_ir::TextureResource>(*input);
        textures.emplace(input.get(), output);
        output->image = clone_image(input->image);
        return output;
    };
    const auto clone_material = [&](const std::shared_ptr<scene_ir::MaterialNode>& input) {
        if (!input)
            return std::shared_ptr<scene_ir::MaterialNode>{};
        if (const auto found = materials.find(input.get());
            found != materials.end())
            return found->second;
        auto output = std::make_shared<scene_ir::MaterialNode>(*input);
        materials.emplace(input.get(), output);
        output->base_color_texture = clone_texture(input->base_color_texture);
        output->roughness_texture = clone_texture(input->roughness_texture);
        output->emission_texture = clone_texture(input->emission_texture);
        output->normal_texture = clone_texture(input->normal_texture);
        if (input->spectral_extension) {
            output->spectral_extension =
                std::make_shared<scene_ir::SpectralMaterialExtension>(
                    *input->spectral_extension);
            output->spectral_extension->albedo_spd = resolve_resource_path(
                archive.execution_root,
                output->spectral_extension->albedo_spd);
            output->spectral_extension->emission_spd = resolve_resource_path(
                archive.execution_root,
                output->spectral_extension->emission_spd);
        }
        if (input->graph) {
            output->graph = std::make_shared<scene_ir::MaterialGraph>(
                *input->graph);
            for (auto& node : output->graph->nodes)
                node.texture = clone_texture(node.texture);
        }
        return output;
    };
    for (auto& image : scene.images)
        image = clone_image(image);
    for (auto& texture : scene.textures)
        texture = clone_texture(texture);
    for (auto& material : scene.materials)
        material = clone_material(material);
    for (auto& instance : scene.instances)
        instance.material = clone_material(instance.material);
    for (auto& sphere : scene.spheres)
        sphere.material = clone_material(sphere.material);
    for (auto& light : scene.quad_lights)
        light.material = clone_material(light.material);
    return scene;
}

std::uint64_t external_resource_bytes(const scene_ir::SceneIR& scene) {
    std::unordered_set<std::string> paths;
    const auto add_texture = [&paths](
        const std::shared_ptr<scene_ir::TextureResource>& texture) {
        if (texture && texture->image && !texture->image->uri.empty())
            paths.insert(texture->image->uri);
    };
    for (const auto& image : scene.images) {
        if (image && !image->uri.empty())
            paths.insert(image->uri);
    }
    for (const auto& texture : scene.textures)
        add_texture(texture);
    for (const auto& material : scene.materials) {
        if (!material)
            continue;
        add_texture(material->base_color_texture);
        add_texture(material->roughness_texture);
        add_texture(material->emission_texture);
        add_texture(material->normal_texture);
        if (material->graph) {
            for (const auto& node : material->graph->nodes)
                add_texture(node.texture);
        }
        if (material->spectral_extension) {
            if (!material->spectral_extension->albedo_spd.empty())
                paths.insert(material->spectral_extension->albedo_spd);
            if (!material->spectral_extension->emission_spd.empty())
                paths.insert(material->spectral_extension->emission_spd);
        }
    }
    std::uint64_t result{};
    for (const auto& path : paths) {
        std::error_code error;
        const auto bytes = std::filesystem::file_size(path, error);
        if (error)
            throw ProductError(ProductFailureCode::ResourceMissing,
                               "product scene resource size is unavailable");
        result = checked_add(result, checked_multiply(bytes, 8));
    }
    return result;
}

ProductMemoryPlan make_memory_plan(
    const native_scene::NativeSceneArchive& archive,
    const scene_ir::SceneIR& scene,
    const RenderConfig& config,
    const BackendSelection& selection,
    const ProductObjective& objective) {
    const auto width = static_cast<std::uint64_t>(scene.width);
    const auto height = static_cast<std::uint64_t>(scene.height);
    const auto pixels = checked_multiply(width, height);
    const auto lanes = static_cast<std::uint64_t>(
        spectral_packet_lanes(config));
    const auto executors = static_cast<std::uint64_t>(std::max(
        config.automatic_integrator.maximum_techniques, 1));

    std::uint64_t descriptor_bytes{};
    for (const auto& resource : archive.document.resources) {
        descriptor_bytes = checked_add(
            descriptor_bytes,
            std::max(resource.resident_bytes, resource.byte_length));
    }
    std::uint64_t mesh_bytes{};
    std::uint64_t triangle_count{};
    for (const auto& mesh : scene.meshes) {
        if (!mesh || !mesh->mesh)
            continue;
        mesh_bytes = checked_add(
            mesh_bytes,
            checked_multiply(mesh->mesh->vertices.size(), sizeof(Vertex)));
        mesh_bytes = checked_add(
            mesh_bytes,
            checked_multiply(mesh->mesh->indices.size(), sizeof(int)));
        triangle_count = checked_add(
            triangle_count, mesh->mesh->indices.size() / 3);
    }
    const auto per_scene_resource = checked_add(
        checked_add(descriptor_bytes, mesh_bytes),
        external_resource_bytes(scene));
    const auto per_acceleration = checked_add(
        checked_multiply(triangle_count, 128),
        checked_multiply(
            scene.instances.size() + scene.spheres.size(), 128));
    const auto per_framebuffer = checked_multiply(pixels, 72);
    const auto per_spectral_plane = checked_multiply(pixels, 12);
    const auto per_queue = checked_multiply(
        pixels, checked_add(288, checked_multiply(lanes, 56)));
    constexpr std::uint64_t executor_state = UINT64_C(8388608);
    const auto scratch = checked_add(
        checked_multiply(triangle_count, 256), UINT64_C(4194304));

    ProductMemoryPlan result;
    result.framebuffer_bytes = checked_multiply(per_framebuffer, executors);
    result.spectral_plane_bytes = checked_multiply(
        per_spectral_plane, executors);
    result.queue_bytes = checked_multiply(per_queue, executors);
    result.acceleration_bytes = checked_multiply(
        per_acceleration, executors);
    result.scene_resource_bytes = checked_multiply(
        per_scene_resource, executors);
    result.executor_state_bytes = checked_multiply(
        executor_state, executors);
    result.scratch_bytes = scratch;
    result.output_bytes = checked_multiply(pixels, 12);
    result.persistent_executor_count = static_cast<std::uint32_t>(executors);
    result.estimated_peak_bytes = result.framebuffer_bytes;
    result.estimated_peak_bytes = checked_add(
        result.estimated_peak_bytes, result.spectral_plane_bytes);
    result.estimated_peak_bytes = checked_add(
        result.estimated_peak_bytes, result.queue_bytes);
    result.estimated_peak_bytes = checked_add(
        result.estimated_peak_bytes, result.acceleration_bytes);
    result.estimated_peak_bytes = checked_add(
        result.estimated_peak_bytes, result.scene_resource_bytes);
    result.estimated_peak_bytes = checked_add(
        result.estimated_peak_bytes, result.executor_state_bytes);
    result.estimated_peak_bytes = checked_add(
        result.estimated_peak_bytes, result.scratch_bytes);
    result.estimated_peak_bytes = checked_add(
        result.estimated_peak_bytes, result.output_bytes);
    result.applicable_budget_bytes = selection.memory_budget_bytes;
    if (objective.memory_budget_bytes != 0) {
        result.applicable_budget_bytes = std::min(
            result.applicable_budget_bytes, objective.memory_budget_bytes);
    }
    if (result.estimated_peak_bytes > result.applicable_budget_bytes)
        throw ProductError(ProductFailureCode::MemoryNotApplicable,
                           "product execution plan is not applicable to the selected memory budget");
    return result;
}

struct PreparedRenderer {
    std::unique_ptr<RenderSession> renderer;
    ProductMemoryPlan memory_plan;
};

PreparedRenderer make_renderer(
    const native_scene::NativeSceneArchive& archive,
    const ProductObjective& objective) {
    const auto config = render_config(objective);
    auto scene = realize_scene(archive);
    if (scene.width <= 0)
        scene.width = 64;
    if (scene.height <= 0)
        scene.height = 64;
    const auto selection = select_backend(config);
    auto memory_plan = make_memory_plan(
        archive, scene, config, selection, objective);
    auto renderer = std::make_unique<RenderSession>(
        RenderSession::create(config));
    renderer->load_scene(scene);
    return PreparedRenderer{std::move(renderer), memory_plan};
}

Identity frame_identity(const ProductFrame& frame) {
    const auto bytes = std::as_bytes(std::span(frame.rgb));
    return content_identity(std::span(
        reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()));
}

class ProductJobImpl final : public ProductJob {
public:
    ProductJobImpl(native_scene::NativeSceneArchive archive,
                   Identity snapshot_identity,
                   ProductObjective objective)
        : archive_(std::move(archive)), objective_(std::move(objective)) {
        identities_.build = identity_from_hex(
            std::span<const char, 64>(URE_PRODUCT_BUILD_DIGEST, 64));
        identities_.snapshot = snapshot_identity;
        identities_.objective = objective_.identity;
        auto prepared = make_renderer(archive_, objective_);
        memory_plan_ = prepared.memory_plan;
        identities_.plan = plan_identity(
            identities_, objective_, memory_plan_);
        renderer_ = std::move(prepared.renderer);
        operation_.requested_samples = objective_.requested_samples;
        operation_.accepted_samples = objective_.requested_samples;
    }

    const ProductIdentitySet& identities() const noexcept override {
        return identities_;
    }

    const ProductObjective& objective() const noexcept override {
        return objective_;
    }

    const ProductMemoryPlan& memory_plan() const noexcept override {
        return memory_plan_;
    }

    ProductOperationSnapshot operation() const noexcept override {
        std::scoped_lock lock(mutex_);
        return operation_;
    }

    void replace_scene(native_scene::NativeSceneArchive archive,
                       Identity snapshot_identity) override {
        auto prepared = make_renderer(archive, objective_);
        std::scoped_lock lock(mutex_);
        archive_ = std::move(archive);
        renderer_ = std::move(prepared.renderer);
        memory_plan_ = prepared.memory_plan;
        identities_.snapshot = snapshot_identity;
        identities_.plan = plan_identity(
            identities_, objective_, memory_plan_);
        operation_ = {};
        operation_.requested_samples = objective_.requested_samples;
        operation_.accepted_samples = objective_.requested_samples;
    }

    void begin() override {
        std::scoped_lock lock(mutex_);
        if (operation_.state == ProductOperationState::Running ||
            operation_.state == ProductOperationState::Paused)
            throw std::logic_error("product job already has active work");
        renderer_->reset_accumulation();
        operation_.state = ProductOperationState::Running;
        operation_.completed_samples = 0;
        operation_.pilot_samples = 0;
        operation_.scene_realizations = 0;
        operation_.executor_creations = 0;
        operation_.actual_renderer_samples = 0;
    }

    void render_sample() override {
        {
            std::scoped_lock lock(mutex_);
            if (operation_.state != ProductOperationState::Running)
                throw std::logic_error("product job is not running");
            if (operation_.completed_samples >= operation_.accepted_samples)
                throw std::out_of_range("product sample budget is exhausted");
        }
        if (objective_.force_device_loss)
            throw std::runtime_error("device lost: conformance product fault");
        renderer_->render_pass();
        const auto report = renderer_->get_automatic_integrator_report();
        std::scoped_lock lock(mutex_);
        const auto completed = operation_.completed_samples + 1;
        if (report.production_sample_count != completed ||
            report.total_allocated_spp != completed) {
            throw ProductError(ProductFailureCode::WorkAccounting,
                "renderer work accounting diverged from the accepted product domain");
        }
        operation_.completed_samples = completed;
        operation_.pilot_samples = report.pilot_sample_count;
        operation_.scene_realizations = report.scene_realization_count;
        operation_.executor_creations =
            report.pilot_executor_creation_count +
            report.production_executor_creation_count;
        operation_.actual_renderer_samples =
            report.production_sample_count;
    }

    ProductFrame publish_frame() override {
        int width{};
        int height{};
        renderer_->get_framebuffer_size(width, height);
        const auto& framebuffer = renderer_->get_framebuffer();
        ProductFrame frame;
        {
            std::scoped_lock lock(mutex_);
            if (operation_.state != ProductOperationState::Running)
                throw std::logic_error("product frame publication requires running work");
            if (operation_.completed_samples != operation_.accepted_samples)
                throw std::logic_error(
                    "product frame publication requires complete accepted work");
            operation_.state = ProductOperationState::Succeeded;
            frame.identities = identities_;
            frame.accepted_samples = operation_.completed_samples;
        }
        frame.width = static_cast<std::uint32_t>(width);
        frame.height = static_cast<std::uint32_t>(height);
        frame.rgb = framebuffer;
        return frame;
    }

    ProductArtifactManifest artifact_manifest(
        const ProductFrame& frame) const override {
        if (frame.identities.plan != identities_.plan ||
            frame.identities.snapshot != identities_.snapshot ||
            frame.identities.objective != identities_.objective ||
            frame.identities.build != identities_.build)
            throw std::invalid_argument("product frame identity mismatch");
        ProductArtifactManifest manifest;
        manifest.identities = frame.identities;
        manifest.frame_content = frame_identity(frame);
        manifest.accepted_samples = frame.accepted_samples;
        manifest.rgb_value_count = frame.rgb.size();
        return manifest;
    }

    void pause() override {
        std::scoped_lock lock(mutex_);
        if (operation_.state != ProductOperationState::Running)
            throw std::logic_error("product job cannot be paused");
        renderer_->pause();
        operation_.state = ProductOperationState::Paused;
    }

    void resume() override {
        std::scoped_lock lock(mutex_);
        if (operation_.state != ProductOperationState::Paused)
            throw std::logic_error("product job cannot be resumed");
        renderer_->resume();
        operation_.state = ProductOperationState::Running;
    }

    void cancel() override {
        std::scoped_lock lock(mutex_);
        renderer_->cancel();
        operation_.state = ProductOperationState::Canceled;
    }

    void fail() noexcept override {
        std::scoped_lock lock(mutex_);
        operation_.state = ProductOperationState::Failed;
    }

    void reset() override {
        std::scoped_lock lock(mutex_);
        if (operation_.state == ProductOperationState::Running ||
            operation_.state == ProductOperationState::Paused)
            throw std::logic_error("active product job cannot be reset");
        renderer_->reset_accumulation();
        operation_ = {};
        operation_.requested_samples = objective_.requested_samples;
        operation_.accepted_samples = objective_.requested_samples;
    }

private:
    mutable std::mutex mutex_;
    native_scene::NativeSceneArchive archive_;
    ProductObjective objective_;
    ProductIdentitySet identities_;
    ProductMemoryPlan memory_plan_;
    std::unique_ptr<RenderSession> renderer_;
    ProductOperationSnapshot operation_;
};

}

ProductError::ProductError(ProductFailureCode code, std::string message)
    : std::runtime_error(std::move(message)), code_(code) {}

ProductFailureCode ProductError::code() const noexcept {
    return code_;
}

Identity identity_from_hex(std::span<const char, 64> text) {
    Identity output{};
    const auto nibble = [](const char character) -> std::uint8_t {
        if (character >= '0' && character <= '9')
            return static_cast<std::uint8_t>(character - '0');
        if (character >= 'a' && character <= 'f')
            return static_cast<std::uint8_t>(character - 'a' + 10);
        throw std::invalid_argument("invalid SHA-256 text");
    };
    for (std::size_t index = 0; index < output.size(); ++index)
        output[index] = static_cast<std::uint8_t>(
            nibble(text[index * 2]) * 16U + nibble(text[index * 2 + 1]));
    return output;
}

Identity content_identity(std::span<const std::uint8_t> bytes) {
    const std::string digest = native_scene::sha256_hex(bytes);
    return identity_from_hex(std::span<const char, 64>(digest.data(), 64));
}

std::unique_ptr<ProductJob> ProductJob::create(
    native_scene::NativeSceneArchive archive,
    Identity snapshot_identity,
    ProductObjective objective) {
    if (objective.requested_samples == 0)
        objective.requested_samples = 1;
    return std::make_unique<ProductJobImpl>(
        std::move(archive), snapshot_identity, std::move(objective));
}

}
