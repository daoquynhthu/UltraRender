#pragma once

#include "ure/transport/semantics.hpp"

#include <cstdint>
#include <functional>
#include <vector>

namespace ure::research {

inline constexpr std::uint64_t kMaxReferenceElements = 1ull << 20;

enum class ReferenceBackendKind : std::uint8_t {
    HostOracle,
    SmallGpuOracle
};

enum class ReferencePrecision : std::uint8_t {
    Float32,
    Float64
};

struct ReferenceBackendDescriptor {
    semantic::IdentityDigest provider_identity = {};
    semantic::IdentityDigest executable_identity = {};
    ReferenceBackendKind kind = ReferenceBackendKind::HostOracle;
    std::vector<transport::ObservableKind> observables;
    std::uint64_t max_elements = 0;
    std::uint64_t max_input_bytes = 0;
    bool deterministic = false;
    bool supports_float64 = false;
};

struct ReferenceRequest {
    semantic::IdentityDigest provider_identity = {};
    transport::ObservableDescriptor observable = {};
    transport::SemanticContext semantics = {};
    ReferencePrecision precision = ReferencePrecision::Float64;
    std::uint64_t element_count = 0;
    std::uint32_t output_components = 1;
    std::vector<double> input;
};

struct ReferenceResult {
    semantic::IdentityDigest provider_identity = {};
    semantic::IdentityDigest executable_identity = {};
    semantic::IdentityDigest evidence_identity = {};
    std::vector<double> values;
};

using ReferenceExecutor =
    std::function<ReferenceResult(const ReferenceRequest&)>;

class ReferenceBackendRegistry {
public:
    void add(ReferenceBackendDescriptor descriptor,
             ReferenceExecutor executor);
    ReferenceResult execute(const ReferenceRequest& request) const;
    std::size_t size() const { return entries_.size(); }

private:
    struct Entry {
        ReferenceBackendDescriptor descriptor;
        ReferenceExecutor executor;
    };
    std::vector<Entry> entries_;
};

}
