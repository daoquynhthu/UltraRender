#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

#include <nlohmann/json.hpp>
#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/verifier.h>

#include <ure/native_scene_hash.hpp>
#include <ure/native_solver_contract.hpp>

#include "ure_solver_contract_v1_generated.h"

namespace ure::native_scene {
namespace {
namespace fb = ure::solver::schema;
void error(ValidationReport& report, std::string code, std::string path, std::string message) { report.diagnostics.push_back({std::move(code), DiagnosticSeverity::Error, std::move(path), std::move(message), {}}); }
bool finite(float value) { return std::isfinite(value); }
bool finite(double value) { return std::isfinite(value); }

fb::SolverContractT to_schema(const NativeSolverContract& value) {
    fb::SolverContractT result; result.id = value.id; result.schema_version = std::make_unique<fb::Version>(value.schema_version.major, value.schema_version.minor);
    result.integrator = static_cast<fb::IntegratorMode>(value.integrator); result.sampler = static_cast<fb::IntegratorSampler>(value.sampler); result.quality = static_cast<fb::Quality>(value.quality); result.allow_biased_reuse = value.allow_biased_reuse; result.spectral_domain_bins = value.spectral_domain_bins; result.spectral_packet_lanes = value.spectral_packet_lanes; result.spectral_resident_mb = value.spectral_resident_mb; result.spectral_sampling = static_cast<fb::SpectralSampling>(value.spectral_sampling); result.max_trace_depth = value.max_trace_depth;
    result.path_guiding = std::make_unique<fb::PathGuidingT>(); auto& pg = *result.path_guiding; pg.enabled = value.path_guiding.enabled; pg.light_mixture = value.path_guiding.light_mixture; pg.learning_rate = value.path_guiding.learning_rate; pg.min_weight = value.path_guiding.min_weight; pg.decay = value.path_guiding.decay; pg.decay_interval = value.path_guiding.decay_interval; pg.spatial_cells = value.path_guiding.spatial_cell_count; pg.directional_bins = value.path_guiding.directional_bin_count; pg.memory_budget_mb = value.path_guiding.memory_budget_mb;
    result.restir_di = std::make_unique<fb::RestirDIT>(); auto& rd = *result.restir_di; rd.enabled = value.restir_di.enabled; rd.temporal_reuse = value.restir_di.temporal_reuse; rd.spatial_reuse = value.restir_di.spatial_reuse; rd.unbiased = value.restir_di.unbiased; rd.max_history = value.restir_di.max_history; rd.min_target = value.restir_di.min_target; rd.spatial_candidate_count = value.restir_di.spatial_candidate_count; rd.spatial_radius = value.restir_di.spatial_radius; rd.position_threshold = value.restir_di.position_threshold; rd.normal_threshold = value.restir_di.normal_threshold;
    result.restir_pt = std::make_unique<fb::RestirPTT>(); auto& rp = *result.restir_pt; rp.enabled = value.restir_pt.enabled; rp.temporal_reuse = value.restir_pt.temporal_reuse; rp.spatial_reuse = value.restir_pt.spatial_reuse; rp.max_reuse_depth = value.restir_pt.max_reuse_depth; rp.candidate_count = value.restir_pt.candidate_count; rp.max_history = value.restir_pt.max_history; rp.position_threshold = value.restir_pt.position_threshold; rp.normal_threshold = value.restir_pt.normal_threshold;
    result.specular_manifold = std::make_unique<fb::SpecularManifoldT>(); auto& sm = *result.specular_manifold; sm.enabled = value.specular_manifold.enabled; sm.max_events = value.specular_manifold.max_specular_events; sm.tolerance = value.specular_manifold.solver_tolerance; sm.max_iterations = value.specular_manifold.max_newton_iterations;
    result.mlt = std::make_unique<fb::MLTT>(); auto& ml = *result.mlt; ml.enabled = value.mlt.enabled; ml.chains = value.mlt.chain_count; ml.mutations = value.mlt.mutations_per_chain; ml.large_step_probability = value.mlt.large_step_probability; ml.small_step_sigma = value.mlt.small_step_sigma; ml.seed = value.mlt.seed; ml.bootstrap_samples = value.mlt.bootstrap_samples; ml.burn_in_mutations = value.mlt.burn_in_mutations; ml.memory_budget_mb = value.mlt.memory_budget_mb; ml.chain_id_offset = value.mlt.chain_id_offset;
    result.wave_optics = std::make_unique<fb::WaveOpticsT>(); auto& wo = *result.wave_optics; wo.mode = static_cast<fb::WaveMode>(value.wave_optics.mode); wo.camera_diffraction = value.wave_optics.camera_diffraction_enabled; wo.coherent_field = value.wave_optics.coherent_field_enabled; wo.partial_coherence = value.wave_optics.partial_coherence_enabled; wo.diffractive_materials = value.wave_optics.diffractive_materials_enabled; wo.fluorescence = value.wave_optics.fluorescence_enabled; wo.specular_manifold = value.wave_optics.specular_manifold_enabled; wo.local_fullwave = value.wave_optics.local_fullwave_enabled; wo.allow_preview_degradation = value.wave_optics.experimental_allow_preview_degradation; wo.camera_aperture_diameter_m = value.wave_optics.camera_aperture_diameter_m; wo.camera_focal_length_m = value.wave_optics.camera_focal_length_m; wo.sensor_pixel_pitch_m = value.wave_optics.sensor_pixel_pitch_m; wo.camera_defocus_waves_at_edge = value.wave_optics.camera_defocus_waves_at_edge; wo.camera_aperture_rotation_rad = value.wave_optics.camera_aperture_rotation_rad; wo.camera_aperture_blade_count = value.wave_optics.camera_aperture_blade_count; wo.camera_psf_radius_pixels = value.wave_optics.camera_psf_radius_pixels; wo.camera_wavelength_bin_count = value.wave_optics.camera_wavelength_bin_count; wo.camera_pupil_sample_count = value.wave_optics.camera_pupil_sample_count;
    result.backend = static_cast<fb::Backend>(value.backend); result.acceleration = static_cast<fb::Acceleration>(value.acceleration); result.coherent_merge = static_cast<fb::CoherentMerge>(value.coherent_merge); result.backend_extension = value.backend_extension; result.acceleration_extension = value.acceleration_extension;
    auto requirements = value.validation; std::ranges::sort(requirements, {}, &ValidationRequirement::metric); for (const auto& requirement : requirements) { auto item = std::make_unique<fb::ValidationRequirementT>(); item->metric = requirement.metric; item->tolerance = requirement.tolerance; result.validation.push_back(std::move(item)); }
    result.hints = std::make_unique<fb::ExecutionHintsT>(); result.hints->workgroup_size = value.hints.workgroup_size; result.hints->rays_per_block = value.hints.rays_per_block; result.hints->samples_per_pass = value.hints.samples_per_pass; result.hints->device_count = value.hints.device_count; return result;
}

NativeSolverContract from_schema(const fb::SolverContractT& source) {
    if (!source.schema_version || !source.path_guiding || !source.restir_di || !source.specular_manifold || !source.mlt || !source.wave_optics || !source.hints) throw std::invalid_argument("Incomplete URSC payload");
    NativeSolverContract value; value.id = source.id; value.schema_version = {source.schema_version->major(), source.schema_version->minor()}; value.integrator = static_cast<NativeIntegratorMode>(source.integrator); value.sampler = static_cast<IntegratorSampler>(source.sampler); value.quality = static_cast<IntegratorQualityPreset>(source.quality); value.allow_biased_reuse = source.allow_biased_reuse; value.spectral_domain_bins = source.spectral_domain_bins; value.spectral_packet_lanes = source.spectral_packet_lanes; value.spectral_resident_mb = source.spectral_resident_mb; value.spectral_sampling = static_cast<SpectralSamplingMode>(source.spectral_sampling); value.max_trace_depth = source.max_trace_depth;
    value.path_guiding = {source.path_guiding->enabled, source.path_guiding->light_mixture, source.path_guiding->learning_rate, source.path_guiding->min_weight, source.path_guiding->decay, source.path_guiding->decay_interval, source.path_guiding->spatial_cells, source.path_guiding->directional_bins, source.path_guiding->memory_budget_mb};
    value.restir_di.enabled = source.restir_di->enabled;
    value.restir_di.temporal_reuse = source.restir_di->temporal_reuse;
    value.restir_di.spatial_reuse = source.restir_di->spatial_reuse;
    value.restir_di.unbiased = source.restir_di->unbiased;
    value.restir_di.max_history = source.restir_di->max_history;
    value.restir_di.min_target = source.restir_di->min_target;
    value.restir_di.spatial_candidate_count = source.restir_di->spatial_candidate_count;
    value.restir_di.spatial_radius = source.restir_di->spatial_radius;
    value.restir_di.position_threshold = source.restir_di->position_threshold;
    value.restir_di.normal_threshold = source.restir_di->normal_threshold;
    if (source.restir_pt) {
        value.restir_pt = {source.restir_pt->enabled, source.restir_pt->temporal_reuse,
            source.restir_pt->spatial_reuse, source.restir_pt->max_reuse_depth,
            source.restir_pt->candidate_count, source.restir_pt->max_history,
            source.restir_pt->position_threshold, source.restir_pt->normal_threshold};
    }
    value.specular_manifold = {source.specular_manifold->enabled, source.specular_manifold->max_events, source.specular_manifold->tolerance, source.specular_manifold->max_iterations}; value.mlt.enabled = source.mlt->enabled; value.mlt.chain_count = source.mlt->chains; value.mlt.mutations_per_chain = source.mlt->mutations; value.mlt.large_step_probability = source.mlt->large_step_probability; value.mlt.small_step_sigma = source.mlt->small_step_sigma; value.mlt.seed = source.mlt->seed; value.mlt.bootstrap_samples = source.mlt->bootstrap_samples; value.mlt.burn_in_mutations = source.mlt->burn_in_mutations; value.mlt.memory_budget_mb = source.mlt->memory_budget_mb; value.mlt.chain_id_offset = source.mlt->chain_id_offset;
    value.wave_optics = {static_cast<WaveOpticsMode>(source.wave_optics->mode), source.wave_optics->camera_diffraction, source.wave_optics->coherent_field, source.wave_optics->partial_coherence, source.wave_optics->diffractive_materials, source.wave_optics->fluorescence, source.wave_optics->specular_manifold, source.wave_optics->local_fullwave, source.wave_optics->allow_preview_degradation, source.wave_optics->camera_aperture_diameter_m, source.wave_optics->camera_focal_length_m, source.wave_optics->sensor_pixel_pitch_m, source.wave_optics->camera_defocus_waves_at_edge, source.wave_optics->camera_aperture_rotation_rad, source.wave_optics->camera_aperture_blade_count, source.wave_optics->camera_psf_radius_pixels, source.wave_optics->camera_wavelength_bin_count, source.wave_optics->camera_pupil_sample_count};
    value.backend = static_cast<ExecutionBackend>(source.backend); value.acceleration = static_cast<AccelerationProvider>(source.acceleration); value.coherent_merge = static_cast<CoherentMergeMode>(source.coherent_merge); value.backend_extension = source.backend_extension; value.acceleration_extension = source.acceleration_extension; for (const auto& item : source.validation) if (item) value.validation.push_back({item->metric, item->tolerance}); value.hints = {source.hints->workgroup_size, source.hints->rays_per_block, source.hints->samples_per_pass, source.hints->device_count}; return value;
}

template <typename T> LoadResult<T> failure(std::string code, std::string message) { LoadResult<T> result; result.diagnostics.push_back({std::move(code), DiagnosticSeverity::Error, "/solver", std::move(message), {}}); return result; }

nlohmann::ordered_json contract_json(const NativeSolverContract& value) {
    auto validation = nlohmann::ordered_json::array(); auto requirements = value.validation; std::ranges::sort(requirements, {}, &ValidationRequirement::metric); for (const auto& item : requirements) validation.push_back({{"metric", item.metric}, {"tolerance", item.tolerance}});
    return {{"acceleration", static_cast<unsigned>(value.acceleration)}, {"acceleration_extension", value.acceleration_extension}, {"allow_biased_reuse", value.allow_biased_reuse}, {"backend", static_cast<unsigned>(value.backend)}, {"backend_extension", value.backend_extension}, {"coherent_merge", static_cast<unsigned>(value.coherent_merge)},
        {"hints", {{"device_count", value.hints.device_count}, {"rays_per_block", value.hints.rays_per_block}, {"samples_per_pass", value.hints.samples_per_pass}, {"workgroup_size", value.hints.workgroup_size}}}, {"id", value.id}, {"integrator", static_cast<unsigned>(value.integrator)}, {"max_trace_depth", value.max_trace_depth},
        {"mlt", {{"bootstrap_samples", value.mlt.bootstrap_samples}, {"burn_in_mutations", value.mlt.burn_in_mutations}, {"chain_id_offset", value.mlt.chain_id_offset}, {"chains", value.mlt.chain_count}, {"enabled", value.mlt.enabled}, {"large_step_probability", value.mlt.large_step_probability}, {"memory_budget_mb", value.mlt.memory_budget_mb}, {"mutations", value.mlt.mutations_per_chain}, {"seed", value.mlt.seed}, {"small_step_sigma", value.mlt.small_step_sigma}}},
        {"path_guiding", {{"decay", value.path_guiding.decay}, {"decay_interval", value.path_guiding.decay_interval}, {"directional_bins", value.path_guiding.directional_bin_count}, {"enabled", value.path_guiding.enabled}, {"learning_rate", value.path_guiding.learning_rate}, {"light_mixture", value.path_guiding.light_mixture}, {"memory_budget_mb", value.path_guiding.memory_budget_mb}, {"min_weight", value.path_guiding.min_weight}, {"spatial_cells", value.path_guiding.spatial_cell_count}}},
        {"quality", static_cast<unsigned>(value.quality)}, {"restir_di", {{"enabled", value.restir_di.enabled}, {"max_history", value.restir_di.max_history}, {"min_target", value.restir_di.min_target}, {"normal_threshold", value.restir_di.normal_threshold}, {"position_threshold", value.restir_di.position_threshold}, {"spatial_candidate_count", value.restir_di.spatial_candidate_count}, {"spatial_radius", value.restir_di.spatial_radius}, {"spatial_reuse", value.restir_di.spatial_reuse}, {"temporal_reuse", value.restir_di.temporal_reuse}, {"unbiased", value.restir_di.unbiased}}},
        {"restir_pt", {{"candidate_count", value.restir_pt.candidate_count}, {"enabled", value.restir_pt.enabled}, {"max_history", value.restir_pt.max_history}, {"max_reuse_depth", value.restir_pt.max_reuse_depth}, {"normal_threshold", value.restir_pt.normal_threshold}, {"position_threshold", value.restir_pt.position_threshold}, {"spatial_reuse", value.restir_pt.spatial_reuse}, {"temporal_reuse", value.restir_pt.temporal_reuse}}}, {"sampler", static_cast<unsigned>(value.sampler)}, {"schema", kSolverContractSchemaIdentity}, {"schema_version", {{"major", value.schema_version.major}, {"minor", value.schema_version.minor}}},
        {"spectral", {{"domain_bins", value.spectral_domain_bins}, {"packet_lanes", value.spectral_packet_lanes}, {"resident_mb", value.spectral_resident_mb}, {"sampling", static_cast<unsigned>(value.spectral_sampling)}}}, {"specular_manifold", {{"enabled", value.specular_manifold.enabled}, {"max_events", value.specular_manifold.max_specular_events}, {"max_iterations", value.specular_manifold.max_newton_iterations}, {"tolerance", value.specular_manifold.solver_tolerance}}}, {"validation", std::move(validation)},
        {"wave_optics", {{"allow_preview_degradation", value.wave_optics.experimental_allow_preview_degradation}, {"camera_aperture_blade_count", value.wave_optics.camera_aperture_blade_count}, {"camera_aperture_diameter_m", value.wave_optics.camera_aperture_diameter_m}, {"camera_aperture_rotation_rad", value.wave_optics.camera_aperture_rotation_rad}, {"camera_defocus_waves_at_edge", value.wave_optics.camera_defocus_waves_at_edge}, {"camera_diffraction", value.wave_optics.camera_diffraction_enabled}, {"camera_focal_length_m", value.wave_optics.camera_focal_length_m}, {"camera_psf_radius_pixels", value.wave_optics.camera_psf_radius_pixels}, {"camera_pupil_sample_count", value.wave_optics.camera_pupil_sample_count}, {"camera_wavelength_bin_count", value.wave_optics.camera_wavelength_bin_count}, {"coherent_field", value.wave_optics.coherent_field_enabled}, {"diffractive_materials", value.wave_optics.diffractive_materials_enabled}, {"fluorescence", value.wave_optics.fluorescence_enabled}, {"local_fullwave", value.wave_optics.local_fullwave_enabled}, {"mode", static_cast<unsigned>(value.wave_optics.mode)}, {"partial_coherence", value.wave_optics.partial_coherence_enabled}, {"sensor_pixel_pitch_m", value.wave_optics.sensor_pixel_pitch_m}, {"specular_manifold", value.wave_optics.specular_manifold_enabled}}}};
}

NativeSolverContract model_from_json(const nlohmann::ordered_json& root) {
    if (root.at("schema") != kSolverContractSchemaIdentity) throw std::invalid_argument("Wrong solver schema identity"); NativeSolverContract value; value.id = root.at("id"); value.schema_version = {root.at("schema_version").at("major"), root.at("schema_version").at("minor")}; value.integrator = static_cast<NativeIntegratorMode>(root.at("integrator").get<unsigned>()); value.sampler = static_cast<IntegratorSampler>(root.at("sampler").get<unsigned>()); value.quality = static_cast<IntegratorQualityPreset>(root.at("quality").get<unsigned>()); value.allow_biased_reuse = root.at("allow_biased_reuse");
    const auto& spectral = root.at("spectral"); value.spectral_domain_bins = spectral.at("domain_bins"); value.spectral_packet_lanes = spectral.at("packet_lanes"); value.spectral_resident_mb = spectral.at("resident_mb"); value.spectral_sampling = static_cast<SpectralSamplingMode>(spectral.at("sampling").get<unsigned>()); value.max_trace_depth = root.at("max_trace_depth");
    const auto& pg = root.at("path_guiding"); value.path_guiding = {pg.at("enabled"), pg.at("light_mixture"), pg.at("learning_rate"), pg.at("min_weight"), pg.at("decay"), pg.at("decay_interval"), pg.at("spatial_cells"), pg.at("directional_bins"), pg.at("memory_budget_mb")};
    const auto& rd = root.at("restir_di"); value.restir_di.enabled = rd.at("enabled"); value.restir_di.temporal_reuse = rd.at("temporal_reuse"); value.restir_di.spatial_reuse = rd.at("spatial_reuse"); value.restir_di.unbiased = rd.at("unbiased"); value.restir_di.max_history = rd.at("max_history"); value.restir_di.min_target = rd.at("min_target"); if (rd.contains("spatial_candidate_count")) value.restir_di.spatial_candidate_count = rd.at("spatial_candidate_count"); if (rd.contains("spatial_radius")) value.restir_di.spatial_radius = rd.at("spatial_radius"); if (rd.contains("position_threshold")) value.restir_di.position_threshold = rd.at("position_threshold"); if (rd.contains("normal_threshold")) value.restir_di.normal_threshold = rd.at("normal_threshold"); if (root.contains("restir_pt")) { const auto& rp = root.at("restir_pt"); value.restir_pt = {rp.at("enabled"), rp.at("temporal_reuse"), rp.at("spatial_reuse"), rp.at("max_reuse_depth"), rp.at("candidate_count"), rp.at("max_history"), rp.at("position_threshold"), rp.at("normal_threshold")}; } const auto& sm = root.at("specular_manifold"); value.specular_manifold = {sm.at("enabled"), sm.at("max_events"), sm.at("tolerance"), sm.at("max_iterations")}; const auto& ml = root.at("mlt"); value.mlt.enabled = ml.at("enabled"); value.mlt.chain_count = ml.at("chains"); value.mlt.mutations_per_chain = ml.at("mutations"); value.mlt.large_step_probability = ml.at("large_step_probability"); value.mlt.small_step_sigma = ml.at("small_step_sigma"); value.mlt.seed = ml.at("seed"); if (ml.contains("bootstrap_samples")) value.mlt.bootstrap_samples = ml.at("bootstrap_samples"); if (ml.contains("burn_in_mutations")) value.mlt.burn_in_mutations = ml.at("burn_in_mutations"); if (ml.contains("memory_budget_mb")) value.mlt.memory_budget_mb = ml.at("memory_budget_mb"); if (ml.contains("chain_id_offset")) value.mlt.chain_id_offset = ml.at("chain_id_offset");
    const auto& wo = root.at("wave_optics"); value.wave_optics = {static_cast<WaveOpticsMode>(wo.at("mode").get<unsigned>()), wo.at("camera_diffraction"), wo.at("coherent_field"), wo.at("partial_coherence"), wo.at("diffractive_materials"), wo.at("fluorescence"), wo.at("specular_manifold"), wo.at("local_fullwave"), wo.at("allow_preview_degradation")}; if (wo.contains("camera_aperture_diameter_m")) value.wave_optics.camera_aperture_diameter_m = wo.at("camera_aperture_diameter_m"); if (wo.contains("camera_focal_length_m")) value.wave_optics.camera_focal_length_m = wo.at("camera_focal_length_m"); if (wo.contains("sensor_pixel_pitch_m")) value.wave_optics.sensor_pixel_pitch_m = wo.at("sensor_pixel_pitch_m"); if (wo.contains("camera_defocus_waves_at_edge")) value.wave_optics.camera_defocus_waves_at_edge = wo.at("camera_defocus_waves_at_edge"); if (wo.contains("camera_aperture_rotation_rad")) value.wave_optics.camera_aperture_rotation_rad = wo.at("camera_aperture_rotation_rad"); if (wo.contains("camera_aperture_blade_count")) value.wave_optics.camera_aperture_blade_count = wo.at("camera_aperture_blade_count"); if (wo.contains("camera_psf_radius_pixels")) value.wave_optics.camera_psf_radius_pixels = wo.at("camera_psf_radius_pixels"); if (wo.contains("camera_wavelength_bin_count")) value.wave_optics.camera_wavelength_bin_count = wo.at("camera_wavelength_bin_count"); if (wo.contains("camera_pupil_sample_count")) value.wave_optics.camera_pupil_sample_count = wo.at("camera_pupil_sample_count"); value.backend = static_cast<ExecutionBackend>(root.at("backend").get<unsigned>()); value.acceleration = static_cast<AccelerationProvider>(root.at("acceleration").get<unsigned>()); value.coherent_merge = static_cast<CoherentMergeMode>(root.at("coherent_merge").get<unsigned>()); value.backend_extension = root.at("backend_extension"); value.acceleration_extension = root.at("acceleration_extension"); for (const auto& item : root.at("validation")) value.validation.push_back({item.at("metric"), item.at("tolerance")}); const auto& hints = root.at("hints"); value.hints = {hints.at("workgroup_size"), hints.at("rays_per_block"), hints.at("samples_per_pass"), hints.at("device_count")}; return value;
}
}

bool CompiledSolverContract::ok() const { return config.has_value() && std::ranges::none_of(diagnostics, [](const auto& item) { return item.severity == DiagnosticSeverity::Error; }); }

ValidationReport validate_solver_contract(const NativeSolverContract& value, const SolverCapabilityRegistry& capabilities) {
    ValidationReport report;
    if (value.id.empty()) error(report, "URE-Q7-ID-001", "/solver/id", "Solver contract ID is empty");
    if (value.schema_version.major != 1) error(report, "URE-Q7-VERSION-001", "/solver/schema_version", "Unsupported solver schema major version");
    if (value.spectral_domain_bins == 0 || value.spectral_packet_lanes < 1 || value.spectral_packet_lanes > 32 || value.spectral_domain_bins < static_cast<std::uint64_t>(value.spectral_packet_lanes)) error(report, "URE-Q7-SPECTRAL-001", "/solver/spectral", "Invalid spectral domain or packet width");
    if (value.max_trace_depth < 1 || value.spectral_resident_mb < 0) error(report, "URE-Q7-BUDGET-001", "/solver", "Invalid trace depth or spectral budget");
    if (!capabilities.integrators.contains(value.integrator)) error(report, "URE-Q7-CAPABILITY-001", "/solver/integrator", "Unsupported integrator mode");
    if (!capabilities.samplers.contains(value.sampler)) error(report, "URE-Q7-CAPABILITY-002", "/solver/sampler", "Unsupported integrator sampler");
    if (!capabilities.wave_modes.contains(value.wave_optics.mode)) error(report, "URE-Q7-CAPABILITY-003", "/solver/wave", "Unsupported wave-optics mode");
    if (!capabilities.backends.contains(value.backend)) error(report, "URE-Q7-CAPABILITY-004", "/solver/backend", "Unsupported execution backend");
    if (!capabilities.acceleration_providers.contains(value.acceleration)) error(report, "URE-Q7-CAPABILITY-005", "/solver/acceleration", "Unsupported acceleration provider");
    if (!capabilities.coherent_merge_modes.contains(value.coherent_merge)) error(report, "URE-Q7-CAPABILITY-006", "/solver/coherent_merge", "Unsupported coherent merge mode");
    if (value.backend == ExecutionBackend::Extension && value.backend_extension.empty()) error(report, "URE-Q7-EXTENSION-001", "/solver/backend", "Backend extension owner is required");
    if (value.acceleration == AccelerationProvider::Extension && value.acceleration_extension.empty()) error(report, "URE-Q7-EXTENSION-002", "/solver/acceleration", "Acceleration extension owner is required");
    if (value.integrator == NativeIntegratorMode::PathGuided && !value.path_guiding.enabled) error(report, "URE-Q7-INTEGRATOR-001", "/solver/path_guiding", "Path-guided mode requires path guiding");
    if (value.integrator == NativeIntegratorMode::RestirDI && !value.restir_di.enabled) error(report, "URE-Q7-INTEGRATOR-002", "/solver/restir_di", "ReSTIR DI mode requires ReSTIR DI");
    if (value.restir_di.enabled && !value.restir_di.unbiased && !value.allow_biased_reuse) error(report, "URE-Q7-BIAS-001", "/solver/restir_di", "Biased reuse requires explicit consent");
    if (value.integrator == NativeIntegratorMode::SpecularManifold && !value.specular_manifold.enabled) error(report, "URE-Q7-INTEGRATOR-003", "/solver/specular_manifold", "Specular-manifold mode requires its solver");
    if (value.integrator == NativeIntegratorMode::MLT && (!value.mlt.enabled || value.sampler != IntegratorSampler::PrimarySampleSpace)) error(report, "URE-Q7-INTEGRATOR-004", "/solver/mlt", "MLT requires enabled MLT and primary-sample-space sampler");
    if (value.integrator == NativeIntegratorMode::RestirPT && !value.restir_pt.enabled) error(report, "URE-Q7-INTEGRATOR-005", "/solver/restir_pt", "ReSTIR PT mode requires ReSTIR PT");
    if (value.wave_optics.mode == WaveOpticsMode::CoherentField && (!value.wave_optics.coherent_field_enabled || value.coherent_merge == CoherentMergeMode::None)) error(report, "URE-Q7-WAVE-001", "/solver/wave", "Coherent field requires coherent transport and merge");
    if (value.wave_optics.mode == WaveOpticsMode::PartialCoherence && (!value.wave_optics.partial_coherence_enabled || value.coherent_merge != CoherentMergeMode::MutualCoherence)) error(report, "URE-Q7-WAVE-002", "/solver/wave", "Partial coherence requires mutual-coherence merge");
    if (value.wave_optics.mode == WaveOpticsMode::Radiometric && value.coherent_merge != CoherentMergeMode::None) error(report, "URE-Q7-WAVE-003", "/solver/wave", "Radiometric mode cannot request coherent merge");
    const bool camera_diffraction_mode =
        value.wave_optics.mode ==
        WaveOpticsMode::CameraDiffraction;
    if (camera_diffraction_mode !=
        value.wave_optics.camera_diffraction_enabled) {
        error(report, "URE-Q7-WAVE-004", "/solver/wave/camera_diffraction", "Camera diffraction mode and enable flag must match");
    }
    if (camera_diffraction_mode &&
        (value.wave_optics.coherent_field_enabled ||
         value.wave_optics.partial_coherence_enabled ||
         value.wave_optics.diffractive_materials_enabled ||
         value.wave_optics.fluorescence_enabled ||
         value.wave_optics.specular_manifold_enabled ||
         value.wave_optics.local_fullwave_enabled ||
         value.integrator != NativeIntegratorMode::Wavefront ||
         value.path_guiding.enabled ||
         value.restir_di.enabled ||
         value.restir_pt.enabled ||
         value.specular_manifold.enabled ||
         value.mlt.enabled ||
         !finite(value.wave_optics.camera_aperture_diameter_m) ||
         value.wave_optics.camera_aperture_diameter_m <= 0.0 ||
         !finite(value.wave_optics.camera_focal_length_m) ||
         value.wave_optics.camera_focal_length_m <= 0.0 ||
         !finite(value.wave_optics.sensor_pixel_pitch_m) ||
         value.wave_optics.sensor_pixel_pitch_m <= 0.0 ||
         !finite(value.wave_optics.camera_defocus_waves_at_edge) ||
         std::abs(value.wave_optics.camera_defocus_waves_at_edge) > 64.0 ||
         !finite(value.wave_optics.camera_aperture_rotation_rad) ||
         (value.wave_optics.camera_aperture_blade_count != 0 &&
          (value.wave_optics.camera_aperture_blade_count < 3 ||
           value.wave_optics.camera_aperture_blade_count > 16)) ||
         value.wave_optics.camera_psf_radius_pixels < 1 ||
         value.wave_optics.camera_psf_radius_pixels > 32 ||
         value.wave_optics.camera_wavelength_bin_count < 2 ||
         value.wave_optics.camera_wavelength_bin_count > 32 ||
         value.wave_optics.camera_pupil_sample_count < 16 ||
         value.wave_optics.camera_pupil_sample_count > 64)) {
        error(report, "URE-Q7-WAVE-005", "/solver/wave/camera_diffraction", "Invalid or incompatible diffraction-camera contract");
    }
    if (value.wave_optics.diffractive_materials_enabled &&
        (value.wave_optics.mode !=
             WaveOpticsMode::Radiometric ||
         value.wave_optics.camera_diffraction_enabled ||
         value.wave_optics.coherent_field_enabled ||
         value.wave_optics.partial_coherence_enabled ||
         value.wave_optics.fluorescence_enabled ||
         value.wave_optics.specular_manifold_enabled ||
         value.wave_optics.local_fullwave_enabled ||
         value.integrator !=
             NativeIntegratorMode::Wavefront ||
         value.path_guiding.enabled ||
         value.restir_di.enabled ||
         value.restir_pt.enabled ||
         value.specular_manifold.enabled ||
         value.mlt.enabled)) {
        error(report, "URE-Q7-WAVE-006", "/solver/wave/diffractive_materials", "Diffractive materials require the ordinary radiometric wavefront boundary");
    }
    if (value.wave_optics.fluorescence_enabled &&
        (value.wave_optics.mode !=
             WaveOpticsMode::Radiometric ||
         value.wave_optics.camera_diffraction_enabled ||
         value.wave_optics.coherent_field_enabled ||
         value.wave_optics.partial_coherence_enabled ||
         value.wave_optics.diffractive_materials_enabled ||
         value.wave_optics.specular_manifold_enabled ||
         value.wave_optics.local_fullwave_enabled ||
         value.integrator !=
             NativeIntegratorMode::Wavefront ||
         value.path_guiding.enabled ||
         value.restir_di.enabled ||
         value.restir_pt.enabled ||
         value.specular_manifold.enabled ||
         value.mlt.enabled)) {
        error(report, "URE-Q7-WAVE-007", "/solver/wave/fluorescence", "Fluorescence requires the ordinary radiometric wavefront boundary");
    }
    if (value.path_guiding.enabled && (!finite(value.path_guiding.light_mixture) || value.path_guiding.light_mixture < 0.0f || value.path_guiding.light_mixture > 1.0f || !finite(value.path_guiding.learning_rate) || value.path_guiding.learning_rate <= 0.0f)) error(report, "URE-Q7-PARAMETER-001", "/solver/path_guiding", "Invalid path-guiding parameters");
    if (value.restir_di.enabled && (value.restir_di.max_history < 1 || !finite(value.restir_di.min_target) || value.restir_di.min_target <= 0.0f || !finite(value.restir_di.position_threshold) || value.restir_di.position_threshold <= 0.0f || !finite(value.restir_di.normal_threshold) || value.restir_di.normal_threshold < 0.0f || value.restir_di.normal_threshold > 1.0f)) error(report, "URE-Q7-PARAMETER-002", "/solver/restir_di", "Invalid ReSTIR parameters");
    if (value.restir_pt.enabled && (value.restir_pt.max_reuse_depth < 1 || value.restir_pt.candidate_count < 1 || value.restir_pt.max_history < 1 || !finite(value.restir_pt.position_threshold) || value.restir_pt.position_threshold <= 0.0f || !finite(value.restir_pt.normal_threshold) || value.restir_pt.normal_threshold < 0.0f || value.restir_pt.normal_threshold > 1.0f)) error(report, "URE-Q7-PARAMETER-003", "/solver/restir_pt", "Invalid ReSTIR PT parameters");
    for (const auto& requirement : value.validation) { if (requirement.metric.empty() || !std::isfinite(requirement.tolerance) || requirement.tolerance < 0.0) error(report, "URE-Q7-VALIDATION-001", "/solver/validation", "Invalid validation requirement"); if (!capabilities.validation_metrics.contains(requirement.metric)) error(report, "URE-Q7-CAPABILITY-007", "/solver/validation", "Unsupported validation metric"); }
    return report;
}

CompiledSolverContract compile_solver_contract(const NativeSolverContract& value, const SolverCapabilityRegistry& capabilities) {
    CompiledSolverContract result; const auto validation = validate_solver_contract(value, capabilities); result.diagnostics = validation.diagnostics; if (!validation.ok()) return result;
    IntegratorMode runtime_mode = IntegratorMode::Wavefront;
    switch (value.integrator) {
    case NativeIntegratorMode::Wavefront: runtime_mode = IntegratorMode::Wavefront; break;
    case NativeIntegratorMode::PathGuided: runtime_mode = IntegratorMode::PathGuided; break;
    case NativeIntegratorMode::RestirDI: runtime_mode = IntegratorMode::RestirDI; break;
    case NativeIntegratorMode::SpecularManifold: runtime_mode = IntegratorMode::SpecularManifold; break;
    case NativeIntegratorMode::MLT: runtime_mode = IntegratorMode::MLT; break;
    case NativeIntegratorMode::BDPT: runtime_mode = IntegratorMode::BDPT; break;
    case NativeIntegratorMode::VCM: runtime_mode = IntegratorMode::VCM; break;
    case NativeIntegratorMode::RestirPT: runtime_mode = IntegratorMode::RestirPT; break;
    }
    RenderConfig config; config.integrator.mode = runtime_mode; config.integrator.sampler = value.sampler; config.integrator.quality_preset = value.quality; config.integrator.allow_biased_reuse = value.allow_biased_reuse; config.spectral_domain_bins = value.spectral_domain_bins; config.spectral_packet_lanes = value.spectral_packet_lanes; config.num_wavelengths = value.spectral_packet_lanes; config.spectral_max_resident_mb = value.spectral_resident_mb; config.spectral_sampling_mode = value.spectral_sampling; config.max_trace_depth = value.max_trace_depth; config.path_guiding = value.path_guiding; config.restir_di = value.restir_di; config.restir_pt = value.restir_pt; config.specular_manifold = value.specular_manifold; config.mlt = value.mlt; config.wave_optics = value.wave_optics; config.wg_size = value.hints.workgroup_size > 0 ? value.hints.workgroup_size : config.wg_size; config.rays_per_block = value.hints.rays_per_block > 0 ? value.hints.rays_per_block : config.rays_per_block; config.samples_per_pass = value.hints.samples_per_pass > 0 ? value.hints.samples_per_pass : config.samples_per_pass; config.num_gpus_to_use = value.hints.device_count > 0 ? value.hints.device_count : config.num_gpus_to_use; result.config = config; return result;
}

std::vector<std::uint8_t> write_solver_contract_binary(const NativeSolverContract& contract) { auto native = to_schema(contract); flatbuffers::FlatBufferBuilder builder; fb::FinishSolverContractBuffer(builder, fb::SolverContract::Pack(builder, &native)); return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()}; }

