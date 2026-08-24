#include "product_build_config.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstring>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <ure/backend.hpp>
#include <ure/native_scene_hash.hpp>
#include <ure/product/product_scene.hpp>
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
                       const ProductMemoryPlan& memory_plan,
                       const ProductExecutionInfo& execution) {
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
    append_bytes(bytes, objective.backend);
    append_identity(bytes, objective.requested_device_identity);
    append_bytes(bytes, objective.required_features);
    append_bytes(bytes, objective.has_requested_device);
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
    append_identity(bytes, execution.device_identity);
    append_bytes(bytes, execution.selection.adapter.kind);
    append_bytes(bytes, execution.provider);
    append_bytes(bytes, execution.selection.required_features);
    append_bytes(bytes, execution.selection.memory_budget_bytes);
    return content_identity(bytes);
}

RenderConfig render_config(const ProductSnapshot& snapshot,
                           const ProductObjective& objective) {
    RenderConfig config = snapshot.solver_config().value_or(RenderConfig{});
    if (!snapshot.solver_config()) {
        config.integrator.mode = IntegratorMode::Automatic;
        config.automatic_integrator.enabled = true;
    } else {
        config.automatic_integrator.enabled = false;
    }
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
    if (objective.backend != BackendKind::Auto &&
        config.backend.kind != BackendKind::Auto &&
        config.backend.kind != objective.backend) {
        throw ProductError(
            ProductFailureCode::CapabilityNotApplicable,
            "product objective backend conflicts with the realized solver contract");
    }
    if (objective.backend != BackendKind::Auto)
        config.backend.kind = objective.backend;
    config.backend.required_features = objective.required_features;
    return config;
}

ProductMemoryPlan make_memory_plan(
    const ProductSnapshot& snapshot,
    const scene_ir::SceneIR& scene,
    const RenderConfig& config,
    const BackendSelection& selection,
    const ProductObjective& objective) {
    const auto width = static_cast<std::uint64_t>(scene.width);
    const auto height = static_cast<std::uint64_t>(scene.height);
    const auto pixels = checked_multiply(width, height);
    const auto lanes = static_cast<std::uint64_t>(
        spectral_packet_lanes(config));
    const auto executors = static_cast<std::uint64_t>(
        config.integrator.mode == IntegratorMode::Automatic
            ? std::max(config.automatic_integrator.maximum_techniques, 1)
            : 1);

    const auto descriptor_bytes = snapshot.resource_budget().resident_bytes;
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
    const auto per_scene_resource = checked_add(descriptor_bytes, mesh_bytes);
    const auto per_acceleration = checked_add(
        checked_multiply(triangle_count, 128),
        checked_multiply(
            scene.instances.size() + scene.spheres.size(), 128));
    const auto per_framebuffer = checked_multiply(pixels, 72);
    const auto per_spectral_plane = checked_multiply(pixels, 12);
    const auto per_queue = checked_multiply(
        checked_multiply(
            pixels, checked_add(288, checked_multiply(lanes, 56))),
        2);
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
    ProductExecutionInfo execution;
    bool automatic{};
};

