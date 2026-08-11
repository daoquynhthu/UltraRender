#include "ure/transport/automatic_integrator.hpp"

#include "ure/runtime/multi_backend.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <limits>
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
    void f64(double value) {
        u64(std::bit_cast<std::uint64_t>(value));
    }
    void digest(const semantic::IdentityDigest& value) {
        for (const auto byte : value) u8(byte);
    }
    std::span<const std::byte> bytes() const { return bytes_; }

private:
    std::vector<std::byte> bytes_;
};

template <typename Issue>
void add(std::vector<Issue>& issues, Issue issue) {
    if (std::ranges::find(issues, issue) == issues.end()) {
        issues.push_back(issue);
    }
}

bool empty(const semantic::IdentityDigest& value) {
    return semantic::identity_empty(value);
}

bool valid_objective(const AutomaticIntegratorObjective& objective) {
    const bool quality = objective.kind == AutomaticObjectiveKind::Quality ||
        objective.kind == AutomaticObjectiveKind::Balanced;
    const bool deadline =
        objective.kind == AutomaticObjectiveKind::Deadline ||
        objective.kind == AutomaticObjectiveKind::Balanced;
    return objective.version == kAutomaticIntegratorContractVersion &&
        objective.kind >= AutomaticObjectiveKind::Quality &&
        objective.kind <= AutomaticObjectiveKind::Balanced &&
        !empty(objective.objective_identity) &&
        objective.objective_identity ==
            compute_automatic_integrator_objective_identity(objective) &&
        std::isfinite(objective.target_relative_standard_error) &&
        (!quality || objective.target_relative_standard_error > 0.0) &&
        (!deadline || objective.deadline_nanoseconds > 0) &&
        objective.resident_budget_bytes > 0 &&
        objective.scratch_budget_bytes > 0 &&
        objective.maximum_samples > 0 &&
        std::isfinite(objective.minimum_wavefront_fraction) &&
        objective.minimum_wavefront_fraction > 0.0 &&
        objective.minimum_wavefront_fraction <= 1.0 &&
        std::isfinite(objective.confidence_level) &&
        objective.confidence_level > 0.5 &&
        objective.confidence_level < 1.0;
}

std::uint64_t checked_add(
    std::uint64_t left,
    std::uint64_t right,
    bool& overflow) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        overflow = true;
        return 0;
    }
    return left + right;
}

semantic::IdentityDigest program_identity(
    const AutomaticPartitionProgram& program) {
    Encoder encoder;
    encoder.digest(program.partition_identity);
    encoder.digest(program.weight_rule_identity);
    encoder.u8(static_cast<std::uint8_t>(program.layer));
    encoder.u8(static_cast<std::uint8_t>(program.family));
    encoder.u64(program.scheduled_technique_mask);
    encoder.u64(program.defensive_technique_mask);
    encoder.u64(program.allocated_samples);
    return runtime::identity_digest(encoder.bytes());
}

semantic::IdentityDigest plan_identity(
    const AutomaticIntegratorPlan& plan) {
    Encoder encoder;
    encoder.u32(plan.version);
    encoder.digest(plan.objective_identity);
    encoder.digest(plan.technique_graph_identity);
    encoder.digest(plan.composition_plan_identity);
    encoder.digest(plan.qualification_report_identity);
    encoder.digest(plan.schedule_identity);
    encoder.digest(plan.world_state_identity);
    encoder.digest(plan.observation_snapshot_identity);
    encoder.u8(static_cast<std::uint8_t>(
        plan.legacy_preset_disposition));
    encoder.u8(plan.automatically_selected ? 1 : 0);
    encoder.u8(plan.production_executable ? 1 : 0);
    encoder.u32(static_cast<std::uint32_t>(plan.decisions.size()));
    for (const auto& decision : plan.decisions) {
        encoder.u32(decision.node_ordinal);
        encoder.u8(static_cast<std::uint8_t>(decision.status));
        encoder.u8(static_cast<std::uint8_t>(decision.reason));
        encoder.digest(decision.technique_identity);
        encoder.digest(decision.evidence_identity);
        encoder.u64(decision.allocated_samples);
        encoder.u64(decision.estimated_nanoseconds);
        encoder.u64(decision.resident_bytes);
        encoder.u64(decision.scratch_bytes);
    }
    encoder.u32(static_cast<std::uint32_t>(plan.programs.size()));
    for (const auto& program : plan.programs) {
        encoder.digest(program.program_identity);
    }
    return runtime::identity_digest(encoder.bytes());
}