LoadResult<NativeSolverContract> read_solver_contract_binary(std::span<const std::uint8_t> bytes, const ValidationLimits& limits) { const auto max_tables = static_cast<flatbuffers::uoffset_t>(std::min<std::uint64_t>(limits.max_object_count, std::numeric_limits<flatbuffers::uoffset_t>::max())); flatbuffers::Verifier verifier(bytes.data(), bytes.size(), limits.max_nesting_depth, max_tables); if (!fb::VerifySolverContractBuffer(verifier)) return failure<NativeSolverContract>("URE-Q7-SCHEMA-001", "Invalid URSC payload"); try { std::unique_ptr<fb::SolverContractT> native(fb::GetSolverContract(bytes.data())->UnPack()); LoadResult<NativeSolverContract> result; result.value = from_schema(*native); return result; } catch (const std::exception& exception) { return failure<NativeSolverContract>("URE-Q7-SCHEMA-002", exception.what()); } }

std::string solver_contract_semantic_hash(const NativeSolverContract& contract) { NativeSolverContract semantic = contract; semantic.hints = {}; const auto bytes = write_solver_contract_binary(semantic); return sha256_hex(bytes); }

std::string write_solver_contract_text(const NativeSolverContract& contract) { return contract_json(contract).dump(2) + "\n"; }

LoadResult<NativeSolverContract> read_solver_contract_text(std::string_view text) { try { LoadResult<NativeSolverContract> result; result.value = model_from_json(nlohmann::ordered_json::parse(text)); return result; } catch (const std::exception& exception) { return failure<NativeSolverContract>("URE-Q7-TEXT-001", exception.what()); } }

}
