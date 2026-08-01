#pragma once

#include "ure/render_config.hpp"
#include "ure/transport/technique_graph.hpp"

#include <cstdint>
#include <vector>

namespace ure::transport {

enum class LegacyRejectionClass : std::uint8_t {
    Mathematical,
    Resource,
    Unimplemented
};

enum class LegacyRejectionCode : std::uint8_t {
    InvalidMode,
    MissingRequiredEnable,
    PrimarySampleSpaceRequiresMlt,
    MltRequiresPrimarySampleSpace,
    RestirFamiliesMutuallyExclusive,
    RestirRequiresReuse,
    BiasedReuseRequiresOptIn,
    BiasedSpatialReuseUnsupported,
    InvalidProbabilityOrThreshold,
    InvalidHistoryOrDepth,
    InvalidResourceBudget,
    InvalidStorageDomain,
    LightTracingRequiresSensorMeasure,
    MltBidirectionalNeedsSharedSpectralSample,
    MltOwnsAdaptiveScheduler,
    UnsupportedWaveCombination
};

struct LegacyTechniqueRejection {
    LegacyRejectionClass rejection_class =
        LegacyRejectionClass::Unimplemented;
    LegacyRejectionCode code = LegacyRejectionCode::InvalidMode;
    TechniqueFamily family = TechniqueFamily::WavefrontPathTracing;

    bool operator==(const LegacyTechniqueRejection&) const = default;
};

struct LegacyExecutionRoute {
    IntegratorMode resolved_mode = IntegratorMode::Wavefront;
    bool path_guiding = false;
    bool restir_direct = false;
    bool restir_path = false;
    bool specular_manifold = false;
    bool bidirectional = false;
    bool vertex_merging = false;
    bool pssmlt = false;
    bool biased_preview = false;

    bool operator==(const LegacyExecutionRoute&) const = default;
};

struct LegacyTechniquePreset {
    std::uint32_t version = kTechniqueGraphVersion;
    IntegratorMode requested_mode = IntegratorMode::Wavefront;
    LegacyExecutionRoute route;
    TechniqueGraph graph;
    std::vector<LegacyTechniqueRejection> rejections;

    bool executable() const { return rejections.empty(); }
};

LegacyTechniquePreset compile_legacy_technique_preset(
    const RenderConfig& config);

bool legacy_preset_equivalent(
    const RenderConfig& config,
    const LegacyTechniquePreset& preset);

const TechniqueDescriptor* find_technique(
    const TechniqueGraph& graph,
    TechniqueFamily family);

}
