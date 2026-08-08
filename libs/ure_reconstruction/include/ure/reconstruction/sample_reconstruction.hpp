#pragma once

#include "ure/reconstruction/measurement.hpp"
#include "ure/research/capability.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace ure::reconstruction {

inline constexpr std::uint32_t kSampleReconstructionVersion = 1;

enum class SampleReconstructionMethod : std::uint8_t {
    AnalyticKernelSplat,
    ExternalKernelPrediction,
    ExternalSampleTransformer,
    ExternalHybrid
};

enum class SampleProjectionPolicy : std::uint8_t {
    None,
    NonnegativeObservationConsistentSpectrum,
    PhysicalStokesCone,
    GaugePreservingComplex
};

enum class SampleReconstructionOodReason : std::uint32_t {
    None = 0,
    Observable = 1u << 0,
    ComponentLayout = 1u << 1,
    Wavelength = 1u << 2,
    Technique = 1u << 3,
    Material = 1u << 4,
    SampleCount = 1u << 5,
    Polarization = 1u << 6,
    World = 1u << 7,
    MeasurementSchema = 1u << 8
};

enum class SampleReconstructionRejection : std::uint8_t {
    None,
    InvalidSample,
    InsufficientSupport,
    OutOfDomain,
    ObservationInconsistent
};

struct SampleReconstructionRecord {
    semantic::IdentityDigest sample_identity = {};
    semantic::IdentityDigest technique_identity = {};
    semantic::IdentityDigest path_event_identity = {};
    semantic::IdentityDigest material_identity = {};
    semantic::IdentityDigest medium_identity = {};
    semantic::IdentityDigest spectral_resource_identity = {};
    semantic::IdentityDigest phase_reference_identity = {};
    double raster_x = 0.0;
    double raster_y = 0.0;
    double time_seconds = 0.0;
    double detector_wavelength_nm = 0.0;
    double transport_wavelength_nm = 0.0;
    double joint_pdf = 1.0;
    double estimator_weight = 1.0;
    double kernel_radius = 1.0;
    double depth = 0.0;
    std::array<double, 3> normal = {0.0, 0.0, 1.0};
    std::vector<double> feature_albedo;
    std::vector<double> value;
    bool valid = true;
};

struct SampleReconstructionBatch {
    std::uint32_t version = kSampleReconstructionVersion;
    semantic::IdentityDigest batch_identity = {};
    transport::ObservableDescriptor observable = {};
    semantic::ProvenanceIdentitySet identities;
    semantic::IdentityDigest measurement_schema_identity = {};
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t component_count = 0;
    std::vector<double> component_wavelength_nm;
    std::vector<double> sensor_response;
    std::vector<double> sensor_observation;
    std::vector<SampleReconstructionRecord> records;
};

semantic::IdentityDigest compute_sample_reconstruction_batch_identity(
    const SampleReconstructionBatch& batch);
void finalize_sample_reconstruction_batch(SampleReconstructionBatch& batch);

struct SampleReconstructionApplicability {
    double minimum_wavelength_nm = 0.0;
    double maximum_wavelength_nm = 0.0;
    double maximum_polarization_degree = 1.0;
    std::uint64_t minimum_sample_count = 1;
    std::uint64_t maximum_sample_count = 0;
    std::uint32_t component_count = 0;
    bool spectrum = false;
    bool stokes = false;
    bool complex_jones = false;
    std::vector<semantic::IdentityDigest> world_definition_identities;
    std::vector<semantic::IdentityDigest> measurement_schema_identities;
    std::vector<semantic::IdentityDigest> technique_identities;
    std::vector<semantic::IdentityDigest> material_identities;
};

struct SampleReconstructionCandidate {
    std::uint32_t version = kSampleReconstructionVersion;
    semantic::IdentityDigest candidate_identity = {};
    semantic::IdentityDigest capsule_identity = {};
    semantic::IdentityDigest source_identity = {};
    semantic::IdentityDigest hypothesis_identity = {};
    semantic::IdentityDigest algorithm_identity = {};
    semantic::IdentityDigest provider_identity = {};
    semantic::IdentityDigest artifact_identity = {};
    semantic::IdentityDigest failure_domain_identity = {};
    research::Maturity maturity = research::Maturity::Research;
    SampleReconstructionMethod method =
        SampleReconstructionMethod::AnalyticKernelSplat;
    SampleReconstructionApplicability applicability;
    bool permutation_invariant = true;
    bool consumes_technique_metadata = true;
    bool consumes_path_metadata = true;
    bool consumes_spectral_metadata = true;
};

semantic::IdentityDigest compute_sample_reconstruction_candidate_identity(
    const SampleReconstructionCandidate& candidate);
void finalize_sample_reconstruction_candidate(
    SampleReconstructionCandidate& candidate);

struct SampleReconstructionWeight {
    semantic::IdentityDigest sample_identity = {};
    double multiplier = 1.0;
    double kernel_radius = 1.0;
    double confidence = 1.0;
};

