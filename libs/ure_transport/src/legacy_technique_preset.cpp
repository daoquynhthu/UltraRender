#include "ure/transport/legacy_technique_preset.hpp"

#include "ure/runtime/multi_backend.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <string_view>
#include <utility>

namespace ure::transport {
namespace {

semantic::IdentityDigest identity(std::string_view tag) {
    return runtime::identity_digest(tag);
}

class ParameterEncoder {
public:
    explicit ParameterEncoder(std::string_view tag) {
        for (const char value : tag) {
            bytes_.push_back(static_cast<std::byte>(value));
        }
    }
    void u32(std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) {
            bytes_.push_back(static_cast<std::byte>(
                static_cast<std::uint8_t>(value >> shift)));
        }
    }
    void u64(std::uint64_t value) {
        for (int shift = 0; shift < 64; shift += 8) {
            bytes_.push_back(static_cast<std::byte>(
                static_cast<std::uint8_t>(value >> shift)));
        }
    }
    void i32(std::int32_t value) {
        u32(static_cast<std::uint32_t>(value));
    }
    void f32(float value) {
        u32(std::bit_cast<std::uint32_t>(value));
    }
    void boolean(bool value) { u32(value ? 1u : 0u); }
    semantic::IdentityDigest finish() const {
        return runtime::identity_digest(bytes_);
    }

private:
    std::vector<std::byte> bytes_;
};

semantic::IdentityDigest parameter_identity(
    TechniqueFamily family,
    const RenderConfig& config) {
    ParameterEncoder encoder("ure.technique-parameters.v1");
    encoder.u32(static_cast<std::uint32_t>(family));
    encoder.i32(config.max_trace_depth);
    encoder.u32(static_cast<std::uint32_t>(config.integrator.sampler));
    encoder.u32(static_cast<std::uint32_t>(
        config.integrator.quality_preset));
    switch (family) {
    case TechniqueFamily::WavefrontPathTracing:
        encoder.i32(spectral_packet_lanes(config));
        encoder.u64(spectral_domain_bins(config));
        break;
    case TechniqueFamily::PathGuiding:
        encoder.f32(config.path_guiding.light_mixture);
        encoder.f32(config.path_guiding.learning_rate);
        encoder.f32(config.path_guiding.min_weight);
        encoder.f32(config.path_guiding.decay);
        encoder.i32(config.path_guiding.decay_interval);
        encoder.i32(config.path_guiding.spatial_cell_count);
        encoder.i32(config.path_guiding.directional_bin_count);
        encoder.i32(config.path_guiding.memory_budget_mb);
        break;
    case TechniqueFamily::RestirDirect:
        encoder.boolean(config.restir_di.temporal_reuse);
        encoder.boolean(config.restir_di.spatial_reuse);
        encoder.boolean(config.restir_di.unbiased);
        encoder.i32(config.restir_di.max_history);
        encoder.i32(config.restir_di.spatial_candidate_count);
        encoder.i32(config.restir_di.spatial_radius);
        encoder.f32(config.restir_di.min_target);
        encoder.f32(config.restir_di.position_threshold);
        encoder.f32(config.restir_di.normal_threshold);
        break;
    case TechniqueFamily::RestirPathReuse:
        encoder.boolean(config.restir_pt.temporal_reuse);
        encoder.boolean(config.restir_pt.spatial_reuse);
        encoder.i32(config.restir_pt.max_reuse_depth);
        encoder.i32(config.restir_pt.candidate_count);
        encoder.i32(config.restir_pt.max_history);
        encoder.f32(config.restir_pt.position_threshold);
        encoder.f32(config.restir_pt.normal_threshold);
        break;
    case TechniqueFamily::SpecularManifold:
        encoder.i32(config.specular_manifold.max_specular_events);
        encoder.f32(config.specular_manifold.solver_tolerance);
        encoder.i32(config.specular_manifold.max_newton_iterations);
        break;
    case TechniqueFamily::BidirectionalPathTracing:
        encoder.i32(config.bidirectional.max_camera_vertices);
        encoder.i32(config.bidirectional.max_light_vertices);
        encoder.i32(config.bidirectional.connections_per_pixel);
        encoder.i32(config.bidirectional.memory_budget_mb);
        encoder.boolean(config.bidirectional.light_tracing);
        break;
    case TechniqueFamily::VertexConnectionMerging:
        encoder.f32(config.vcm.initial_radius);
        encoder.f32(config.vcm.alpha);
        encoder.i32(config.vcm.grid_capacity);
        encoder.boolean(config.vcm.merge_surfaces);
        encoder.boolean(config.vcm.merge_volumes);
        break;
    case TechniqueFamily::PrimarySampleSpaceMlt:
        encoder.i32(config.mlt.chain_count);
        encoder.i32(config.mlt.bootstrap_samples);
        encoder.i32(config.mlt.burn_in_mutations);
        encoder.i32(config.mlt.mutations_per_chain);
        encoder.f32(config.mlt.large_step_probability);
        encoder.f32(config.mlt.small_step_sigma);
        encoder.i32(config.mlt.memory_budget_mb);
        encoder.u32(config.mlt.seed);
        encoder.u64(config.mlt.chain_id_offset);
        break;
    }
    return encoder.finish();
}

std::uint64_t megabytes(int value) {
    return value > 0
        ? static_cast<std::uint64_t>(value) * 1024ull * 1024ull
        : 0;
}

TechniqueResourceDescriptor resource_descriptor(
    TechniqueFamily family,
    const RenderConfig& config) {
    TechniqueResourceDescriptor result;
    result.backend_capability_identity = identity(
        "ure.backend-capability.cuda-complete-scene.v1");
    switch (family) {
    case TechniqueFamily::WavefrontPathTracing:
        result.scaling = TechniqueResourceScaling::Pixel;
        break;
    case TechniqueFamily::PathGuiding:
        result.scaling = TechniqueResourceScaling::PixelAndScene;
        result.persistent_budget_bytes = megabytes(
            config.path_guiding.memory_budget_mb);
        break;
    case TechniqueFamily::RestirDirect:
    case TechniqueFamily::RestirPathReuse:
        result.scaling = TechniqueResourceScaling::Pixel;
        break;
    case TechniqueFamily::SpecularManifold:
        result.scaling = TechniqueResourceScaling::Solver;
        result.max_samples_per_pass = 1;
        break;
    case TechniqueFamily::BidirectionalPathTracing:
        result.scaling = TechniqueResourceScaling::PixelAndScene;
        result.persistent_budget_bytes = megabytes(
            config.bidirectional.memory_budget_mb);
        result.max_samples_per_pass = 1;
        break;
    case TechniqueFamily::VertexConnectionMerging:
        result.scaling = TechniqueResourceScaling::PixelAndScene;
        result.max_samples_per_pass = 1;
        break;
    case TechniqueFamily::PrimarySampleSpaceMlt:
        result.scaling = TechniqueResourceScaling::Chain;
        result.persistent_budget_bytes = megabytes(
            config.mlt.memory_budget_mb);
        break;
    }
    return result;
}

bool valid_mode(IntegratorMode mode) {
    return mode >= IntegratorMode::Wavefront &&
           mode <= IntegratorMode::VCM;
}

LegacyExecutionRoute route_from_config(const RenderConfig& config) {
    LegacyExecutionRoute route;
    route.resolved_mode = valid_mode(config.integrator.mode)
        ? config.integrator.mode
        : IntegratorMode::Wavefront;
    route.path_guiding = config.path_guiding.enabled;
    route.restir_direct = config.integrator.mode == IntegratorMode::RestirDI ||
                          config.restir_di.enabled;
    route.restir_path = config.integrator.mode == IntegratorMode::RestirPT ||
                        config.restir_pt.enabled;
    if (route.restir_direct) route.resolved_mode = IntegratorMode::RestirDI;
    if (route.restir_path) route.resolved_mode = IntegratorMode::RestirPT;
    route.specular_manifold =
        config.integrator.mode == IntegratorMode::SpecularManifold ||
        config.specular_manifold.enabled;
    route.bidirectional =
        config.integrator.mode == IntegratorMode::BDPT ||
        config.integrator.mode == IntegratorMode::VCM ||
        config.bidirectional.enabled || config.vcm.enabled;
    route.vertex_merging =
        config.integrator.mode == IntegratorMode::VCM || config.vcm.enabled;
    route.pssmlt = config.integrator.mode == IntegratorMode::MLT;
    route.biased_preview = route.restir_direct &&
                           !config.restir_di.unbiased;
    return route;
}

ObservableDescriptor radiance_observable() {
    ObservableDescriptor result;
    result.kind = ObservableKind::StokesRadiance;
    result.value_domain = ValueDomain::Stokes;
    result.coherence = CoherenceClass::Incoherent;
    result.component_count = 4;
    result.unit.dimension.length = -1;
    result.unit.dimension.mass = 1;
    result.unit.dimension.time = -3;
    return result;
}

EstimatorDescriptor estimator(
    TechniqueFamily family,
    std::string_view technique_tag,
    std::string_view sample_space_tag,
    std::uint64_t events,
    std::uint32_t max_depth,
    DensityKind density,
    NormalizationKind normalization,
    CorrelationModel correlation,
    BiasClass bias) {
    EstimatorDescriptor result;
    result.technique_identity = identity(technique_tag);
    result.observable = radiance_observable();
    result.measure.integral_identity = identity(
        "ure.integral.sensor-spectral-stokes-radiance.v1");
    result.measure.coordinate_identity = identity(sample_space_tag);
    result.measure.terms[0] = {MeasureDomain::Path, 1};
    result.measure.terms[1] = {MeasureDomain::Wavelength, 1};
    result.measure.term_count = 2;
    result.support.event_mask = events;
    result.support.max_depth = std::max(1u, max_depth);
    result.support.overlap_known = family !=
        TechniqueFamily::VertexConnectionMerging;
    result.support.singular_support =
        family == TechniqueFamily::SpecularManifold;
    result.density = density;
    result.normalization = normalization;
    result.correlation = correlation;
    result.bias = bias;
    return result;
}

TechniqueDescriptor contributor(
    TechniqueFamily family,
    EstimatorDescriptor value,
    std::string_view state_tag = {},
    std::string_view replay_tag = {},
    bool adaptive_state = false) {
    TechniqueDescriptor result;
    result.family = family;
    result.role = TechniqueRole::Estimator;
    result.technique_identity = value.technique_identity;
    result.sample_space_identity = value.measure.coordinate_identity;
    if (!state_tag.empty()) {
        result.persistent_state_identity = identity(state_tag);
        result.adaptive_state = adaptive_state;
    }
    if (!replay_tag.empty()) {
        result.replay_layout_identity = identity(replay_tag);
        result.replayable = true;
        value.replayable = true;
    }
    result.estimator = value;
    return result;
}

TechniqueDescriptor service(TechniqueFamily family,
                            TechniqueRole role,
                            std::string_view technique_tag,
                            std::string_view sample_space_tag,
                            std::string_view state_tag,
                            std::string_view replay_tag = {}) {
    TechniqueDescriptor result;
    result.family = family;
    result.role = role;
    result.technique_identity = identity(technique_tag);
    result.sample_space_identity = identity(sample_space_tag);
    result.contributes_estimate = false;
    result.owns_normalization = false;
    if (!state_tag.empty()) {
        result.persistent_state_identity = identity(state_tag);
        result.adaptive_state = true;
    }
    if (!replay_tag.empty()) {
        result.replay_layout_identity = identity(replay_tag);
        result.replayable = true;
    }
    return result;
}

std::uint32_t add_node(TechniqueGraph& graph,
                       TechniqueDescriptor descriptor) {
    const auto ordinal = static_cast<std::uint32_t>(graph.nodes.size());
    graph.nodes.push_back({ordinal, std::move(descriptor)});
    return ordinal;
}

void reject(LegacyTechniquePreset& preset,
            LegacyRejectionClass rejection_class,
            LegacyRejectionCode code,
            TechniqueFamily family) {
    const LegacyTechniqueRejection value{
        rejection_class, code, family};
    if (std::ranges::find(preset.rejections, value) ==
        preset.rejections.end()) {
        preset.rejections.push_back(value);
    }
}

std::uint64_t ordinary_events() {
    return path_event_mask(PathEvent::Camera) |
           path_event_mask(PathEvent::Emitter) |
           path_event_mask(PathEvent::Diffuse) |
           path_event_mask(PathEvent::Glossy) |
           path_event_mask(PathEvent::DeltaReflection) |
           path_event_mask(PathEvent::DeltaTransmission) |
           path_event_mask(PathEvent::VolumeScatter) |
           path_event_mask(PathEvent::WavelengthShift) |
           path_event_mask(PathEvent::Diffractive);
}

void add_parameter_rejections(const RenderConfig& config,
                              LegacyTechniquePreset& preset) {
    if (config.path_guiding.enabled) {
        if (!std::isfinite(config.path_guiding.light_mixture) ||
            !std::isfinite(config.path_guiding.learning_rate) ||
            !std::isfinite(config.path_guiding.min_weight) ||
            !std::isfinite(config.path_guiding.decay) ||
            config.path_guiding.light_mixture <= 0.0f ||
            config.path_guiding.light_mixture > 0.95f ||
            config.path_guiding.learning_rate <= 0.0f ||
            config.path_guiding.learning_rate > 1.0f ||
            config.path_guiding.min_weight < 0.0f ||
            config.path_guiding.decay <= 0.0f ||
            config.path_guiding.decay > 1.0f) {
            reject(preset, LegacyRejectionClass::Mathematical,
                   LegacyRejectionCode::InvalidProbabilityOrThreshold,
                   TechniqueFamily::PathGuiding);
        }
        if (config.path_guiding.decay_interval <= 0 ||
            config.path_guiding.spatial_cell_count <= 0 ||
            config.path_guiding.spatial_cell_count > 4096 ||
            config.path_guiding.directional_bin_count <= 0 ||
            config.path_guiding.directional_bin_count > 64) {
            reject(preset, LegacyRejectionClass::Resource,
                   LegacyRejectionCode::InvalidStorageDomain,
                   TechniqueFamily::PathGuiding);
        }
        if (config.path_guiding.memory_budget_mb < 0 ||
            config.path_guiding.memory_budget_mb > 1048576) {
            reject(preset, LegacyRejectionClass::Resource,
                   LegacyRejectionCode::InvalidResourceBudget,
                   TechniqueFamily::PathGuiding);
        }
    }
    if (config.restir_di.enabled) {
        if (!config.restir_di.temporal_reuse &&
            !config.restir_di.spatial_reuse) {
            reject(preset, LegacyRejectionClass::Mathematical,
                   LegacyRejectionCode::RestirRequiresReuse,
                   TechniqueFamily::RestirDirect);
        }
        if (!config.restir_di.unbiased &&
            config.restir_di.spatial_reuse) {
            reject(preset, LegacyRejectionClass::Unimplemented,
                   LegacyRejectionCode::BiasedSpatialReuseUnsupported,
                   TechniqueFamily::RestirDirect);
        }
        if (config.restir_di.max_history <= 0 ||
            config.restir_di.spatial_candidate_count <= 0 ||
            config.restir_di.spatial_radius <= 0 ||
            config.restir_di.spatial_radius > 32767) {
            reject(preset, LegacyRejectionClass::Resource,
                   LegacyRejectionCode::InvalidHistoryOrDepth,
                   TechniqueFamily::RestirDirect);
        }
        const auto diameter =
            static_cast<std::int64_t>(config.restir_di.spatial_radius) * 2 + 1;
        const auto spatial_domain = diameter > 0
            ? diameter * diameter - 1
            : 0;
        if (spatial_domain <= 0 ||
            config.restir_di.spatial_candidate_count > spatial_domain) {
            reject(preset, LegacyRejectionClass::Resource,
                   LegacyRejectionCode::InvalidStorageDomain,
                   TechniqueFamily::RestirDirect);
        }
        if (!std::isfinite(config.restir_di.min_target) ||
            config.restir_di.min_target <= 0.0f ||
            !std::isfinite(config.restir_di.position_threshold) ||
            config.restir_di.position_threshold <= 0.0f ||
            !std::isfinite(config.restir_di.normal_threshold) ||
            config.restir_di.normal_threshold < 0.0f ||
            config.restir_di.normal_threshold > 1.0f) {
            reject(preset, LegacyRejectionClass::Mathematical,
                   LegacyRejectionCode::InvalidProbabilityOrThreshold,
                   TechniqueFamily::RestirDirect);
        }
    }
    if (config.restir_pt.enabled) {
        if (!config.restir_pt.temporal_reuse &&
            !config.restir_pt.spatial_reuse) {
            reject(preset, LegacyRejectionClass::Mathematical,
                   LegacyRejectionCode::RestirRequiresReuse,
                   TechniqueFamily::RestirPathReuse);
        }
        if (config.restir_pt.max_reuse_depth <= 0 ||
            config.restir_pt.max_reuse_depth > 4 ||
            config.restir_pt.candidate_count <= 0 ||
            config.restir_pt.candidate_count > 64 ||
            config.restir_pt.max_history <= 0 ||
            config.restir_pt.max_history > 1024) {
            reject(preset, LegacyRejectionClass::Resource,
                   LegacyRejectionCode::InvalidHistoryOrDepth,
                   TechniqueFamily::RestirPathReuse);
        }
        if (!std::isfinite(config.restir_pt.position_threshold) ||
            config.restir_pt.position_threshold <= 0.0f ||
            !std::isfinite(config.restir_pt.normal_threshold) ||
            config.restir_pt.normal_threshold < 0.0f ||
            config.restir_pt.normal_threshold > 1.0f) {
            reject(preset, LegacyRejectionClass::Mathematical,
                   LegacyRejectionCode::InvalidProbabilityOrThreshold,
                   TechniqueFamily::RestirPathReuse);
        }
    }
    if (config.specular_manifold.enabled) {
        if (config.specular_manifold.max_specular_events <= 0 ||
            config.specular_manifold.max_specular_events > 4 ||
            config.specular_manifold.max_newton_iterations <= 0 ||
            config.specular_manifold.max_newton_iterations > 64) {
            reject(preset, LegacyRejectionClass::Resource,
                   LegacyRejectionCode::InvalidHistoryOrDepth,
                   TechniqueFamily::SpecularManifold);
        }
        if (!std::isfinite(config.specular_manifold.solver_tolerance) ||
            config.specular_manifold.solver_tolerance <= 0.0f) {
            reject(preset, LegacyRejectionClass::Mathematical,
                   LegacyRejectionCode::InvalidProbabilityOrThreshold,
                   TechniqueFamily::SpecularManifold);
        }
    }
}

void add_bidirectional_rejections(const RenderConfig& config,
                                  LegacyTechniquePreset& preset) {
    if (!preset.route.bidirectional &&
        !preset.route.specular_manifold) return;
    if (config.bidirectional.light_tracing) {
        reject(preset, LegacyRejectionClass::Unimplemented,
               LegacyRejectionCode::LightTracingRequiresSensorMeasure,
               TechniqueFamily::BidirectionalPathTracing);
    }
    if (config.bidirectional.max_camera_vertices < 2 ||
        config.bidirectional.max_camera_vertices > 32 ||
        config.bidirectional.max_light_vertices < 1 ||
        config.bidirectional.max_light_vertices > 32 ||
        config.bidirectional.connections_per_pixel <
            config.bidirectional.max_camera_vertices + 1 ||
        config.bidirectional.connections_per_pixel > 1024) {
        reject(preset, LegacyRejectionClass::Resource,
               LegacyRejectionCode::InvalidStorageDomain,
               TechniqueFamily::BidirectionalPathTracing);
    }
    if (config.bidirectional.memory_budget_mb < 0) {
        reject(preset, LegacyRejectionClass::Resource,
               LegacyRejectionCode::InvalidResourceBudget,
               TechniqueFamily::BidirectionalPathTracing);
    }
    if (preset.route.vertex_merging) {
        if (!std::isfinite(config.vcm.initial_radius) ||
            !std::isfinite(config.vcm.alpha) ||
            config.vcm.initial_radius <= 0.0f ||
            config.vcm.alpha <= 0.0f || config.vcm.alpha > 1.0f) {
            reject(preset, LegacyRejectionClass::Mathematical,
                   LegacyRejectionCode::InvalidProbabilityOrThreshold,
                   TechniqueFamily::VertexConnectionMerging);
        }
        if (config.vcm.grid_capacity < 0 ||
            (!config.vcm.merge_surfaces && !config.vcm.merge_volumes)) {
            reject(preset, LegacyRejectionClass::Resource,
                   LegacyRejectionCode::InvalidStorageDomain,
                   TechniqueFamily::VertexConnectionMerging);
        }
    }
}

void add_mlt_rejections(const RenderConfig& config,
                        LegacyTechniquePreset& preset) {
    if (!config.mlt.enabled && !preset.route.pssmlt) return;
    if (config.max_trace_depth <= 0 || config.max_trace_depth > 256 ||
        config.mlt.chain_count <= 0 ||
        config.mlt.bootstrap_samples < config.mlt.chain_count ||
        config.mlt.burn_in_mutations < 0 ||
        config.mlt.mutations_per_chain <= 0) {
        reject(preset, LegacyRejectionClass::Resource,
               LegacyRejectionCode::InvalidHistoryOrDepth,
               TechniqueFamily::PrimarySampleSpaceMlt);
    }
    if (!std::isfinite(config.mlt.large_step_probability) ||
        config.mlt.large_step_probability < 0.0f ||
        config.mlt.large_step_probability > 1.0f ||
        !std::isfinite(config.mlt.small_step_sigma) ||
        config.mlt.small_step_sigma <= 0.0f || config.mlt.seed == 0) {
        reject(preset, LegacyRejectionClass::Mathematical,
               LegacyRejectionCode::InvalidProbabilityOrThreshold,
               TechniqueFamily::PrimarySampleSpaceMlt);
    }
    if (config.mlt.memory_budget_mb < 0) {
        reject(preset, LegacyRejectionClass::Resource,
               LegacyRejectionCode::InvalidResourceBudget,
               TechniqueFamily::PrimarySampleSpaceMlt);
    }
    if (config.mlt.chain_count > 0 && config.max_trace_depth > 0) {
        constexpr std::uint64_t base_dimensions = 8;
        constexpr std::uint64_t path_stride = 16;
        const auto dimensions = base_dimensions +
            static_cast<std::uint64_t>(config.max_trace_depth) * path_stride;
        if (static_cast<std::uint64_t>(config.mlt.chain_count) * dimensions >
                static_cast<std::uint64_t>(
                    std::numeric_limits<int>::max()) ||
            (config.mlt.bootstrap_samples > 0 &&
             static_cast<std::uint64_t>(config.mlt.bootstrap_samples) *
                     dimensions >
                 static_cast<std::uint64_t>(
                     std::numeric_limits<int>::max()))) {
            reject(preset, LegacyRejectionClass::Resource,
                   LegacyRejectionCode::InvalidStorageDomain,
                   TechniqueFamily::PrimarySampleSpaceMlt);
        }
    }
    if (config.mlt.bootstrap_samples > 0 &&
        config.mlt.chain_id_offset >
            std::numeric_limits<std::uint64_t>::max() /
                static_cast<std::uint64_t>(config.mlt.bootstrap_samples)) {
        reject(preset, LegacyRejectionClass::Resource,
               LegacyRejectionCode::InvalidStorageDomain,
               TechniqueFamily::PrimarySampleSpaceMlt);
    }
}

void add_route_rejections(const RenderConfig& config,
                          LegacyTechniquePreset& preset) {
    if (!valid_mode(config.integrator.mode)) {
        reject(preset, LegacyRejectionClass::Unimplemented,
               LegacyRejectionCode::InvalidMode,
               TechniqueFamily::WavefrontPathTracing);
    }
    if (config.integrator.sampler ==
            IntegratorSampler::PrimarySampleSpace &&
        config.integrator.mode != IntegratorMode::MLT) {
        reject(preset, LegacyRejectionClass::Mathematical,
               LegacyRejectionCode::PrimarySampleSpaceRequiresMlt,
               TechniqueFamily::PrimarySampleSpaceMlt);
    }
    if (config.integrator.mode == IntegratorMode::PathGuided &&
        !config.path_guiding.enabled) {
        reject(preset, LegacyRejectionClass::Unimplemented,
               LegacyRejectionCode::MissingRequiredEnable,
               TechniqueFamily::PathGuiding);
    }
    if (config.integrator.mode == IntegratorMode::RestirDI &&
        !config.restir_di.enabled) {
        reject(preset, LegacyRejectionClass::Unimplemented,
               LegacyRejectionCode::MissingRequiredEnable,
               TechniqueFamily::RestirDirect);
    }
    if (config.integrator.mode == IntegratorMode::RestirPT &&
        !config.restir_pt.enabled) {
        reject(preset, LegacyRejectionClass::Unimplemented,
               LegacyRejectionCode::MissingRequiredEnable,
               TechniqueFamily::RestirPathReuse);
    }
    if (config.integrator.mode == IntegratorMode::SpecularManifold &&
        !config.specular_manifold.enabled) {
        reject(preset, LegacyRejectionClass::Unimplemented,
               LegacyRejectionCode::MissingRequiredEnable,
               TechniqueFamily::SpecularManifold);
    }
    if (preset.route.restir_direct && preset.route.restir_path) {
        reject(preset, LegacyRejectionClass::Mathematical,
               LegacyRejectionCode::RestirFamiliesMutuallyExclusive,
               TechniqueFamily::RestirPathReuse);
    }
    if (config.integrator.mode == IntegratorMode::RestirDI &&
        preset.route.biased_preview &&
        !config.integrator.allow_biased_reuse) {
        reject(preset, LegacyRejectionClass::Mathematical,
               LegacyRejectionCode::BiasedReuseRequiresOptIn,
               TechniqueFamily::RestirDirect);
    }
    if (preset.route.pssmlt) {
        if (!config.mlt.enabled) {
            reject(preset, LegacyRejectionClass::Unimplemented,
                   LegacyRejectionCode::MissingRequiredEnable,
                   TechniqueFamily::PrimarySampleSpaceMlt);
        }
        if (config.integrator.sampler !=
            IntegratorSampler::PrimarySampleSpace) {
            reject(preset, LegacyRejectionClass::Mathematical,
                   LegacyRejectionCode::MltRequiresPrimarySampleSpace,
                   TechniqueFamily::PrimarySampleSpaceMlt);
        }
        if (config.bidirectional.enabled) {
            reject(preset, LegacyRejectionClass::Unimplemented,
                   LegacyRejectionCode::MltBidirectionalNeedsSharedSpectralSample,
                   TechniqueFamily::PrimarySampleSpaceMlt);
        }
        if (config.path_guiding.enabled || config.restir_di.enabled ||
            config.restir_pt.enabled || config.vcm.enabled ||
            config.specular_manifold.enabled) {
            reject(preset, LegacyRejectionClass::Mathematical,
                   LegacyRejectionCode::MltOwnsAdaptiveScheduler,
                   TechniqueFamily::PrimarySampleSpaceMlt);
        }
    }
    const bool diffraction = config.wave_optics.mode ==
            WaveOpticsMode::CameraDiffraction ||
        config.wave_optics.camera_diffraction_enabled;
    if (diffraction &&
        (config.integrator.mode != IntegratorMode::Wavefront ||
         config.path_guiding.enabled || config.restir_di.enabled ||
         config.restir_pt.enabled || config.specular_manifold.enabled ||
         config.bidirectional.enabled || config.vcm.enabled ||
         config.mlt.enabled)) {
        reject(preset, LegacyRejectionClass::Unimplemented,
               LegacyRejectionCode::UnsupportedWaveCombination,
               TechniqueFamily::WavefrontPathTracing);
    }
}

TechniqueGraph build_graph(const RenderConfig& config,
                           const LegacyExecutionRoute& route) {
    TechniqueGraph graph;
    const auto depth = static_cast<std::uint32_t>(
        std::max(1, config.max_trace_depth));
    if (route.pssmlt) {
        const auto replay = add_node(graph, service(
            TechniqueFamily::WavefrontPathTracing,
            TechniqueRole::ReplayKernel,
            "ure.technique.wavefront-replay.v1",
            "ure.sample-space.camera-path.v1",
            {},
            "ure.replay.primary-sample-layout.v1"));
        auto mlt = estimator(
            TechniqueFamily::PrimarySampleSpaceMlt,
            "ure.technique.pssmlt.v1",
            "ure.sample-space.primary-markov-chain.v1",
            ordinary_events(), depth,
            DensityKind::MarkovTransition,
            NormalizationKind::ChainBootstrap,
            CorrelationModel::MarkovChain,
            BiasClass::AsymptoticallyUnbiased);
        const auto estimator_node = add_node(graph, contributor(
            TechniqueFamily::PrimarySampleSpaceMlt,
            mlt,
            "ure.state.pssmlt-chains.v1",
            "ure.replay.primary-sample-layout.v1",
            true));
        graph.edges.push_back(
            {replay, estimator_node, TechniqueEdgeKind::ReplayFor});
        for (auto& node : graph.nodes) {
            node.descriptor.parameter_identity = parameter_identity(
                node.descriptor.family, config);
            node.descriptor.resources = resource_descriptor(
                node.descriptor.family, config);
            node.descriptor.requires_shared_spectral_primary_sample =
                node.descriptor.family ==
                TechniqueFamily::PrimarySampleSpaceMlt;
        }
        finalize_technique_graph(graph);
        return graph;
    }

    auto wavefront = estimator(
        TechniqueFamily::WavefrontPathTracing,
        "ure.technique.wavefront-pt.v1",
        "ure.sample-space.camera-path.v1",
        ordinary_events(), depth,
        DensityKind::ExplicitPdf,
        NormalizationKind::IndependentSampleMean,
        CorrelationModel::Independent,
        BiasClass::Unbiased);
    const auto wavefront_node = add_node(graph, contributor(
        TechniqueFamily::WavefrontPathTracing,
        wavefront,
        "ure.state.wavefront-film-queues.v1",
        "ure.replay.wavefront-dimensions.v1"));

    if (route.path_guiding) {
        const auto guide = add_node(graph, service(
            TechniqueFamily::PathGuiding,
            TechniqueRole::ProposalService,
            "ure.technique.path-guiding.v1",
            "ure.sample-space.guided-direction.v1",
            "ure.state.path-guide-cache.v1"));
        graph.edges.push_back(
            {guide, wavefront_node, TechniqueEdgeKind::ProposalFor});
    }
    if (route.restir_direct) {
        auto restir = estimator(
            TechniqueFamily::RestirDirect,
            "ure.technique.restir-di.v1",
            "ure.sample-space.restir-direct-candidates.v1",
            path_event_mask(PathEvent::Camera) |
                path_event_mask(PathEvent::Emitter) |
                path_event_mask(PathEvent::Diffuse) |
                path_event_mask(PathEvent::Glossy) |
                path_event_mask(PathEvent::VolumeScatter),
            2,
            DensityKind::NormalizedReservoirWeight,
            NormalizationKind::ReservoirNormalization,
            CorrelationModel::ReservoirReuse,
            route.biased_preview ? BiasClass::BiasedPreview
                                 : BiasClass::Unbiased);
        const auto node = add_node(graph, contributor(
            TechniqueFamily::RestirDirect,
            restir,
            "ure.state.restir-di-reservoirs.v1",
            {},
            true));
        graph.edges.push_back(
            {wavefront_node, node,
             TechniqueEdgeKind::CoupledEstimatorFamily});
    }
    if (route.restir_path) {
        auto restir = estimator(
            TechniqueFamily::RestirPathReuse,
            "ure.technique.restir-pt.v1",
            "ure.sample-space.restir-suffix-replay.v1",
            ordinary_events(),
            static_cast<std::uint32_t>(
                std::max(1, config.restir_pt.max_reuse_depth)),
            DensityKind::NormalizedReservoirWeight,
            NormalizationKind::ReservoirNormalization,
            CorrelationModel::ReservoirReuse,
            BiasClass::Unbiased);
        const auto node = add_node(graph, contributor(
            TechniqueFamily::RestirPathReuse,
            restir,
            "ure.state.restir-pt-reservoirs.v1",
            "ure.replay.restir-suffix-layout.v1",
            true));
        graph.edges.push_back(
            {wavefront_node, node,
             TechniqueEdgeKind::CoupledEstimatorFamily});
    }
    if (route.bidirectional) {
        auto bdpt = estimator(
            TechniqueFamily::BidirectionalPathTracing,
            "ure.technique.bdpt.v1",
            "ure.sample-space.bidirectional-subpaths.v1",
            ordinary_events(), depth,
            DensityKind::ExplicitPdf,
            NormalizationKind::MultipleImportanceSampling,
            CorrelationModel::Independent,
            BiasClass::Unbiased);
        const auto bdpt_node = add_node(graph, contributor(
            TechniqueFamily::BidirectionalPathTracing,
            bdpt,
            "ure.state.bidirectional-subpaths.v1"));
        graph.edges.push_back(
            {wavefront_node, bdpt_node,
             TechniqueEdgeKind::CoupledEstimatorFamily});
        if (route.vertex_merging) {
            auto vcm = estimator(
                TechniqueFamily::VertexConnectionMerging,
                "ure.technique.vcm.v1",
                "ure.sample-space.vcm-merge-kernel.v1",
                ordinary_events(), depth,
                DensityKind::ExplicitPdf,
                NormalizationKind::ProgressiveKernel,
                CorrelationModel::AdaptiveHistory,
                BiasClass::Consistent);
            const auto vcm_node = add_node(graph, contributor(
                TechniqueFamily::VertexConnectionMerging,
                vcm,
                "ure.state.vcm-progressive-grid.v1",
                {},
                true));
            graph.edges.push_back(
                {bdpt_node, vcm_node,
                 TechniqueEdgeKind::CoupledEstimatorFamily});
        }
    }
    if (route.specular_manifold) {
        auto sms = estimator(
            TechniqueFamily::SpecularManifold,
            "ure.technique.specular-manifold.v1",
            "ure.sample-space.manifold-root-chain.v1",
            path_event_mask(PathEvent::Camera) |
                path_event_mask(PathEvent::Emitter) |
                path_event_mask(PathEvent::DeltaReflection) |
                path_event_mask(PathEvent::DeltaTransmission),
            static_cast<std::uint32_t>(std::max(
                1, config.specular_manifold.max_specular_events)),
            DensityKind::UnbiasedContributionWeight,
            NormalizationKind::IndependentSampleMean,
            CorrelationModel::Independent,
            BiasClass::Unbiased);
        const auto node = add_node(graph, contributor(
            TechniqueFamily::SpecularManifold,
            sms,
            "ure.state.manifold-solutions.v1"));
        graph.edges.push_back(
            {wavefront_node, node,
             TechniqueEdgeKind::CoupledEstimatorFamily});
    }
    for (auto& node : graph.nodes) {
        node.descriptor.parameter_identity = parameter_identity(
            node.descriptor.family, config);
        node.descriptor.resources = resource_descriptor(
            node.descriptor.family, config);
        node.descriptor.requires_shared_spectral_primary_sample =
            node.descriptor.family ==
            TechniqueFamily::PrimarySampleSpaceMlt;
    }
    finalize_technique_graph(graph);
    return graph;
}

}

