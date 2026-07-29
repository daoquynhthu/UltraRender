$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

function Require-Text {
    param(
        [string]$Path,
        [string[]]$Patterns
    )
    $resolved = Join-Path $root $Path
    if (-not (Test-Path -LiteralPath $resolved)) {
        throw "Missing W.11 artifact: $Path"
    }
    $text = Get-Content -Raw -LiteralPath $resolved
    foreach ($pattern in $Patterns) {
        if (-not $text.Contains($pattern)) {
            throw "Missing W.11 contract '$pattern' in $Path"
        }
    }
}

Require-Text "libs/ure_core/include/ure/distributed_contract.hpp" @(
    "DistributedFrameKind",
    "Radiance",
    "ComplexField",
    "MutualIntensity",
    "CoherentRealization",
    "phase_reference_identity",
    "field_layout_identity",
    "DistributedRealizationRange",
    "compatible_frame_semantics_for_merge"
)
Require-Text "libs/ure_core/include/ure/distributed_wave_io.hpp" @(
    "DistributedComplexFrameStorage",
    "DistributedMutualIntensityFrameStorage",
    "DistributedPartialCoherenceAccumulator",
    "merge_complex_field_frame",
    "merge_mutual_intensity_frame",
    "append_coherent_realization",
    "kMaxDistributedComplexFrameElements"
)
Require-Text "libs/ure_core/src/distributed_contract.cpp" @(
    "valid_realization_ranges",
    "ranges_overlap",
    "make_coherent_realization_semantics",
    "make_mutual_intensity_semantics",
    "RGB distributed framebuffer accepts radiance frames only"
)
Require-Text "libs/ure_core/src/distributed_wave_io.cpp" @(
    "distributed_complex_field_layout_identity",
    "distributed_mutual_intensity_layout_identity",
    "resolved_amplitude_at",
    "resolved_density",
    "merge_realization_ranges",
    "frame.realization_weight"
)
Require-Text "libs/ure_core/src/distributed_file_io.cpp" @(
    "constexpr int kVersion = 6",
    "kComplexFrameMagic",
    "kMutualFrameMagic",
    "kByteOrderMarker",
    "append_content_digest",
    "verify_content_digest",
    "write_frame_semantics",
    "read_frame_semantics"
)
Require-Text "tests/host/test_distributed_wave_io.cpp" @(
    "test_complex_field_merge_and_file",
    "test_coherent_before_incoherent_reduction",
    "test_mutual_intensity_merge_and_file",
    "test_radiance_separation_and_invalid_inputs",
    "overlapping realization range was accepted",
    "coherent field was serialized as RGB radiance",
    "corrupted complex field file was accepted",
    "film.resolved_power_at"
)
Require-Text "tests/host/test_public_surface_sdk_free.cpp" @(
    "ure/distributed_wave_io.hpp"
)

$radianceMerge = Get-Content -Raw -LiteralPath (
    Join-Path $root "libs/ure_core/src/distributed_contract.cpp")
if ($radianceMerge.Contains(
        "static_cast<float>(amplitude") -or
    $radianceMerge.Contains(
        "amplitude.power()")) {
    throw "W.11 complex data was flattened into the RGB merge path"
}

Write-Host "Phase W.11 static audit passed."
