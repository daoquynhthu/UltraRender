#include "ure/transport/technique_graph.hpp"

#include "ure/runtime/multi_backend.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <vector>

namespace ure::transport {
namespace {

class Encoder {
public:
    void u8(std::uint8_t value) {
        bytes_.push_back(static_cast<std::byte>(value));
    }
    void i8(std::int8_t value) {
        u8(static_cast<std::uint8_t>(value));
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
    void digest(const semantic::IdentityDigest& value) {
        for (const auto byte : value) u8(byte);
    }
    std::span<const std::byte> bytes() const { return bytes_; }

private:
    std::vector<std::byte> bytes_;
};

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

void encode_estimator(Encoder& encoder,
                      const EstimatorDescriptor& value) {
    encoder.u32(value.version);
    encoder.digest(value.technique_identity);
    encode_observable(encoder, value.observable);
    encoder.u32(value.measure.version);
    encoder.digest(value.measure.integral_identity);
    encoder.digest(value.measure.coordinate_identity);
    encoder.digest(value.measure.conversion_identity);
    encoder.u8(value.measure.term_count);
    const auto term_count = std::min<std::size_t>(
        value.measure.term_count, kMaxMeasureTerms);
    for (std::size_t index = 0; index < term_count; ++index) {
        encoder.u8(static_cast<std::uint8_t>(
            value.measure.terms[index].domain));
        encoder.i8(value.measure.terms[index].exponent);
    }
    encoder.u32(value.support.version);
    encoder.u64(value.support.event_mask);
    encoder.u32(value.support.max_depth);
    encoder.u8(value.support.overlap_known ? 1 : 0);
    encoder.u8(value.support.singular_support ? 1 : 0);
    encoder.digest(value.support.partition_identity);
    encoder.u32(value.support.partition_index);
    encoder.u32(value.support.partition_count);
    encoder.u8(static_cast<std::uint8_t>(value.density));
    encoder.u8(static_cast<std::uint8_t>(value.normalization));
    encoder.u8(static_cast<std::uint8_t>(value.correlation));
    encoder.u8(static_cast<std::uint8_t>(value.bias));
    encoder.u8(value.replayable ? 1 : 0);
    encoder.u8(value.tangent_capable ? 1 : 0);
    encoder.u8(value.adjoint_capable ? 1 : 0);
}

void add(TechniqueGraphValidation& result,
         TechniqueGraphIssue issue,
         std::uint32_t node = 0,
         std::uint32_t edge = 0) {
    result.diagnostics.push_back({issue, node, edge});
}

bool same_observable(const ObservableDescriptor& left,
                     const ObservableDescriptor& right) {
    return left == right;
}

bool edge_less(const TechniqueEdge& left,
               const TechniqueEdge& right) {
    if (left.source != right.source) return left.source < right.source;
    if (left.target != right.target) return left.target < right.target;
    return left.kind < right.kind;
}

bool valid_family(TechniqueFamily family) {
    return family >= TechniqueFamily::WavefrontPathTracing &&
           family <= TechniqueFamily::ResearchExtension;
}

bool valid_role(TechniqueRole role) {
    return role >= TechniqueRole::Estimator &&
           role <= TechniqueRole::ReplayKernel;
}

bool valid_edge_kind(TechniqueEdgeKind kind) {
    return kind >= TechniqueEdgeKind::ProposalFor &&
           kind <= TechniqueEdgeKind::CoupledEstimatorFamily;
}

}

bool TechniqueGraphValidation::has(TechniqueGraphIssue issue) const {
    return std::ranges::any_of(
        diagnostics,
        [issue](const TechniqueGraphDiagnostic& diagnostic) {
            return diagnostic.issue == issue;
        });
}

semantic::IdentityDigest compute_technique_graph_identity(
    const TechniqueGraph& graph) {
    Encoder encoder;
    encoder.u32(graph.version);
    encoder.u32(static_cast<std::uint32_t>(graph.nodes.size()));
    for (const auto& node : graph.nodes) {
        const auto& value = node.descriptor;
        encoder.u32(node.ordinal);
        encoder.u32(value.version);
        encoder.u8(static_cast<std::uint8_t>(value.family));
        encoder.u8(static_cast<std::uint8_t>(value.role));
        encoder.digest(value.technique_identity);
        encoder.digest(value.sample_space_identity);
        encoder.digest(value.parameter_identity);
        encoder.digest(value.persistent_state_identity);
        encoder.digest(value.replay_layout_identity);
        if (value.family == TechniqueFamily::ResearchExtension) {
            encoder.digest(value.research_capsule_identity);
        }
        encode_estimator(encoder, value.estimator);
        encoder.u8(static_cast<std::uint8_t>(value.resources.scaling));
        encoder.u8(value.resources.cost_estimate_known ? 1 : 0);
        encoder.u64(value.resources.nanoseconds_per_sample);
        encoder.u8(value.resources.scratch_bound_known ? 1 : 0);
        encoder.u64(value.resources.scratch_bytes_per_work_item);
        encoder.u64(value.resources.persistent_budget_bytes);
        encoder.u32(value.resources.max_samples_per_pass);
        encoder.digest(value.resources.backend_capability_identity);
        encoder.u8(value.contributes_estimate ? 1 : 0);
        encoder.u8(value.owns_normalization ? 1 : 0);
        encoder.u8(value.adaptive_state ? 1 : 0);
        encoder.u8(value.replayable ? 1 : 0);
        encoder.u8(value.requires_shared_spectral_primary_sample ? 1 : 0);
    }
    encoder.u32(static_cast<std::uint32_t>(graph.edges.size()));
    for (const auto& edge : graph.edges) {
        encoder.u32(edge.source);
        encoder.u32(edge.target);
        encoder.u8(static_cast<std::uint8_t>(edge.kind));
    }
    return runtime::identity_digest(encoder.bytes());
}

void finalize_technique_graph(TechniqueGraph& graph) {
    std::ranges::sort(graph.edges, edge_less);
    graph.graph_identity = compute_technique_graph_identity(graph);
    const auto validation = validate_technique_graph(graph);
    if (!validation.ok()) {
        throw std::invalid_argument("Invalid technique graph");
    }
}

TechniqueGraphValidation validate_technique_graph(
    const TechniqueGraph& graph) {
    TechniqueGraphValidation result;
    if (graph.version != kTechniqueGraphVersion) {
        add(result, TechniqueGraphIssue::Version);
    }
    if (semantic::identity_empty(graph.graph_identity)) {
        add(result, TechniqueGraphIssue::Identity);
    }
    if (graph.nodes.empty()) {
        add(result, TechniqueGraphIssue::MissingEstimator);
        return result;
    }

    bool has_estimator = false;
    const ObservableDescriptor* observable = nullptr;
    semantic::IdentityDigest integral_identity{};
    std::vector<std::uint32_t> outgoing(graph.nodes.size(), 0);
    for (std::size_t index = 0; index < graph.nodes.size(); ++index) {
        const auto& node = graph.nodes[index];
        const auto& descriptor = node.descriptor;
        if (node.ordinal != index) {
            add(result, TechniqueGraphIssue::NodeOrder,
                static_cast<std::uint32_t>(index));
        }
        if (descriptor.version != kTechniqueGraphVersion ||
            !valid_family(descriptor.family) ||
            !valid_role(descriptor.role) ||
            descriptor.resources.scaling < TechniqueResourceScaling::None ||
            descriptor.resources.scaling > TechniqueResourceScaling::Solver ||
            (descriptor.resources.cost_estimate_known !=
             (descriptor.resources.nanoseconds_per_sample != 0)) ||
            (!descriptor.resources.scratch_bound_known &&
             descriptor.resources.scratch_bytes_per_work_item != 0) ||
            semantic::identity_empty(
                descriptor.resources.backend_capability_identity) ||
            semantic::identity_empty(descriptor.technique_identity) ||
            semantic::identity_empty(descriptor.sample_space_identity) ||
            semantic::identity_empty(descriptor.parameter_identity) ||
            ((descriptor.family == TechniqueFamily::ResearchExtension) !=
             !semantic::identity_empty(
                 descriptor.research_capsule_identity)) ||
            (descriptor.adaptive_state &&
             semantic::identity_empty(
                 descriptor.persistent_state_identity)) ||
            (descriptor.replayable &&
             semantic::identity_empty(
                 descriptor.replay_layout_identity)) ||
            (descriptor.contributes_estimate !=
             descriptor.owns_normalization) ||
            (descriptor.contributes_estimate &&
             descriptor.role != TechniqueRole::Estimator) ||
            (!descriptor.contributes_estimate &&
             descriptor.role == TechniqueRole::Estimator)) {
            add(result, TechniqueGraphIssue::Descriptor,
                static_cast<std::uint32_t>(index));
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (graph.nodes[previous].descriptor.technique_identity ==
                descriptor.technique_identity) {
                add(result, TechniqueGraphIssue::DuplicateNode,
                    static_cast<std::uint32_t>(index));
                break;
            }
        }
        if (!descriptor.contributes_estimate) continue;
        has_estimator = true;
        if (!validate_estimator(descriptor.estimator).ok() ||
            descriptor.estimator.technique_identity !=
                descriptor.technique_identity) {
            add(result, TechniqueGraphIssue::Estimator,
                static_cast<std::uint32_t>(index));
            continue;
        }
        if (!observable) {
            observable = &descriptor.estimator.observable;
            integral_identity =
                descriptor.estimator.measure.integral_identity;
        } else {
            if (!same_observable(*observable,
                                 descriptor.estimator.observable)) {
                add(result, TechniqueGraphIssue::ObservableMismatch,
                    static_cast<std::uint32_t>(index));
            }
            if (integral_identity !=
                descriptor.estimator.measure.integral_identity) {
                add(result, TechniqueGraphIssue::IntegralMismatch,
                    static_cast<std::uint32_t>(index));
            }
        }
    }
    if (!has_estimator) add(result, TechniqueGraphIssue::MissingEstimator);

    std::vector<std::uint32_t> indegree(graph.nodes.size(), 0);
    for (std::size_t index = 0; index < graph.edges.size(); ++index) {
        const auto& edge = graph.edges[index];
        if (index > 0 && !edge_less(graph.edges[index - 1], edge)) {
            add(result, TechniqueGraphIssue::EdgeOrder, 0,
                static_cast<std::uint32_t>(index));
        }
        if (!valid_edge_kind(edge.kind) ||
            edge.source >= graph.nodes.size() ||
            edge.target >= graph.nodes.size() ||
            edge.source == edge.target) {
            add(result, TechniqueGraphIssue::EdgeEndpoint, 0,
                static_cast<std::uint32_t>(index));
            continue;
        }
        const auto& source = graph.nodes[edge.source].descriptor;
        const auto& target = graph.nodes[edge.target].descriptor;
        bool semantic_ok = false;
        switch (edge.kind) {
        case TechniqueEdgeKind::ProposalFor:
            semantic_ok = source.role == TechniqueRole::ProposalService &&
                          !source.contributes_estimate &&
                          target.contributes_estimate;
            break;
        case TechniqueEdgeKind::ReplayFor:
            semantic_ok = source.role == TechniqueRole::ReplayKernel &&
                          !source.contributes_estimate &&
                          target.contributes_estimate;
            break;
        case TechniqueEdgeKind::DisjointSupport:
            semantic_ok = source.contributes_estimate &&
                          target.contributes_estimate &&
                          source.estimator.support.partition_count > 1 &&
                          source.estimator.support.partition_identity ==
                              target.estimator.support.partition_identity &&
                          source.estimator.support.partition_index !=
                              target.estimator.support.partition_index;
            break;
        case TechniqueEdgeKind::CoupledEstimatorFamily:
            semantic_ok = source.contributes_estimate &&
                          target.contributes_estimate;
            break;
        }
        if (!semantic_ok) {
            add(result, TechniqueGraphIssue::EdgeSemantics, 0,
                static_cast<std::uint32_t>(index));
        }
        ++outgoing[edge.source];
        ++indegree[edge.target];
    }
    for (std::size_t index = 0; index < graph.nodes.size(); ++index) {
        if (!graph.nodes[index].descriptor.contributes_estimate &&
            outgoing[index] == 0) {
            add(result, TechniqueGraphIssue::MissingConsumer,
                static_cast<std::uint32_t>(index));
        }
    }

    std::vector<std::uint32_t> queue;
    for (std::size_t index = 0; index < indegree.size(); ++index) {
        if (indegree[index] == 0) {
            queue.push_back(static_cast<std::uint32_t>(index));
        }
    }
    std::size_t visited = 0;
    for (std::size_t cursor = 0; cursor < queue.size(); ++cursor) {
        const auto node = queue[cursor];
        ++visited;
        for (const auto& edge : graph.edges) {
            if (edge.source == node && --indegree[edge.target] == 0) {
                queue.push_back(edge.target);
            }
        }
    }
    if (visited != graph.nodes.size()) {
        add(result, TechniqueGraphIssue::Cycle);
    }
    if (!semantic::identity_empty(graph.graph_identity) &&
        graph.graph_identity != compute_technique_graph_identity(graph)) {
        add(result, TechniqueGraphIssue::GraphIdentity);
    }
    return result;
}

}
