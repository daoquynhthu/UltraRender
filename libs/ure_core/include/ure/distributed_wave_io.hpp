#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "ure/distributed_contract.hpp"
#include "ure/wave_optics.hpp"

namespace ure::gpu {

constexpr std::size_t
    kMaxDistributedComplexFrameElements = 1048576;

struct DistributedComplexFrameStorage {
    int width = 0;
    int height = 0;
    int total_samples = 0;
    DistributedShardMetadata shard;
    std::vector<double> wavelengths_m;
    std::vector<wave::ComplexAmplitude>
        amplitude_sums;
    std::vector<double> estimator_weights;
    double realization_weight = 0.0;

    bool is_valid() const;
    std::size_t element_count() const;
    wave::ComplexAmplitude resolved_amplitude_at(
        int x,
        int y,
        std::size_t lane) const;
};

struct DistributedMutualIntensityFrameStorage {
    int total_samples = 0;
    DistributedShardMetadata shard;
    wave::CrossSpectralDensity weighted_density;
    double total_statistical_weight = 0.0;

    bool is_valid() const;
    wave::CrossSpectralDensity resolved_density() const;
};

struct DistributedPartialCoherenceAccumulator {
    wave::PartialCoherenceFilm film;
    std::vector<DistributedFrameSemantics>
        realization_semantics;

    bool is_valid() const;
};

runtime::IdentityDigest
distributed_complex_field_layout_identity(
    int width,
    int height,
    const std::vector<double>& wavelengths_m);

runtime::IdentityDigest
distributed_mutual_intensity_layout_identity(
    const wave::CrossSpectralDensity& density);

void merge_complex_field_frame(
    DistributedComplexFrameStorage& accum,
    const DistributedComplexFrameStorage& incoming);

void merge_mutual_intensity_frame(
    DistributedMutualIntensityFrameStorage& accum,
    const DistributedMutualIntensityFrameStorage&
        incoming);

bool append_coherent_realization(
    DistributedPartialCoherenceAccumulator& accum,
    const DistributedComplexFrameStorage& frame);

void write_complex_field_frame_file(
    const std::filesystem::path& path,
    const DistributedComplexFrameStorage& frame);

DistributedComplexFrameStorage
read_complex_field_frame_file(
    const std::filesystem::path& path);

void merge_complex_field_frame_files(
    const std::filesystem::path& accum_path,
    const std::vector<std::filesystem::path>&
        incoming_paths,
    const std::filesystem::path& output_path);

void write_mutual_intensity_frame_file(
    const std::filesystem::path& path,
    const DistributedMutualIntensityFrameStorage&
        frame);

DistributedMutualIntensityFrameStorage
read_mutual_intensity_frame_file(
    const std::filesystem::path& path);

void merge_mutual_intensity_frame_files(
    const std::filesystem::path& accum_path,
    const std::vector<std::filesystem::path>&
        incoming_paths,
    const std::filesystem::path& output_path);

}
