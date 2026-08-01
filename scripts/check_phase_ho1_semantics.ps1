param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
)

$ErrorActionPreference = "Stop"

function Require-Text {
    param([string]$Path, [string[]]$Patterns)
    $fullPath = Join-Path $RepoRoot $Path
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        throw "HO.1 required file is missing: $Path"
    }
    $text = Get-Content -LiteralPath $fullPath -Raw
    foreach ($pattern in $Patterns) {
        if (-not [regex]::IsMatch($text, $pattern)) {
            throw "HO.1 marker is missing in ${Path}: $pattern"
        }
    }
}

function Reject-Text {
    param([string]$Path, [string[]]$Patterns)
    $fullPath = Join-Path $RepoRoot $Path
    $text = Get-Content -LiteralPath $fullPath -Raw
    foreach ($pattern in $Patterns) {
        if ([regex]::IsMatch($text, $pattern, [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)) {
            throw "HO.1 public semantic contract leaks forbidden dependency in ${Path}: $pattern"
        }
    }
}

Require-Text "libs/ure_types/include/ure/semantic_types.hpp" @(
    "using IdentityDigest",
    "struct ProvenanceIdentitySet",
    "struct DimensionVector",
    "struct UnitDescriptor",
    "struct TimeBasis",
    "struct TimeInterval")
Require-Text "libs/ure_transport/include/ure/transport/semantics.hpp" @(
    "enum class ObservableKind",
    "enum class MeasureDomain",
    "struct EstimatorDescriptor",
    "struct UncertaintyDescriptor",
    "enum class CompatibilityKind",
    "Compatible",
    "RequiresTransform",
    "IndependentAggregate",
    "PreviewOnly",
    "Undefined",
    "classify_compatibility")
Require-Text "libs/ure_transport/src/semantics.cpp" @(
    "validate_observable",
    "validate_measure",
    "validate_support",
    "validate_estimator",
    "validate_uncertainty",
    "validate_context",
    "MeasureTransformThenMis",
    "IndependentReplicateAggregate",
    "GeneralizedResampling")
Require-Text "libs/ure_runtime/include/ure/runtime/multi_backend.hpp" @(
    "using IdentityDigest = semantic::IdentityDigest")
Require-Text "tests/host/test_high_order_semantics.cpp" @(
    "is_trivially_copyable",
    "test_observable_validation",
    "test_measure_support_and_estimator_validation",
    "test_uncertainty_validation",
    "test_exact_and_unit_compatibility",
    "test_measure_and_support_compatibility",
    "test_correlation_and_preview_compatibility",
    "test_context_compatibility")
Require-Text "tests/sdk_free/package_consumer/CMakeLists.txt" @(
    "COMPONENTS ure_types ure_runtime ure_transport",
    "UltraRender::ure_transport")
Require-Text "CMakeLists.txt" @("add_subdirectory\(libs/ure_transport\)")
Require-Text "libs/ure_transport/CMakeLists.txt" @(
    "add_library\(ure_transport STATIC",
    "install\(TARGETS ure_transport EXPORT UltraRender_Targets")

$publicHeaders = @(
    "libs/ure_types/include/ure/semantic_types.hpp",
    "libs/ure_transport/include/ure/transport/semantics.hpp")
foreach ($header in $publicHeaders) {
    Reject-Text $header @(
        '#include\s*[<"]cuda',
        '#include\s*[<"]vulkan',
        '#include\s*[<"]d3d',
        '#include\s*[<"]windows',
        '#include\s*[<"]pxr/',
        "std::vector",
        "std::string")
}

Write-Host "HO.1 semantic static audit passed"
