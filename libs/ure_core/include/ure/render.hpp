#pragma once

#include "ure/backend_types.hpp"
#include "ure/render_config.hpp"
#include "ure/runtime/dynamic_geometry.hpp"
#include "ure/ure_api.hpp"
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace ure {

enum class IntegratorEstimatorPolicy : std::uint32_t {
    Standard = 0,
    RestirDIBiasedPreview = 1,
    RestirDIUnbiasedProduction = 2,
    RestirPTPathReuse = 3,
    AutomaticPortfolio = 4
};

struct IntegratorEstimatorMetadata {
    IntegratorMode mode = IntegratorMode::Wavefront;
    IntegratorEstimatorPolicy policy = IntegratorEstimatorPolicy::Standard;
    bool biased = false;
    bool temporal_reuse = false;
    bool spatial_reuse = false;
    std::uint32_t sample_space_version = 0;
    std::uint32_t scene_epoch = 0;
};

constexpr std::uint32_t kRestirDISampleSpaceVersion = 1;
constexpr std::uint32_t kRestirPTSampleSpaceVersion = 1;
constexpr std::uint32_t kAutomaticPortfolioSampleSpaceVersion = 1;

struct AutomaticTechniqueReport {
    IntegratorMode mode = IntegratorMode::Wavefront;
    bool qualified = false;
    bool selected = false;
    std::string reason;
    int pilot_spp = 0;
    int allocated_spp = 0;
    double pilot_mean = 0.0;
    double pilot_variance = 0.0;
    double maximum_absolute_pilot_contribution = 0.0;
    double nanoseconds_per_sample = 0.0;
    double aggregation_weight = 0.0;
};

struct AutomaticIntegratorReport {
    bool automatic = false;
    bool complete = false;
    bool quality_target_met = false;
    bool time_budget_met = false;
    bool memory_budget_met = false;
    int requested_spp = 0;
    int total_allocated_spp = 0;
    double estimated_relative_standard_error = 0.0;
    std::uint64_t elapsed_nanoseconds = 0;
    std::uint64_t peak_memory_budget_bytes = 0;
    std::uint64_t measured_peak_resident_device_bytes = 0;
    std::uint64_t estimated_peak_device_bytes = 0;
    std::uint64_t technique_coverage_mask = 0;
    bool independent_endpoint_ensemble = false;
    bool pilot_precision_weighted = false;
    bool conservative_uncertainty_bound = false;
    bool auxiliary_outputs_wavefront_only = false;
    std::vector<AutomaticTechniqueReport> techniques;
};

IntegratorEstimatorMetadata make_integrator_estimator_metadata(
    const RenderConfig& config,
    std::uint32_t scene_epoch);

bool compatible_integrator_estimator_metadata(
    const IntegratorEstimatorMetadata& left,
    const IntegratorEstimatorMetadata& right);

bool validate_integrator_estimator_metadata(
    const IntegratorEstimatorMetadata& metadata);

enum class AovType {
    Beauty = 0,
    Normal = 1,
    Albedo = 2,
    Depth = 3,
    Uv = 4,
    MotionVector = 5
};

int aov_channel_count(AovType type);

// =========================================================================
// IRenderEngine — Stable public API for GPU/CPU rendering
// =========================================================================

class IRenderEngine {
public:
    virtual ~IRenderEngine() = default;

    // First-time SceneIR load uploads all static GPU data. Subsequent calls on
    // the same engine act as a transform update only.
    virtual void load_scene_ir(const scene_ir::SceneIR& scene_ir) = 0;

    // Explicit full SceneIR replacement. Use this for topology/resource changes;
    // load_scene_ir remains the transform-hot-update compatible entry point.
    virtual void reload_scene_ir(const scene_ir::SceneIR& scene_ir) = 0;

    // Hot-update instance transforms without reloading the entire scene.
    // Must be called after load_scene_ir().
    virtual void update_transforms(const scene_ir::SceneIR& scene_ir) = 0;

    // Hot-update the scene-owned material segment without reallocating geometry.
    // The engine owns default/internal material offsets; callers pass only scene materials.
    virtual void update_materials(const scene_ir::SceneIR& scene_ir) = 0;

    virtual void update_geometry(
        const scene_ir::SceneIR& scene_ir) = 0;

    // Blocking render, implemented using render_pass loop.
    virtual void render(const RenderSettings& settings) = 0;

    // Render one pass (or a batch of samples) and accumulate to the frame buffer.
    // Returns the current accumulated sample count (SPP).
    virtual int render_pass() = 0;

    // Reset accumulation buffer (clear to black, reset SPP to 0).
    // Should be called when scene/camera changes.
    virtual void reset_accumulation() = 0;

    // Update camera parameters without reloading the entire scene.
    virtual void update_camera(const Camera& camera) = 0;

    // Get current sample count
    virtual int get_current_spp() const = 0;

    virtual void get_framebuffer_size(int& out_width, int& out_height) const = 0;

    // Get raw frame buffer (Linear RGB float)
    virtual const std::vector<float>& get_framebuffer() const = 0;

    // Get an auxiliary output buffer. Beauty/Normal/Albedo are RGB triples;
    // Depth is one camera-visible hit distance per pixel. UV is a 2-channel
    // first-hit texture coordinate buffer. MotionVector is a 2-channel
    // current-minus-previous screen-space delta for camera motion.
    virtual const std::vector<float>& get_aov(AovType type) const = 0;

    virtual IntegratorEstimatorMetadata get_estimator_metadata() const = 0;
    virtual const BackendSelection& get_backend_selection() const = 0;
    virtual AccelerationStats get_acceleration_stats() const = 0;
    virtual runtime::DynamicGeometryStats
        get_dynamic_geometry_stats() const = 0;

    virtual AutomaticIntegratorReport
        get_automatic_integrator_report() const { return {}; }

    // Backward compatibility alias
    const std::vector<float>& get_frame_buffer() const { return get_framebuffer(); }

};

// Factory
class RenderEngineFactory {
public:
    static std::unique_ptr<IRenderEngine> create_gpu_renderer();
    static std::unique_ptr<IRenderEngine> create_gpu_renderer(const RenderConfig& config);
    static std::unique_ptr<IRenderEngine> create_gpu_engine(); // backward compat alias
};

} // namespace ure
