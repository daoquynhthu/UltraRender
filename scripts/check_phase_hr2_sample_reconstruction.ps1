param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
)

$ErrorActionPreference = "Stop"

function Require-Text {
    param([string]$Path, [string[]]$Patterns)
    $fullPath = Join-Path $RepoRoot $Path
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        throw "HR.2 required file is missing: $Path"
    }
    $text = Get-Content -LiteralPath $fullPath -Raw
    foreach ($pattern in $Patterns) {
        if (-not [regex]::IsMatch($text, $pattern)) {
            throw "HR.2 marker is missing in ${Path}: $pattern"
        }
    }
}

function Reject-Text {
    param([string]$Path, [string[]]$Patterns)
    $text = Get-Content -LiteralPath (Join-Path $RepoRoot $Path) -Raw
    foreach ($pattern in $Patterns) {
        if ([regex]::IsMatch($text, $pattern)) {
            throw "HR.2 boundary violation in ${Path}: $pattern"
        }
    }
}

function Require-Capsule {
    param([string]$Path, [string]$Outcome)
    $fullPath = Join-Path $RepoRoot $Path
    $capsule = Get-Content -LiteralPath $fullPath -Raw | ConvertFrom-Json
    if ($capsule.schema -ne "ure.research.capsule/1.0" -or
        $capsule.maturity -ne "Research" -or
        $capsule.result.outcome -ne $Outcome -or
        -not $capsule.reproducibility.replay_command -or
        $capsule.metrics.Count -eq 0 -or
        $capsule.artifacts.Count -eq 0 -or
        $capsule.known_failure_domain.Count -eq 0) {
        throw "HR.2 research capsule is incomplete: $Path"
    }
}

$header = "libs/ure_reconstruction/include/ure/reconstruction/sample_reconstruction.hpp"
$source = "libs/ure_reconstruction/src/sample_reconstruction.cpp"
$test = "tests/host/test_sample_reconstruction.cpp"

Require-Text $header @(
    "SampleReconstructionRecord", "technique_identity",
    "path_event_identity", "detector_wavelength_nm", "joint_pdf",
    "ExternalKernelPrediction", "ExternalSampleTransformer",
    "ExternalHybrid", "SampleReconstructionExternalWeights",
    "NonnegativeObservationConsistentSpectrum", "PhysicalStokesCone",
    "GaugePreservingComplex", "SampleReconstructionOodReason",
    "MeasurementSchema", "batch_identity",
    "research::Maturity maturity = research::Maturity::Research",
    "coverage_one_sigma", "calibration_error_one_sigma",
    "maximum_permutation_error")
Require-Text $source @(
    "std::ranges::sort\(records", "project_spectrum", "project_stokes",
    "phase_reference_identity", "explicit_research_opt_in",
    "assess_sample_reconstruction_ood", "SampleCount", "Polarization",
    "World", "MeasurementSchema",
    "external_weights->batch_identity",
    "compute_sample_reconstruction_output_identity",
    "evaluation.maximum_permutation_error")
Require-Text $test @(
    "test_sample_splat_and_permutation",
    "evaluation.reconstructed_mse < evaluation.raw_mse",
    "coverage_two_sigma >= 0.8",
    "ExternalKernelPrediction", "ExternalSampleTransformer",
    "ExternalHybrid", "test_stokes_physical_projection",
    "test_adversarial_spectrum_projection",
    "test_complex_gauge_covariance", "wrong-phase",
    "SampleReconstructionOodReason::World",
    "SampleReconstructionOodReason::MeasurementSchema",
    "stale_weights_rejected",
    "SampleReconstructionOodReason::Material")
Require-Text "tests/sdk_free/CMakeLists.txt" @(
    "sample_reconstruction\.cpp", "sample_reconstruction_sdk_free")
Require-Text "tests/sdk_free/package_consumer/main.cpp" @(
    "ure/reconstruction/sample_reconstruction\.hpp",
    "kSampleReconstructionVersion")
Require-Text "docs/HR_2_Sample_Reconstruction.md" @(
    "All HR\.2 outputs are explicitly .*Research.* maturity",
    "does not freeze a model file or inference ABI",
    "Sample-based Monte Carlo Denoising",
    "Kernel-Predicting Convolutional Networks", "PointNet")

foreach ($path in @($header, $source)) {
    Reject-Text $path @(
        '#include\s*[<"]cuda', '#include\s*[<"]vulkan',
        '#include\s*[<"]d3d', '#include\s*[<"]windows',
        '#include\s*[<"]pxr/', 'onnx', 'TensorRT', 'tone.?map', 'sRGB')
}

Require-Capsule "docs/research/hr2/capsules/analytic_sample_splat.json" "positive"
Require-Capsule "docs/research/hr2/capsules/unbound_external_model_rejected.json" "negative"

Write-Host "HR.2 sample reconstruction static audit passed: sample metadata, permutation-invariant splatting, external research candidates, physical Spectrum/Stokes/Complex handling, OOD, calibration and positive/negative capsules are present."
