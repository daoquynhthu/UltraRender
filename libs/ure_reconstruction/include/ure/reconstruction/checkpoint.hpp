#pragma once

#include "ure/reconstruction/measurement.hpp"
#include "ure/research/artifact.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ure::reconstruction {

inline constexpr std::uint32_t kMeasurementCheckpointVersion = 1;

struct MeasurementCheckpointIndex {
    research::ArtifactIndex artifact;
    std::size_t metadata_chunk = 0;
    std::vector<std::size_t> plane_chunks;
};

std::vector<std::uint8_t> write_measurement_checkpoint(
    const MeasurementBundle& bundle,
    const research::ArtifactLimits& limits = {});

MeasurementCheckpointIndex inspect_measurement_checkpoint(
    std::span<const std::uint8_t> index_bytes,
    std::uint64_t total_container_bytes,
    const research::ArtifactLimits& limits = {});

MeasurementPlane read_measurement_checkpoint_plane(
    std::span<const std::uint8_t> stored_payload,
    const MeasurementCheckpointIndex& index,
    std::size_t plane_ordinal,
    const MeasurementSchema& expected_schema,
    const research::ArtifactLimits& limits = {});

MeasurementBundle read_measurement_checkpoint(
    std::span<const std::uint8_t> bytes,
    const research::ArtifactLimits& limits = {});

}
