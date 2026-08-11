#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <ure/native_scene_ir.hpp>

namespace ure::product {

using Identity = std::array<std::uint8_t, 32>;

struct ProductObjective {
    Identity identity{};
    std::uint64_t wall_time_budget_ns{};
    std::uint64_t memory_budget_bytes{};
    std::uint64_t requested_samples{1};
    std::uint64_t latency_budget_ns{};
    std::uint32_t determinism_policy{};
    std::uint32_t usage_policy{};
    std::vector<std::uint32_t> output_semantics;
    bool force_device_loss{};
};

struct ProductIdentitySet {
    Identity build{};
    Identity snapshot{};
    Identity objective{};
    Identity plan{};
};

enum class ProductOperationState : std::uint32_t {
    Ready,
    Running,
    Paused,
    Canceled,
    Succeeded,
    Failed
};

struct ProductOperationSnapshot {
    ProductOperationState state{ProductOperationState::Ready};
    std::uint64_t requested_samples{};
    std::uint64_t accepted_samples{};
};

struct ProductFrame {
    ProductIdentitySet identities;
    std::uint64_t accepted_samples{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<float> rgb;
};

struct ProductArtifactManifest {
    ProductIdentitySet identities;
    Identity frame_content{};
    std::uint64_t accepted_samples{};
    std::uint64_t rgb_value_count{};
};

class ProductJob {
public:
    static std::unique_ptr<ProductJob> create(
        native_scene::NativeSceneArchive archive,
        Identity snapshot_identity,
        ProductObjective objective);

    virtual ~ProductJob() = default;
    ProductJob(const ProductJob&) = delete;
    ProductJob& operator=(const ProductJob&) = delete;

    virtual const ProductIdentitySet& identities() const noexcept = 0;
    virtual const ProductObjective& objective() const noexcept = 0;
    virtual ProductOperationSnapshot operation() const noexcept = 0;
    virtual void replace_scene(native_scene::NativeSceneArchive archive,
                               Identity snapshot_identity) = 0;
    virtual void begin() = 0;
    virtual void render_sample() = 0;
    virtual ProductFrame publish_frame() = 0;
    virtual ProductArtifactManifest artifact_manifest(
        const ProductFrame& frame) const = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual void cancel() = 0;
    virtual void fail() noexcept = 0;
    virtual void reset() = 0;

protected:
    ProductJob() = default;
};

Identity identity_from_hex(std::span<const char, 64> text);
Identity content_identity(std::span<const std::uint8_t> bytes);

}
