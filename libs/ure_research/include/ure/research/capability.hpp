#pragma once

#include "ure/semantic_types.hpp"

#include <cstdint>
#include <vector>

namespace ure::research {

enum class Maturity : std::uint8_t {
    Research,
    Experimental,
    Production
};

struct FeatureCapability {
    semantic::IdentityDigest feature_identity = {};
    semantic::IdentityDigest execution_contract_identity = {};
    Maturity maturity = Maturity::Research;
    bool implemented = false;
    bool default_enabled = false;
    std::vector<semantic::IdentityDigest> dependencies;
};

struct FeatureRequirement {
    semantic::IdentityDigest feature_identity = {};
    semantic::IdentityDigest execution_contract_identity = {};
    Maturity minimum_maturity = Maturity::Research;
    bool allow_opt_in = false;
};

enum class CapabilityStatus : std::uint8_t {
    Executable,
    ExecutableOptIn,
    OptInRequired,
    NotImplemented,
    MaturityInsufficient,
    ContractMismatch,
    DependencyUnavailable
};

struct CapabilityDecision {
    semantic::IdentityDigest feature_identity = {};
    CapabilityStatus status = CapabilityStatus::NotImplemented;
};

struct CapabilityReport {
    bool executable = false;
    std::vector<CapabilityDecision> decisions;
};

void validate_feature_capabilities(
    const std::vector<FeatureCapability>& capabilities);

CapabilityReport negotiate_capabilities(
    const std::vector<FeatureRequirement>& requirements,
    const std::vector<FeatureCapability>& capabilities);

}