bool valid_decision(const AutomaticTechniqueDecision& decision) {
    return decision.status >= AutomaticDecisionStatus::Included &&
        decision.status <= AutomaticDecisionStatus::Excluded &&
        decision.reason >= AutomaticDecisionReason::Scheduled &&
        decision.reason <=
            AutomaticDecisionReason::MissingExecutionEvidence &&
        !empty(decision.technique_identity) &&
        !empty(decision.evidence_identity) &&
        ((decision.status == AutomaticDecisionStatus::Excluded) ==
         (decision.allocated_samples == 0));
}

bool valid_program(const AutomaticPartitionProgram& program) {
    return !empty(program.program_identity) &&
        program.program_identity == program_identity(program) &&
        !empty(program.partition_identity) &&
        !empty(program.weight_rule_identity) &&
        program.layer >= EstimateLayer::Unbiased &&
        program.layer <= EstimateLayer::Research &&
        program.family >= CompositionFamily::MultipleImportanceSampling &&
        program.family <= CompositionFamily::MarkovChainReplicate &&
        program.scheduled_technique_mask != 0 &&
        program.allocated_samples > 0;
}

semantic::IdentityDigest weight_rule_identity(
    const CompiledCompositionPlan& composition_plan,
    const CompositionGroup& group) {
    Encoder encoder;
    encoder.digest(composition_plan.plan_identity);
    encoder.digest(group.partition_identity);
    encoder.u8(static_cast<std::uint8_t>(group.layer));
    encoder.u8(static_cast<std::uint8_t>(group.family));
    encoder.u64(group.technique_mask);
    encoder.f64(group.fixed_aggregation_weight);
    encoder.u8(static_cast<std::uint8_t>(
        composition_plan.mis_family.heuristic));
    encoder.f64(composition_plan.mis_family.power);
    return runtime::identity_digest(encoder.bytes());
}

double normal_quantile(double probability) {
    constexpr std::array a{
        -3.969683028665376e+01, 2.209460984245205e+02,
        -2.759285104469687e+02, 1.383577518672690e+02,
        -3.066479806614716e+01, 2.506628277459239e+00};
    constexpr std::array b{
        -5.447609879822406e+01, 1.615858368580409e+02,
        -1.556989798598866e+02, 6.680131188771972e+01,
        -1.328068155288572e+01};
    constexpr std::array c{
        -7.784894002430293e-03, -3.223964580411365e-01,
        -2.400758277161838e+00, -2.549732539343734e+00,
        4.374664141464968e+00, 2.938163982698783e+00};
    constexpr std::array d{
        7.784695709041462e-03, 3.224671290700398e-01,
        2.445134137142996e+00, 3.754408661907416e+00};
    constexpr double low = 0.02425;
    constexpr double high = 1.0 - low;
    if (probability < low) {
        const auto q = std::sqrt(-2.0 * std::log(probability));
        return (((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q +
                  c[4]) * q + c[5]) /
            ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1.0);
    }
    if (probability <= high) {
        const auto q = probability - 0.5;
        const auto r = q * q;
        return (((((a[0] * r + a[1]) * r + a[2]) * r + a[3]) * r +
                  a[4]) * r + a[5]) * q /
            (((((b[0] * r + b[1]) * r + b[2]) * r + b[3]) * r +
              b[4]) * r + 1.0);
    }
    const auto q = std::sqrt(-2.0 * std::log(1.0 - probability));
    return -(((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q +
               c[4]) * q + c[5]) /
        ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1.0);
}