struct SampleReconstructionExternalWeights {
    std::uint32_t version = kSampleReconstructionVersion;
    semantic::IdentityDigest weights_identity = {};
    semantic::IdentityDigest batch_identity = {};
    semantic::IdentityDigest candidate_identity = {};
    semantic::IdentityDigest provider_identity = {};
    semantic::IdentityDigest artifact_identity = {};
    std::vector<SampleReconstructionWeight> weights;
};

semantic::IdentityDigest compute_sample_reconstruction_weights_identity(
    const SampleReconstructionExternalWeights& weights);
void finalize_sample_reconstruction_weights(
    SampleReconstructionExternalWeights& weights);

struct SampleReconstructionConfig {
    std::uint32_t version = kSampleReconstructionVersion;
    semantic::IdentityDigest config_identity = {};
    SampleReconstructionMethod method =
        SampleReconstructionMethod::AnalyticKernelSplat;
    SampleProjectionPolicy projection =
        SampleProjectionPolicy::NonnegativeObservationConsistentSpectrum;
    double normal_sigma = 0.1;
    double depth_sigma = 0.02;
    double albedo_sigma = 0.1;
    double time_sigma_seconds = 0.01;
    double wavelength_sigma_nm = 20.0;
    double maximum_kernel_radius = 4.0;
    double observation_tolerance = 1e-8;
    std::uint32_t minimum_support = 2;
    bool explicit_research_opt_in = false;
    bool allow_ood = false;
};

semantic::IdentityDigest compute_sample_reconstruction_config_identity(
    const SampleReconstructionConfig& config);
void finalize_sample_reconstruction_config(
    SampleReconstructionConfig& config);

enum class SampleReconstructionIssue : std::uint8_t {
    Version,
    Identity,
    Shape,
    Observable,
    Provenance,
    NonFinite,
    Sample,
    Candidate,
    ExternalWeights,
    Configuration,
    PhysicalDomain
};

struct SampleReconstructionValidation {
    std::vector<SampleReconstructionIssue> issues;

    bool ok() const { return issues.empty(); }
    bool has(SampleReconstructionIssue issue) const;
};

SampleReconstructionValidation validate_sample_reconstruction_batch(
    const SampleReconstructionBatch& batch);
SampleReconstructionValidation validate_sample_reconstruction_candidate(
    const SampleReconstructionCandidate& candidate);
SampleReconstructionValidation validate_sample_reconstruction_weights(
    const SampleReconstructionExternalWeights& weights);
SampleReconstructionValidation validate_sample_reconstruction_config(
    const SampleReconstructionConfig& config);

std::uint32_t assess_sample_reconstruction_ood(
    const SampleReconstructionBatch& batch,
    const SampleReconstructionCandidate& candidate);

struct SampleReconstructionOutput {
    std::uint32_t version = kSampleReconstructionVersion;
    semantic::IdentityDigest output_identity = {};
    semantic::IdentityDigest batch_identity = {};
    semantic::IdentityDigest config_identity = {};
    semantic::IdentityDigest candidate_identity = {};
    semantic::IdentityDigest weights_identity = {};
    research::Maturity maturity = research::Maturity::Research;
    transport::ObservableDescriptor observable = {};
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t component_count = 0;
    std::uint32_t ood_mask = 0;
    std::vector<double> raw_estimate;
    std::vector<double> raw_uncertainty;
    std::vector<double> reconstructed;
    std::vector<double> uncertainty;
    std::vector<double> effective_support;
    std::vector<double> confidence;
    std::vector<double> projection_delta;
    std::vector<SampleReconstructionRejection> rejection_reason;
};

semantic::IdentityDigest compute_sample_reconstruction_output_identity(
    const SampleReconstructionOutput& output);
bool validate_sample_reconstruction_output(
    const SampleReconstructionOutput& output);
SampleReconstructionOutput reconstruct_samples(
    const SampleReconstructionBatch& batch,
    const SampleReconstructionConfig& config,
    const SampleReconstructionCandidate* candidate = nullptr,
    const SampleReconstructionExternalWeights* external_weights = nullptr);

struct SampleReconstructionEvaluation {
    semantic::IdentityDigest evaluation_identity = {};
    semantic::IdentityDigest output_identity = {};
    double raw_mse = 0.0;
    double reconstructed_mse = 0.0;
    double coverage_one_sigma = 0.0;
    double coverage_two_sigma = 0.0;
    double calibration_error_one_sigma = 0.0;
    double calibration_error_two_sigma = 0.0;
    double maximum_observation_residual = 0.0;
    double maximum_permutation_error = 0.0;
    std::uint64_t physical_violation_count = 0;
};

SampleReconstructionEvaluation evaluate_sample_reconstruction(
    const SampleReconstructionOutput& output,
    std::span<const double> reference,
    const SampleReconstructionOutput* permuted_output = nullptr,
    std::span<const double> sensor_response = {},
    std::span<const double> sensor_observation = {});
bool validate_sample_reconstruction_evaluation(
    const SampleReconstructionEvaluation& evaluation);

}
