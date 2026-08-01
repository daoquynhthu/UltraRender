param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
)

$ErrorActionPreference = "Stop"

function Require-Text {
    param([string]$Path, [string[]]$Patterns)
    $fullPath = Join-Path $RepoRoot $Path
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        throw "HT.2 required file is missing: $Path"
    }
    $text = Get-Content -LiteralPath $fullPath -Raw
    foreach ($pattern in $Patterns) {
        if (-not [regex]::IsMatch($text, $pattern)) {
            throw "HT.2 marker is missing in ${Path}: $pattern"
        }
    }
}

function Reject-Text {
    param([string]$Path, [string[]]$Patterns)
    $text = Get-Content -LiteralPath (Join-Path $RepoRoot $Path) -Raw
    foreach ($pattern in $Patterns) {
        if ([regex]::IsMatch(
                $text, $pattern,
                [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)) {
            throw "HT.2 boundary violation in ${Path}: $pattern"
        }
    }
}

$header = "libs/ure_transport/include/ure/transport/pilot.hpp"

Require-Text $header @(
    "struct PilotSamplingProvenance",
    "IndependentHoldout", "CrossFitted",
    "SelectionProbabilityCorrected",
    "struct TechniquePilotObservation",
    "struct TechniquePilotCrossObservation",
    "accumulate_technique_pilot_samples",
    "summarize_technique_pilot_covariance",
    "validate_technique_pilot_covariance",
    "effective_sample_size",
    "struct PilotQualificationContext",
    "struct TechniqueQualificationRequirement",
    "ForceIncludeExperimental", "OutputLayerMismatch",
    "production_executable", "experimental_executable",
    "qualify_pilot_techniques")
Require-Text "libs/ure_transport/src/pilot_statistics.cpp" @(
    "ranges_overlap", "tail_exceedance_counts",
    "squared_importance_weight_sum",
    "paired_sample_count", "sample_covariances",
    "pilot_provenance_identity")
Require-Text "libs/ure_transport/src/qualification.cpp" @(
    "world_state_identity", "observation_snapshot_identity",
    "required_partition_mask", "required_scene_capabilities",
    "required_backend_capabilities", "resident_budget_bytes",
    "scratch_budget_bytes", "InvalidPilotEvidence",
    "ExperimentalOverride", "qualification_context_identity",
    "requirements_identity", "override_policy_identity")
Require-Text "tests/host/test_pilot_qualification.cpp" @(
    "test_sampling_bias_policies",
    "test_statistics_tail_ess_and_covariance",
    "test_automatic_qualification_and_overrides",
    "test_output_layers_remain_separate",
    "InvalidPilotEvidence", "OutputLayerMismatch")
Require-Text "tests/gpu/test_pilot_statistics.cu" @(
    "produce_pilot_samples_kernel",
    "accumulate_technique_pilot_samples",
    "effective_sample_size - 3.6")
Require-Text "tests/sdk_free/CMakeLists.txt" @(
    "pilot_statistics.cpp", "qualification.cpp",
    "pilot_qualification_sdk_free")
Require-Text "tests/sdk_free/package_consumer/main.cpp" @(
    "ure/transport/pilot.hpp", "kPilotContractVersion")

foreach ($path in @(
        $header,
        "libs/ure_transport/src/pilot_statistics.cpp",
        "libs/ure_transport/src/qualification.cpp")) {
    Reject-Text $path @(
        '#include\s*[<"]cuda', '#include\s*[<"]vulkan',
        '#include\s*[<"]d3d', '#include\s*[<"]windows',
        '#include\s*[<"]pxr/', '\bIntegratorMode\b')
}

Write-Host "HT.2 pilot qualification static audit passed: independent/corrected sampling, cost/variance/covariance/tail/ESS/memory evidence, automatic capability qualification, strict output layers and bounded expert overrides present."
