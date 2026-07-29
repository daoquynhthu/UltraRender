#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include "ure/local_fullwave.hpp"
#include "ure/native_scene_hash.hpp"
#include "ure/wave_optics.hpp"

namespace ure::wave {

namespace {

constexpr std::uint32_t kRequestMagic = 0x57524655U;
constexpr std::uint32_t kArtifactMagic = 0x57414655U;
constexpr std::uint32_t kFormatVersion = 1;
constexpr std::size_t kMaxTextBytes = 1024;

bool valid_digest(std::string_view value) {
    return value.size() == 64 &&
           std::ranges::all_of(
               value,
               [](char character) {
                   return (character >= '0' &&
                           character <= '9') ||
                          (character >= 'a' &&
                           character <= 'f');
               });
}

bool valid_id(std::string_view value) {
    return !value.empty() &&
           value.size() <= 128 &&
           std::ranges::all_of(
               value,
               [](char character) {
                   return
                       (character >= 'a' &&
                        character <= 'z') ||
                       (character >= 'A' &&
                        character <= 'Z') ||
                       (character >= '0' &&
                        character <= '9') ||
                       character == '.' ||
                       character == '_' ||
                       character == '-' ||
                       character == '/';
               });
}

bool valid_solver_kind(LocalFullWaveSolverKind kind) {
    return kind >= LocalFullWaveSolverKind::Rcwa &&
           kind <=
               LocalFullWaveSolverKind::SMatrixImport;
}

bool checked_add(
    std::size_t first,
    std::size_t second,
    std::size_t& result) {
    if (second >
        std::numeric_limits<std::size_t>::max() -
            first) {
        return false;
    }
    result = first + second;
    return true;
}

bool checked_multiply(
    std::size_t first,
    std::size_t second,
    std::size_t& result) {
    if (first != 0 &&
        second >
            std::numeric_limits<std::size_t>::max() /
                first) {
        return false;
    }
    result = first * second;
    return true;
}

std::size_t expected_entry_count(
    const LocalFullWaveRequest& request) {
    const std::size_t order_count =
        static_cast<std::size_t>(
            request.maximum_order -
            request.minimum_order +
            1);
    const std::size_t side_count =
        static_cast<std::size_t>(
            request.reflection) +
        static_cast<std::size_t>(
            request.transmission);
    std::size_t count = 0;
    if (!checked_multiply(
            request.wavelengths_nm.size(),
            request.incident_cosines.size(),
            count) ||
        !checked_multiply(
            count,
            order_count,
            count) ||
        !checked_multiply(
            count,
            side_count,
            count)) {
        return 0;
    }
    return count;
}

class Writer {
public:
    void u8(std::uint8_t value) {
        bytes_.push_back(value);
    }

    void u32(std::uint32_t value) {
        scalar(value);
    }

    void i32(std::int32_t value) {
        scalar(std::bit_cast<std::uint32_t>(value));
    }

    void u64(std::uint64_t value) {
        scalar(value);
    }

    void f32(float value) {
        scalar(std::bit_cast<std::uint32_t>(value));
    }

    void f64(double value) {
        scalar(std::bit_cast<std::uint64_t>(value));
    }

    void string(std::string_view value) {
        if (value.size() > kMaxTextBytes) {
            throw std::length_error(
                "Local full-wave string is too large");
        }
        u32(static_cast<std::uint32_t>(value.size()));
        bytes_.insert(
            bytes_.end(),
            value.begin(),
            value.end());
    }

    void data(std::span<const std::uint8_t> value) {
        if (value.size() >
            std::numeric_limits<std::uint32_t>::max()) {
            throw std::length_error(
                "Local full-wave payload is too large");
        }
        u32(static_cast<std::uint32_t>(value.size()));
        bytes_.insert(
            bytes_.end(),
            value.begin(),
            value.end());
    }

    std::vector<std::uint8_t> take() {
        return std::move(bytes_);
    }

private:
    template <typename Value>
    void scalar(Value value) {
        using Unsigned = std::make_unsigned_t<Value>;
        const Unsigned encoded =
            static_cast<Unsigned>(value);
        for (std::size_t index = 0;
             index < sizeof(Value);
             ++index) {
            bytes_.push_back(
                static_cast<std::uint8_t>(
                    encoded >> (8 * index)));
        }
    }