semantic::IdentityDigest output_trace_identity(
    const AutomaticOutputTrace& trace) {
    Encoder encoder;
    encoder.u32(trace.version);
    encoder.digest(trace.plan_identity);
    encoder.digest(trace.objective_identity);
    encoder.digest(trace.technique_graph_identity);
    encoder.digest(trace.composition_plan_identity);
    encoder.digest(trace.schedule_identity);
    encoder.digest(trace.world_state_identity);
    encoder.digest(trace.observation_snapshot_identity);
    encoder.digest(trace.measurement_set_identity);
    encoder.u64(trace.technique_coverage_mask);
    encoder.u64(trace.sample_count);
    encoder.f64(trace.estimate);
    encoder.f64(trace.standard_error);
    encoder.f64(trace.confidence_level);
    encoder.f64(trace.confidence_lower);
    encoder.f64(trace.confidence_upper);
    encoder.f64(trace.maximum_absolute_contribution);
    encoder.u64(trace.elapsed_nanoseconds);
    encoder.u64(trace.peak_resident_bytes);
    encoder.u64(trace.peak_scratch_bytes);
    encoder.u8(trace.quality_target_met ? 1 : 0);
    encoder.u8(trace.deadline_met ? 1 : 0);
    encoder.u8(trace.complete ? 1 : 0);
    encoder.u32(static_cast<std::uint32_t>(
        trace.partition_observation_identities.size()));
    for (const auto& value : trace.partition_observation_identities) {
        encoder.digest(value);
    }
    return runtime::identity_digest(encoder.bytes());
}

bool valid_observation(const AutomaticPartitionObservation& value) {
    return value.version == kAutomaticIntegratorContractVersion &&
        !empty(value.observation_identity) &&
        value.observation_identity ==
            compute_automatic_partition_observation_identity(value) &&
        !empty(value.plan_identity) &&
        !empty(value.program_identity) &&
        !empty(value.partition_identity) &&
        !empty(value.measurement_identity) &&
        !empty(value.weight_rule_identity) &&
        !empty(value.normalization_identity) &&
        value.technique_mask != 0 &&
        value.sample_count > 1 &&
        std::isfinite(value.estimate) &&
        std::isfinite(value.sample_variance) &&
        value.sample_variance >= 0.0 &&
        std::isfinite(value.effective_sample_size) &&
        value.effective_sample_size > 0.0 &&
        value.effective_sample_size <=
            static_cast<double>(value.sample_count) &&
        std::isfinite(value.maximum_absolute_contribution) &&
        value.maximum_absolute_contribution >= 0.0 &&
        value.elapsed_nanoseconds > 0 &&
        value.peak_resident_bytes > 0 &&
        value.peak_scratch_bytes > 0;
}

}

semantic::IdentityDigest compute_automatic_integrator_objective_identity(
    const AutomaticIntegratorObjective& objective) {
    Encoder encoder;
    encoder.u32(objective.version);
    encoder.u8(static_cast<std::uint8_t>(objective.kind));
    encoder.f64(objective.target_relative_standard_error);
    encoder.u64(objective.deadline_nanoseconds);
    encoder.u64(objective.resident_budget_bytes);
    encoder.u64(objective.scratch_budget_bytes);
    encoder.u64(objective.maximum_samples);
    encoder.f64(objective.minimum_wavefront_fraction);
    encoder.f64(objective.confidence_level);
    encoder.u8(objective.allow_experimental ? 1 : 0);
    encoder.u8(objective.allow_non_unbiased_output ? 1 : 0);
    return runtime::identity_digest(encoder.bytes());
}

void finalize_automatic_integrator_objective(
    AutomaticIntegratorObjective& objective) {
    objective.objective_identity =
        compute_automatic_integrator_objective_identity(objective);
    if (!valid_objective(objective)) {
        throw std::invalid_argument("Invalid automatic integrator objective");
    }
}

bool AutomaticIntegratorPlan::has(AutomaticPlanIssue issue) const {
    return std::ranges::find(issues, issue) != issues.end();
}

