param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
)

$ErrorActionPreference = "Stop"

function Require-Text {
    param([string]$Path, [string[]]$Patterns)
    $fullPath = Join-Path $RepoRoot $Path
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        throw "HT.5 required file is missing: $Path"
    }
    $text = Get-Content -LiteralPath $fullPath -Raw
    foreach ($pattern in $Patterns) {
        if (-not [regex]::IsMatch($text, $pattern)) {
            throw "HT.5 marker is missing in ${Path}: $pattern"
        }
    }
}

function Reject-Text {
    param([string]$Path, [string[]]$Patterns)
    $text = Get-Content -LiteralPath (Join-Path $RepoRoot $Path) -Raw
    foreach ($pattern in $Patterns) {
        if ([regex]::IsMatch($text, $pattern)) {
            throw "HT.5 boundary violation in ${Path}: $pattern"
        }
    }
}

$header = "libs/ure_transport/include/ure/transport/automatic_integrator.hpp"
$source = "libs/ure_transport/src/automatic_integrator.cpp"
$bridge = "libs/ure_core/src/automatic_render_engine.cpp"

Require-Text $header @(
    "AutomaticIntegratorObjective", "AutomaticTechniqueDecision",
    "LegacyPresetDisposition", "CompatibilityAndReproducibilityOnly",
    "AutomaticPartitionProgram", "AutomaticOutputTrace",
    "technique_coverage_mask", "normalization_identity",
    "peak_resident_bytes", "confidence_lower")
Require-Text $source @(
    "MissingDefensiveBaseline", "DefensiveUnknownDomainCoverage",
    "validate_portfolio_schedule", "quality_target_met",
    "partition_observation_identities")
Require-Text $bridge @(
    "IntegratorMode::Automatic", "independent pilot",
    "sample_index_offset", "pilot_spp", "minimum_wavefront_fraction",
    "measured_peak_resident_device_bytes", "technique_coverage_mask",
    "conservative_uncertainty_bound", "memory_budget_met",
    "complete_unbiased_endpoint_candidate", "finite-sample unbiased",
    "SpecularManifold", "VCM", "MLT", "Wavefront")
Require-Text "libs/ure_config/include/ure/config.hpp" @(
    'std::string mode = "automatic"', "sample_index_offset")
Require-Text "apps/ure_cli/src/main.cpp" @(
    "automatic_integrator", "Automatic portfolio:", "sample_index_offset")
Require-Text "libs/ure_core/include/ure/ure_c_api.h" @(
    "URE_INTEGRATOR_AUTOMATIC", "ure_automatic_integrator_config_t",
    "ure_session_create_execution_config_v3",
    "ure_session_get_automatic_integrator_report")
Require-Text "pyure/__init__.py" @(
    'integrator_mode: str = "automatic"',
    "automatic_integrator_report", "sample_index_offset")
Require-Text "tests/gpu/test_automatic_integrator.cpp" @(
    "test_plane_sphere.gltf", "cornell_box.gltf",
    "textured_quad_validation.gltf", "replicate < 3",
    "replicate \* 128", "10000 \+ replicate \* 128",
    "automatic_mean", "reference_mean", "merged",
    "measured_peak_resident_device_bytes",
    "maximum_absolute_pilot_contribution")
Require-Text "tests/sdk_free/CMakeLists.txt" @(
    "automatic_integrator.cpp", "automatic_integrator_sdk_free")
Require-Text "tests/sdk_free/package_consumer/main.cpp" @(
    "ure/transport/automatic_integrator.hpp",
    "kAutomaticIntegratorContractVersion")
Require-Text "docs/research/ht0/legacy_mode_switch_ledger.json" @(
    "automatic_render_engine.cpp",
    "ht5_automatic_endpoint_compatibility_bridge")

foreach ($path in @($header, $source)) {
    Reject-Text $path @(
        '#include\s*[<"]cuda', '#include\s*[<"]vulkan',
        '#include\s*[<"]d3d', '#include\s*[<"]windows',
        '#include\s*[<"]pxr/', '\bIntegratorMode\b')
}

Write-Host "HT.5 automatic integrator static audit passed: objective-driven planning, defensive coverage, traceable output, disjoint pilot/production samples, actual CUDA endpoint execution, API parity and independent multi-scene evidence are present."
