#include "ure/transport/pilot.hpp"

#include "ure/runtime/multi_backend.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <map>
#include <set>
#include <stdexcept>

namespace ure::transport {
namespace {

class Encoder {
public:
    void u8(std::uint8_t value) {
        bytes_.push_back(static_cast<std::byte>(value));
    }
    void u32(std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) {
            u8(static_cast<std::uint8_t>(value >> shift));
        }
    }
    void u64(std::uint64_t value) {
        for (int shift = 0; shift < 64; shift += 8) {
            u8(static_cast<std::uint8_t>(value >> shift));
        }
    }
    void i8(std::int8_t value) {
        u8(static_cast<std::uint8_t>(value));
    }
    void digest(const semantic::IdentityDigest& value) {
        for (const auto byte : value) u8(byte);
    }
    std::span<const std::byte> bytes() const { return bytes_; }

private:
    std::vector<std::byte> bytes_;
};

bool identity_set_valid(
    std::span<const semantic::IdentityDigest> identities) {
    if (std::ranges::any_of(
            identities, semantic::identity_empty)) {
        return false;
    }
    std::set<semantic::IdentityDigest> unique(
        identities.begin(), identities.end());
    return unique.size() == identities.size();
}

bool contains(std::span<const semantic::IdentityDigest> identities,
              const semantic::IdentityDigest& identity) {
    return std::ranges::find(identities, identity) != identities.end();
}

bool contains_all(
    std::span<const semantic::IdentityDigest> available,
    std::span<const semantic::IdentityDigest> required) {
    return std::ranges::all_of(
        required,
        [available](const semantic::IdentityDigest& identity) {
            return contains(available, identity);
        });
}

std::uint64_t pilot_sample_capacity(
    std::span<const PilotSampleRange> ranges) {
    std::uint64_t result = 0;
    for (const auto& range : ranges) result += range.count;
    return result;
}

void encode_identity_set(
    Encoder& encoder,
    std::span<const semantic::IdentityDigest> identities) {
    std::vector<semantic::IdentityDigest> ordered(
        identities.begin(), identities.end());
    std::ranges::sort(ordered);
    encoder.u32(static_cast<std::uint32_t>(ordered.size()));
    for (const auto& identity : ordered) encoder.digest(identity);
}

void encode_observable(Encoder& encoder,
                       const ObservableDescriptor& value) {
    encoder.u32(value.version);
    encoder.u8(static_cast<std::uint8_t>(value.kind));
    encoder.u8(static_cast<std::uint8_t>(value.value_domain));
    encoder.u8(static_cast<std::uint8_t>(value.coherence));
    encoder.u32(value.component_count);
    encoder.u8(value.time_resolved ? 1 : 0);
    encoder.i8(value.unit.dimension.length);
    encoder.i8(value.unit.dimension.mass);
    encoder.i8(value.unit.dimension.time);
    encoder.i8(value.unit.dimension.electric_current);
    encoder.i8(value.unit.dimension.temperature);
    encoder.i8(value.unit.dimension.amount);
    encoder.i8(value.unit.dimension.luminous_intensity);
    encoder.u64(std::bit_cast<std::uint64_t>(value.unit.scale_to_si));
    encoder.u64(std::bit_cast<std::uint64_t>(value.unit.offset_to_si));
    encoder.u8(value.unit.affine ? 1 : 0);
    encoder.digest(value.phase_reference_identity);
    encoder.digest(value.sensor_response_identity);
}

semantic::IdentityDigest qualification_context_identity(
    const PilotQualificationContext& context) {
    Encoder encoder;
    encoder.u32(context.version);
    const auto& provenance = context.provenance;
    encoder.digest(provenance.world_definition);
    encoder.digest(provenance.world_state);
    encoder.digest(provenance.time_sample);
    encoder.digest(provenance.observation_snapshot);
    encoder.digest(provenance.technique_graph);
    encoder.digest(provenance.measurement_schema);
    encoder.digest(provenance.parameter_set);
    encoder.digest(provenance.solver_semantics);
    encoder.digest(provenance.evidence);
    encode_observable(encoder, context.observable);
    encoder.digest(context.support_partition_identity);
    encoder.u64(context.path_event_mask);
    encode_identity_set(encoder, context.scene_capabilities);
    encode_identity_set(encoder, context.backend_capabilities);
    encoder.u64(context.resident_budget_bytes);
    encoder.u64(context.scratch_budget_bytes);
    return runtime::identity_digest(encoder.bytes());
}

semantic::IdentityDigest qualification_requirements_identity(
    std::span<const TechniqueQualificationRequirement> requirements) {
    std::vector<TechniqueQualificationRequirement> ordered(
        requirements.begin(), requirements.end());
    std::ranges::sort(
        ordered, {}, &TechniqueQualificationRequirement::node_ordinal);
    Encoder encoder;
    encoder.u32(static_cast<std::uint32_t>(ordered.size()));
    for (const auto& requirement : ordered) {
        encoder.u32(requirement.node_ordinal);
        encode_identity_set(
            encoder, requirement.required_scene_capabilities);
        encode_identity_set(
            encoder, requirement.required_backend_capabilities);
    }
    return runtime::identity_digest(encoder.bytes());
}

semantic::IdentityDigest override_identity(
    const TechniqueExpertOverride& value);

semantic::IdentityDigest override_policy_identity(
    bool enabled,
    std::span<const TechniqueExpertOverride> overrides) {
    std::vector<TechniqueExpertOverride> ordered(
        overrides.begin(), overrides.end());
    std::ranges::sort(
        ordered, {}, &TechniqueExpertOverride::node_ordinal);
    Encoder encoder;
    encoder.u8(enabled ? 1 : 0);
    encoder.u32(static_cast<std::uint32_t>(ordered.size()));
    for (const auto& value : ordered) {
        encoder.digest(override_identity(value));
    }
    return runtime::identity_digest(encoder.bytes());
}

bool context_valid(const PilotQualificationContext& context) {
    const auto& provenance = context.provenance;
    return context.version == kPilotContractVersion &&
        !semantic::identity_empty(provenance.world_definition) &&
        !semantic::identity_empty(provenance.world_state) &&
        !semantic::identity_empty(provenance.time_sample) &&
        !semantic::identity_empty(provenance.observation_snapshot) &&
        !semantic::identity_empty(provenance.technique_graph) &&
        !semantic::identity_empty(context.support_partition_identity) &&
        context.path_event_mask != 0 &&
        validate_observable(context.observable).ok() &&
        identity_set_valid(context.scene_capabilities) &&
        identity_set_valid(context.backend_capabilities) &&
        context.resident_budget_bytes != 0 &&
        context.scratch_budget_bytes != 0;
}

semantic::IdentityDigest override_identity(
    const TechniqueExpertOverride& value) {
    Encoder encoder;
    encoder.u32(value.node_ordinal);
    encoder.u8(static_cast<std::uint8_t>(value.action));
    encoder.digest(value.experiment_identity);
    encoder.digest(value.rationale_identity);
    return runtime::identity_digest(encoder.bytes());
}

semantic::IdentityDigest report_identity(
    const PilotQualificationReport& report) {
    Encoder encoder;
    encoder.u32(report.version);
    encoder.digest(report.composition_plan_identity);
    encoder.digest(report.pilot_provenance_identity);
    encoder.digest(report.technique_graph_identity);
    encoder.digest(report.world_state_identity);
    encoder.digest(report.observation_snapshot_identity);
    encoder.digest(report.qualification_context_identity);
    encoder.digest(report.requirements_identity);
    encoder.digest(report.override_policy_identity);
    encoder.u8(report.production_executable ? 1 : 0);
    encoder.u8(report.experimental_executable ? 1 : 0);
    encoder.u8(report.executable ? 1 : 0);
    encoder.u32(static_cast<std::uint32_t>(report.decisions.size()));
    for (const auto& decision : report.decisions) {
        encoder.u32(decision.node_ordinal);
        encoder.u8(static_cast<std::uint8_t>(decision.status));
        encoder.u8(static_cast<std::uint8_t>(decision.reason));
        encoder.digest(decision.estimate_identity);
        encoder.digest(decision.override_identity);
    }
    return runtime::identity_digest(encoder.bytes());
}

TechniqueQualificationDecision ineligible(
    std::uint32_t node,
    QualificationReason reason) {
    return {node, QualificationStatus::Ineligible, reason, {}, {}};
}

}

