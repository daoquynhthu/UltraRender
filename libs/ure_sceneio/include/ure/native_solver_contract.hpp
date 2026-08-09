#pragma once

#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

#include <ure/native_scene.hpp>
#include <ure/render_config.hpp>

namespace ure::native_scene {

inline constexpr const char* kSolverContractSchemaIdentity = "ure.solver-contract/1.0";
inline constexpr const char* kSolverContractFeature = "ure.render.solver";

enum class NativeIntegratorMode : std::uint8_t { Wavefront, PathGuided, RestirDI, SpecularManifold, MLT, BDPT, VCM, RestirPT };
enum class ExecutionBackend : std::uint8_t { Cuda, Vulkan, D3D12, CpuOracle, Extension };
enum class AccelerationProvider : std::uint8_t { SoftwareBvh, Optix, VulkanRT, DXR, Extension };
enum class CoherentMergeMode : std::uint8_t { None, ComplexAmplitude, MutualCoherence };

struct ValidationRequirement {
    std::string metric;
    double tolerance = 0.0;
};

struct ExecutionHints {
    int workgroup_size = 0;
    int rays_per_block = 0;
    int samples_per_pass = 0;
    int device_count = 0;
};

struct NativeSolverContract {
    std::string id;
    Version schema_version;
    NativeIntegratorMode integrator = NativeIntegratorMode::Wavefront;
    IntegratorSampler sampler = IntegratorSampler::Default;
    IntegratorQualityPreset quality = IntegratorQualityPreset::Default;
    bool allow_biased_reuse = false;
    std::uint64_t spectral_domain_bins = 0;
    int spectral_packet_lanes = 0;
    int spectral_resident_mb = 0;
    SpectralSamplingMode spectral_sampling = SpectralSamplingMode::PacketUniform;
    int max_trace_depth = 50;
    PathGuidingConfig path_guiding;
    RestirDirectConfig restir_di;
    RestirPathConfig restir_pt;
    SpecularManifoldConfig specular_manifold;
    MltIntegratorConfig mlt;
    WaveOpticsConfig wave_optics;
    ExecutionBackend backend = ExecutionBackend::Cuda;
    AccelerationProvider acceleration = AccelerationProvider::SoftwareBvh;
    CoherentMergeMode coherent_merge = CoherentMergeMode::None;
    std::string backend_extension;
    std::string acceleration_extension;
    std::vector<ValidationRequirement> validation;
    ExecutionHints hints;
};

struct SolverCapabilityRegistry {
    std::set<NativeIntegratorMode> integrators;
    std::set<IntegratorSampler> samplers;
    std::set<WaveOpticsMode> wave_modes;
    std::set<ExecutionBackend> backends;
    std::set<AccelerationProvider> acceleration_providers;
    std::set<CoherentMergeMode> coherent_merge_modes;
    std::map<std::string, Version> validation_metrics;
};

struct CompiledSolverContract {
    std::optional<RenderConfig> config;
    std::vector<ValidationDiagnostic> diagnostics;
    bool ok() const;
};

ValidationReport validate_solver_contract(const NativeSolverContract& contract,
                                          const SolverCapabilityRegistry& capabilities);
CompiledSolverContract compile_solver_contract(const NativeSolverContract& contract,
                                               const SolverCapabilityRegistry& capabilities);
std::string solver_contract_semantic_hash(const NativeSolverContract& contract);
std::vector<std::uint8_t> write_solver_contract_binary(const NativeSolverContract& contract);
LoadResult<NativeSolverContract> read_solver_contract_binary(
    std::span<const std::uint8_t> bytes, const ValidationLimits& limits = {});
std::string write_solver_contract_text(const NativeSolverContract& contract);
LoadResult<NativeSolverContract> read_solver_contract_text(std::string_view text);

}
