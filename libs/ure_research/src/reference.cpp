#include "ure/research/reference.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace ure::research {
namespace {

bool supports_observable(const ReferenceBackendDescriptor& descriptor,
                         transport::ObservableKind observable) {
    return std::ranges::find(descriptor.observables, observable) !=
           descriptor.observables.end();
}

bool checked_multiply(std::uint64_t left,
                      std::uint64_t right,
                      std::uint64_t& result) {
    if (left != 0 &&
        right > std::numeric_limits<std::uint64_t>::max() / left) {
        return false;
    }
    result = left * right;
    return true;
}

bool valid_backend_kind(ReferenceBackendKind kind) {
    return kind == ReferenceBackendKind::HostOracle ||
           kind == ReferenceBackendKind::SmallGpuOracle;
}

bool valid_observable_kind(transport::ObservableKind kind) {
    return kind >= transport::ObservableKind::SpectralRadiance &&
           kind <= transport::ObservableKind::LossFunctional;
}

}

void ReferenceBackendRegistry::add(
    ReferenceBackendDescriptor descriptor,
    ReferenceExecutor executor) {
    if (!executor || !valid_backend_kind(descriptor.kind) ||
        semantic::identity_empty(descriptor.provider_identity) ||
        semantic::identity_empty(descriptor.executable_identity) ||
        descriptor.observables.empty() || descriptor.max_elements == 0 ||
        descriptor.max_elements > kMaxReferenceElements ||
        descriptor.max_input_bytes == 0 || !descriptor.deterministic ||
        !std::ranges::all_of(descriptor.observables,
                            valid_observable_kind) ||
        std::ranges::any_of(
            entries_,
            [&descriptor](const Entry& entry) {
                return entry.descriptor.provider_identity ==
                       descriptor.provider_identity;
            })) {
        throw std::invalid_argument("Invalid reference backend descriptor");
    }
    std::ranges::sort(descriptor.observables);
    if (std::ranges::adjacent_find(descriptor.observables) !=
        descriptor.observables.end()) {
        throw std::invalid_argument("Duplicate reference observable");
    }
    entries_.push_back({std::move(descriptor), std::move(executor)});
}

ReferenceResult ReferenceBackendRegistry::execute(
    const ReferenceRequest& request) const {
    std::uint64_t input_bytes = 0;
    if (!transport::validate_observable(request.observable).ok() ||
        !transport::validate_context(request.semantics).ok() ||
        request.element_count == 0 ||
        request.element_count > kMaxReferenceElements ||
        request.output_components == 0 ||
        (request.precision != ReferencePrecision::Float32 &&
         request.precision != ReferencePrecision::Float64) ||
        !checked_multiply(request.input.size(),
                          sizeof(double),
                          input_bytes)) {
        throw std::invalid_argument("Invalid reference request");
    }
    const Entry* selected = nullptr;
    for (const auto& entry : entries_) {
        if ((!semantic::identity_empty(request.provider_identity) &&
             entry.descriptor.provider_identity !=
                 request.provider_identity) ||
            !supports_observable(entry.descriptor,
                                 request.observable.kind) ||
            request.element_count > entry.descriptor.max_elements ||
            (request.precision == ReferencePrecision::Float64 &&
             !entry.descriptor.supports_float64) ||
            input_bytes > entry.descriptor.max_input_bytes) {
            continue;
        }
        if (!selected ||
            entry.descriptor.kind < selected->descriptor.kind) {
            selected = &entry;
        }
    }
    if (!selected) {
        throw std::runtime_error("No bounded reference backend is executable");
    }
    std::uint64_t expected_values = 0;
    if (!checked_multiply(request.element_count,
                          request.output_components,
                          expected_values) ||
        expected_values > std::numeric_limits<std::size_t>::max()) {
        throw std::overflow_error("Reference output size overflow");
    }
    auto result = selected->executor(request);
    if (result.provider_identity != selected->descriptor.provider_identity ||
        result.executable_identity !=
            selected->descriptor.executable_identity ||
        semantic::identity_empty(result.evidence_identity) ||
        result.values.size() != expected_values ||
        !std::ranges::all_of(result.values, [](double value) {
            return std::isfinite(value);
        })) {
        throw std::runtime_error("Invalid bounded reference result");
    }
    return result;
}

}
