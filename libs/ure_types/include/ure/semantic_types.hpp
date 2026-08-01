#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace ure::semantic {

using IdentityDigest = std::array<std::uint8_t, 32>;

inline bool identity_empty(const IdentityDigest& identity) {
    for (const std::uint8_t value : identity) {
        if (value != 0) return false;
    }
    return true;
}

enum class IdentityKind : std::uint8_t {
    WorldDefinition,
    WorldState,
    TimeSample,
    ObservationSnapshot,
    TechniqueGraph,
    MeasurementSchema,
    ParameterSet,
    SolverSemantics,
    Evidence
};

struct ProvenanceIdentitySet {
    IdentityDigest world_definition = {};
    IdentityDigest world_state = {};
    IdentityDigest time_sample = {};
    IdentityDigest observation_snapshot = {};
    IdentityDigest technique_graph = {};
    IdentityDigest measurement_schema = {};
    IdentityDigest parameter_set = {};
    IdentityDigest solver_semantics = {};
    IdentityDigest evidence = {};
};

struct DimensionVector {
    std::int8_t length = 0;
    std::int8_t mass = 0;
    std::int8_t time = 0;
    std::int8_t electric_current = 0;
    std::int8_t temperature = 0;
    std::int8_t amount = 0;
    std::int8_t luminous_intensity = 0;

    bool operator==(const DimensionVector&) const = default;
};

struct UnitDescriptor {
    DimensionVector dimension = {};
    double scale_to_si = 1.0;
    double offset_to_si = 0.0;
    bool affine = false;

    bool operator==(const UnitDescriptor&) const = default;
};

inline bool valid_unit(const UnitDescriptor& unit) {
    return std::isfinite(unit.scale_to_si) && unit.scale_to_si > 0.0 &&
           std::isfinite(unit.offset_to_si) &&
           (unit.affine || unit.offset_to_si == 0.0);
}

inline bool same_dimension(const UnitDescriptor& left,
                           const UnitDescriptor& right) {
    return left.dimension == right.dimension;
}

inline bool same_unit_mapping(const UnitDescriptor& left,
                              const UnitDescriptor& right) {
    return left == right;
}

struct TimeBasis {
    std::uint64_t ticks_per_second = 1;
    std::int64_t synchronization_epoch = 0;
    IdentityDigest clock_identity = {};

    bool operator==(const TimeBasis&) const = default;
};

struct TimeInterval {
    TimeBasis basis = {};
    std::int64_t start_tick = 0;
    std::int64_t end_tick = 0;

    bool operator==(const TimeInterval&) const = default;
};

inline bool valid_time_basis(const TimeBasis& basis) {
    return basis.ticks_per_second != 0 &&
           !identity_empty(basis.clock_identity);
}

inline bool valid_time_interval(const TimeInterval& interval) {
    return valid_time_basis(interval.basis) &&
           interval.end_tick >= interval.start_tick;
}

inline bool same_time_basis(const TimeBasis& left,
                            const TimeBasis& right) {
    return left == right;
}

}
