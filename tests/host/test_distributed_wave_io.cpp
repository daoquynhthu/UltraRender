#include <ure/distributed_file_io.hpp>
#include <ure/distributed_wave_io.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

template <typename Function>
bool throws(Function&& function) {
    try {
        function();
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

std::filesystem::path temporary_path(
    const char* name) {
    return std::filesystem::temp_directory_path() /
           name;
}

void remove_if_exists(
    const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::remove(path, error);
}

ure::runtime::WorkerCapability worker(
    ure::BackendKind backend,
    std::string adapter,
    const ure::runtime::IdentityDigest& semantics) {
    ure::runtime::WorkerCapability result;
    result.adapter.kind = backend;
    result.adapter.vendor_id =
        backend == ure::BackendKind::Cuda
        ? 0x10deu
        : 0x8086u;
    result.adapter.device_id =
        0x3000u +
        static_cast<std::uint32_t>(backend);
    result.adapter.adapter_id = adapter;
    result.adapter.name = adapter;
    result.adapter.features =
        ure::backend_feature_bit(
            ure::BackendFeature::Compute) |
        ure::backend_feature_bit(
            ure::BackendFeature::SpectralTransport) |
        ure::backend_feature_bit(
            ure::BackendFeature::Polarization);
    result.adapter.memory.total_bytes = 16384;
    result.adapter.memory.available_bytes = 12288;
    result.adapter.memory.budget_bytes = 8192;
    result.adapter.driver_identity =
        "driver:" + adapter;
    result.adapter.compiler_identity =
        "compiler:" + adapter;
    result.coherence_modes |=
        ure::runtime::coherence_mode_bit(
            ure::runtime::CoherenceMode::
                CoherentField);
    result.semantic_identity = semantics;
    result.executable_identity =
        ure::runtime::identity_digest(
            "executable:" + adapter);
    return result;
}

struct ScheduleFixture {
    ure::resource::ResourceSetMetadata resources;
    ure::runtime::MultiBackendSchedule schedule;
    ure::gpu::DistributedSpectralDomainShard spectral;
    ure::gpu::DistributedFrameShard frame;
};

ScheduleFixture schedule_fixture(
    std::uint64_t total_samples) {
    ScheduleFixture result;
    result.resources.content_hash =
        ure::runtime::identity_digest(
            "wave-distributed-resources");
    result.resources.descriptor_count = 2;
    result.resources.logical_bytes = 4096;
    result.resources.minimum_resident_bytes = 2048;
    const auto semantic_identity =
        ure::runtime::identity_digest(
            "wave-distributed-kernels");
    const std::vector workers{
        worker(
            ure::BackendKind::Cuda,
            "cuda:wave",
            semantic_identity),
        worker(
            ure::BackendKind::Vulkan,
            "vulkan:wave",
            semantic_identity)};
    ure::runtime::ExecutionRequirements requirements;
    requirements.required_features =
        ure::backend_feature_bit(
            ure::BackendFeature::Compute) |
        ure::backend_feature_bit(
            ure::BackendFeature::SpectralTransport) |
        ure::backend_feature_bit(
            ure::BackendFeature::Polarization);
    requirements.coherence =
        ure::runtime::CoherenceMode::CoherentField;
    requirements.minimum_resident_bytes =
        result.resources.minimum_resident_bytes;
    requirements.semantic_identity =
        semantic_identity;
    result.schedule =
        ure::runtime::negotiate_sample_shards(
            requirements,
            result.resources,
            workers,
            total_samples);
    result.spectral =
        ure::gpu::make_aggregate_spectral_domain(
            16,
            450.0f,
            650.0f);
    result.frame =
        ure::gpu::make_frame_shard(0, 1);
    return result;
}

ure::gpu::DistributedComplexFrameStorage
complex_frame(
    const ScheduleFixture& fixture,
    std::size_t shard_index,
    std::uint64_t realization_id,
    ure::wave::ComplexAmplitude amplitude_sum,
    double estimator_weight,
    double realization_weight = 1.0) {
    ure::gpu::DistributedComplexFrameStorage result;
    result.width = 1;
    result.height = 1;
    result.wavelengths_m = {550.0e-9};
    const auto phase_identity =
        ure::runtime::identity_digest(
            "wave-phase-origin");
    const auto layout_identity =
        ure::gpu::
            distributed_complex_field_layout_identity(
                result.width,
                result.height,
                result.wavelengths_m);
    const auto semantics =
        ure::gpu::
            make_coherent_realization_semantics(
                phase_identity,
                layout_identity,
                7,
                11,
                realization_id);
    result.shard =
        ure::gpu::make_scheduled_shard_metadata(
            fixture.spectral,
            fixture.frame,
            fixture.resources,
            fixture.schedule,
            shard_index,
            semantics);
    result.total_samples =
        static_cast<int>(
            result.shard.execution.shards[0]
                .sample_count);
    result.amplitude_sums = {amplitude_sum};
    result.estimator_weights = {
        estimator_weight};
    result.realization_weight =
        realization_weight;
    return result;
}

ure::wave::CrossSpectralDensity density_for(
    std::uint64_t realization_id,
    ure::wave::ComplexAmplitude second) {
    const std::vector<ure::wave::WavePoint2D>
        points{
            {0.0, 0.0},
            {1.0e-6, 0.0}};
    const ure::wave::CoherentRealization
        realization{
            realization_id,
            1.0,
            {{1.0, 0.0}, second}};
    return ure::wave::
        estimate_cross_spectral_density(
            550.0e-9,
            points,
            {realization});
}

ure::gpu::DistributedMutualIntensityFrameStorage
mutual_frame(
    const ScheduleFixture& fixture,
    std::size_t shard_index,
    ure::wave::CrossSpectralDensity density) {
    ure::gpu::
        DistributedMutualIntensityFrameStorage
            result;
    const auto& scheduled =
        fixture.schedule.shards[shard_index];
    const ure::gpu::DistributedRealizationRange
        range{
            scheduled.sample_start,
            scheduled.sample_count};
    const auto semantics =
        ure::gpu::
            make_mutual_intensity_semantics(
                ure::runtime::identity_digest(
                    "wave-phase-origin"),
                ure::gpu::
                    distributed_mutual_intensity_layout_identity(
                        density),
                7,
                11,
                range);
    result.shard =
        ure::gpu::make_scheduled_shard_metadata(
            fixture.spectral,
            fixture.frame,
            fixture.resources,
            fixture.schedule,
            shard_index,
            semantics);
    result.total_samples =
        static_cast<int>(scheduled.sample_count);
    result.total_statistical_weight = 1.0;
    result.weighted_density = std::move(density);
    return result;
}

void corrupt_byte(
    const std::filesystem::path& path,
    std::streamoff offset) {
    std::fstream file(
        path,
        std::ios::binary |
            std::ios::in |
            std::ios::out);
    file.seekg(offset);
    char value = 0;
    file.read(&value, 1);
    value ^= 0x20;
    file.seekp(offset);
    file.write(&value, 1);
}

int test_complex_field_merge_and_file() {
    const auto fixture = schedule_fixture(4);
    auto first = complex_frame(
        fixture,
        0,
        42,
        {2.0, 0.0},
        2.0);
    const auto second = complex_frame(
        fixture,
        1,
        42,
        {0.0, 2.0},
        2.0);
    const auto field_semantics =
        ure::gpu::make_complex_field_semantics(
            first.shard.frame_semantics
                .phase_reference_identity,
            first.shard.frame_semantics
                .field_layout_identity,
            7,
            11);
    first.shard.frame_semantics =
        field_semantics;
    first.realization_weight = 0.0;
    auto second_field = second;
    second_field.shard.frame_semantics =
        field_semantics;
    second_field.realization_weight = 0.0;
    check(first.is_valid() && second_field.is_valid(),
          "valid complex field frame rejected");
    auto kind_mismatch = first;
    check(
        throws([&] {
            ure::gpu::merge_complex_field_frame(
                kind_mismatch,
                second);
        }),
        "coherent realization was merged as a generic complex field");
    ure::gpu::merge_complex_field_frame(
        first,
        second_field);
    const auto field =
        first.resolved_amplitude_at(0, 0, 0);
    check(
        std::abs(field.real - 0.5) < 1.0e-12 &&
            std::abs(field.imag - 0.5) <
                1.0e-12 &&
            first.total_samples == 4,
        "complex field was not merged before normalization");

    const auto first_path =
        temporary_path(
            "ure_wave_complex_first.urwf");
    const auto second_path =
        temporary_path(
            "ure_wave_complex_second.urwf");
    const auto output_path =
        temporary_path(
            "ure_wave_complex_output.urwf");
    remove_if_exists(first_path);
    remove_if_exists(second_path);
    remove_if_exists(output_path);
    auto first_shard = complex_frame(
        fixture,
        0,
        42,
        {2.0, 0.0},
        2.0);
    first_shard.shard.frame_semantics =
        field_semantics;
    first_shard.realization_weight = 0.0;
    ure::gpu::write_complex_field_frame_file(
        first_path,
        first_shard);
    ure::gpu::write_complex_field_frame_file(
        second_path,
        second_field);
    const auto loaded =
        ure::gpu::read_complex_field_frame_file(
            first_path);
    check(
        loaded.shard.frame_semantics ==
                first_shard.shard.frame_semantics &&
            loaded.amplitude_sums.size() == 1,
        "complex field file changed semantics");
    ure::gpu::merge_complex_field_frame_files(
        first_path,
        {second_path},
        output_path);
    const auto merged =
        ure::gpu::read_complex_field_frame_file(
            output_path);
    const auto merged_field =
        merged.resolved_amplitude_at(0, 0, 0);
    check(
        std::abs(merged_field.real - 0.5) <
                1.0e-12 &&
            std::abs(merged_field.imag - 0.5) <
                1.0e-12,
        "complex field file merge changed the field");
    corrupt_byte(first_path, 24);
    check(
        throws([&] {
            static_cast<void>(
                ure::gpu::
                    read_complex_field_frame_file(
                        first_path));
        }),
        "corrupted complex field file was accepted");
    check(
        throws([&] {
            static_cast<void>(
                ure::gpu::
                    read_mutual_intensity_frame_file(
                        output_path));
        }),
        "complex field file was reinterpreted as mutual intensity");
    remove_if_exists(first_path);
    remove_if_exists(second_path);
    remove_if_exists(output_path);
    return 0;
}

int test_coherent_before_incoherent_reduction() {
    const auto fixture = schedule_fixture(4);
    auto realization = complex_frame(
        fixture,
        0,
        42,
        {2.0, 0.0},
        2.0);
    const auto continuation = complex_frame(
        fixture,
        1,
        42,
        {0.0, 2.0},
        2.0);
    ure::gpu::merge_complex_field_frame(
        realization,
        continuation);
    ure::gpu::DistributedPartialCoherenceAccumulator
        accumulator;
    accumulator.film =
        ure::wave::make_partial_coherence_film(
            1,
            1,
            {550.0e-9},
            8);
    check(
        ure::gpu::append_coherent_realization(
            accumulator,
            realization),
        "merged coherent realization was not accepted");
    auto other = realization;
    other.shard.frame_semantics =
        ure::gpu::
            make_coherent_realization_semantics(
                realization.shard.frame_semantics
                    .phase_reference_identity,
                realization.shard.frame_semantics
                    .field_layout_identity,
                7,
                11,
                43);
    other.amplitude_sums = {{4.0, 0.0}};
    check(
        ure::gpu::append_coherent_realization(
            accumulator,
            other),
        "second coherent realization was not accepted");
    check(
        std::abs(
            accumulator.film.resolved_power_at(
                0,
                0,
                0) -
            0.75) < 1.0e-12,
        "realization power was not averaged after coherent merge");
    check(
        !ure::gpu::append_coherent_realization(
            accumulator,
            other),
        "duplicate coherent realization was accepted");
    auto wrong_phase = other;
    wrong_phase.shard.frame_semantics.realization_id =
        44;
    wrong_phase.shard.frame_semantics
        .realization_ranges[0].start = 44;
    wrong_phase.shard.frame_semantics
        .phase_reference_identity =
        ure::runtime::identity_digest(
            "different-phase-origin");
    check(
        !ure::gpu::append_coherent_realization(
            accumulator,
            wrong_phase),
        "incompatible phase reference entered one coherence group");
    return 0;
}

int test_mutual_intensity_merge_and_file() {
    const auto fixture = schedule_fixture(2);
    auto first = mutual_frame(
        fixture,
        0,
        density_for(0, {1.0, 0.0}));
    const auto second = mutual_frame(
        fixture,
        1,
        density_for(1, {-1.0, 0.0}));
    check(first.is_valid() && second.is_valid(),
          "valid mutual-intensity frame rejected");
    auto merged = first;
    ure::gpu::merge_mutual_intensity_frame(
        merged,
        second);
    const auto density = merged.resolved_density();
    check(
        density.is_valid(1.0e-8) &&
            std::abs(density.at(0, 0).real - 1.0) <
                1.0e-12 &&
            std::abs(density.at(1, 1).real - 1.0) <
                1.0e-12 &&
            std::abs(density.at(0, 1).real) <
                1.0e-12,
        "mutual-intensity realizations were not reduced correctly");
    const auto snapshot = merged;
    check(
        throws([&] {
            ure::gpu::merge_mutual_intensity_frame(
                merged,
                first);
        }),
        "overlapping realization range was accepted");
    check(
        merged.total_samples == snapshot.total_samples &&
            merged.weighted_density.values[1].real ==
                snapshot.weighted_density.values[1]
                    .real,
        "rejected mutual-intensity merge was not transactional");

    const auto first_path =
        temporary_path(
            "ure_wave_mutual_first.urwm");
    const auto second_path =
        temporary_path(
            "ure_wave_mutual_second.urwm");
    const auto output_path =
        temporary_path(
            "ure_wave_mutual_output.urwm");
    remove_if_exists(first_path);
    remove_if_exists(second_path);
    remove_if_exists(output_path);
    ure::gpu::write_mutual_intensity_frame_file(
        first_path,
        first);
    ure::gpu::write_mutual_intensity_frame_file(
        second_path,
        second);
    ure::gpu::merge_mutual_intensity_frame_files(
        first_path,
        {second_path},
        output_path);
    const auto loaded =
        ure::gpu::read_mutual_intensity_frame_file(
            output_path);
    const auto loaded_density =
        loaded.resolved_density();
    check(
        loaded.total_samples == 2 &&
            loaded.shard.frame_semantics
                    .realization_ranges.size() ==
                2 &&
            std::abs(
                loaded_density.at(0, 1).real) <
                1.0e-12,
        "mutual-intensity file merge changed statistics");
    remove_if_exists(first_path);
    remove_if_exists(second_path);
    remove_if_exists(output_path);
    return 0;
}

int test_radiance_separation_and_invalid_inputs() {
    const auto fixture = schedule_fixture(4);
    const auto coherent = complex_frame(
        fixture,
        0,
        9,
        {1.0, 0.0},
        1.0);
    float rgb[3]{};
    ure::gpu::DistributedFrameBuffer framebuffer{
        1,
        1,
        coherent.total_samples,
        rgb,
        coherent.shard,
        {}};
    const auto path =
        temporary_path(
            "ure_wave_as_radiance.urf");
    remove_if_exists(path);
    check(
        throws([&] {
            ure::gpu::write_framebuffer_file(
                path,
                framebuffer);
        }),
        "coherent field was serialized as RGB radiance");
    auto invalid = coherent;
    invalid.shard.frame_semantics
        .field_layout_identity[0] ^= 1;
    check(
        !invalid.is_valid(),
        "forged field layout identity was accepted");
    invalid = coherent;
    invalid.estimator_weights[0] = 0.0;
    check(
        !invalid.is_valid(),
        "nonzero amplitude without estimator weight was accepted");
    remove_if_exists(path);
    return 0;
}

}

int main() {
    test_complex_field_merge_and_file();
    test_coherent_before_incoherent_reduction();
    test_mutual_intensity_merge_and_file();
    test_radiance_separation_and_invalid_inputs();
    if (failures == 0) {
        std::cout
            << "Distributed wave frame tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
