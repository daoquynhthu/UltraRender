#include "ure/research/capability.hpp"

#include <algorithm>
#include <stdexcept>

namespace ure::research {
namespace {

bool maturity_at_least(Maturity actual, Maturity required) {
    return static_cast<std::uint8_t>(actual) >=
           static_cast<std::uint8_t>(required);
}

bool valid_maturity(Maturity maturity) {
    return maturity == Maturity::Research ||
           maturity == Maturity::Experimental ||
           maturity == Maturity::Production;
}

const FeatureCapability* find_capability(
    const std::vector<FeatureCapability>& capabilities,
    const semantic::IdentityDigest& identity) {
    const auto found = std::ranges::find(
        capabilities, identity, &FeatureCapability::feature_identity);
    return found == capabilities.end() ? nullptr : &*found;
}

bool executable_status(CapabilityStatus status) {
    return status == CapabilityStatus::Executable ||
           status == CapabilityStatus::ExecutableOptIn;
}

}

void validate_feature_capabilities(
    const std::vector<FeatureCapability>& capabilities) {
    for (std::size_t index = 0; index < capabilities.size(); ++index) {
        const auto& capability = capabilities[index];
        if (semantic::identity_empty(capability.feature_identity) ||
            !valid_maturity(capability.maturity) ||
            (capability.implemented &&
             semantic::identity_empty(
                 capability.execution_contract_identity)) ||
            (capability.default_enabled &&
             (!capability.implemented ||
              capability.maturity != Maturity::Production))) {
            throw std::invalid_argument("Invalid feature capability");
        }
        if (std::ranges::find(capabilities.begin(),
                              capabilities.begin() + index,
                              capability.feature_identity,
                              &FeatureCapability::feature_identity) !=
            capabilities.begin() + index) {
            throw std::invalid_argument("Duplicate feature capability");
        }
        for (std::size_t dependency_index = 0;
             dependency_index < capability.dependencies.size();
             ++dependency_index) {
            const auto& dependency =
                capability.dependencies[dependency_index];
            if (semantic::identity_empty(dependency) ||
                dependency == capability.feature_identity ||
                std::ranges::find(
                    capability.dependencies.begin(),
                    capability.dependencies.begin() + dependency_index,
                    dependency) !=
                    capability.dependencies.begin() + dependency_index) {
                throw std::invalid_argument("Invalid feature dependency");
            }
        }
    }
}

CapabilityReport negotiate_capabilities(
    const std::vector<FeatureRequirement>& requirements,
    const std::vector<FeatureCapability>& capabilities) {
    validate_feature_capabilities(capabilities);
    CapabilityReport report;
    report.executable = true;
    report.decisions.reserve(requirements.size());
    for (std::size_t index = 0; index < requirements.size(); ++index) {
        const auto& requirement = requirements[index];
        if (semantic::identity_empty(requirement.feature_identity) ||
            !valid_maturity(requirement.minimum_maturity) ||
            std::ranges::find(
                requirements.begin(),
                requirements.begin() + index,
                requirement.feature_identity,
                &FeatureRequirement::feature_identity) !=
                requirements.begin() + index) {
            throw std::invalid_argument("Invalid feature requirement");
        }
        CapabilityDecision decision{requirement.feature_identity,
                                      CapabilityStatus::NotImplemented};
        const auto* capability = find_capability(
            capabilities, requirement.feature_identity);
        if (capability && capability->implemented) {
            if (!maturity_at_least(capability->maturity,
                                   requirement.minimum_maturity)) {
                decision.status = CapabilityStatus::MaturityInsufficient;
            } else if (!semantic::identity_empty(
                           requirement.execution_contract_identity) &&
                       requirement.execution_contract_identity !=
                           capability->execution_contract_identity) {
                decision.status = CapabilityStatus::ContractMismatch;
            } else {
                const bool dependency_missing = std::ranges::any_of(
                    capability->dependencies,
                    [&capabilities](const semantic::IdentityDigest& identity) {
                        const auto* dependency =
                            find_capability(capabilities, identity);
                        return !dependency || !dependency->implemented;
                    });
                if (dependency_missing) {
                    decision.status =
                        CapabilityStatus::DependencyUnavailable;
                } else if (capability->maturity != Maturity::Production &&
                           !requirement.allow_opt_in) {
                    decision.status = CapabilityStatus::OptInRequired;
                } else if (capability->maturity != Maturity::Production) {
                    decision.status = CapabilityStatus::ExecutableOptIn;
                } else {
                    decision.status = CapabilityStatus::Executable;
                }
            }
        }
        report.executable = report.executable &&
                            executable_status(decision.status);
        report.decisions.push_back(decision);
    }
    return report;
}

}
