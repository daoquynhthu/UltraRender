#include <ure/local_fullwave.hpp>
#include <ure/native_scene_hash.hpp>
#include <ure/wave_optics.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
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

std::string digest(
    std::span<const std::uint8_t> bytes) {
    return ure::native_scene::sha256_hex(bytes);
}

std::string digest(std::string_view text) {
    return digest(
        std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(
                text.data()),
            text.size()));
}

ure::wave::LocalFullWaveRequest request() {
    ure::wave::LocalFullWaveRequest value;
    value.request_id = "wave/cell/1";
    value.provider_id = "ure.test.fullwave";
    value.minimum_provider_version = {1, 0, 0};
    value.solver_kind =
        ure::wave::LocalFullWaveSolverKind::Rcwa;
    value.geometry_payload = {1, 2, 3, 4};
    value.material_payload = {5, 6, 7, 8};
    value.geometry_digest =
        digest(value.geometry_payload);
    value.material_digest =
        digest(value.material_payload);
    value.wavelengths_nm = {500.0f, 600.0f};
    value.incident_cosines = {0.5f, 1.0f};
    value.minimum_order = -1;
    value.maximum_order = 1;
    value.period_m = 1.0e-6;
    value.orientation_rad = 0.25;
    value.tolerance = 1.0e-5;
    value.memory_budget_bytes = 8 * 1024 * 1024;
    value.iteration_budget = 4096;
    return value;
}

ure::wave::LocalFullWaveProviderDescriptor
descriptor() {
    using Kind =
        ure::wave::LocalFullWaveSolverKind;
    ure::wave::LocalFullWaveProviderDescriptor
        value;
    value.provider_id = "ure.test.fullwave";
    value.version = {1, 2, 0};
    value.executable_digest =
        digest("test-fullwave-executable");
    value.semantic_digest =
        digest("test-fullwave-semantics-v1");
    value.solver_kinds = {
        Kind::Rcwa,
        Kind::Fdtd,
        Kind::Fem,
        Kind::Bem,
        Kind::Fmm,
        Kind::Dda,
        Kind::SMatrixImport};
    value.maximum_wavelength_samples = 64;
    value.maximum_incidence_samples = 64;
    value.maximum_scattering_entries =
        ure::scene_ir::
            kMaxDiffractiveScatteringEntries;
    value.maximum_memory_bytes =
        16 * 1024 * 1024;
    value.deterministic = true;
    return value;
}

ure::scene_ir::DiffractiveOperator scattering(
    const ure::wave::LocalFullWaveRequest& request) {
    ure::scene_ir::DiffractiveOperator result;
    result.kind =
        ure::scene_ir::
            DiffractiveOperatorKind::
                ScatteringTable;
    result.period_m = request.period_m;
    result.orientation_rad =
        request.orientation_rad;
    result.max_order = std::max(
        std::abs(request.minimum_order),
        std::abs(request.maximum_order));
    result.table_id = "local/fullwave/table";
    const float amplitude =
        static_cast<float>(std::sqrt(0.1));
    for (const float wavelength :
         request.wavelengths_nm) {
        for (const float cosine :
             request.incident_cosines) {
            for (int order = request.minimum_order;
                 order <= request.maximum_order;
                 ++order) {
                const auto append =
                    [&](ure::scene_ir::
                            DiffractiveScatterSide side) {
                        ure::scene_ir::
                            DiffractiveScatteringEntry
                                entry;
                        entry.wavelength_nm =
                            wavelength;
                        entry.incident_cosine = cosine;
                        entry.order = order;
                        entry.side = side;
                        entry.jones_ss.real = amplitude;
                        entry.jones_pp.real = amplitude;
                        result.table.push_back(entry);
                    };
                if (request.reflection) {
                    append(
                        ure::scene_ir::
                            DiffractiveScatterSide::
                                Reflection);
                }
                if (request.transmission) {
                    append(
                        ure::scene_ir::
                            DiffractiveScatterSide::
                                Transmission);
                }
            }
        }
    }
    return result;
}