PilotQualificationReport qualify_pilot_techniques(
    const TechniqueGraph& technique_graph,
    const CompiledCompositionPlan& composition_plan,
    const PilotSamplingProvenance& pilot_provenance,
    const PilotQualificationContext& context,
    std::span<const TechniqueQualificationRequirement> requirements,
    std::span<const TechniquePilotEstimate> estimates,
    bool expert_overrides_enabled,
    std::span<const TechniqueExpertOverride> overrides) {
    PilotQualificationReport result;
    result.composition_plan_identity = composition_plan.plan_identity;
    result.technique_graph_identity = technique_graph.graph_identity;
    result.world_state_identity = context.provenance.world_state;
    result.observation_snapshot_identity =
        context.provenance.observation_snapshot;
    result.qualification_context_identity =
        qualification_context_identity(context);
    result.requirements_identity =
        qualification_requirements_identity(requirements);
    result.override_policy_identity =
        override_policy_identity(expert_overrides_enabled, overrides);
    const auto provenance_validation =
        validate_pilot_sampling_provenance(pilot_provenance);
    if (provenance_validation.ok()) {
        result.pilot_provenance_identity =
            pilot_sampling_provenance_identity(pilot_provenance);
    }
    const bool base_valid =
        validate_technique_graph(technique_graph).ok() &&
        validate_compiled_composition_plan(composition_plan) &&
        composition_plan.technique_graph_identity ==
            technique_graph.graph_identity &&
        provenance_validation.ok() && context_valid(context) &&
        context.provenance.technique_graph ==
            technique_graph.graph_identity &&
        pilot_provenance.technique_graph_identity ==
            technique_graph.graph_identity &&
        pilot_provenance.world_state_identity ==
            context.provenance.world_state &&
        pilot_provenance.observation_snapshot_identity ==
            context.provenance.observation_snapshot;
    std::map<std::uint32_t, TechniqueQualificationRequirement>
        requirement_map;
    bool requirements_valid =
        requirements.size() == composition_plan.bindings.size();
    for (const auto& requirement : requirements) {
        requirements_valid = requirements_valid &&
            requirement.node_ordinal < technique_graph.nodes.size() &&
            identity_set_valid(
                requirement.required_scene_capabilities) &&
            identity_set_valid(
                requirement.required_backend_capabilities) &&
            requirement_map.emplace(
                requirement.node_ordinal, requirement).second;
    }
    for (const auto& binding : composition_plan.bindings) {
        requirements_valid = requirements_valid &&
            requirement_map.contains(binding.node_ordinal);
    }
    std::set<std::uint32_t> binding_nodes;
    for (const auto& binding : composition_plan.bindings) {
        binding_nodes.insert(binding.node_ordinal);
    }
    std::map<std::uint32_t, TechniquePilotEstimate> estimate_map;
    std::set<std::uint32_t> invalid_estimates;
    bool estimate_set_valid = true;
    const auto pilot_capacity = provenance_validation.ok()
        ? pilot_sample_capacity(pilot_provenance.pilot_ranges)
        : 0;
    for (const auto& estimate : estimates) {
        if (!binding_nodes.contains(estimate.node_ordinal)) {
            estimate_set_valid = false;
            continue;
        }
        if (!validate_technique_pilot_estimate(estimate) ||
            estimate.support_partition_identity !=
                context.support_partition_identity ||
            estimate.pilot_provenance_identity !=
                result.pilot_provenance_identity ||
            estimate.sample_count > pilot_capacity ||
            !estimate_map.emplace(
                estimate.node_ordinal, estimate).second) {
            invalid_estimates.insert(estimate.node_ordinal);
        }
    }
    std::map<std::uint32_t, TechniqueExpertOverride> override_map;
    bool overrides_valid = true;
    for (const auto& value : overrides) {
        overrides_valid = overrides_valid &&
            binding_nodes.contains(value.node_ordinal) &&
            !semantic::identity_empty(value.experiment_identity) &&
            !semantic::identity_empty(value.rationale_identity) &&
            value.action >=
                ExpertOverrideAction::ForceIncludeExperimental &&
            value.action <= ExpertOverrideAction::ForceExclude &&
            override_map.emplace(value.node_ordinal, value).second;
    }
    std::uint64_t partition_mask = 0;
    std::uint64_t required_partition_mask = 0;
    for (const auto& group : composition_plan.groups) {
        if (group.partition_identity ==
            context.support_partition_identity) {
            partition_mask |= group.technique_mask;
            if (group.layer == composition_plan.required_layer) {
                required_partition_mask |= group.technique_mask;
            }
        }
    }
    if (!base_valid || !requirements_valid || !estimate_set_valid ||
        !overrides_valid ||
        partition_mask == 0) {
        for (const auto& binding : composition_plan.bindings) {
            result.decisions.push_back(ineligible(
                binding.node_ordinal,
                QualificationReason::InvalidContext));
        }
        result.report_identity = report_identity(result);
        return result;
    }
    for (std::size_t index = 0;
         index < composition_plan.bindings.size(); ++index) {
        const auto node =
            composition_plan.bindings[index].node_ordinal;
        const auto& descriptor =
            technique_graph.nodes[node].descriptor;
        TechniqueQualificationDecision decision;
        decision.node_ordinal = node;
        const auto override = override_map.find(node);
        if (override != override_map.end() &&
            !expert_overrides_enabled) {
            decision.status = QualificationStatus::Ineligible;
            decision.reason = QualificationReason::OverrideDisabled;
            decision.override_identity =
                override_identity(override->second);
            result.decisions.push_back(decision);
            continue;
        }
        if (override != override_map.end() &&
            override->second.action ==
                ExpertOverrideAction::ForceExclude) {
            decision.status = QualificationStatus::ExcludedByOverride;
            decision.reason = QualificationReason::ForcedExclude;
            decision.override_identity =
                override_identity(override->second);
            result.decisions.push_back(decision);
            continue;
        }
        QualificationReason failure = QualificationReason::Eligible;
        if ((partition_mask & (std::uint64_t{1} << index)) == 0) {
            failure = QualificationReason::NotInSupportPartition;
        } else if ((required_partition_mask &
                    (std::uint64_t{1} << index)) == 0) {
            failure = QualificationReason::OutputLayerMismatch;
        } else if (!(descriptor.estimator.observable ==
                     context.observable)) {
            failure = QualificationReason::ObservableMismatch;
        } else if ((context.path_event_mask &
                    ~descriptor.estimator.support.event_mask) != 0) {
            failure = QualificationReason::EventMismatch;
        } else if (!contains_all(
                       context.scene_capabilities,
                       requirement_map.at(node)
                           .required_scene_capabilities)) {
            failure = QualificationReason::SceneCapability;
        } else if (!contains(
                       context.backend_capabilities,
                       descriptor.resources
                           .backend_capability_identity) ||
                   !contains_all(
                       context.backend_capabilities,
                       requirement_map.at(node)
                           .required_backend_capabilities)) {
            failure = QualificationReason::BackendCapability;
        } else if (descriptor.resources.persistent_budget_bytes >
                   context.resident_budget_bytes) {
            failure = QualificationReason::ResidentBudget;
        } else if (!descriptor.resources.scratch_bound_known ||
                   descriptor.resources
                           .scratch_bytes_per_work_item >
                       context.scratch_budget_bytes) {
            failure = QualificationReason::ScratchBudget;
        }
        const auto estimate = estimate_map.find(node);
        if (failure == QualificationReason::Eligible &&
            invalid_estimates.contains(node)) {
            failure = QualificationReason::InvalidPilotEvidence;
        } else if (failure == QualificationReason::Eligible &&
                   estimate == estimate_map.end()) {
            failure = QualificationReason::MissingPilotEvidence;
        } else if (failure == QualificationReason::Eligible &&
                   estimate->second.persistent_bytes >
                       context.resident_budget_bytes) {
            failure = QualificationReason::ResidentBudget;
        } else if (failure == QualificationReason::Eligible &&
                   estimate->second.peak_scratch_bytes >
                       context.scratch_budget_bytes) {
            failure = QualificationReason::ScratchBudget;
        }
        if (failure == QualificationReason::Eligible) {
            decision.status = QualificationStatus::Eligible;
            decision.reason = QualificationReason::Eligible;
            decision.estimate_identity = estimate->second.estimate_identity;
        } else if (override != override_map.end() &&
                   override->second.action ==
                       ExpertOverrideAction::ForceIncludeExperimental &&
                   (failure == QualificationReason::MissingPilotEvidence ||
                    failure ==
                        QualificationReason::InvalidPilotEvidence ||
                    failure ==
                        QualificationReason::OutputLayerMismatch)) {
            if (expert_overrides_enabled) {
                decision.status =
                    QualificationStatus::ExperimentalOverride;
                decision.reason = QualificationReason::ForcedExperimental;
                if (estimate != estimate_map.end() &&
                    !invalid_estimates.contains(node)) {
                    decision.estimate_identity =
                        estimate->second.estimate_identity;
                }
            } else {
                decision.status = QualificationStatus::Ineligible;
                decision.reason = QualificationReason::OverrideDisabled;
            }
            decision.override_identity =
                override_identity(override->second);
        } else {
            decision.status = QualificationStatus::Ineligible;
            decision.reason = failure;
        }
        result.decisions.push_back(decision);
    }
    result.production_executable = std::ranges::any_of(
        result.decisions,
        [](const TechniqueQualificationDecision& decision) {
            return decision.status == QualificationStatus::Eligible;
        });
    result.experimental_executable = std::ranges::any_of(
        result.decisions,
        [](const TechniqueQualificationDecision& decision) {
            return decision.status ==
                QualificationStatus::ExperimentalOverride;
        });
    result.executable = result.production_executable ||
        result.experimental_executable;
    result.report_identity = report_identity(result);
    return result;
}