AutomaticIntegratorPlan compile_automatic_integrator_plan(
    const TechniqueGraph& technique_graph,
    const CompiledCompositionPlan& composition_plan,
    const PilotQualificationReport& qualification_report,
    const PortfolioSchedule& schedule,
    const AutomaticIntegratorObjective& objective) {
    AutomaticIntegratorPlan result;
    result.objective_identity = objective.objective_identity;
    result.technique_graph_identity = technique_graph.graph_identity;
    result.composition_plan_identity = composition_plan.plan_identity;
    result.qualification_report_identity =
        qualification_report.report_identity;
    result.schedule_identity = schedule.schedule_identity;
    result.world_state_identity = schedule.world_state_identity;
    result.observation_snapshot_identity =
        schedule.observation_snapshot_identity;

    if (!valid_objective(objective)) {
        add(result.issues, AutomaticPlanIssue::Objective);
    }
    if (!validate_technique_graph(technique_graph).ok()) {
        add(result.issues, AutomaticPlanIssue::TechniqueGraph);
    }
    if (!validate_compiled_composition_plan(composition_plan) ||
        composition_plan.technique_graph_identity !=
            technique_graph.graph_identity) {
        add(result.issues, AutomaticPlanIssue::Composition);
    }
    if (!validate_pilot_qualification_report(qualification_report) ||
        qualification_report.technique_graph_identity !=
            technique_graph.graph_identity ||
        qualification_report.composition_plan_identity !=
            composition_plan.plan_identity) {
        add(result.issues, AutomaticPlanIssue::Qualification);
    }
    if (!validate_portfolio_schedule(schedule).ok() ||
        schedule.technique_graph_identity != technique_graph.graph_identity ||
        schedule.composition_plan_identity != composition_plan.plan_identity ||
        schedule.qualification_report_identity !=
            qualification_report.report_identity) {
        add(result.issues, AutomaticPlanIssue::Schedule);
    }
    if (qualification_report.world_state_identity !=
            schedule.world_state_identity ||
        qualification_report.observation_snapshot_identity !=
            schedule.observation_snapshot_identity) {
        add(result.issues, AutomaticPlanIssue::Provenance);
    }

    bool arithmetic_overflow = false;
    std::uint64_t total_samples = 0;
    std::uint64_t wavefront_samples = 0;
    std::uint64_t wavefront_mask = 0;
    for (const auto& node : technique_graph.nodes) {
        AutomaticTechniqueDecision decision;
        decision.node_ordinal = node.ordinal;
        decision.technique_identity = node.descriptor.technique_identity;
        const auto qualification = std::ranges::find(
            qualification_report.decisions,
            node.ordinal,
            &TechniqueQualificationDecision::node_ordinal);
        if (qualification != qualification_report.decisions.end()) {
            decision.evidence_identity = qualification->estimate_identity;
            if (empty(decision.evidence_identity)) {
                decision.evidence_identity =
                    qualification->override_identity;
            }
        }
        for (const auto& allocation : schedule.allocations) {
            if (allocation.node_ordinal != node.ordinal) continue;
            decision.allocated_samples = checked_add(
                decision.allocated_samples,
                allocation.sample_count,
                arithmetic_overflow);
            decision.estimated_nanoseconds = checked_add(
                decision.estimated_nanoseconds,
                allocation.estimated_nanoseconds,
                arithmetic_overflow);
        }
        decision.resident_bytes = node.descriptor.resources.
            persistent_budget_bytes;
        decision.scratch_bytes = node.descriptor.resources.
            scratch_bytes_per_work_item;
        if (qualification == qualification_report.decisions.end() ||
            qualification->status == QualificationStatus::Ineligible ||
            qualification->status == QualificationStatus::ExcludedByOverride) {
            decision.reason = AutomaticDecisionReason::Qualification;
        } else if (qualification->status ==
                       QualificationStatus::ExperimentalOverride &&
                   !objective.allow_experimental) {
            decision.reason = AutomaticDecisionReason::ExperimentalPolicy;
        } else if (!objective.allow_non_unbiased_output &&
                   node.descriptor.estimator.bias != BiasClass::Unbiased) {
            decision.reason = AutomaticDecisionReason::OutputLayer;
        } else if (decision.allocated_samples == 0) {
            decision.reason = AutomaticDecisionReason::NoAllocation;
        } else if (node.descriptor.family ==
                   TechniqueFamily::WavefrontPathTracing) {
            decision.status = AutomaticDecisionStatus::DefensiveBaseline;
            decision.reason =
                AutomaticDecisionReason::DefensiveUnknownDomainCoverage;
        } else {
            decision.status = AutomaticDecisionStatus::Included;
            decision.reason = AutomaticDecisionReason::Scheduled;
        }
        if (empty(decision.evidence_identity)) {
            decision.evidence_identity = result.schedule_identity;
        }
        total_samples = checked_add(
            total_samples,
            decision.allocated_samples,
            arithmetic_overflow);
        if (node.descriptor.family ==
                TechniqueFamily::WavefrontPathTracing &&
            decision.status != AutomaticDecisionStatus::Excluded) {
            wavefront_samples = checked_add(
                wavefront_samples,
                decision.allocated_samples,
                arithmetic_overflow);
            if (node.ordinal < 64) {
                wavefront_mask |= std::uint64_t{1} << node.ordinal;
            }
        }
        result.decisions.push_back(std::move(decision));
    }
    if (arithmetic_overflow || total_samples > objective.maximum_samples ||
        (schedule.spent_nanoseconds > objective.deadline_nanoseconds &&
         objective.kind != AutomaticObjectiveKind::Quality) ||
        schedule.reserved_resident_bytes >
            objective.resident_budget_bytes ||
        schedule.reserved_scratch_bytes > objective.scratch_budget_bytes) {
        add(result.issues, AutomaticPlanIssue::Budget);
    }
    if (wavefront_samples == 0 || total_samples == 0 ||
        static_cast<double>(wavefront_samples) /
                static_cast<double>(total_samples) + 1e-15 <
            objective.minimum_wavefront_fraction) {
        add(result.issues, AutomaticPlanIssue::MissingDefensiveBaseline);
    }

    for (const auto& group : composition_plan.groups) {
        if (!objective.allow_non_unbiased_output &&
            group.layer != EstimateLayer::Unbiased) {
            continue;
        }
        AutomaticPartitionProgram program;
        program.partition_identity = group.partition_identity;
        program.weight_rule_identity =
            weight_rule_identity(composition_plan, group);
        program.layer = group.layer;
        program.family = group.family;
        for (const auto& allocation : schedule.allocations) {
            if (allocation.node_ordinal >= 64 ||
                (group.technique_mask &
                 (std::uint64_t{1} << allocation.node_ordinal)) == 0) {
                continue;
            }
            program.scheduled_technique_mask |=
                std::uint64_t{1} << allocation.node_ordinal;
            program.allocated_samples = checked_add(
                program.allocated_samples,
                allocation.sample_count,
                arithmetic_overflow);
        }
        program.defensive_technique_mask =
            program.scheduled_technique_mask & wavefront_mask;
        if (program.scheduled_technique_mask == 0 ||
            program.allocated_samples == 0) {
            add(result.issues,
                AutomaticPlanIssue::MissingPartitionCoverage);
            continue;
        }
        program.program_identity = program_identity(program);
        result.programs.push_back(std::move(program));
    }
    if (arithmetic_overflow) {
        add(result.issues, AutomaticPlanIssue::Budget);
    }
    if (result.programs.empty()) {
        add(result.issues, AutomaticPlanIssue::OutputLayer);
    }
    result.automatically_selected = result.issues.empty();
    result.production_executable = result.issues.empty() &&
        !objective.allow_experimental &&
        !objective.allow_non_unbiased_output &&
        qualification_report.production_executable;
    if (result.production_executable) {
        result.plan_identity = plan_identity(result);
    }
    if (result.production_executable &&
        !validate_automatic_integrator_plan(result)) {
        throw std::runtime_error("Generated invalid automatic integrator plan");
    }
    return result;
}