ure::wave::LocalFullWaveEvidence evidence() {
    ure::wave::LocalFullWaveEvidence result;
    result.converged = true;
    result.iterations = 128;
    result.peak_memory_bytes = 1024 * 1024;
    result.residual = 1.0e-7;
    result.reciprocity_error = 2.0e-6;
    result.energy_error = 3.0e-6;
    result.solver_artifact_digest =
        digest("solver-native-artifact");
    return result;
}

int test_request_and_artifact_roundtrip() {
    const auto source = request();
    check(
        ure::wave::is_valid(source),
        "valid request rejected");
    const auto bytes =
        ure::wave::write_local_fullwave_request(
            source);
    const auto loaded =
        ure::wave::read_local_fullwave_request(
            bytes);
    check(
        loaded.geometry_digest ==
            source.geometry_digest &&
            loaded.material_payload ==
                source.material_payload &&
            loaded.maximum_order ==
                source.maximum_order,
        "request roundtrip changed semantics");
    auto renamed = source;
    renamed.request_id = "wave/cell/renamed";
    check(
        ure::wave::local_fullwave_request_digest(
            renamed) ==
            ure::wave::
                local_fullwave_request_digest(
                    source),
        "request label changed semantic digest");
    check(
        ure::wave::local_fullwave_cache_key(
            renamed,
            descriptor()) ==
            ure::wave::local_fullwave_cache_key(
                source,
                descriptor()),
        "request label fragmented the cache");
    auto physically_changed = source;
    physically_changed.period_m *= 2.0;
    check(
        ure::wave::local_fullwave_cache_key(
            physically_changed,
            descriptor()) !=
            ure::wave::local_fullwave_cache_key(
                source,
                descriptor()),
        "physical request change did not invalidate the cache");
    auto provider_changed = descriptor();
    provider_changed.semantic_digest =
        digest("test-fullwave-semantics-v2");
    check(
        ure::wave::local_fullwave_cache_key(
            source,
            provider_changed) !=
            ure::wave::local_fullwave_cache_key(
                source,
                descriptor()),
        "provider semantics did not invalidate the cache");

    const auto artifact =
        ure::wave::make_local_fullwave_artifact(
            source,
            descriptor(),
            scattering(source),
            evidence());
    check(
        ure::wave::is_valid(
            artifact,
            source,
            descriptor()),
        "valid artifact rejected");
    const auto artifact_bytes =
        ure::wave::write_local_fullwave_artifact(
            artifact);
    const auto artifact_loaded =
        ure::wave::read_local_fullwave_artifact(
            artifact_bytes);
    check(
        artifact_loaded.request_digest ==
                artifact.request_digest &&
            artifact_loaded.content_digest ==
                artifact.content_digest &&
            artifact_loaded.scattering.table.size() ==
                artifact.scattering.table.size(),
        "artifact roundtrip changed content");
    auto truncated = artifact_bytes;
    truncated.pop_back();
    check(
        throws([&] {
            static_cast<void>(
                ure::wave::
                    read_local_fullwave_artifact(
                        truncated));
        }),
        "truncated artifact was accepted");
    auto nonfinite = artifact;
    nonfinite.evidence.residual =
        std::numeric_limits<double>::
            quiet_NaN();
    check(
        throws([&] {
            const auto encoded =
                ure::wave::
                    write_local_fullwave_artifact(
                        nonfinite);
            static_cast<void>(
                ure::wave::
                    read_local_fullwave_artifact(
                        encoded));
        }),
        "non-finite evidence was accepted");
    auto altered_table = artifact;
    altered_table.scattering.table.front()
        .jones_ss.real *= 0.5f;
    check(
        throws([&] {
            static_cast<void>(
                ure::wave::
                    write_local_fullwave_artifact(
                        altered_table));
        }),
        "artifact content corruption was accepted");
    return 0;
}