    std::vector<std::uint8_t> bytes_;
};

class Reader {
public:
    explicit Reader(
        std::span<const std::uint8_t> bytes)
        : bytes_(bytes) {}

    std::uint8_t u8() {
        require(1);
        return bytes_[offset_++];
    }

    std::uint32_t u32() {
        return scalar<std::uint32_t>();
    }

    std::int32_t i32() {
        return std::bit_cast<std::int32_t>(
            scalar<std::uint32_t>());
    }

    std::uint64_t u64() {
        return scalar<std::uint64_t>();
    }

    float f32() {
        return std::bit_cast<float>(u32());
    }

    double f64() {
        return std::bit_cast<double>(u64());
    }

    std::string string(
        std::size_t maximum = kMaxTextBytes) {
        const std::size_t size = u32();
        if (size > maximum) {
            throw std::length_error(
                "Local full-wave string exceeds its bound");
        }
        require(size);
        std::string result(
            reinterpret_cast<const char*>(
                bytes_.data() + offset_),
            size);
        offset_ += size;
        return result;
    }

    std::vector<std::uint8_t> data(
        std::size_t maximum) {
        const std::size_t size = u32();
        if (size > maximum) {
            throw std::length_error(
                "Local full-wave payload exceeds its bound");
        }
        require(size);
        std::vector<std::uint8_t> result(
            bytes_.begin() +
                static_cast<std::ptrdiff_t>(offset_),
            bytes_.begin() +
                static_cast<std::ptrdiff_t>(
                    offset_ + size));
        offset_ += size;
        return result;
    }

    bool empty() const {
        return offset_ == bytes_.size();
    }

private:
    void require(std::size_t size) {
        if (size > bytes_.size() - offset_) {
            throw std::invalid_argument(
                "Truncated local full-wave payload");
        }
    }

    template <typename Value>
    Value scalar() {
        using Unsigned = std::make_unsigned_t<Value>;
        require(sizeof(Value));
        Unsigned value = 0;
        for (std::size_t index = 0;
             index < sizeof(Value);
             ++index) {
            value |=
                static_cast<Unsigned>(
                    bytes_[offset_++])
                << (8 * index);
        }
        return static_cast<Value>(value);
    }

