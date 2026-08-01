param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
)

$ErrorActionPreference = "Stop"

function Require-Text {
    param([string]$Path, [string[]]$Patterns)
    $fullPath = Join-Path $RepoRoot $Path
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        throw "HR.0 required file is missing: $Path"
    }
    $text = Get-Content -LiteralPath $fullPath -Raw
    foreach ($pattern in $Patterns) {
        if (-not [regex]::IsMatch($text, $pattern)) {
            throw "HR.0 marker is missing in ${Path}: $pattern"
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
            throw "HR.0 SDK-free boundary violation in ${Path}: $pattern"
        }
    }
}

$measurementHeader = "libs/ure_reconstruction/include/ure/reconstruction/measurement.hpp"
$checkpointHeader = "libs/ure_reconstruction/include/ure/reconstruction/checkpoint.hpp"

Require-Text $measurementHeader @(
    "struct MeasurementSchema", "struct MeasurementBundle",
    "struct MeasurementProvenance", "MeasurementRetention",
    "MeasurementSelectionLoss", "select_measurement_schema",
    "merge_measurement_bundles", "refresh_derived_measurement_planes",
    "EffectiveSampleCount", "Variance", "Covariance",
    "ComplexField", "JonesField", "MutualIntensity",
    "DetectorWavelength", "TransportWavelength", "JointPdf",
    "TechniqueIdentity", "SupportClass", "EstimatorWeight",
    "PathEventSignature", "OpticalPathLength", "ValidityMask",
    "SampleRecord")
Require-Text $checkpointHeader @(
    "write_measurement_checkpoint", "inspect_measurement_checkpoint",
    "read_measurement_checkpoint_plane", "read_measurement_checkpoint")
Require-Text "libs/ure_reconstruction/src/measurement.cpp" @(
    "compute_measurement_schema_identity", "MeasurementMergeRule::Sum",
    "MeasurementMergeRule::RequireEqual", "MeasurementMergeRule::Append",
    "MeasurementMergeRule::Derived", "sample_ranges",
    "MeasurementLossReason::Budget", "SampleVariance",
    "SampleCovariance")
Require-Text "libs/ure_reconstruction/src/checkpoint.cpp" @(
    "write_measurement_artifact", "inspect_measurement_artifact_index",
    "read_artifact_chunk_payload", "measurement_schema_identity",
    "digest_payload", "RunLength")
Require-Text "tests/host/test_measurement_bundle.cpp" @(
    "test_schema_and_budget_selection", "test_bundle_merge_is_canonical",
    "test_derived_statistics_merge", "test_typed_complex_plane",
    "test_checkpoint_and_partial_read")
Require-Text "tests/sdk_free/CMakeLists.txt" @(
    "ure_reconstruction_sdk_free", "measurement_bundle_sdk_free")
Require-Text "tests/sdk_free/package_consumer/main.cpp" @(
    "ure/reconstruction/measurement.hpp")
Require-Text "libs/ure_reconstruction/CMakeLists.txt" @(
    "add_library\(ure_reconstruction", "ure_research",
    "install\(TARGETS ure_reconstruction")

foreach ($header in @($measurementHeader, $checkpointHeader)) {
    Reject-Text $header @(
        '#include\s*[<"]cuda', '#include\s*[<"]vulkan',
        '#include\s*[<"]d3d', '#include\s*[<"]windows',
        '#include\s*[<"]pxr/')
}

Write-Host "HR.0 MeasurementBundle static audit passed: typed planes, budget loss, canonical merge, derived statistics, checkpoint and partial-read contracts present."