int test_registry_cache_and_consumption() {
    auto source = request();
    const auto provider_descriptor = descriptor();
    int invocation_count = 0;
    ure::wave::LocalFullWaveRegistry registry;
    check(
        registry.register_provider({
            provider_descriptor,
            [&](std::span<const std::uint8_t>
                    request_bytes) {
                ++invocation_count;
                const auto decoded =
                    ure::wave::
                        read_local_fullwave_request(
                            request_bytes);
                return ure::wave::
                    write_local_fullwave_artifact(
                        ure::wave::
                            make_local_fullwave_artifact(
                                decoded,
                                provider_descriptor,
                                scattering(decoded),
                                evidence()));
            }}),
        "provider registration failed");
    check(
        !registry.register_provider({
            provider_descriptor,
            [](std::span<const std::uint8_t>) {
                return std::vector<std::uint8_t>{};
            }}),
        "duplicate provider registration succeeded");
    ure::wave::LocalFullWaveCache cache;
    const auto first =
        registry.solve(source, &cache);
    const auto second =
        registry.solve(source, &cache);
    check(
        invocation_count == 1 &&
            cache.entry_count() == 1 &&
            cache.resident_bytes() > 0 &&
            first.request_digest ==
                second.request_digest,
        "deterministic cache did not reuse artifact");
    auto incompatible_request = source;
    incompatible_request.period_m *= 2.0;
    check(
        !cache.insert(
            incompatible_request,
            provider_descriptor,
            first),
        "cache accepted an artifact under incompatible request identity");
    const auto responses =
        ure::wave::diffractive_orders(
            first.scattering,
            550.0,
            std::sqrt(1.0 - 0.75 * 0.75));
    check(
        responses.size() == 6,
        "verified artifact did not enter W.5 scattering consumption");
    double efficiency = 0.0;
    std::size_t evanescent_count = 0;
    for (const auto& response : responses) {
        efficiency +=
            response.unpolarized_efficiency;
        evanescent_count +=
            static_cast<std::size_t>(
                !response.propagating);
    }
    check(
        std::abs(efficiency - 0.4) < 1.0e-5 &&
            evanescent_count == 2,
        "scattering-table efficiency changed");

    using Kind =
        ure::wave::LocalFullWaveSolverKind;
    for (const auto kind : {
             Kind::Rcwa,
             Kind::Fdtd,
             Kind::Fem,
             Kind::Bem,
             Kind::Fmm,
             Kind::Dda,
             Kind::SMatrixImport}) {
        source.solver_kind = kind;
        const auto result =
            registry.solve(source);
        check(
            ure::wave::is_valid(
                result,
                source,
                provider_descriptor),
            "supported solver kind failed negotiation");
    }
    return 0;
}

