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

Identity plan_identity(const ProductIdentitySet& identities,
                       const ProductObjective& objective) {
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

std::unique_ptr<RenderSession> make_renderer(
    const native_scene::NativeSceneArchive& archive,
    const ProductObjective& objective) {
    auto renderer = std::make_unique<RenderSession>(
        RenderSession::create(render_config(objective)));
    auto scene = archive.scene;
    if (scene.width <= 0)
        scene.width = 64;
    if (scene.height <= 0)
        scene.height = 64;
    renderer->load_scene(scene);
    return renderer;
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
        identities_.plan = plan_identity(identities_, objective_);
        renderer_ = make_renderer(archive_, objective_);
        operation_.requested_samples = objective_.requested_samples;
    }

    const ProductIdentitySet& identities() const noexcept override {
        return identities_;
    }

    const ProductObjective& objective() const noexcept override {
        return objective_;
    }

    ProductOperationSnapshot operation() const noexcept override {
        std::scoped_lock lock(mutex_);
        return operation_;
    }

    void replace_scene(native_scene::NativeSceneArchive archive,
                       Identity snapshot_identity) override {
        auto renderer = make_renderer(archive, objective_);
        std::scoped_lock lock(mutex_);
        archive_ = std::move(archive);
        renderer_ = std::move(renderer);
        identities_.snapshot = snapshot_identity;
        identities_.plan = plan_identity(identities_, objective_);
        operation_ = {};
        operation_.requested_samples = objective_.requested_samples;
    }

    void begin() override {
        std::scoped_lock lock(mutex_);
        if (operation_.state == ProductOperationState::Running ||
            operation_.state == ProductOperationState::Paused)
            throw std::logic_error("product job already has active work");
        renderer_->reset_accumulation();
        operation_.state = ProductOperationState::Running;
        operation_.accepted_samples = 0;
    }

    void render_sample() override {
        {
            std::scoped_lock lock(mutex_);
            if (operation_.state != ProductOperationState::Running)
                throw std::logic_error("product job is not running");
            if (operation_.accepted_samples >= operation_.requested_samples)
                throw std::out_of_range("product sample budget is exhausted");
        }
        if (objective_.force_device_loss)
            throw std::runtime_error("device lost: conformance product fault");
        renderer_->render_pass();
        std::scoped_lock lock(mutex_);
        ++operation_.accepted_samples;
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
            operation_.state = ProductOperationState::Succeeded;
            frame.identities = identities_;
            frame.accepted_samples = operation_.accepted_samples;
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
    }

private:
    mutable std::mutex mutex_;
    native_scene::NativeSceneArchive archive_;
    ProductObjective objective_;
    ProductIdentitySet identities_;
    std::unique_ptr<RenderSession> renderer_;
    ProductOperationSnapshot operation_;
};

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
