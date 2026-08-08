#pragma once

#include "ure/reconstruction/measurement.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace ure::reconstruction {

inline constexpr std::uint32_t kStatisticalReconstructionVersion = 1;

enum class ReconstructionTailClass : std::uint8_t {
    Ordinary,
    HeavyTail,
    HighEnergyPreserved,
    InvalidSample
};

enum class ReconstructionRejectionReason : std::uint8_t {
    None,
    InvalidCurrentSample,
    NoHistory,
    HistoryIdentityMismatch,
    InvalidMotion,
    ReprojectionOutsideFrame,
    InvalidHistorySample,
    DisoccludedDepth,
    DisoccludedNormal,
    DisoccludedAlbedo,
    InsufficientSpatialSupport
};

struct StatisticalReconstructionConfig {
    std::uint32_t version = kStatisticalReconstructionVersion;
    semantic::IdentityDigest config_identity = {};
    std::uint32_t spatial_iteration_count = 3;
    std::uint32_t minimum_spatial_support = 2;
    std::uint32_t maximum_history_length = 32;
    double signal_sigma = 4.0;
    double normal_sigma = 0.1;
    double depth_sigma = 0.02;
    double albedo_sigma = 0.1;
    double minimum_normal_dot = 0.85;
    double maximum_relative_depth_difference = 0.02;
    double maximum_albedo_distance = 0.2;
    double maximum_history_weight = 0.95;
    double heavy_tail_frequency = 0.05;
    double heavy_tail_scale = 8.0;
    double high_energy_sigma = 6.0;
    bool temporal_enabled = true;
};

semantic::IdentityDigest compute_statistical_reconstruction_config_identity(
    const StatisticalReconstructionConfig& config);
void finalize_statistical_reconstruction_config(
    StatisticalReconstructionConfig& config);

struct StatisticalReconstructionFrame {
    std::uint32_t version = kStatisticalReconstructionVersion;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t component_count = 0;
    transport::ObservableDescriptor observable = {};
    semantic::ProvenanceIdentitySet identities;
    semantic::IdentityDigest measurement_schema_identity = {};
    semantic::IdentityDigest frame_identity = {};
    std::vector<double> raw_estimate;
    std::vector<double> sample_variance;
    std::vector<double> effective_sample_count;
    std::vector<double> tail_frequency;
    std::vector<double> maximum_absolute_contribution;
    std::vector<double> normal;
    std::vector<double> albedo;
    std::vector<double> depth;
    std::vector<double> motion;
    std::vector<double> motion_time_confidence;
    std::vector<std::uint8_t> validity;
};

semantic::IdentityDigest compute_statistical_reconstruction_frame_identity(
    const StatisticalReconstructionFrame& frame);
void finalize_statistical_reconstruction_frame(
    StatisticalReconstructionFrame& frame);

struct StatisticalReconstructionHistory {
    std::uint32_t version = kStatisticalReconstructionVersion;
    semantic::IdentityDigest history_identity = {};
    semantic::IdentityDigest frame_identity = {};
    transport::ObservableDescriptor observable = {};
    semantic::ProvenanceIdentitySet identities;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t component_count = 0;
    std::vector<double> reconstructed;
    std::vector<double> variance;
    std::vector<double> normal;
    std::vector<double> albedo;
    std::vector<double> depth;
    std::vector<double> confidence;
    std::vector<std::uint32_t> length;
    std::vector<std::uint8_t> validity;
};

semantic::IdentityDigest compute_statistical_reconstruction_history_identity(
    const StatisticalReconstructionHistory& history);
void finalize_statistical_reconstruction_history(
    StatisticalReconstructionHistory& history);

enum class StatisticalReconstructionIssue : std::uint8_t {
    Version,
    Identity,
    Shape,
    Observable,
    Provenance,
    NonFinite,
    Variance,
    EffectiveSampleCount,
    TailStatistics,
    PhysicalDomain,
    Configuration,
    History
};

struct StatisticalReconstructionValidation {
    std::vector<StatisticalReconstructionIssue> issues;

    bool ok() const { return issues.empty(); }
    bool has(StatisticalReconstructionIssue issue) const;
};

StatisticalReconstructionValidation validate_statistical_reconstruction_config(
    const StatisticalReconstructionConfig& config);
StatisticalReconstructionValidation validate_statistical_reconstruction_frame(
    const StatisticalReconstructionFrame& frame);
StatisticalReconstructionValidation validate_statistical_reconstruction_history(
    const StatisticalReconstructionHistory& history);

struct StatisticalReconstructionOutput {
    std::uint32_t version = kStatisticalReconstructionVersion;
    semantic::IdentityDigest output_identity = {};
    semantic::IdentityDigest config_identity = {};
    semantic::IdentityDigest frame_identity = {};
    semantic::IdentityDigest history_identity = {};
    transport::ObservableDescriptor observable = {};
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t component_count = 0;
    std::vector<double> raw_estimate;
    std::vector<double> reconstructed;
    std::vector<double> variance;
    std::vector<double> uncertainty;
    std::vector<double> spatial_support;
    std::vector<double> history_confidence;
    std::vector<std::uint32_t> history_length;
    std::vector<ReconstructionTailClass> tail_class;
    std::vector<ReconstructionRejectionReason> rejection_reason;
    std::vector<std::uint8_t> validity;
};

semantic::IdentityDigest compute_statistical_reconstruction_output_identity(
    const StatisticalReconstructionOutput& output);
bool validate_statistical_reconstruction_output(
    const StatisticalReconstructionOutput& output);
StatisticalReconstructionOutput reconstruct_statistics(
    const StatisticalReconstructionFrame& frame,
    const StatisticalReconstructionConfig& config,
    const StatisticalReconstructionHistory* history = nullptr);
StatisticalReconstructionHistory make_statistical_reconstruction_history(
    const StatisticalReconstructionFrame& frame,
    const StatisticalReconstructionOutput& output);

}
