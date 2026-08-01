#include "ure/transport/support_measure_graph.hpp"

#include "ure/runtime/multi_backend.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <stdexcept>
#include <utility>

namespace ure::transport {
namespace {

constexpr std::array<PathEvent, kPathEventSymbolCount> kPathEvents = {
    PathEvent::Camera,
    PathEvent::Emitter,
    PathEvent::Diffuse,
    PathEvent::Glossy,
    PathEvent::DeltaReflection,
    PathEvent::DeltaTransmission,
    PathEvent::VolumeScatter,
    PathEvent::WavelengthShift,
    PathEvent::Diffractive,
    PathEvent::Coherent};

constexpr std::uint64_t kKnownPathEventMask =
    path_event_mask(PathEvent::Camera) |
    path_event_mask(PathEvent::Emitter) |
    path_event_mask(PathEvent::Diffuse) |
    path_event_mask(PathEvent::Glossy) |
    path_event_mask(PathEvent::DeltaReflection) |
    path_event_mask(PathEvent::DeltaTransmission) |
    path_event_mask(PathEvent::VolumeScatter) |
    path_event_mask(PathEvent::WavelengthShift) |
    path_event_mask(PathEvent::Diffractive) |
    path_event_mask(PathEvent::Coherent);

class Encoder {
public:
    void u8(std::uint8_t value) {
        bytes_.push_back(static_cast<std::byte>(value));
    }
    void u16(std::uint16_t value) {
        u8(static_cast<std::uint8_t>(value));
        u8(static_cast<std::uint8_t>(value >> 8));
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

struct NfaEdge {
    std::uint32_t target = 0;
    std::uint64_t event_mask = 0;
    bool epsilon = false;
};

struct NfaState {
    std::vector<NfaEdge> edges;
    bool accepting = false;
};

struct ProductState {
    std::uint32_t target = kInvalidAutomatonState;
    std::vector<std::uint32_t> techniques;

    bool operator<(const ProductState& other) const {
        if (target != other.target) return target < other.target;
        return techniques < other.techniques;
    }
};

struct ProductRecord {
    ProductState state;
    std::uint32_t parent = kInvalidAutomatonState;
    std::uint8_t event = 0;
};

void add(PathGrammarValidation& result,
         PathGrammarIssue issue,
         std::uint32_t alternative = 0,
         std::uint32_t clause = 0) {
    result.diagnostics.push_back({issue, alternative, clause});
}

void add(CompiledSupportPartitionGraph& result,
         SupportPartitionIssue issue,
         std::uint32_t node_ordinal = 0,
         std::vector<PathEvent> witness = {}) {
    result.diagnostics.push_back(
        {issue, node_ordinal, std::move(witness)});
}

std::uint32_t transition(const CompiledPathEventGrammar& grammar,
                         std::uint32_t state,
                         std::size_t symbol) {
    if (state == kInvalidAutomatonState ||
        state >= grammar.states.size()) {
        return kInvalidAutomatonState;
    }
    return grammar.states[state].transitions[symbol];
}

bool accepting(const CompiledPathEventGrammar& grammar,
               std::uint32_t state) {
    return state != kInvalidAutomatonState &&
           state < grammar.states.size() &&
           grammar.states[state].accepting;
}

std::vector<std::uint32_t> epsilon_closure(
    const std::vector<NfaState>& nfa,
    std::vector<std::uint32_t> states) {
    std::ranges::sort(states);
    states.erase(std::unique(states.begin(), states.end()), states.end());
    for (std::size_t cursor = 0; cursor < states.size(); ++cursor) {
        for (const auto& edge : nfa[states[cursor]].edges) {
            if (!edge.epsilon ||
                std::ranges::binary_search(states, edge.target)) {
                continue;
            }
            states.insert(
                std::ranges::lower_bound(states, edge.target),
                edge.target);
        }
    }
    return states;
}

std::vector<PathEvent> witness_for(
    const std::vector<ProductRecord>& records,
    std::uint32_t record) {
    std::vector<PathEvent> result;
    while (record != 0 && record != kInvalidAutomatonState) {
        result.push_back(kPathEvents[records[record].event]);
        record = records[record].parent;
    }
    std::ranges::reverse(result);
    return result;
}

semantic::IdentityDigest partition_identity(
    const semantic::IdentityDigest& graph_identity,
    std::uint64_t mask) {
    Encoder encoder;
    encoder.digest(graph_identity);
    encoder.u64(mask);
    return runtime::identity_digest(encoder.bytes());
}

semantic::IdentityDigest compute_partition_graph_identity(
    const CompiledSupportPartitionGraph& graph) {
    Encoder encoder;
    encoder.u32(graph.version);
    encoder.digest(graph.technique_graph_identity);
    encoder.digest(graph.target.grammar_identity);
    encoder.u32(static_cast<std::uint32_t>(graph.technique_nodes.size()));
    for (std::size_t index = 0; index < graph.technique_nodes.size();
         ++index) {
        encoder.u32(graph.technique_nodes[index]);
        encoder.digest(graph.technique_grammars[index].grammar_identity);
    }
    return runtime::identity_digest(encoder.bytes());
}

semantic::IdentityDigest compute_automaton_identity(
    const CompiledPathEventGrammar& grammar) {
    Encoder encoder;
    encoder.digest(grammar.grammar_identity);
    encoder.u32(grammar.maximum_path_events);
    encoder.u32(static_cast<std::uint32_t>(grammar.states.size()));
    for (const auto& state : grammar.states) {
        encoder.u8(state.accepting ? 1 : 0);
        for (const auto next : state.transitions) encoder.u32(next);
    }
    return runtime::identity_digest(encoder.bytes());
}

}

bool PathGrammarValidation::has(PathGrammarIssue issue) const {
    return std::ranges::any_of(
        diagnostics,
        [issue](const PathGrammarDiagnostic& diagnostic) {
            return diagnostic.issue == issue;
        });
}

semantic::IdentityDigest compute_path_event_grammar_identity(
    const PathEventGrammar& grammar) {
    Encoder encoder;
    encoder.u32(grammar.version);
    encoder.u32(grammar.maximum_path_events);
    encoder.u32(static_cast<std::uint32_t>(grammar.alternatives.size()));
    for (const auto& alternative : grammar.alternatives) {
        encoder.u32(static_cast<std::uint32_t>(alternative.clauses.size()));
        for (const auto& clause : alternative.clauses) {
            encoder.u64(clause.event_mask);
            encoder.u16(clause.minimum_occurrences);
            encoder.u16(clause.maximum_occurrences);
        }
    }
    return runtime::identity_digest(encoder.bytes());
}

void finalize_path_event_grammar(PathEventGrammar& grammar) {
    grammar.grammar_identity =
        compute_path_event_grammar_identity(grammar);
    if (!validate_path_event_grammar(grammar).ok()) {
        throw std::invalid_argument("Invalid path-event grammar");
    }
}

PathGrammarValidation validate_path_event_grammar(
    const PathEventGrammar& grammar,
    const PathGrammarCompileLimits& limits) {
    PathGrammarValidation result;
    if (grammar.version != kSupportMeasureGraphVersion) {
        add(result, PathGrammarIssue::Version);
    }
    if (semantic::identity_empty(grammar.grammar_identity)) {
        add(result, PathGrammarIssue::Identity);
    }
    if (grammar.maximum_path_events == 0 ||
        grammar.maximum_path_events > 256) {
        add(result, PathGrammarIssue::Depth);
    }
    if (grammar.alternatives.empty()) {
        add(result, PathGrammarIssue::Empty);
    }
    std::uint64_t estimated_states = 1;
    for (std::size_t alternative_index = 0;
         alternative_index < grammar.alternatives.size();
         ++alternative_index) {
        const auto& alternative =
            grammar.alternatives[alternative_index];
        if (alternative.clauses.empty()) {
            add(result, PathGrammarIssue::Empty,
                static_cast<std::uint32_t>(alternative_index));
            continue;
        }
        std::uint64_t minimum_depth = 0;
        std::uint64_t maximum_depth = 0;
        for (std::size_t clause_index = 0;
             clause_index < alternative.clauses.size();
             ++clause_index) {
            const auto& clause = alternative.clauses[clause_index];
            if (clause.event_mask == 0 ||
                (clause.event_mask & ~kKnownPathEventMask) != 0) {
                add(result, PathGrammarIssue::EventMask,
                    static_cast<std::uint32_t>(alternative_index),
                    static_cast<std::uint32_t>(clause_index));
            }
            if (clause.maximum_occurrences == 0 ||
                clause.minimum_occurrences >
                    clause.maximum_occurrences) {
                add(result, PathGrammarIssue::Quantifier,
                    static_cast<std::uint32_t>(alternative_index),
                    static_cast<std::uint32_t>(clause_index));
            }
            minimum_depth += clause.minimum_occurrences;
            maximum_depth += clause.maximum_occurrences;
        }
        if (minimum_depth == 0 ||
            maximum_depth > grammar.maximum_path_events) {
            add(result, PathGrammarIssue::Depth,
                static_cast<std::uint32_t>(alternative_index));
        }
        estimated_states += maximum_depth + 1;
    }
    if (limits.maximum_nfa_states == 0 ||
        limits.maximum_dfa_states == 0 ||
        estimated_states > limits.maximum_nfa_states) {
        add(result, PathGrammarIssue::StateBudget);
    }
    if (!semantic::identity_empty(grammar.grammar_identity) &&
        grammar.grammar_identity !=
            compute_path_event_grammar_identity(grammar)) {
        add(result, PathGrammarIssue::GrammarIdentity);
    }
    return result;
}

CompiledPathEventGrammar compile_path_event_grammar(
    const PathEventGrammar& grammar,
    const PathGrammarCompileLimits& limits) {
    const auto validation =
        validate_path_event_grammar(grammar, limits);
    if (!validation.ok()) {
        throw std::invalid_argument("Invalid path-event grammar");
    }
    std::vector<NfaState> nfa(1);
    for (const auto& alternative : grammar.alternatives) {
        const auto alternative_start =
            static_cast<std::uint32_t>(nfa.size());
        nfa.emplace_back();
        nfa[0].edges.push_back({alternative_start, 0, true});
        auto current = alternative_start;
        for (const auto& clause : alternative.clauses) {
            for (std::uint16_t occurrence = 0;
                 occurrence < clause.maximum_occurrences;
                 ++occurrence) {
                const auto next = static_cast<std::uint32_t>(nfa.size());
                nfa.emplace_back();
                nfa[current].edges.push_back(
                    {next, clause.event_mask, false});
                if (occurrence >= clause.minimum_occurrences) {
                    nfa[current].edges.push_back({next, 0, true});
                }
                current = next;
            }
        }
        nfa[current].accepting = true;
    }
    std::map<std::vector<std::uint32_t>, std::uint32_t> state_map;
    std::vector<std::vector<std::uint32_t>> subsets;
    const auto start = epsilon_closure(nfa, {0});
    state_map.emplace(start, 0);
    subsets.push_back(start);
    CompiledPathEventGrammar result;
    result.grammar_identity = grammar.grammar_identity;
    result.maximum_path_events = grammar.maximum_path_events;
    for (std::size_t cursor = 0; cursor < subsets.size(); ++cursor) {
        if (subsets.size() > limits.maximum_dfa_states) {
            throw std::length_error("Path-event DFA state budget exceeded");
        }
        PathAutomatonState state;
        state.transitions.fill(kInvalidAutomatonState);
        state.accepting = std::ranges::any_of(
            subsets[cursor],
            [&nfa](std::uint32_t item) { return nfa[item].accepting; });
        for (std::size_t symbol = 0;
             symbol < kPathEvents.size(); ++symbol) {
            std::vector<std::uint32_t> moved;
            const auto event = path_event_mask(kPathEvents[symbol]);
            for (const auto item : subsets[cursor]) {
                for (const auto& edge : nfa[item].edges) {
                    if (!edge.epsilon &&
                        (edge.event_mask & event) != 0) {
                        moved.push_back(edge.target);
                    }
                }
            }
            if (moved.empty()) continue;
            moved = epsilon_closure(nfa, std::move(moved));
            const auto [found, inserted] = state_map.emplace(
                moved, static_cast<std::uint32_t>(subsets.size()));
            if (inserted) subsets.push_back(std::move(moved));
            state.transitions[symbol] = found->second;
        }
        result.states.push_back(state);
    }
    result.automaton_identity = compute_automaton_identity(result);
    return result;
}

bool path_event_grammar_accepts(
    const CompiledPathEventGrammar& grammar,
    std::span<const PathEvent> events) {
    if (grammar.version != kSupportMeasureGraphVersion ||
        grammar.states.empty() ||
        events.size() > grammar.maximum_path_events) {
        return false;
    }
    std::uint32_t state = 0;
    for (const auto event : events) {
        const auto found = std::ranges::find(kPathEvents, event);
        if (found == kPathEvents.end()) return false;
        state = transition(
            grammar, state,
            static_cast<std::size_t>(found - kPathEvents.begin()));
        if (state == kInvalidAutomatonState) return false;
    }
    return accepting(grammar, state);
}

bool CompiledSupportPartitionGraph::has(
    SupportPartitionIssue issue) const {
    return std::ranges::any_of(
        diagnostics,
        [issue](const SupportPartitionDiagnostic& diagnostic) {
            return diagnostic.issue == issue;
        });
}

CompiledSupportPartitionGraph compile_support_partition_graph(
    const TechniqueGraph& technique_graph,
    const PathEventGrammar& target,
    std::span<const TechniqueSupportBinding> bindings,
    const SupportPartitionCompileLimits& limits) {
    CompiledSupportPartitionGraph result;
    result.technique_graph_identity = technique_graph.graph_identity;
    if (technique_graph.version != kTechniqueGraphVersion ||
        !validate_technique_graph(technique_graph).ok()) {
        add(result, SupportPartitionIssue::TechniqueGraph);
        return result;
    }
    if (!validate_path_event_grammar(target, limits.grammar).ok()) {
        add(result, SupportPartitionIssue::TargetGrammar);
        return result;
    }
    std::vector<std::uint32_t> estimator_nodes;
    for (const auto& node : technique_graph.nodes) {
        if (node.descriptor.contributes_estimate) {
            estimator_nodes.push_back(node.ordinal);
        }
    }
    if (bindings.size() != estimator_nodes.size() ||
        bindings.empty() || bindings.size() > kMaxComposedTechniques) {
        add(result, SupportPartitionIssue::BindingCount);
        return result;
    }
    std::vector<TechniqueSupportBinding> ordered(
        bindings.begin(), bindings.end());
    std::ranges::sort(
        ordered, {}, &TechniqueSupportBinding::node_ordinal);
    for (std::size_t index = 0; index < ordered.size(); ++index) {
        if (index > 0 && ordered[index - 1].node_ordinal ==
                             ordered[index].node_ordinal) {
            add(result, SupportPartitionIssue::DuplicateBinding,
                ordered[index].node_ordinal);
            continue;
        }
        if (ordered[index].node_ordinal != estimator_nodes[index]) {
            add(result, SupportPartitionIssue::BindingNode,
                ordered[index].node_ordinal);
            continue;
        }
        if (!validate_path_event_grammar(
                ordered[index].grammar, limits.grammar).ok()) {
            add(result, SupportPartitionIssue::TechniqueGrammar,
                ordered[index].node_ordinal);
        } else {
            const auto allowed = technique_graph.nodes[
                ordered[index].node_ordinal]
                                     .descriptor.estimator.support.event_mask;
            const bool outside_descriptor = std::ranges::any_of(
                ordered[index].grammar.alternatives,
                [allowed](const PathGrammarAlternative& alternative) {
                    return std::ranges::any_of(
                        alternative.clauses,
                        [allowed](const PathGrammarClause& clause) {
                            return (clause.event_mask & ~allowed) != 0;
                        });
                });
            if (outside_descriptor) {
                add(result,
                    SupportPartitionIssue::BindingSupportMismatch,
                    ordered[index].node_ordinal);
            }
        }
    }
    if (!result.diagnostics.empty()) return result;
    try {
        result.target = compile_path_event_grammar(target, limits.grammar);
        for (const auto& binding : ordered) {
            result.technique_nodes.push_back(binding.node_ordinal);
            result.technique_grammars.push_back(
                compile_path_event_grammar(
                    binding.grammar, limits.grammar));
        }
    } catch (const std::length_error&) {
        add(result, SupportPartitionIssue::StateBudget);
        return result;
    }
    result.graph_identity = compute_partition_graph_identity(result);
    ProductState start;
    start.target = 0;
    start.techniques.assign(result.technique_grammars.size(), 0);
    std::map<ProductState, std::uint32_t> seen;
    std::vector<ProductRecord> records;
    seen.emplace(start, 0);
    records.push_back({start, kInvalidAutomatonState, 0});
    std::map<std::uint64_t, std::vector<PathEvent>> partitions;
    for (std::size_t cursor = 0; cursor < records.size(); ++cursor) {
        if (records.size() > limits.maximum_product_states) {
            add(result, SupportPartitionIssue::StateBudget);
            return result;
        }
        const auto product = records[cursor].state;
        const bool target_accepting = accepting(result.target, product.target);
        std::uint64_t technique_mask = 0;
        for (std::size_t index = 0;
             index < product.techniques.size(); ++index) {
            if (accepting(result.technique_grammars[index],
                          product.techniques[index])) {
                technique_mask |= std::uint64_t{1} << index;
            }
        }
        if (target_accepting && technique_mask == 0) {
            add(result, SupportPartitionIssue::SupportHole, 0,
                witness_for(records, static_cast<std::uint32_t>(cursor)));
            return result;
        }
        if (!target_accepting && technique_mask != 0) {
            const auto first = static_cast<std::size_t>(
                std::countr_zero(technique_mask));
            add(result, SupportPartitionIssue::OutsideTarget,
                result.technique_nodes[first],
                witness_for(records, static_cast<std::uint32_t>(cursor)));
            return result;
        }
        if (target_accepting) {
            partitions.try_emplace(
                technique_mask,
                witness_for(records, static_cast<std::uint32_t>(cursor)));
            if (partitions.size() > limits.maximum_partitions) {
                add(result, SupportPartitionIssue::PartitionBudget);
                return result;
            }
        }
        for (std::size_t symbol = 0;
             symbol < kPathEvents.size(); ++symbol) {
            ProductState next;
            next.target = transition(result.target, product.target, symbol);
            next.techniques.reserve(product.techniques.size());
            bool live = next.target != kInvalidAutomatonState;
            for (std::size_t index = 0;
                 index < product.techniques.size(); ++index) {
                const auto state = transition(
                    result.technique_grammars[index],
                    product.techniques[index], symbol);
                next.techniques.push_back(state);
                live = live || state != kInvalidAutomatonState;
            }
            if (!live) continue;
            const auto record_index =
                static_cast<std::uint32_t>(records.size());
            const auto [found, inserted] = seen.emplace(next, record_index);
            if (inserted) {
                records.push_back({std::move(next),
                    static_cast<std::uint32_t>(cursor),
                    static_cast<std::uint8_t>(symbol)});
            }
        }
    }
    for (auto& [mask, witness] : partitions) {
        result.partitions.push_back({
            mask, partition_identity(result.graph_identity, mask),
            std::move(witness)});
    }
    return result;
}

std::uint64_t classify_path_support(
    const CompiledSupportPartitionGraph& graph,
    std::span<const PathEvent> events) {
    if (!graph.executable() ||
        !path_event_grammar_accepts(graph.target, events)) {
        return 0;
    }
    std::uint64_t result = 0;
    for (std::size_t index = 0;
         index < graph.technique_grammars.size(); ++index) {
        if (path_event_grammar_accepts(
                graph.technique_grammars[index], events)) {
            result |= std::uint64_t{1} << index;
        }
    }
    return result;
}

bool validate_compiled_support_partition_graph(
    const CompiledSupportPartitionGraph& graph) {
    if (!graph.executable() ||
        graph.version != kSupportMeasureGraphVersion ||
        semantic::identity_empty(graph.technique_graph_identity) ||
        graph.technique_nodes.empty() ||
        graph.technique_nodes.size() > kMaxComposedTechniques ||
        graph.technique_nodes.size() !=
            graph.technique_grammars.size() ||
        graph.target.version != kSupportMeasureGraphVersion ||
        graph.target.states.empty() ||
        graph.target.automaton_identity !=
            compute_automaton_identity(graph.target) ||
        graph.graph_identity != compute_partition_graph_identity(graph)) {
        return false;
    }
    for (std::size_t index = 0;
         index < graph.technique_nodes.size(); ++index) {
        if ((index > 0 && graph.technique_nodes[index - 1] >=
                            graph.technique_nodes[index]) ||
            graph.technique_grammars[index].states.empty() ||
            graph.technique_grammars[index].automaton_identity !=
                compute_automaton_identity(
                    graph.technique_grammars[index])) {
            return false;
        }
    }
    std::uint64_t previous_mask = 0;
    bool first = true;
    for (const auto& partition : graph.partitions) {
        if (partition.technique_mask == 0 ||
            (!first && partition.technique_mask <= previous_mask) ||
            (graph.technique_nodes.size() < kMaxComposedTechniques &&
             (partition.technique_mask >>
              graph.technique_nodes.size()) != 0) ||
            partition.partition_identity != partition_identity(
                graph.graph_identity, partition.technique_mask) ||
            !path_event_grammar_accepts(
                graph.target, partition.witness)) {
            return false;
        }
        std::uint64_t witness_mask = 0;
        for (std::size_t index = 0;
             index < graph.technique_grammars.size(); ++index) {
            if (path_event_grammar_accepts(
                    graph.technique_grammars[index],
                    partition.witness)) {
                witness_mask |= std::uint64_t{1} << index;
            }
        }
        if (witness_mask != partition.technique_mask) return false;
        previous_mask = partition.technique_mask;
        first = false;
    }
    return true;
}

}
