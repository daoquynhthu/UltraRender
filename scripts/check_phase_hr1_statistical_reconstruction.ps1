param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
)

$ErrorActionPreference = "Stop"

function Require-Text {
    param([string]$Path, [string[]]$Patterns)
    $fullPath = Join-Path $RepoRoot $Path
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        throw "HR.1 required file is missing: $Path"
    }
    $text = Get-Content -LiteralPath $fullPath -Raw
    foreach ($pattern in $Patterns) {
        if (-not [regex]::IsMatch($text, $pattern)) {
            throw "HR.1 marker is missing in ${Path}: $pattern"
        }
    }
}

function Reject-Text {
    param([string]$Path, [string[]]$Patterns)
    $text = Get-Content -LiteralPath (Join-Path $RepoRoot $Path) -Raw
    foreach ($pattern in $Patterns) {
        if ([regex]::IsMatch($text, $pattern)) {
            throw "HR.1 boundary violation in ${Path}: $pattern"
        }
    }
}

$header = "libs/ure_reconstruction/include/ure/reconstruction/statistical_reconstruction.hpp"
$source = "libs/ure_reconstruction/src/statistical_reconstruction.cpp"
$cuda = "libs/ure_core/src/path_tracer_denoise.cu"
$hostTest = "tests/host/test_statistical_reconstruction.cpp"
$gpuTest = "tests/gpu/test_gpu_denoise.cu"

Require-Text $header @(
    "StatisticalReconstructionFrame", "raw_estimate",
    "sample_variance", "effective_sample_count", "tail_frequency",
    "maximum_absolute_contribution", "motion_time_confidence",
    "HighEnergyPreserved", "InvalidCurrentSample",
    "HistoryIdentityMismatch", "DisoccludedDepth",
    "spatial_support", "history_confidence", "uncertainty")
Require-Text $source @(
    "classify_tail", "maximum_absolute_contribution",
    "maximum_relative_depth_difference", "minimum_normal_dot",
    "maximum_albedo_distance", "history_weight", "weight_tail",
    "output\.raw_estimate = frame\.raw_estimate",
    "valid_physical_value", "compute_statistical_reconstruction_output_identity")
Require-Text $cuda @(
    "statistical_temporal_reconstruction_kernel",
    "statistical_atrous_reconstruction_kernel",
    "motion_time_confidence", "history_pixel_confidence",
    "heavy_tail_frequency", "stokes_domain")
Require-Text $hostTest @(
    "test_spatial_spectral_baseline", "test_tail_classification",
    "HighEnergyPreserved", "test_temporal_confidence_and_rejection",
    "DisoccludedDepth", "HistoryIdentityMismatch",
    "test_stokes_physical_domain", "output\.raw_estimate == input\.raw_estimate")
Require-Text $gpuTest @(
    "test_statistical_reconstruction",
    "statistical_temporal_reconstruction_kernel",
    "statistical_atrous_reconstruction_kernel",
    "host_output\.reconstructed", "raw_after == raw")
Require-Text "tests/sdk_free/CMakeLists.txt" @(
    "statistical_reconstruction\.cpp",
    "statistical_reconstruction_sdk_free")
Require-Text "tests/sdk_free/package_consumer/main.cpp" @(
    "ure/reconstruction/statistical_reconstruction\.hpp",
    "kStatisticalReconstructionVersion")

foreach ($path in @($header, $source)) {
    Reject-Text $path @(
        '#include\s*[<"]cuda', '#include\s*[<"]vulkan',
        '#include\s*[<"]d3d', '#include\s*[<"]windows',
        '#include\s*[<"]pxr/', 'tone.?map', 'sRGB')
}

Write-Host "HR.1 statistical reconstruction static audit passed: raw estimates, tail attribution, dynamic confidence, physical spectral/Stokes filtering, uncertainty, support, rejection provenance and host/CUDA parity are present."