PreparedRenderer make_renderer(
    const std::shared_ptr<const ProductSnapshot>& snapshot,
    const ProductObjective& objective) {
    auto config = render_config(*snapshot, objective);
    auto scene = snapshot->render_scene();
    if (scene.width <= 0)
        scene.width = 64;
    if (scene.height <= 0)
        scene.height = 64;
    BackendSelection selection;
    try {
        if (objective.has_requested_device) {
            const auto adapters =
                enumerate_backend_adapters(config.backend.kind);
            const auto found = std::ranges::find_if(
                adapters, [&objective](const BackendAdapterInfo& adapter) {
                    return backend_adapter_identity(adapter) ==
                           objective.requested_device_identity;
                });
            if (found == adapters.end())
                throw std::invalid_argument(
                    "requested product device identity is unavailable");
            config.backend.kind = found->kind;
            config.backend.adapter_id = found->adapter_id;
        }
        selection = select_backend(config);
    } catch (const std::exception&) {
        throw ProductError(
            ProductFailureCode::CapabilityNotApplicable,
            "requested product backend, provider, or device is not applicable");
    }
    ProductMemoryPlan memory_plan;
    for (;;) {
        try {
            memory_plan = make_memory_plan(
                *snapshot, scene, config, selection, objective);
            break;
        } catch (const ProductError& error) {
            if (error.code() != ProductFailureCode::MemoryNotApplicable ||
                config.automatic_integrator.maximum_techniques <= 1)
                throw;
            --config.automatic_integrator.maximum_techniques;
        }
    }
    auto renderer = std::make_unique<RenderSession>(
        RenderSession::create(config));
    renderer->load_scene(scene);
    ProductExecutionInfo execution;
    execution.device_identity = backend_adapter_identity(selection.adapter);
    execution.selection = selection;
    execution.provider = 1;
    return PreparedRenderer{std::move(renderer), memory_plan,
                            std::move(execution),
                            config.integrator.mode == IntegratorMode::Automatic};
}

std::shared_ptr<const ProductSnapshot> realize_or_throw(
    native_scene::NativeSceneArchive archive,
    const Identity& source_identity) {
    auto realization = realize_product_scene(
        std::move(archive), source_identity);
    if (realization.ok())
        return std::move(realization.snapshot);
    const auto& diagnostic = realization.diagnostics.front();
    ProductFailureCode code = ProductFailureCode::CapabilityNotApplicable;
    if (diagnostic.domain == ProductSceneDiagnosticDomain::Resource)
        code = diagnostic.detail == 606 ||
                       (diagnostic.detail >= 614 && diagnostic.detail <= 617)
                   ? ProductFailureCode::MemoryNotApplicable
                   : ProductFailureCode::ResourceMissing;
    else if (diagnostic.domain == ProductSceneDiagnosticDomain::Parse ||
             diagnostic.domain == ProductSceneDiagnosticDomain::Schema ||
             diagnostic.domain == ProductSceneDiagnosticDomain::Package ||
             diagnostic.domain == ProductSceneDiagnosticDomain::Procedural)
        code = ProductFailureCode::MalformedScene;
    throw ProductError(code, diagnostic.code + ": " + diagnostic.message,
                       diagnostic.detail);
}

Identity frame_identity(const ProductFrame& frame) {
    const auto bytes = std::as_bytes(std::span(frame.rgb));
    return content_identity(std::span(
        reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()));
}

