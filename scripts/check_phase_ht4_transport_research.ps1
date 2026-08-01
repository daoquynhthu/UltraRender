param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
)

$ErrorActionPreference = "Stop"

function Require-Text {
    param([string]$Path, [string[]]$Patterns)
    $fullPath = Join-Path $RepoRoot $Path
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        throw "HT.4 required file is missing: $Path"
    }
    $text = Get-Content -LiteralPath $fullPath -Raw
    foreach ($pattern in $Patterns) {
        if (-not [regex]::IsMatch($text, $pattern)) {
            throw "HT.4 marker is missing in ${Path}: $pattern"
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
            throw "HT.4 boundary violation in ${Path}: $pattern"
        }
    }
}

$header = "libs/ure_research/include/ure/research/transport.hpp"
$source = "libs/ure_research/src/transport.cpp"
$test = "tests/host/test_transport_research.cpp"

Require-Text "libs/ure_transport/include/ure/transport/technique_graph.hpp" @(
    "ResearchExtension")
Require-Text $header @(
    "TransportResearchMechanism", "MarkovEstimator", "Proposal",
    "ControlVariate", "ShiftMap", "MultifidelityEstimator",
    "HybridObservableEstimator", "ResearchJointSampleContract",
    "ResearchReuseContract", "ReweightedTransportMap",
    "SupportCoverage", "TimeToError", "ObservableUnlock",
    "TransportResearchRegistry", "materialize_graph",
    "TransportResearchAssessment", "promotion_review_eligible")
Require-Text $source @(
    "TechniqueFamily::ResearchExtension",
    "CorrelationModel::MarkovChain", "independent_replicates",
    "known_expectation_identity", "forward_map_identity",
    "inverse_map_identity", "jacobian_identity",
    "explicit_opt_in", "validate_comparison_result",
    "minimum_effect", "NoImprovement")
Require-Text "libs/ure_research/src/experiment.cpp" @(
    "validate_comparison_result",
    "Generated invalid comparison result")
Require-Text $test @(
    "test_descriptor_registry_and_graph",
    "test_joint_sample_and_reuse_boundaries",
    "test_replicated_control_variate_assessment",
    "x \* x - \(x - 0.5\)", "replicate_count = 8",
    "TransportResearchOutcome::Positive",
    "TransportResearchOutcome::Negative")
Require-Text "tests/sdk_free/CMakeLists.txt" @(
    "transport.cpp", "transport_research_sdk_free")
Require-Text "tests/sdk_free/package_consumer/main.cpp" @(
    "ure/research/transport.hpp", "kTransportResearchContractVersion")

foreach ($path in @($header, $source)) {
    Reject-Text $path @(
        '#include\s*[<"]cuda', '#include\s*[<"]vulkan',
        '#include\s*[<"]d3d', '#include\s*[<"]windows',
        '#include\s*[<"]pxr/', '\bIntegratorMode\b')
}

$capsuleRoot = Join-Path $RepoRoot "docs/research/ht4/capsules"
$capsules = @(Get-ChildItem -LiteralPath $capsuleRoot -File -Filter "*.json" | Sort-Object Name)
if ($capsules.Count -lt 2) {
    throw "HT.4 requires positive and negative research capsules"
}
$outcomes = [System.Collections.Generic.HashSet[string]]::new()
$required = @(
    "schema", "id", "maturity", "question", "hypothesis", "inputs",
    "reproducibility", "baseline", "candidate", "metrics", "artifacts",
    "result", "known_failure_domain")
foreach ($file in $capsules) {
    $capsule = Get-Content -LiteralPath $file.FullName -Raw | ConvertFrom-Json
    foreach ($name in $required) {
        if ($capsule.PSObject.Properties.Name -notcontains $name) {
            throw "HT.4 capsule $($file.Name) is missing $name"
        }
    }
    if ($capsule.schema -ne "ure.research.capsule/1.0" -or
        $capsule.maturity -ne "Research" -or
        @($capsule.metrics).Count -eq 0 -or
        @($capsule.artifacts).Count -eq 0 -or
        @($capsule.known_failure_domain).Count -eq 0) {
        throw "HT.4 capsule $($file.Name) violates Research Capsule v1"
    }
    [void]$outcomes.Add([string]$capsule.result.outcome)
    foreach ($assertion in @($capsule.inputs.source_assertions)) {
        Require-Text ([string]$assertion.path) @([string]$assertion.pattern)
    }
}
if (-not $outcomes.Contains("positive") -or
    -not $outcomes.Contains("negative")) {
    throw "HT.4 capsules must preserve positive and negative outcomes"
}

Write-Host "HT.4 transport research static audit passed: generic ResearchExtension descriptors, joint sample/reuse contracts, capsule registry, opt-in graph materialization, replicated assessment and positive/negative research records are present."
