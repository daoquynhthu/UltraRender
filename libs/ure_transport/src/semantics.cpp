#include "ure/transport/semantics.hpp"

#include <algorithm>
#include <cmath>

namespace ure::transport {
namespace {

void add_issue(ValidationResult& result, DescriptorIssue issue) {
    result.issues |= static_cast<std::uint64_t>(issue);
}

bool requires_phase_reference(const ObservableDescriptor& descriptor) {
    return descriptor.coherence != CoherenceClass::Incoherent ||
           descriptor.value_domain == ValueDomain::ComplexJones ||
           descriptor.value_domain ==
               ValueDomain::HermitianCrossSpectralDensity;
}

bool valid_observable_domain(const ObservableDescriptor& descriptor) {
    switch (descriptor.kind) {
    case ObservableKind::SpectralRadiance:
        return descriptor.value_domain == ValueDomain::Spectrum;
    case ObservableKind::StokesRadiance:
        return descriptor.value_domain == ValueDomain::Stokes;
    case ObservableKind::JonesField:
        return descriptor.value_domain == ValueDomain::ComplexJones &&
               descriptor.coherence == CoherenceClass::Coherent;
    case ObservableKind::MutualIntensity:
        return descriptor.value_domain ==
                   ValueDomain::HermitianCrossSpectralDensity &&
               descriptor.coherence ==
                   CoherenceClass::PartiallyCoherent;
    case ObservableKind::TransientRadiance:
        return descriptor.time_resolved &&
               (descriptor.value_domain == ValueDomain::Spectrum ||
                descriptor.value_domain == ValueDomain::Stokes);
    case ObservableKind::SensorResponse:
        return descriptor.value_domain == ValueDomain::Scalar ||
               descriptor.value_domain == ValueDomain::LinearRgb ||
               descriptor.value_domain == ValueDomain::Spectrum;
    case ObservableKind::LossFunctional:
        return descriptor.value_domain == ValueDomain::Scalar;
    }
    return false;
}

bool valid_component_count(const ObservableDescriptor& descriptor) {
    switch (descriptor.value_domain) {
    case ValueDomain::Scalar:
        return descriptor.component_count == 1;
    case ValueDomain::LinearRgb:
        return descriptor.component_count == 3;
    case ValueDomain::Stokes:
    case ValueDomain::ComplexJones:
        return descriptor.component_count == 4;
    case ValueDomain::Spectrum:
        return descriptor.component_count > 0;
    case ValueDomain::HermitianCrossSpectralDensity:
        return descriptor.component_count > 0 &&
               descriptor.component_count % 2 == 0;
    }
    return false;
}

bool required_provenance_present(
    const semantic::ProvenanceIdentitySet& provenance) {
    return !semantic::identity_empty(provenance.world_definition) &&
           !semantic::identity_empty(provenance.world_state) &&
           !semantic::identity_empty(provenance.time_sample) &&
           !semantic::identity_empty(
               provenance.observation_snapshot) &&
           !semantic::identity_empty(provenance.technique_graph) &&
           !semantic::identity_empty(
               provenance.measurement_schema);
}

bool required_provenance_equal(
    const semantic::ProvenanceIdentitySet& left,
    const semantic::ProvenanceIdentitySet& right) {
    return left.world_definition == right.world_definition &&
           left.world_state == right.world_state &&
           left.time_sample == right.time_sample &&
           left.observation_snapshot ==
               right.observation_snapshot &&
           left.technique_graph == right.technique_graph &&
           left.measurement_schema ==
               right.measurement_schema;
}

bool same_observable_shape(const ObservableDescriptor& left,
                           const ObservableDescriptor& right) {
    return left.kind == right.kind &&
           left.value_domain == right.value_domain &&
           left.coherence == right.coherence &&
           left.component_count == right.component_count &&
           left.time_resolved == right.time_resolved &&
           left.phase_reference_identity ==
               right.phase_reference_identity &&
           left.sensor_response_identity ==
               right.sensor_response_identity;
}

bool same_measure_terms(const MeasureDescriptor& left,
                        const MeasureDescriptor& right) {
    if (left.term_count != right.term_count) return false;
    for (std::uint8_t index = 0; index < left.term_count; ++index) {
        if (left.terms[index] != right.terms[index]) return false;
    }
    return left.coordinate_identity == right.coordinate_identity;
}

CompatibilityDecision decision(CompatibilityKind kind,
                               CompatibilityReason reason,
                               CombinationRule rule,
                               bool share,
                               bool mis) {
    return {kind, reason, rule, share, mis};
}

}

ValidationResult validate_observable(
    const ObservableDescriptor& descriptor) {
    ValidationResult result;
    if (descriptor.version != kSemanticContractVersion) {
        add_issue(result, DescriptorIssue::Version);
    }
    if (!semantic::valid_unit(descriptor.unit)) {
        add_issue(result, DescriptorIssue::Unit);
    }
    if (!valid_observable_domain(descriptor)) {
        add_issue(result, DescriptorIssue::ObservableDomain);
    }
    if (!valid_component_count(descriptor)) {
        add_issue(result, DescriptorIssue::ComponentCount);
    }
    if (descriptor.coherence == CoherenceClass::Incoherent &&
        (descriptor.value_domain == ValueDomain::ComplexJones ||
         descriptor.value_domain ==
             ValueDomain::HermitianCrossSpectralDensity)) {
        add_issue(result, DescriptorIssue::Coherence);
    }
    if (requires_phase_reference(descriptor) &&
        semantic::identity_empty(
            descriptor.phase_reference_identity)) {
        add_issue(result, DescriptorIssue::PhaseReference);
    }
    if (descriptor.kind == ObservableKind::SensorResponse &&
        semantic::identity_empty(
            descriptor.sensor_response_identity)) {
        add_issue(result, DescriptorIssue::Identity);
    }
    return result;
}

ValidationResult validate_measure(
    const MeasureDescriptor& descriptor) {
    ValidationResult result;
    if (descriptor.version != kSemanticContractVersion) {
        add_issue(result, DescriptorIssue::Version);
    }
    if (semantic::identity_empty(descriptor.integral_identity)) {
        add_issue(result, DescriptorIssue::Identity);
    }
    if (descriptor.term_count == 0 ||
        descriptor.term_count > kMaxMeasureTerms) {
        add_issue(result, DescriptorIssue::Measure);
        return result;
    }
    for (std::uint8_t index = 0; index < descriptor.term_count;
         ++index) {
        if (descriptor.terms[index].exponent == 0) {
            add_issue(result, DescriptorIssue::Measure);
        }
        if (index > 0 &&
            descriptor.terms[index - 1].domain >=
                descriptor.terms[index].domain) {
            add_issue(result, DescriptorIssue::Measure);
        }
    }
    return result;
}

ValidationResult validate_support(
    const SupportDescriptor& descriptor) {
    ValidationResult result;
    if (descriptor.version != kSemanticContractVersion) {
        add_issue(result, DescriptorIssue::Version);
    }
    if (descriptor.event_mask == 0 || descriptor.max_depth == 0 ||
        descriptor.partition_count == 0 ||
        descriptor.partition_index >= descriptor.partition_count) {
        add_issue(result, DescriptorIssue::Support);
    }
    if (descriptor.partition_count > 1 &&
        semantic::identity_empty(descriptor.partition_identity)) {
        add_issue(result, DescriptorIssue::Identity);
    }
    return result;
}

ValidationResult validate_estimator(
    const EstimatorDescriptor& descriptor) {
    ValidationResult result;
    if (descriptor.version != kSemanticContractVersion) {
        add_issue(result, DescriptorIssue::Version);
    }
    if (semantic::identity_empty(descriptor.technique_identity)) {
        add_issue(result, DescriptorIssue::Identity);
    }
    result.issues |= validate_observable(descriptor.observable).issues;
    result.issues |= validate_measure(descriptor.measure).issues;
    result.issues |= validate_support(descriptor.support).issues;
    if (descriptor.density == DensityKind::Unknown &&
        descriptor.bias != BiasClass::UnknownResearch) {
        add_issue(result, DescriptorIssue::Density);
    }
    if (descriptor.normalization == NormalizationKind::Unknown &&
        descriptor.bias != BiasClass::UnknownResearch) {
        add_issue(result, DescriptorIssue::Normalization);
    }
    if (descriptor.correlation == CorrelationModel::MarkovChain &&
        (descriptor.density != DensityKind::MarkovTransition ||
         descriptor.normalization !=
             NormalizationKind::ChainBootstrap)) {
        add_issue(result, DescriptorIssue::Correlation);
    }
    if (descriptor.correlation ==
            CorrelationModel::ReservoirReuse &&
        (descriptor.density !=
             DensityKind::NormalizedReservoirWeight ||
         descriptor.normalization !=
             NormalizationKind::ReservoirNormalization)) {
        add_issue(result, DescriptorIssue::Correlation);
    }
    return result;
}

ValidationResult validate_uncertainty(
    const UncertaintyDescriptor& descriptor) {
    ValidationResult result;
    if (descriptor.version != kSemanticContractVersion) {
        add_issue(result, DescriptorIssue::Version);
    }
    if (descriptor.channel_count == 0 ||
        !std::isfinite(descriptor.effective_sample_size) ||
        descriptor.effective_sample_size < 0.0 ||
        !std::isfinite(descriptor.confidence_level) ||
        descriptor.confidence_level <= 0.0 ||
        descriptor.confidence_level >= 1.0 ||
        (descriptor.cross_moments &&
         descriptor.channel_count < 2) ||
        (descriptor.second_moment && !descriptor.first_moment)) {
        add_issue(result, DescriptorIssue::Uncertainty);
    }
    if (descriptor.ood_status != OodStatus::NotApplicable &&
        !descriptor.calibrated_model_confidence) {
        add_issue(result, DescriptorIssue::Uncertainty);
    }
    return result;
}

ValidationResult validate_context(const SemanticContext& context) {
    ValidationResult result;
    if (context.version != kSemanticContractVersion) {
        add_issue(result, DescriptorIssue::Version);
    }
    if (!required_provenance_present(context.provenance)) {
        add_issue(result, DescriptorIssue::Provenance);
    }
    if (!semantic::valid_time_interval(context.observation_time)) {
        add_issue(result, DescriptorIssue::Time);
    }
    return result;
}

CompatibilityDecision classify_compatibility(
    const EstimatorDescriptor& left,
    const SemanticContext& left_context,
    const EstimatorDescriptor& right,
    const SemanticContext& right_context) {
    if (!validate_estimator(left).ok() ||
        !validate_estimator(right).ok() ||
        !validate_context(left_context).ok() ||
        !validate_context(right_context).ok()) {
        return decision(CompatibilityKind::Undefined,
                        CompatibilityReason::InvalidDescriptor,
                        CombinationRule::None,
                        false,
                        false);
    }
    if (left.density == DensityKind::Unknown ||
        right.density == DensityKind::Unknown ||
        left.normalization == NormalizationKind::Unknown ||
        right.normalization == NormalizationKind::Unknown) {
        return decision(CompatibilityKind::Undefined,
                        CompatibilityReason::InvalidDescriptor,
                        CombinationRule::None,
                        false,
                        false);
    }
    if (!required_provenance_equal(left_context.provenance,
                                   right_context.provenance)) {
        return decision(CompatibilityKind::Undefined,
                        CompatibilityReason::ProvenanceMismatch,
                        CombinationRule::None,
                        false,
                        false);
    }
    if (left_context.observation_time.basis.clock_identity !=
        right_context.observation_time.basis.clock_identity) {
        return decision(CompatibilityKind::Undefined,
                        CompatibilityReason::TimeClockMismatch,
                        CombinationRule::None,
                        false,
                        false);
    }
    if (!semantic::same_time_basis(
            left_context.observation_time.basis,
            right_context.observation_time.basis)) {
        return decision(CompatibilityKind::RequiresTransform,
                        CompatibilityReason::TimeBasisTransform,
                        CombinationRule::TemporalTransformThenCombine,
                        false,
                        false);
    }
    if (left_context.observation_time.start_tick !=
            right_context.observation_time.start_tick ||
        left_context.observation_time.end_tick !=
            right_context.observation_time.end_tick) {
        return decision(CompatibilityKind::Undefined,
                        CompatibilityReason::TimeIntervalMismatch,
                        CombinationRule::None,
                        false,
                        false);
    }
    if (!same_observable_shape(left.observable, right.observable)) {
        return decision(CompatibilityKind::Undefined,
                        CompatibilityReason::ObservableMismatch,
                        CombinationRule::None,
                        false,
                        false);
    }
    if (!semantic::same_dimension(left.observable.unit,
                                  right.observable.unit)) {
        return decision(CompatibilityKind::Undefined,
                        CompatibilityReason::UnitMismatch,
                        CombinationRule::None,
                        false,
                        false);
    }
    if (!semantic::same_unit_mapping(left.observable.unit,
                                     right.observable.unit)) {
        return decision(CompatibilityKind::RequiresTransform,
                        CompatibilityReason::UnitTransform,
                        CombinationRule::ValueTransformThenCombine,
                        false,
                        false);
    }
    if (left.measure.integral_identity !=
        right.measure.integral_identity) {
        return decision(CompatibilityKind::Undefined,
                        CompatibilityReason::IntegralIdentityMismatch,
                        CombinationRule::None,
                        false,
                        false);
    }
    if (!same_measure_terms(left.measure, right.measure)) {
        if (!semantic::identity_empty(
                left.measure.conversion_identity) &&
            left.measure.conversion_identity ==
                right.measure.conversion_identity) {
            return decision(CompatibilityKind::RequiresTransform,
                            CompatibilityReason::MeasureTransform,
                            CombinationRule::MeasureTransformThenMis,
                            false,
                            false);
        }
        return decision(CompatibilityKind::Undefined,
                        CompatibilityReason::MeasureMismatch,
                        CombinationRule::None,
                        false,
                        false);
    }
    if (left.bias == BiasClass::BiasedPreview ||
        right.bias == BiasClass::BiasedPreview) {
        return decision(CompatibilityKind::PreviewOnly,
                        CompatibilityReason::BiasedPreview,
                        CombinationRule::SeparatePreview,
                        false,
                        false);
    }
    if (!left.support.overlap_known ||
        !right.support.overlap_known) {
        return decision(CompatibilityKind::Undefined,
                        CompatibilityReason::SupportUnknown,
                        CombinationRule::None,
                        false,
                        false);
    }
    if (left.correlation == CorrelationModel::MarkovChain ||
        right.correlation == CorrelationModel::MarkovChain) {
        return decision(
            CompatibilityKind::IndependentAggregate,
            CompatibilityReason::MarkovChains,
            CombinationRule::IndependentReplicateAggregate,
            true,
            false);
    }
    const bool partitioned =
        !semantic::identity_empty(
            left.support.partition_identity) &&
        left.support.partition_identity ==
            right.support.partition_identity &&
        left.support.partition_count ==
            right.support.partition_count &&
        left.support.partition_index !=
            right.support.partition_index;
    const bool disjoint_events =
        (left.support.event_mask & right.support.event_mask) == 0;
    if (partitioned || disjoint_events) {
        return decision(CompatibilityKind::Compatible,
                        CompatibilityReason::DisjointSupport,
                        CombinationRule::DisjointSupportSum,
                        true,
                        false);
    }
    if (left.correlation == CorrelationModel::ReservoirReuse ||
        right.correlation == CorrelationModel::ReservoirReuse) {
        return decision(CompatibilityKind::Compatible,
                        CompatibilityReason::CorrelatedReuse,
                        CombinationRule::GeneralizedResampling,
                        true,
                        false);
    }
    return decision(CompatibilityKind::Compatible,
                    CompatibilityReason::Exact,
                    CombinationRule::MultipleImportanceSampling,
                    true,
                    true);
}

}