int test_fail_loud_boundaries() {
    const auto provider_descriptor = descriptor();
    auto invalid = request();
    invalid.geometry_digest =
        std::string(64, '0');
    check(
        !ure::wave::is_valid(invalid),
        "payload digest mismatch was accepted");
    invalid = request();
    invalid.maximum_order = 16;
    invalid.minimum_order = -16;
    invalid.wavelengths_nm.resize(100);
    for (std::size_t index = 0;
         index < invalid.wavelengths_nm.size();
         ++index) {
        invalid.wavelengths_nm[index] =
            400.0f +
            static_cast<float>(index);
    }
    check(
        !ure::wave::is_valid(invalid),
        "oversized requested table was accepted");

    ure::wave::LocalFullWaveRegistry registry;
    auto nondeterministic = provider_descriptor;
    nondeterministic.provider_id =
        "ure.test.nondeterministic";
    nondeterministic.deterministic = false;
    registry.register_provider({
        nondeterministic,
        [&](std::span<const std::uint8_t>
                request_bytes) {
            const auto decoded =
                ure::wave::
                    read_local_fullwave_request(
                        request_bytes);
            return ure::wave::
                write_local_fullwave_artifact(
                    ure::wave::
                        make_local_fullwave_artifact(
                            decoded,
                            nondeterministic,
                            scattering(decoded),
                            evidence()));
        }});
    auto nondeterministic_request = request();
    nondeterministic_request.provider_id =
        nondeterministic.provider_id;
    check(
        throws([&] {
            static_cast<void>(
                registry.solve(
                    nondeterministic_request));
        }),
        "deterministic request accepted nondeterministic provider");
    nondeterministic_request.deterministic_required =
        false;
    check(
        ure::wave::is_valid(
            registry.solve(
                nondeterministic_request),
            nondeterministic_request,
            nondeterministic),
        "explicit nondeterministic provider execution failed");
    ure::wave::LocalFullWaveCache deterministic_cache;
    check(
        throws([&] {
            static_cast<void>(
                registry.solve(
                    nondeterministic_request,
                    &deterministic_cache));
        }),
        "nondeterministic provider entered deterministic cache");
    check(
        throws([&] {
            static_cast<void>(
                registry.solve(request()));
        }),
        "missing provider silently fell back");

    ure::wave::LocalFullWaveRegistry versioned;
    versioned.register_provider({
        provider_descriptor,
        [](std::span<const std::uint8_t>) {
            return std::vector<std::uint8_t>{};
        }});
    auto future_request = request();
    future_request.minimum_provider_version = {
        2,
        0,
        0};
    check(
        throws([&] {
            static_cast<void>(
                versioned.solve(future_request));
        }),
        "insufficient provider version was accepted");

    ure::wave::LocalFullWaveRegistry corrupt;
    corrupt.register_provider({
        provider_descriptor,
        [&](std::span<const std::uint8_t>
                request_bytes) {
            const auto decoded =
                ure::wave::
                    read_local_fullwave_request(
                        request_bytes);
            auto bad_evidence = evidence();
            bad_evidence.residual = 0.5;
            auto artifact =
                ure::wave::
                    make_local_fullwave_artifact(
                        decoded,
                        provider_descriptor,
                        scattering(decoded),
                        evidence());
            artifact.evidence = bad_evidence;
            return ure::wave::
                write_local_fullwave_artifact(
                    artifact);
        }});
    check(
        throws([&] {
            static_cast<void>(
                corrupt.solve(request()));
        }),
        "provider residual violation was accepted");

    ure::wave::LocalFullWaveRegistry incomplete;
    incomplete.register_provider({
        provider_descriptor,
        [&](std::span<const std::uint8_t>
                request_bytes) {
            const auto decoded =
                ure::wave::
                    read_local_fullwave_request(
                        request_bytes);
            auto table = scattering(decoded);
            table.table.pop_back();
            auto artifact =
                ure::wave::
                    make_local_fullwave_artifact(
                        decoded,
                        provider_descriptor,
                        scattering(decoded),
                        evidence());
            artifact.scattering = std::move(table);
            return ure::wave::
                write_local_fullwave_artifact(
                    artifact);
        }});
    check(
        throws([&] {
            static_cast<void>(
                incomplete.solve(request()));
        }),
        "incomplete scattering grid was accepted");

    ure::wave::LocalFullWaveCache tiny_cache(64);
    ure::wave::LocalFullWaveRegistry valid;
    valid.register_provider({
        provider_descriptor,
        [&](std::span<const std::uint8_t>
                request_bytes) {
            const auto decoded =
                ure::wave::
                    read_local_fullwave_request(
                        request_bytes);
            return ure::wave::
                write_local_fullwave_artifact(
                    ure::wave::
                        make_local_fullwave_artifact(
                            decoded,
                            provider_descriptor,
                            scattering(decoded),
                            evidence()));
        }});
    check(
        throws([&] {
            static_cast<void>(
                valid.solve(
                    request(),
                    &tiny_cache));
        }),
        "cache budget silently dropped a verified artifact");
    return 0;
}

}

int main() {
    test_request_and_artifact_roundtrip();
    test_registry_cache_and_consumption();
    test_fail_loud_boundaries();
    if (failures == 0) {
        std::cout
            << "Local full-wave contract tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
