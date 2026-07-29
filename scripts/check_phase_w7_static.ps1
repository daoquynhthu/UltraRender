$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

function Require-Text {
    param(
        [string]$Path,
        [string[]]$Patterns
    )
    $resolved = Join-Path $root $Path
    if (-not (Test-Path -LiteralPath $resolved)) {
        throw "Missing W.7 artifact: $Path"
    }
    $text = Get-Content -Raw -LiteralPath $resolved
    foreach ($pattern in $Patterns) {
        if (-not $text.Contains($pattern)) {
            throw "Missing W.7 contract '$pattern' in $Path"
        }
    }
}

Require-Text "libs/ure_core/include/ure/wave_optics.hpp" @(
    "CrossSpectralDensity",
    "CoherentRealization",
    "GeneralizedRay",
    "PartialCoherenceFilm",
    "kMaxPartialCoherenceSamples",
    "kMaxPartialCoherenceRealizations",
    "kMaxPartialCoherenceContributions",
    "merge_partial_coherence_film"
)
Require-Text "libs/ure_core/src/wave_optics.cpp" @(
    "hermitian_psd_factor",
    "make_gaussian_schell_csd",
    "sample_coherent_realization",
    "estimate_cross_spectral_density",
    "propagate_generalized_ray",
    "gaussian_temporal_coherence",
    "interferometric_power",
    "weighted_power",
    "merge_partial_coherence_film"
)
Require-Text "libs/ure_core/src/wave_optics_gpu.cu" @(
    "cross_spectral_density_kernel",
    "partial-coherence.fields",
    "partial-coherence.weights",
    "estimate_cross_spectral_density_gpu",
    "complete_external"
)
Require-Text "libs/ure_core/src/path_tracer_host_api.cu" @(
    "config.wave_optics.partial_coherence_enabled",
    "unimplemented wave-optics features"
)
Require-Text "tests/host/test_wave_optics.cpp" @(
    "test_gaussian_schell_cross_spectral_density",
    "test_partial_coherence_realization_statistics",
    "test_generalized_ray_and_interferometry",
    "test_partial_coherence_film_averaging_order",
    "merge_partial_coherence_film"
)
Require-Text "tests/gpu/test_wave_optics_gpu.cu" @(
    "test_gpu_partial_coherence_ensemble_reduction",
    "estimate_cross_spectral_density_gpu"
)
Require-Text "tests/host/test_session.cpp" @(
    "URE_WAVE_OPTICS_PARTIAL_COHERENCE",
    "partial_coherence_enabled"
)
Require-Text "tests/host/test_pyure_smoke.py" @(
    'wave_optics_mode="partial_coherence"',
    "reference-only partial coherence must fail"
)

$runtime = Get-Content -Raw -LiteralPath (
    Join-Path $root "libs/ure_core/src/path_tracer_host_api.cu")
if ($runtime.Contains(
        "partial_coherence_enabled = false")) {
    throw "W.7 production boundary must not silently disable partial coherence"
}

Write-Host "Phase W.7 static audit passed."