bool validate_automatic_integrator_plan(
    const AutomaticIntegratorPlan& plan) {
    if (plan.version != kAutomaticIntegratorContractVersion ||
        empty(plan.plan_identity) ||
        empty(plan.objective_identity) ||
        empty(plan.technique_graph_identity) ||
        empty(plan.composition_plan_identity) ||
        empty(plan.qualification_report_identity) ||
        empty(plan.schedule_identity) ||
        empty(plan.world_state_identity) ||
        empty(plan.observation_snapshot_identity) ||
        plan.legacy_preset_disposition !=
            LegacyPresetDisposition::CompatibilityAndReproducibilityOnly ||
        !plan.automatically_selected ||
        !plan.production_executable ||
        !plan.issues.empty() ||
        plan.decisions.empty() ||
        plan.programs.empty()) {
        return false;
    }
    std::set<std::uint32_t> ordinals;
    bool defensive = false;
    for (const auto& decision : plan.decisions) {
        if (!valid_decision(decision) ||
            !ordinals.insert(decision.node_ordinal).second) {
            return false;
        }
        defensive = defensive ||
            decision.status == AutomaticDecisionStatus::DefensiveBaseline;
    }
    if (!defensive) return false;
    std::set<semantic::IdentityDigest> partitions;
    for (const auto& program : plan.programs) {
        if (!valid_program(program) ||
            !partitions.insert(program.partition_identity).second) {
            return false;
        }
    }
    return plan.plan_identity == plan_identity(plan);
}

