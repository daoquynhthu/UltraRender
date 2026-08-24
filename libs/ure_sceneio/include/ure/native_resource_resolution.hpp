#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <ure/native_scene_ir.hpp>

namespace ure::native_scene {

struct NativeResolvedResource {
    std::string logical_id;
    std::string source_uri;
    ResourceDescriptor descriptor;
    ResourceResidency residency{ResourceResidency::Resident};
    std::vector<std::uint8_t> payload;
};

struct NativeResourceBudgetBreakdown {
    std::uint64_t stored_bytes{};
    std::uint64_t decompressed_bytes{};
    std::uint64_t resident_bytes{};
    std::uint64_t streamed_bytes{};
    std::uint64_t temporary_bytes{};
    std::uint64_t output_bytes{};
};

struct NativeResourceResolution {
    std::vector<NativeResolvedResource> resources;
    NativeResourceBudgetBreakdown budget;
    std::vector<ValidationDiagnostic> diagnostics;

    bool ok() const;
};

NativeResourceResolution resolve_native_scene_resources(
    const NativeSceneArchive& archive,
    const ValidationLimits& limits = {});

}
