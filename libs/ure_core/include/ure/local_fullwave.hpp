#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "ure/scene_ir.hpp"

namespace ure::wave {

constexpr std::size_t kMaxLocalFullWaveInputBytes =
    16 * 1024 * 1024;
constexpr std::size_t kMaxLocalFullWaveArtifactBytes =
    32 * 1024 * 1024;

enum class LocalFullWaveSolverKind : std::uint8_t {
    Rcwa,
    Fdtd,
    Fem,
    Bem,
    Fmm,
    Dda,
    SMatrixImport
};

enum class LocalFullWavePolarizationBasis : std::uint8_t {
    SP
};

struct LocalFullWaveVersion {
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t patch = 0;

    auto operator<=>(const LocalFullWaveVersion&) const =
        default;
};

struct LocalFullWaveRequest {
    std::string request_id;
    std::string provider_id;
    LocalFullWaveVersion minimum_provider_version;
    LocalFullWaveSolverKind solver_kind =
        LocalFullWaveSolverKind::Rcwa;
    LocalFullWavePolarizationBasis polarization_basis =
        LocalFullWavePolarizationBasis::SP;
    std::string geometry_digest;
    std::string material_digest;
    std::vector<std::uint8_t> geometry_payload;
    std::vector<std::uint8_t> material_payload;
    std::vector<float> wavelengths_nm;
    std::vector<float> incident_cosines;
    int minimum_order = 0;
    int maximum_order = 0;
    bool reflection = true;
    bool transmission = true;
    double period_m = 0.0;
    double orientation_rad = 0.0;
    double tolerance = 1.0e-5;
    std::uint64_t memory_budget_bytes = 0;
    std::uint64_t iteration_budget = 0;
    bool deterministic_required = true;
};

struct LocalFullWaveProviderDescriptor {
    std::string provider_id;
    LocalFullWaveVersion version;
    std::string executable_digest;
    std::string semantic_digest;
    std::vector<LocalFullWaveSolverKind> solver_kinds;
    std::size_t maximum_wavelength_samples = 0;
    std::size_t maximum_incidence_samples = 0;
    std::size_t maximum_scattering_entries = 0;
    std::uint64_t maximum_memory_bytes = 0;
    bool deterministic = false;
};

struct LocalFullWaveEvidence {
    bool converged = false;
    std::uint64_t iterations = 0;
    std::uint64_t peak_memory_bytes = 0;
    double residual = 0.0;
    double reciprocity_error = 0.0;
    double energy_error = 0.0;
    std::string solver_artifact_digest;
};

struct LocalFullWaveArtifact {
    std::string schema_identity =
        "ure.local-fullwave.scattering/1.0";
    std::string content_digest;
    std::string request_digest;
    std::string provider_id;
    LocalFullWaveVersion provider_version;
    std::string provider_executable_digest;
    std::string provider_semantic_digest;
    scene_ir::DiffractiveOperator scattering;
    LocalFullWaveEvidence evidence;
};

using LocalFullWaveInvoke =
    std::function<std::vector<std::uint8_t>(
        std::span<const std::uint8_t>)>;

struct LocalFullWaveProvider {
    LocalFullWaveProviderDescriptor descriptor;
    LocalFullWaveInvoke invoke;
};

class LocalFullWaveCache {
public:
    explicit LocalFullWaveCache(
        std::size_t byte_budget =
            kMaxLocalFullWaveArtifactBytes);

    std::size_t byte_budget() const;
    std::size_t resident_bytes() const;
    std::size_t entry_count() const;
    std::optional<LocalFullWaveArtifact> find(
        const LocalFullWaveRequest& request,
        const LocalFullWaveProviderDescriptor&
            descriptor) const;
    bool insert(
        const LocalFullWaveRequest& request,
        const LocalFullWaveProviderDescriptor&
            descriptor,
        const LocalFullWaveArtifact& artifact);

private:
    struct Entry {
        LocalFullWaveArtifact artifact;
        std::size_t byte_size = 0;
    };

    std::size_t byte_budget_ = 0;
    std::size_t resident_bytes_ = 0;
    std::map<std::string, Entry> entries_;
};

class LocalFullWaveRegistry {
public:
    bool register_provider(LocalFullWaveProvider provider);
    bool has_provider(const std::string& provider_id) const;
    LocalFullWaveArtifact solve(
        const LocalFullWaveRequest& request,
        LocalFullWaveCache* cache = nullptr) const;

private:
    std::map<std::string, LocalFullWaveProvider> providers_;
};

bool is_valid(const LocalFullWaveRequest& request);
bool is_valid(
    const LocalFullWaveProviderDescriptor& descriptor);
bool is_valid(
    const LocalFullWaveArtifact& artifact,
    const LocalFullWaveRequest& request,
    const LocalFullWaveProviderDescriptor& descriptor);
std::string local_fullwave_request_digest(
    const LocalFullWaveRequest& request);
std::string local_fullwave_cache_key(
    const LocalFullWaveRequest& request,
    const LocalFullWaveProviderDescriptor& descriptor);
std::vector<std::uint8_t>
write_local_fullwave_request(
    const LocalFullWaveRequest& request);
LocalFullWaveRequest read_local_fullwave_request(
    std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t>
write_local_fullwave_artifact(
    const LocalFullWaveArtifact& artifact);
LocalFullWaveArtifact read_local_fullwave_artifact(
    std::span<const std::uint8_t> bytes);
LocalFullWaveArtifact make_local_fullwave_artifact(
    const LocalFullWaveRequest& request,
    const LocalFullWaveProviderDescriptor& descriptor,
    scene_ir::DiffractiveOperator scattering,
    LocalFullWaveEvidence evidence);

}
