#pragma once

#include "ure/transport/semantics.hpp"

#include <cstdint>
#include <vector>

namespace ure::transport {

inline constexpr std::uint32_t kTechniqueGraphVersion = 1;

enum class TechniqueFamily : std::uint8_t {
    WavefrontPathTracing,
    PathGuiding,
    RestirDirect,
    RestirPathReuse,
    SpecularManifold,
    BidirectionalPathTracing,
    VertexConnectionMerging,
    PrimarySampleSpaceMlt
};

enum class TechniqueRole : std::uint8_t {
    Estimator,
    ProposalService,
    ReplayKernel
};

enum class TechniqueResourceScaling : std::uint8_t {
    None,
    Pixel,
    Scene,
    PixelAndScene,
    Chain,
    Solver
};

struct TechniqueResourceDescriptor {
    TechniqueResourceScaling scaling = TechniqueResourceScaling::None;
    bool cost_estimate_known = false;
    std::uint64_t nanoseconds_per_sample = 0;
    bool scratch_bound_known = false;
    std::uint64_t scratch_bytes_per_work_item = 0;
    std::uint64_t persistent_budget_bytes = 0;
    std::uint32_t max_samples_per_pass = 0;
    semantic::IdentityDigest backend_capability_identity = {};
};

struct TechniqueDescriptor {
    std::uint32_t version = kTechniqueGraphVersion;
    TechniqueFamily family = TechniqueFamily::WavefrontPathTracing;
    TechniqueRole role = TechniqueRole::Estimator;
    semantic::IdentityDigest technique_identity = {};
    semantic::IdentityDigest sample_space_identity = {};
    semantic::IdentityDigest parameter_identity = {};
    semantic::IdentityDigest persistent_state_identity = {};
    semantic::IdentityDigest replay_layout_identity = {};
    EstimatorDescriptor estimator = {};
    TechniqueResourceDescriptor resources = {};
    bool contributes_estimate = true;
    bool owns_normalization = true;
    bool adaptive_state = false;
    bool replayable = false;
    bool requires_shared_spectral_primary_sample = false;
};

struct TechniqueNode {
    std::uint32_t ordinal = 0;
    TechniqueDescriptor descriptor;
};

enum class TechniqueEdgeKind : std::uint8_t {
    ProposalFor,
    ReplayFor,
    DisjointSupport,
    CoupledEstimatorFamily
};

struct TechniqueEdge {
    std::uint32_t source = 0;
    std::uint32_t target = 0;
    TechniqueEdgeKind kind = TechniqueEdgeKind::ProposalFor;

    bool operator==(const TechniqueEdge&) const = default;
};

struct TechniqueGraph {
    std::uint32_t version = kTechniqueGraphVersion;
    semantic::IdentityDigest graph_identity = {};
    std::vector<TechniqueNode> nodes;
    std::vector<TechniqueEdge> edges;
};

enum class TechniqueGraphIssue : std::uint8_t {
    Version,
    Identity,
    NodeOrder,
    DuplicateNode,
    Descriptor,
    Estimator,
    EdgeOrder,
    EdgeEndpoint,
    EdgeSemantics,
    MissingConsumer,
    ObservableMismatch,
    IntegralMismatch,
    Cycle,
    MissingEstimator,
    GraphIdentity
};

struct TechniqueGraphDiagnostic {
    TechniqueGraphIssue issue = TechniqueGraphIssue::Version;
    std::uint32_t node = 0;
    std::uint32_t edge = 0;
};

struct TechniqueGraphValidation {
    std::vector<TechniqueGraphDiagnostic> diagnostics;

    bool ok() const { return diagnostics.empty(); }
    bool has(TechniqueGraphIssue issue) const;
};

semantic::IdentityDigest compute_technique_graph_identity(
    const TechniqueGraph& graph);
void finalize_technique_graph(TechniqueGraph& graph);
TechniqueGraphValidation validate_technique_graph(
    const TechniqueGraph& graph);

}
