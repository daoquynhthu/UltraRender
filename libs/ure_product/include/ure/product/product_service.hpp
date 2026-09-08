#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <ure/backend_types.hpp>
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
    BackendKind backend{BackendKind::Auto};
    Identity requested_device_identity{};
    BackendFeatureSet required_features{};
    bool has_requested_device{};
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

enum class ProductFailureCode : std::uint32_t {
    MalformedScene,
    ResourceMissing,
    ResourceEscape,
    MemoryNotApplicable,
    CapabilityNotApplicable,
    WorkAccounting
};

class ProductError final : public std::runtime_error {
public:
    ProductError(ProductFailureCode code, std::string message,
                 std::uint32_t detail = 0,
                 std::string diagnostic_code = {},
                 std::string field_path = {},
                 std::string recovery = {});
    ProductFailureCode code() const noexcept;
    std::uint32_t detail() const noexcept;
    const std::string& diagnostic_code() const noexcept;
    const std::string& field_path() const noexcept;
    const std::string& recovery() const noexcept;

private:
    ProductFailureCode code_;
    std::uint32_t detail_{};
    std::string diagnostic_code_;
    std::string field_path_;
    std::string recovery_;
};

struct ProductMemoryPlan {
    std::uint64_t framebuffer_bytes{};
    std::uint64_t spectral_plane_bytes{};
    std::uint64_t queue_bytes{};
    std::uint64_t acceleration_bytes{};
    std::uint64_t scene_resource_bytes{};
    std::uint64_t executor_state_bytes{};
    std::uint64_t scratch_bytes{};
    std::uint64_t output_bytes{};
    std::uint64_t estimated_peak_bytes{};
    std::uint64_t applicable_budget_bytes{};
    std::uint32_t persistent_executor_count{};
};

struct ProductExecutionInfo {
    BackendSelection selection;
    Identity device_identity{};
    std::uint32_t provider{};
};

struct ProductOperationSnapshot {
    ProductOperationState state{ProductOperationState::Ready};
    std::uint64_t requested_samples{};
    std::uint64_t accepted_samples{};
    std::uint64_t completed_samples{};
    std::uint64_t pilot_samples{};
    std::uint64_t scene_realizations{};
    std::uint64_t executor_creations{};
    std::uint64_t actual_renderer_samples{};
    std::uint64_t eligible_integrator_modes{};
    std::uint64_t qualified_integrator_modes{};
    std::uint64_t executed_integrator_modes{};
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
        Identity source_identity,
        ProductObjective objective);

    virtual ~ProductJob() = default;
    ProductJob(const ProductJob&) = delete;
    ProductJob& operator=(const ProductJob&) = delete;

    virtual const ProductIdentitySet& identities() const noexcept = 0;
    virtual const ProductObjective& objective() const noexcept = 0;
    virtual const ProductMemoryPlan& memory_plan() const noexcept = 0;
    virtual const ProductExecutionInfo& execution() const noexcept = 0;
    virtual ProductOperationSnapshot operation() const noexcept = 0;
    virtual void replace_scene(native_scene::NativeSceneArchive archive,
                               Identity source_identity) = 0;
    virtual void begin() = 0;
    virtual void render_sample() = 0;
    virtual ProductFrame snapshot_frame() const = 0;
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
Identity backend_adapter_identity(const BackendAdapterInfo& adapter);

}