semantic::IdentityDigest compute_automatic_partition_observation_identity(
    const AutomaticPartitionObservation& observation) {
    Encoder encoder;
    encoder.u32(observation.version);
    encoder.digest(observation.plan_identity);
    encoder.digest(observation.program_identity);
    encoder.digest(observation.partition_identity);
    encoder.digest(observation.measurement_identity);
    encoder.digest(observation.weight_rule_identity);
    encoder.digest(observation.normalization_identity);
    encoder.u64(observation.technique_mask);
    encoder.u64(observation.sample_count);
    encoder.f64(observation.estimate);
    encoder.f64(observation.sample_variance);
    encoder.f64(observation.effective_sample_size);
    encoder.f64(observation.maximum_absolute_contribution);
    encoder.u64(observation.elapsed_nanoseconds);
    encoder.u64(observation.peak_resident_bytes);
    encoder.u64(observation.peak_scratch_bytes);
    return runtime::identity_digest(encoder.bytes());
}

void finalize_automatic_partition_observation(
    AutomaticPartitionObservation& observation) {
    observation.observation_identity =
        compute_automatic_partition_observation_identity(observation);
    if (!valid_observation(observation)) {
        throw std::invalid_argument(
            "Invalid automatic partition observation");
    }
}

AutomaticOutputTrace close_automatic_integrator_output(
    const AutomaticIntegratorPlan& plan,
    const AutomaticIntegratorObjective& objective,
    std::span<const AutomaticPartitionObservation> observations) {
    if (!validate_automatic_integrator_plan(plan) ||
        !valid_objective(objective) ||
        objective.objective_identity != plan.objective_identity ||
        observations.size() != plan.programs.size()) {
        throw std::invalid_argument("Invalid automatic output closure input");
    }
    AutomaticOutputTrace result;
    result.plan_identity = plan.plan_identity;
    result.objective_identity = plan.objective_identity;
    result.technique_graph_identity = plan.technique_graph_identity;
    result.composition_plan_identity = plan.composition_plan_identity;
    result.schedule_identity = plan.schedule_identity;
    result.world_state_identity = plan.world_state_identity;
    result.observation_snapshot_identity =
        plan.observation_snapshot_identity;
    result.confidence_level = objective.confidence_level;
    std::set<semantic::IdentityDigest> seen_programs;
    Encoder measurements;
    for (const auto& program : plan.programs) {
        const auto found = std::ranges::find(
            observations,
            program.program_identity,
            &AutomaticPartitionObservation::program_identity);
        if (found == observations.end() ||
            !valid_observation(*found) ||
            found->plan_identity != plan.plan_identity ||
            found->partition_identity != program.partition_identity ||
            found->weight_rule_identity != program.weight_rule_identity ||
            found->technique_mask != program.scheduled_technique_mask ||
            !seen_programs.insert(found->program_identity).second) {
            throw std::invalid_argument(
                "Automatic partition evidence does not match the plan");
        }
        result.estimate += found->estimate;
        result.standard_error += std::sqrt(
            found->sample_variance / found->effective_sample_size);
        result.maximum_absolute_contribution = std::max(
            result.maximum_absolute_contribution,
            found->maximum_absolute_contribution);
        result.elapsed_nanoseconds = checked_add(
            result.elapsed_nanoseconds,
            found->elapsed_nanoseconds,
            result.complete);
        result.sample_count = checked_add(
            result.sample_count,
            found->sample_count,
            result.complete);
        result.peak_resident_bytes = std::max(
            result.peak_resident_bytes,
            found->peak_resident_bytes);
        result.peak_scratch_bytes = std::max(
            result.peak_scratch_bytes,
            found->peak_scratch_bytes);
        result.technique_coverage_mask |= found->technique_mask;
        result.partition_observation_identities.push_back(
            found->observation_identity);
        measurements.digest(found->measurement_identity);
    }
    if (result.complete) {
        throw std::overflow_error("Automatic output counters overflowed");
    }
    result.measurement_set_identity =
        runtime::identity_digest(measurements.bytes());
    const auto quantile = normal_quantile(
        0.5 + objective.confidence_level * 0.5);
    result.confidence_lower =
        result.estimate - quantile * result.standard_error;
    result.confidence_upper =
        result.estimate + quantile * result.standard_error;
    const auto relative_scale = std::max(
        std::abs(result.estimate), 1e-12);
    result.quality_target_met =
        result.standard_error / relative_scale <=
        objective.target_relative_standard_error;
    result.deadline_met = objective.deadline_nanoseconds == 0 ||
        result.elapsed_nanoseconds <= objective.deadline_nanoseconds;
    result.complete = true;
    result.trace_identity = output_trace_identity(result);
    if (!validate_automatic_output_trace(result)) {
        throw std::runtime_error("Generated invalid automatic output trace");
    }
    return result;
}

