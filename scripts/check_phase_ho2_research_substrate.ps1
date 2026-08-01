param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
)

$ErrorActionPreference = "Stop"

function Require-Text {
    param([string]$Path, [string[]]$Patterns)
    $fullPath = Join-Path $RepoRoot $Path
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        throw "HO.2 required file is missing: $Path"
    }
    $text = Get-Content -LiteralPath $fullPath -Raw
    foreach ($pattern in $Patterns) {
        if (-not [regex]::IsMatch($text, $pattern)) {
            throw "HO.2 marker is missing in ${Path}: $pattern"
        }
    }
}

function Reject-Text {
    param([string]$Path, [string[]]$Patterns)
    $text = Get-Content -LiteralPath (Join-Path $RepoRoot $Path) -Raw
    foreach ($pattern in $Patterns) {
        if ([regex]::IsMatch($text, $pattern, [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)) {
            throw "HO.2 public research contract violates its boundary in ${Path}: $pattern"
        }
    }
}

Require-Text "libs/ure_research/include/ure/research/execution.hpp" @(
    "ResearchExecutionManifest", "CounterRange", "allocate_execution_shards",
    "validate_execution_shards")
Require-Text "libs/ure_research/include/ure/research/artifact.hpp" @(
    "ArtifactChunkKind", "ArtifactCodec", "ArtifactLimits",
    "measurement_artifact_index_size", "inspect_measurement_artifact_index",
    "read_artifact_chunk_payload", "has_sufficient_statistics")
Require-Text "libs/ure_research/include/ure/research/experiment.hpp" @(
    "ExperimentRegistry", "ComparisonRequest", "ConfidenceInterval",
    "run_comparison")
Require-Text "libs/ure_research/include/ure/research/capability.hpp" @(
    "enum class Maturity", "implemented", "default_enabled",
    "ExecutableOptIn", "negotiate_capabilities")
Require-Text "libs/ure_research/include/ure/research/reference.hpp" @(
    "HostOracle", "SmallGpuOracle", "kMaxReferenceElements",
    "ReferenceBackendRegistry")
Require-Text "libs/ure_research/include/ure/research/promotion.hpp" @(
    "IndependentReplicates", "FailLoudBoundary", "evaluate_promotion")
Require-Text "tests/host/test_research_substrate.cpp" @(
    "test_execution_manifest_and_ranges", "test_measurement_artifact_container",
    "test_capability_negotiation", "test_experiment_registry_and_comparison",
    "test_reference_backend_hooks", "test_promotion_checklist")
Require-Text "tests/sdk_free/CMakeLists.txt" @(
    "ure_research_sdk_free", "research_substrate_sdk_free")
Require-Text "tests/sdk_free/package_consumer/CMakeLists.txt" @(
    "ure_research", "UltraRender::ure_research")
Require-Text "CMakeLists.txt" @("add_subdirectory\(libs/ure_research\)")
Require-Text "libs/ure_research/CMakeLists.txt" @(
    "add_library\(ure_research STATIC",
    "install\(TARGETS ure_research EXPORT UltraRender_Targets")

$headers = Get-ChildItem -LiteralPath (Join-Path $RepoRoot "libs/ure_research/include") -File -Recurse -Filter "*.hpp"
foreach ($header in $headers) {
    $relative = [System.IO.Path]::GetRelativePath($RepoRoot, $header.FullName)
    Reject-Text $relative @(
        '#include\s*[<"]cuda', '#include\s*[<"]vulkan',
        '#include\s*[<"]d3d', '#include\s*[<"]windows',
        '#include\s*[<"]pxr/', 'ProductionRenderer')
}

Write-Host "HO.2 executable research substrate static audit passed"