class ProductJobImpl final : public ProductJob {
public:
    ProductJobImpl(native_scene::NativeSceneArchive archive,
                   Identity source_identity,
                   ProductObjective objective)
        : objective_(std::move(objective)) {
        identities_.build = identity_from_hex(
            std::span<const char, 64>(URE_PRODUCT_BUILD_DIGEST, 64));
        snapshot_ = realize_or_throw(std::move(archive), source_identity);
        if (objective_.requested_samples == 0) {
            objective_.requested_samples =
                snapshot_->declared_samples() > 0
                    ? static_cast<std::uint64_t>(
                          snapshot_->declared_samples())
                    : UINT64_C(1);
        }
        identities_.snapshot = snapshot_->identity();
        identities_.objective = objective_.identity;
        auto prepared = make_renderer(snapshot_, objective_);
        memory_plan_ = prepared.memory_plan;
        execution_ = std::move(prepared.execution);
        automatic_execution_ = prepared.automatic;
        identities_.plan = plan_identity(
            identities_, objective_, memory_plan_, execution_);
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

    const ProductExecutionInfo& execution() const noexcept override {
        return execution_;
    }

    ProductOperationSnapshot operation() const noexcept override {
        std::scoped_lock lock(mutex_);
        return operation_;
    }

    void replace_scene(native_scene::NativeSceneArchive archive,
                       Identity source_identity) override {
        auto snapshot = realize_or_throw(std::move(archive), source_identity);
        auto prepared = make_renderer(snapshot, objective_);
        std::scoped_lock lock(mutex_);
        snapshot_ = std::move(snapshot);
        renderer_ = std::move(prepared.renderer);
        memory_plan_ = prepared.memory_plan;
        execution_ = std::move(prepared.execution);
        automatic_execution_ = prepared.automatic;
        identities_.snapshot = snapshot_->identity();
        identities_.plan = plan_identity(
            identities_, objective_, memory_plan_, execution_);
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
        std::scoped_lock lock(mutex_);
        const auto completed = operation_.completed_samples + 1;
        if (automatic_execution_) {
            const auto report = renderer_->get_automatic_integrator_report();
            if (report.production_sample_count != completed ||
                report.total_allocated_spp != completed) {
                throw ProductError(ProductFailureCode::WorkAccounting,
                    "renderer work accounting diverged from the accepted product domain");
            }
            operation_.pilot_samples = report.pilot_sample_count;
            operation_.scene_realizations = report.scene_realization_count;
            operation_.executor_creations =
                report.pilot_executor_creation_count +
                report.production_executor_creation_count;
            operation_.actual_renderer_samples =
                report.production_sample_count;
        } else {
            operation_.scene_realizations = 1;
            operation_.executor_creations = 1;
            operation_.actual_renderer_samples = completed;
        }
        operation_.completed_samples = completed;
    }

    ProductFrame snapshot_frame() const override {
        int width{};
        int height{};
        renderer_->get_framebuffer_size(width, height);
        const auto& framebuffer = renderer_->get_framebuffer();
        ProductFrame frame;
        {
            std::scoped_lock lock(mutex_);
            if (operation_.state != ProductOperationState::Running)
                throw std::logic_error("product frame snapshot requires running work");
            if (operation_.completed_samples == 0)
                throw std::logic_error("product frame snapshot requires completed work");
            frame.identities = identities_;
            frame.accepted_samples = operation_.completed_samples;
        }
        frame.width = static_cast<std::uint32_t>(width);
        frame.height = static_cast<std::uint32_t>(height);
        frame.rgb = framebuffer;
        return frame;
    }

    ProductFrame publish_frame() override {
        auto frame = snapshot_frame();
        std::scoped_lock lock(mutex_);
        if (operation_.completed_samples != operation_.accepted_samples)
            throw std::logic_error(
                "product frame publication requires complete accepted work");
        operation_.state = ProductOperationState::Succeeded;
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
    std::shared_ptr<const ProductSnapshot> snapshot_;
    ProductObjective objective_;
    ProductIdentitySet identities_;
    ProductMemoryPlan memory_plan_;
    ProductExecutionInfo execution_;
    std::unique_ptr<RenderSession> renderer_;
    ProductOperationSnapshot operation_;
    bool automatic_execution_{};
};

}

ProductError::ProductError(ProductFailureCode code, std::string message,
                           std::uint32_t detail)
    : std::runtime_error(std::move(message)), code_(code), detail_(detail) {}

ProductFailureCode ProductError::code() const noexcept {
    return code_;
}

std::uint32_t ProductError::detail() const noexcept { return detail_; }

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

Identity backend_adapter_identity(const BackendAdapterInfo& adapter) {
    std::vector<std::uint8_t> bytes;
    constexpr std::string_view domain =
        "UltraRender.ProductDeviceIdentity.v0";
    bytes.insert(bytes.end(), domain.begin(), domain.end());
    bytes.push_back(0);
    append_bytes(bytes, adapter.kind);
    append_bytes(bytes, adapter.vendor_id);
    append_bytes(bytes, adapter.device_id);
    bytes.insert(bytes.end(), adapter.adapter_id.begin(),
                 adapter.adapter_id.end());
    return content_identity(bytes);
}

std::unique_ptr<ProductJob> ProductJob::create(
    native_scene::NativeSceneArchive archive,
    Identity source_identity,
    ProductObjective objective) {
    return std::make_unique<ProductJobImpl>(
        std::move(archive), source_identity, std::move(objective));
}

}