bool validate_automatic_output_trace(
    const AutomaticOutputTrace& trace) {
    return trace.version == kAutomaticIntegratorContractVersion &&
        !empty(trace.trace_identity) &&
        trace.trace_identity == output_trace_identity(trace) &&
        !empty(trace.plan_identity) &&
        !empty(trace.objective_identity) &&
        !empty(trace.technique_graph_identity) &&
        !empty(trace.composition_plan_identity) &&
        !empty(trace.schedule_identity) &&
        !empty(trace.world_state_identity) &&
        !empty(trace.observation_snapshot_identity) &&
        !empty(trace.measurement_set_identity) &&
        trace.technique_coverage_mask != 0 &&
        trace.sample_count > 1 &&
        std::isfinite(trace.estimate) &&
        std::isfinite(trace.standard_error) &&
        trace.standard_error >= 0.0 &&
        std::isfinite(trace.confidence_level) &&
        trace.confidence_level > 0.5 &&
        trace.confidence_level < 1.0 &&
        std::isfinite(trace.confidence_lower) &&
        std::isfinite(trace.confidence_upper) &&
        trace.confidence_lower <= trace.confidence_upper &&
        std::isfinite(trace.maximum_absolute_contribution) &&
        trace.maximum_absolute_contribution >= 0.0 &&
        trace.elapsed_nanoseconds > 0 &&
        trace.peak_resident_bytes > 0 &&
        trace.peak_scratch_bytes > 0 &&
        trace.complete &&
        !trace.partition_observation_identities.empty();
}

}