    std::span<const std::uint8_t> bytes_;
    std::size_t offset_ = 0;
};

void write_version(
    Writer& writer,
    const LocalFullWaveVersion& version) {
    writer.u32(version.major);
    writer.u32(version.minor);
    writer.u32(version.patch);
}

LocalFullWaveVersion read_version(Reader& reader) {
    return {
        reader.u32(),
        reader.u32(),
        reader.u32()};
}

void write_request_body(
    Writer& writer,
    const LocalFullWaveRequest& request) {
    writer.string(request.request_id);
    writer.string(request.provider_id);
    write_version(
        writer,
        request.minimum_provider_version);
    writer.u8(
        static_cast<std::uint8_t>(
            request.solver_kind));
    writer.u8(
        static_cast<std::uint8_t>(
            request.polarization_basis));
    writer.string(request.geometry_digest);
    writer.string(request.material_digest);
    writer.data(request.geometry_payload);
    writer.data(request.material_payload);
    writer.u32(
        static_cast<std::uint32_t>(
            request.wavelengths_nm.size()));
    for (const float value : request.wavelengths_nm) {
        writer.f32(value);
    }
    writer.u32(
        static_cast<std::uint32_t>(
            request.incident_cosines.size()));
    for (const float value :
         request.incident_cosines) {
        writer.f32(value);
    }
    writer.i32(request.minimum_order);
    writer.i32(request.maximum_order);
    writer.u8(
        static_cast<std::uint8_t>(
            request.reflection));
    writer.u8(
        static_cast<std::uint8_t>(
            request.transmission));
    writer.f64(request.period_m);
    writer.f64(request.orientation_rad);
    writer.f64(request.tolerance);
    writer.u64(request.memory_budget_bytes);
    writer.u64(request.iteration_budget);
    writer.u8(
        static_cast<std::uint8_t>(
            request.deterministic_required));
}

LocalFullWaveRequest read_request_body(
    Reader& reader) {
    LocalFullWaveRequest request;
    request.request_id = reader.string();
    request.provider_id = reader.string();
    request.minimum_provider_version =
        read_version(reader);
    request.solver_kind =
        static_cast<LocalFullWaveSolverKind>(
            reader.u8());
    request.polarization_basis =
        static_cast<
            LocalFullWavePolarizationBasis>(
            reader.u8());
    request.geometry_digest = reader.string();
    request.material_digest = reader.string();
    request.geometry_payload =
        reader.data(kMaxLocalFullWaveInputBytes);
    const std::size_t remaining_budget =
        kMaxLocalFullWaveInputBytes -
        request.geometry_payload.size();
    request.material_payload =
        reader.data(remaining_budget);
    const std::size_t wavelength_count =
        reader.u32();
    if (wavelength_count >
        scene_ir::kMaxDiffractiveScatteringEntries) {
        throw std::length_error(
            "Local full-wave wavelength grid exceeds its bound");
    }
    request.wavelengths_nm.resize(
        wavelength_count);
    for (auto& value : request.wavelengths_nm) {
        value = reader.f32();
    }
    const std::size_t incidence_count =
        reader.u32();
    if (incidence_count >
        scene_ir::kMaxDiffractiveScatteringEntries) {
        throw std::length_error(
            "Local full-wave incidence grid exceeds its bound");
    }
    request.incident_cosines.resize(
        incidence_count);
    for (auto& value :
         request.incident_cosines) {
        value = reader.f32();
    }
    request.minimum_order = reader.i32();
    request.maximum_order = reader.i32();
    request.reflection = reader.u8() != 0;
    request.transmission = reader.u8() != 0;
    request.period_m = reader.f64();
    request.orientation_rad = reader.f64();
    request.tolerance = reader.f64();
    request.memory_budget_bytes = reader.u64();
    request.iteration_budget = reader.u64();
    request.deterministic_required =
        reader.u8() != 0;
    return request;
}

void write_coefficient(
    Writer& writer,
    const scene_ir::ComplexCoefficient& value) {
    writer.f32(value.real);
    writer.f32(value.imag);
}

scene_ir::ComplexCoefficient read_coefficient(
    Reader& reader) {
    return {
        reader.f32(),
        reader.f32()};
}

void write_scattering(
    Writer& writer,
    const scene_ir::DiffractiveOperator& value) {
    writer.u8(
        static_cast<std::uint8_t>(value.kind));
    writer.u8(
        static_cast<std::uint8_t>(value.side));
    writer.f64(value.period_m);
    writer.f64(value.orientation_rad);
    writer.f64(value.duty_cycle);
    writer.f64(value.phase_depth_rad);
    writer.f64(value.design_wavelength_nm);
    writer.f64(value.focal_length_m);
    writer.f64(value.aperture_radius_m);
    writer.i32(value.max_order);
    writer.string(value.table_id);
    writer.u32(
        static_cast<std::uint32_t>(
            value.table.size()));
    for (const auto& entry : value.table) {
        writer.f32(entry.wavelength_nm);
        writer.f32(entry.incident_cosine);
        writer.i32(entry.order);
        writer.u8(
            static_cast<std::uint8_t>(
                entry.side));
        write_coefficient(writer, entry.jones_ss);
        write_coefficient(writer, entry.jones_sp);
        write_coefficient(writer, entry.jones_ps);
        write_coefficient(writer, entry.jones_pp);
    }
}

scene_ir::DiffractiveOperator read_scattering(
    Reader& reader) {
    scene_ir::DiffractiveOperator value;
    value.kind =
        static_cast<
            scene_ir::DiffractiveOperatorKind>(
            reader.u8());
    value.side =
        static_cast<
            scene_ir::DiffractiveScatterSide>(
            reader.u8());
    value.period_m = reader.f64();
    value.orientation_rad = reader.f64();
    value.duty_cycle = reader.f64();
    value.phase_depth_rad = reader.f64();
    value.design_wavelength_nm = reader.f64();
    value.focal_length_m = reader.f64();
    value.aperture_radius_m = reader.f64();
    value.max_order = reader.i32();
    value.table_id = reader.string();
    const std::size_t entry_count = reader.u32();
    if (entry_count >
        scene_ir::kMaxDiffractiveScatteringEntries) {
        throw std::length_error(
            "Local full-wave scattering table exceeds its bound");
    }
    value.table.resize(entry_count);
    for (auto& entry : value.table) {
        entry.wavelength_nm = reader.f32();
        entry.incident_cosine = reader.f32();
        entry.order = reader.i32();
        entry.side =
            static_cast<
                scene_ir::DiffractiveScatterSide>(
                reader.u8());
        entry.jones_ss = read_coefficient(reader);
        entry.jones_sp = read_coefficient(reader);
        entry.jones_ps = read_coefficient(reader);
        entry.jones_pp = read_coefficient(reader);
    }
    return value;
}

void write_artifact_body(
    Writer& writer,
    const LocalFullWaveArtifact& artifact) {
    writer.string(artifact.schema_identity);
    writer.string(artifact.request_digest);
    writer.string(artifact.provider_id);
    write_version(writer, artifact.provider_version);
    writer.string(
        artifact.provider_executable_digest);
    writer.string(
        artifact.provider_semantic_digest);
    write_scattering(writer, artifact.scattering);
    writer.u8(
        static_cast<std::uint8_t>(
            artifact.evidence.converged));
    writer.u64(artifact.evidence.iterations);
    writer.u64(
        artifact.evidence.peak_memory_bytes);
    writer.f64(artifact.evidence.residual);
    writer.f64(
        artifact.evidence.reciprocity_error);
    writer.f64(artifact.evidence.energy_error);
    writer.string(
        artifact.evidence.
            solver_artifact_digest);
}

std::string digest(
    std::span<const std::uint8_t> bytes) {
    return native_scene::sha256_hex(bytes);
}

std::string content_digest(
    const LocalFullWaveArtifact& artifact) {
    Writer writer;
    write_artifact_body(writer, artifact);
    return digest(writer.take());
}

bool exact_grid_match(
    const LocalFullWaveArtifact& artifact,
    const LocalFullWaveRequest& request) {
    using Key = std::tuple<float, float, int, int>;
    std::set<Key> actual;
    for (const auto& entry :
         artifact.scattering.table) {
        actual.emplace(
            entry.wavelength_nm,
            entry.incident_cosine,
            entry.order,
            static_cast<int>(entry.side));
    }
    std::set<Key> expected;
    for (const float wavelength :
         request.wavelengths_nm) {
        for (const float cosine :
             request.incident_cosines) {
            for (int order = request.minimum_order;
                 order <= request.maximum_order;
                 ++order) {
                if (request.reflection) {
                    expected.emplace(
                        wavelength,
                        cosine,
                        order,
                        static_cast<int>(
                            scene_ir::
                                DiffractiveScatterSide::
                                    Reflection));
                }
                if (request.transmission) {
                    expected.emplace(
                        wavelength,
                        cosine,
                        order,
                        static_cast<int>(
                            scene_ir::
                                DiffractiveScatterSide::
                                    Transmission));
                }
            }
        }
    }
    return actual == expected;
}

bool structurally_valid(
    const LocalFullWaveArtifact& artifact) {
    return artifact.schema_identity ==
               "ure.local-fullwave.scattering/1.0" &&
           valid_digest(artifact.content_digest) &&
           valid_digest(artifact.request_digest) &&
           valid_id(artifact.provider_id) &&
           valid_digest(
               artifact.provider_executable_digest) &&
           valid_digest(
               artifact.provider_semantic_digest) &&
           valid_digest(
               artifact.evidence.
                   solver_artifact_digest) &&
           artifact.scattering.table_id.size() <=
               kMaxTextBytes &&
           std::isfinite(artifact.evidence.residual) &&
           artifact.evidence.residual >= 0.0 &&
           std::isfinite(
               artifact.evidence.reciprocity_error) &&
           artifact.evidence.reciprocity_error >= 0.0 &&
           std::isfinite(
               artifact.evidence.energy_error) &&
           artifact.evidence.energy_error >= 0.0 &&
           ure::wave::is_valid(
               artifact.scattering) &&
           content_digest(artifact) ==
               artifact.content_digest;
}

}

bool is_valid(const LocalFullWaveRequest& request) {
    std::size_t input_bytes = 0;
    if (!valid_id(request.request_id) ||
        !valid_id(request.provider_id) ||
        !valid_solver_kind(request.solver_kind) ||
        request.polarization_basis !=
            LocalFullWavePolarizationBasis::SP ||
        !valid_digest(request.geometry_digest) ||
        !valid_digest(request.material_digest) ||
        request.geometry_payload.empty() ||
        request.material_payload.empty() ||
        !checked_add(
            request.geometry_payload.size(),
            request.material_payload.size(),
            input_bytes) ||
        input_bytes > kMaxLocalFullWaveInputBytes ||
        digest(request.geometry_payload) !=
            request.geometry_digest ||
        digest(request.material_payload) !=
            request.material_digest ||
        request.wavelengths_nm.empty() ||
        request.incident_cosines.empty() ||
        request.minimum_order < -16 ||
        request.maximum_order > 16 ||
        request.minimum_order >
            request.maximum_order ||
        (!request.reflection &&
         !request.transmission) ||
        !std::isfinite(request.period_m) ||
        request.period_m <= 0.0 ||
        !std::isfinite(request.orientation_rad) ||
        !std::isfinite(request.tolerance) ||
        request.tolerance <= 0.0 ||
        request.tolerance > 0.1 ||
        request.memory_budget_bytes == 0 ||
        request.iteration_budget == 0) {
        return false;
    }
    for (std::size_t index = 0;
         index < request.wavelengths_nm.size();
         ++index) {
        const float value =
            request.wavelengths_nm[index];
        if (!std::isfinite(value) ||
            value <= 0.0f ||
            (index > 0 &&
             value <=
                 request.wavelengths_nm[index - 1])) {
            return false;
        }
    }
    for (std::size_t index = 0;
         index < request.incident_cosines.size();
         ++index) {
        const float value =
            request.incident_cosines[index];
        if (!std::isfinite(value) ||
            value < 0.0f ||
            value > 1.0f ||
            (index > 0 &&
             value <=
                 request.incident_cosines[index - 1])) {
            return false;
        }
    }
    const std::size_t entries =
        expected_entry_count(request);
    return entries > 0 &&
           entries <=
               scene_ir::
                   kMaxDiffractiveScatteringEntries;
}

bool is_valid(
    const LocalFullWaveProviderDescriptor& descriptor) {
    if (!valid_id(descriptor.provider_id) ||
        descriptor.version.major == 0 ||
        !valid_digest(descriptor.executable_digest) ||
        !valid_digest(descriptor.semantic_digest) ||
        descriptor.solver_kinds.empty() ||
        descriptor.maximum_wavelength_samples == 0 ||
        descriptor.maximum_incidence_samples == 0 ||
        descriptor.maximum_scattering_entries == 0 ||
        descriptor.maximum_scattering_entries >
            scene_ir::
                kMaxDiffractiveScatteringEntries ||
        descriptor.maximum_memory_bytes == 0) {
        return false;
    }
    std::set<LocalFullWaveSolverKind> kinds;
    for (const auto kind : descriptor.solver_kinds) {
        if (!valid_solver_kind(kind) ||
            !kinds.insert(kind).second) {
            return false;
        }
    }
    return true;
}

bool is_valid(
    const LocalFullWaveArtifact& artifact,
    const LocalFullWaveRequest& request,
    const LocalFullWaveProviderDescriptor& descriptor) {
    const double error_bound =
        std::max(1.0e-4, 10.0 * request.tolerance);
    const bool import =
        request.solver_kind ==
        LocalFullWaveSolverKind::SMatrixImport;
    return is_valid(request) &&
           is_valid(descriptor) &&
           structurally_valid(artifact) &&
           artifact.schema_identity ==
               "ure.local-fullwave.scattering/1.0" &&
           artifact.request_digest ==
               local_fullwave_request_digest(request) &&
           artifact.provider_id ==
               descriptor.provider_id &&
           artifact.provider_version ==
               descriptor.version &&
           artifact.provider_executable_digest ==
               descriptor.executable_digest &&
           artifact.provider_semantic_digest ==
               descriptor.semantic_digest &&
           artifact.scattering.kind ==
               scene_ir::
                   DiffractiveOperatorKind::
                       ScatteringTable &&
           artifact.scattering.period_m ==
               request.period_m &&
           artifact.scattering.orientation_rad ==
               request.orientation_rad &&
           artifact.scattering.max_order ==
               std::max(
                   std::abs(request.minimum_order),
                   std::abs(request.maximum_order)) &&
           artifact.scattering.table.size() ==
               expected_entry_count(request) &&
           exact_grid_match(artifact, request) &&
           ure::wave::is_valid(
               artifact.scattering) &&
           artifact.evidence.converged &&
           (import ||
            (artifact.evidence.iterations > 0 &&
             artifact.evidence.iterations <=
                 request.iteration_budget)) &&
           artifact.evidence.peak_memory_bytes <=
               request.memory_budget_bytes &&
           artifact.evidence.peak_memory_bytes <=
               descriptor.maximum_memory_bytes &&
           std::isfinite(artifact.evidence.residual) &&
           artifact.evidence.residual >= 0.0 &&
           artifact.evidence.residual <=
               request.tolerance &&
           std::isfinite(
               artifact.evidence.reciprocity_error) &&
           artifact.evidence.reciprocity_error >= 0.0 &&
           artifact.evidence.reciprocity_error <=
               error_bound &&
           std::isfinite(
               artifact.evidence.energy_error) &&
           artifact.evidence.energy_error >= 0.0 &&
           artifact.evidence.energy_error <=
               error_bound &&
           valid_digest(
               artifact.evidence.
                   solver_artifact_digest);
}

std::vector<std::uint8_t>
write_local_fullwave_request(
    const LocalFullWaveRequest& request) {
    if (!is_valid(request)) {
        throw std::invalid_argument(
            "Invalid local full-wave request");
    }
    Writer writer;
    writer.u32(kRequestMagic);
    writer.u32(kFormatVersion);
    write_request_body(writer, request);
    auto bytes = writer.take();
    if (bytes.size() >
        kMaxLocalFullWaveArtifactBytes) {
        throw std::length_error(
            "Local full-wave request serialization exceeds its bound");
    }
    return bytes;
}

LocalFullWaveRequest read_local_fullwave_request(
    std::span<const std::uint8_t> bytes) {
    if (bytes.size() >
        kMaxLocalFullWaveArtifactBytes) {
        throw std::length_error(
            "Local full-wave request exceeds its bound");
    }
    Reader reader(bytes);
    if (reader.u32() != kRequestMagic ||
        reader.u32() != kFormatVersion) {
        throw std::invalid_argument(
            "Invalid local full-wave request envelope");
    }
    auto request = read_request_body(reader);
    if (!reader.empty() || !is_valid(request)) {
        throw std::invalid_argument(
            "Invalid local full-wave request payload");
    }
    return request;
}

std::string local_fullwave_request_digest(
    const LocalFullWaveRequest& request) {
    if (!is_valid(request)) return {};
    auto semantic = request;
    semantic.request_id = "semantic";
    return digest(
        write_local_fullwave_request(semantic));
}

std::string local_fullwave_cache_key(
    const LocalFullWaveRequest& request,
    const LocalFullWaveProviderDescriptor& descriptor) {
    if (!is_valid(request) ||
        !is_valid(descriptor)) {
        return {};
    }
    Writer writer;
    writer.string(
        local_fullwave_request_digest(request));
    writer.string(descriptor.provider_id);
    write_version(writer, descriptor.version);
    writer.string(descriptor.executable_digest);
    writer.string(descriptor.semantic_digest);
    return digest(writer.take());
}

std::vector<std::uint8_t>
write_local_fullwave_artifact(
    const LocalFullWaveArtifact& artifact) {
    if (!structurally_valid(artifact)) {
        throw std::invalid_argument(
            "Invalid local full-wave artifact");
    }
    Writer writer;
    writer.u32(kArtifactMagic);
    writer.u32(kFormatVersion);
    write_artifact_body(writer, artifact);
    writer.string(artifact.content_digest);
    auto bytes = writer.take();
    if (bytes.size() >
        kMaxLocalFullWaveArtifactBytes) {
        throw std::length_error(
            "Local full-wave artifact serialization exceeds its bound");
    }
    return bytes;
}

LocalFullWaveArtifact read_local_fullwave_artifact(
    std::span<const std::uint8_t> bytes) {
    if (bytes.size() >
        kMaxLocalFullWaveArtifactBytes) {
        throw std::length_error(
            "Local full-wave artifact exceeds its bound");
    }
    Reader reader(bytes);
    if (reader.u32() != kArtifactMagic ||
        reader.u32() != kFormatVersion) {
        throw std::invalid_argument(
            "Invalid local full-wave artifact envelope");
    }
    LocalFullWaveArtifact artifact;
    artifact.schema_identity = reader.string();
    artifact.request_digest = reader.string();
    artifact.provider_id = reader.string();
    artifact.provider_version = read_version(reader);
    artifact.provider_executable_digest =
        reader.string();
    artifact.provider_semantic_digest =
        reader.string();
    artifact.scattering = read_scattering(reader);
    artifact.evidence.converged =
        reader.u8() != 0;
    artifact.evidence.iterations = reader.u64();
    artifact.evidence.peak_memory_bytes =
        reader.u64();
    artifact.evidence.residual = reader.f64();
    artifact.evidence.reciprocity_error =
        reader.f64();
    artifact.evidence.energy_error =
        reader.f64();
    artifact.evidence.solver_artifact_digest =
        reader.string();
    artifact.content_digest = reader.string();
    if (!reader.empty() ||
        !structurally_valid(artifact)) {
        throw std::invalid_argument(
            "Invalid local full-wave artifact payload");
    }
    return artifact;
}

LocalFullWaveArtifact make_local_fullwave_artifact(
    const LocalFullWaveRequest& request,
    const LocalFullWaveProviderDescriptor& descriptor,
    scene_ir::DiffractiveOperator scattering,
    LocalFullWaveEvidence evidence) {
    LocalFullWaveArtifact artifact;
    artifact.request_digest =
        local_fullwave_request_digest(request);
    artifact.provider_id = descriptor.provider_id;
    artifact.provider_version = descriptor.version;
    artifact.provider_executable_digest =
        descriptor.executable_digest;
    artifact.provider_semantic_digest =
        descriptor.semantic_digest;
    artifact.scattering = std::move(scattering);
    artifact.evidence = std::move(evidence);
    artifact.content_digest =
        content_digest(artifact);
    if (!is_valid(artifact, request, descriptor)) {
        return {};
    }
    return artifact;
}

LocalFullWaveCache::LocalFullWaveCache(
    std::size_t byte_budget)
    : byte_budget_(byte_budget) {
    if (byte_budget_ == 0 ||
        byte_budget_ >
            kMaxLocalFullWaveArtifactBytes) {
        throw std::invalid_argument(
            "Invalid local full-wave cache budget");
    }
}

std::size_t LocalFullWaveCache::byte_budget() const {
    return byte_budget_;
}

std::size_t LocalFullWaveCache::resident_bytes() const {
    return resident_bytes_;
}

std::size_t LocalFullWaveCache::entry_count() const {
    return entries_.size();
}

std::optional<LocalFullWaveArtifact>
LocalFullWaveCache::find(
    const LocalFullWaveRequest& request,
    const LocalFullWaveProviderDescriptor&
        descriptor) const {
    const std::string cache_key =
        local_fullwave_cache_key(
            request,
            descriptor);
    if (cache_key.empty()) {
        return std::nullopt;
    }
    const auto found = entries_.find(cache_key);
    if (found == entries_.end()) {
        return std::nullopt;
    }
    if (!is_valid(
            found->second.artifact,
            request,
            descriptor)) {
        throw std::runtime_error(
            "Local full-wave cache contains an incompatible artifact");
    }
    return found->second.artifact;
}

bool LocalFullWaveCache::insert(
    const LocalFullWaveRequest& request,
    const LocalFullWaveProviderDescriptor&
        descriptor,
    const LocalFullWaveArtifact& artifact) {
    if (!is_valid(artifact, request, descriptor)) {
        return false;
    }
    const std::string cache_key =
        local_fullwave_cache_key(
            request,
            descriptor);
    if (cache_key.empty()) return false;
    const auto bytes =
        write_local_fullwave_artifact(artifact);
    LocalFullWaveArtifact decoded;
    try {
        decoded =
            read_local_fullwave_artifact(bytes);
    } catch (const std::exception&) {
        return false;
    }
    const auto found = entries_.find(cache_key);
    if (found != entries_.end()) {
        return found->second.byte_size ==
                   bytes.size() &&
               write_local_fullwave_artifact(
                   found->second.artifact) ==
                   bytes;
    }
    if (bytes.size() >
        byte_budget_ - resident_bytes_) {
        return false;
    }
    entries_.emplace(
        cache_key,
        Entry{std::move(decoded), bytes.size()});
    resident_bytes_ += bytes.size();
    return true;
}

bool LocalFullWaveRegistry::register_provider(
    LocalFullWaveProvider provider) {
    if (!is_valid(provider.descriptor) ||
        !provider.invoke) {
        return false;
    }
    const std::string id =
        provider.descriptor.provider_id;
    return providers_.emplace(
        id,
        std::move(provider))
        .second;
}

bool LocalFullWaveRegistry::has_provider(
    const std::string& provider_id) const {
    return providers_.contains(provider_id);
}

LocalFullWaveArtifact LocalFullWaveRegistry::solve(
    const LocalFullWaveRequest& request,
    LocalFullWaveCache* cache) const {
    if (!is_valid(request)) {
        throw std::invalid_argument(
            "Invalid local full-wave request");
    }
    const auto found =
        providers_.find(request.provider_id);
    if (found == providers_.end()) {
        throw std::invalid_argument(
            "Requested local full-wave provider is unavailable");
    }
    const auto& provider = found->second;
    const auto& descriptor = provider.descriptor;
    if (descriptor.version <
            request.minimum_provider_version ||
        !std::ranges::contains(
            descriptor.solver_kinds,
            request.solver_kind) ||
        request.wavelengths_nm.size() >
            descriptor.maximum_wavelength_samples ||
        request.incident_cosines.size() >
            descriptor.maximum_incidence_samples ||
        expected_entry_count(request) >
            descriptor.maximum_scattering_entries ||
        request.memory_budget_bytes >
            descriptor.maximum_memory_bytes ||
        (cache && !descriptor.deterministic) ||
        (request.deterministic_required &&
         !descriptor.deterministic)) {
        throw std::invalid_argument(
            "Local full-wave provider cannot satisfy the request contract");
    }
    if (cache) {
        const auto cached =
            cache->find(request, descriptor);
        if (cached) {
            return *cached;
        }
    }
    const auto request_bytes =
        write_local_fullwave_request(request);
    const auto response =
        provider.invoke(request_bytes);
    if (response.empty() ||
        response.size() >
            kMaxLocalFullWaveArtifactBytes) {
        throw std::runtime_error(
            "Local full-wave provider returned an invalid response size");
    }
    const auto artifact =
        read_local_fullwave_artifact(response);
    if (!is_valid(
            artifact,
            request,
            descriptor)) {
        throw std::runtime_error(
            "Local full-wave provider returned an incompatible artifact");
    }
    if (cache &&
        !cache->insert(
            request,
            descriptor,
            artifact)) {
        throw std::runtime_error(
            "Local full-wave cache budget rejected the artifact");
    }
    return artifact;
}

}