bool validate_pilot_qualification_report(
    const PilotQualificationReport& report) {
    if (report.version != kPilotContractVersion ||
        semantic::identity_empty(report.report_identity) ||
        semantic::identity_empty(report.composition_plan_identity) ||
        semantic::identity_empty(report.pilot_provenance_identity) ||
        semantic::identity_empty(report.technique_graph_identity) ||
        semantic::identity_empty(report.world_state_identity) ||
        semantic::identity_empty(
            report.observation_snapshot_identity) ||
        semantic::identity_empty(
            report.qualification_context_identity) ||
        semantic::identity_empty(report.requirements_identity) ||
        semantic::identity_empty(report.override_policy_identity) ||
        report.decisions.empty()) {
        return false;
    }
    bool production = false;
    bool experimental = false;
    std::set<std::uint32_t> nodes;
    for (const auto& decision : report.decisions) {
        if (!nodes.insert(decision.node_ordinal).second ||
            decision.status < QualificationStatus::Eligible ||
            decision.status >
                QualificationStatus::ExcludedByOverride ||
            decision.reason < QualificationReason::Eligible ||
            decision.reason > QualificationReason::ForcedExclude) {
            return false;
        }
        switch (decision.status) {
        case QualificationStatus::Eligible:
            if (decision.reason != QualificationReason::Eligible ||
                semantic::identity_empty(decision.estimate_identity) ||
                !semantic::identity_empty(decision.override_identity)) {
                return false;
            }
            production = true;
            break;
        case QualificationStatus::ExperimentalOverride:
            if (decision.reason !=
                    QualificationReason::ForcedExperimental ||
                semantic::identity_empty(decision.override_identity)) {
                return false;
            }
            experimental = true;
            break;
        case QualificationStatus::ExcludedByOverride:
            if (decision.reason != QualificationReason::ForcedExclude ||
                semantic::identity_empty(decision.override_identity) ||
                !semantic::identity_empty(decision.estimate_identity)) {
                return false;
            }
            break;
        case QualificationStatus::Ineligible:
            if (decision.reason == QualificationReason::Eligible ||
                decision.reason ==
                    QualificationReason::ForcedExperimental ||
                decision.reason == QualificationReason::ForcedExclude ||
                !semantic::identity_empty(decision.estimate_identity) ||
                (decision.reason == QualificationReason::OverrideDisabled) !=
                    !semantic::identity_empty(decision.override_identity)) {
                return false;
            }
            break;
        }
    }
    return report.production_executable == production &&
        report.experimental_executable == experimental &&
        report.executable == (production || experimental) &&
        report.report_identity == report_identity(report);
}

}