LegacyTechniquePreset compile_legacy_technique_preset(
    const RenderConfig& config) {
    LegacyTechniquePreset result;
    result.requested_mode = valid_mode(config.integrator.mode)
        ? config.integrator.mode
        : IntegratorMode::Wavefront;
    result.route = route_from_config(config);
    add_route_rejections(config, result);
    add_parameter_rejections(config, result);
    add_bidirectional_rejections(config, result);
    add_mlt_rejections(config, result);
    result.graph = build_graph(config, result.route);
    return result;
}

bool legacy_preset_equivalent(
    const RenderConfig& config,
    const LegacyTechniquePreset& preset) {
    if (preset.version != kTechniqueGraphVersion ||
        preset.requested_mode !=
            (valid_mode(config.integrator.mode)
                 ? config.integrator.mode
                 : IntegratorMode::Wavefront) ||
        preset.route != route_from_config(config) ||
        !validate_technique_graph(preset.graph).ok()) {
        return false;
    }
    const auto present = [&preset](TechniqueFamily family) {
        return find_technique(preset.graph, family) != nullptr;
    };
    return present(TechniqueFamily::WavefrontPathTracing) &&
           present(TechniqueFamily::PathGuiding) ==
               preset.route.path_guiding &&
           present(TechniqueFamily::RestirDirect) ==
               preset.route.restir_direct &&
           present(TechniqueFamily::RestirPathReuse) ==
               preset.route.restir_path &&
           present(TechniqueFamily::SpecularManifold) ==
               preset.route.specular_manifold &&
           present(TechniqueFamily::BidirectionalPathTracing) ==
               preset.route.bidirectional &&
           present(TechniqueFamily::VertexConnectionMerging) ==
               preset.route.vertex_merging &&
           present(TechniqueFamily::PrimarySampleSpaceMlt) ==
               preset.route.pssmlt;
}

const TechniqueDescriptor* find_technique(
    const TechniqueGraph& graph,
    TechniqueFamily family) {
    const auto found = std::ranges::find_if(
        graph.nodes,
        [family](const TechniqueNode& node) {
            return node.descriptor.family == family;
        });
    return found == graph.nodes.end() ? nullptr : &found->descriptor;
}

}
